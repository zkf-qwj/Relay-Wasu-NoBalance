/***************************************************************************
 * Authors:       Fasihullah Askiri
 * Synopsys:
 *
 * This file contains a scalable fdevent based implementation of Darwin event
 * management.
 * 
 * Copyright 2008 by CNStreaming Networks.
 * The material in this file is subject to copyright. It may not be used,
 * copied or transferred by any means without the prior written approval
 * of CNStreaming Networks.
 *
 * DILITHIUM NETWORKS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO 
 * EVENT SHALL DILITHIUM NETWORKS BE LIABLE FOR ANY SPECIAL, INDIRECT OR 
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR 
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 *****************************************************************************/

#include <queue>
#include "iosocket.h"
#include "sys-socket.h"
using namespace std;

extern "C" {
#include "fdevent.h"
}

#include "OSMutex.h"
#include "MyAssert.h"
#include "ev.h"

#define MAX_FDS 65536
// There is no callback to shutdown, this class is just a wrapper to free resources
class FDEvent
{
    public:
        typedef struct event_request
        {
            event_request (struct eventreq *p_req, int p_event) : req (p_req), event (p_event) {}
            struct eventreq *req;
            int              event;
        } event_request_t;

        FDEvent () :
            m_pev (0),
            m_Mutex (),
            m_AddQ (),
            m_DelQ (),
            m_pCookies (),
            m_iEventsAvailable (0) {}

        ~FDEvent ()
        {
            if (m_pev)
                fdevent_free (m_pev);
        }
        void Initialize ();
        void PushAddQ (const event_request_t& er);
        void PushDelQ (int fd);

        int  GetNextEvent (struct eventreq* req);

    private:
        void PrepareEvents ();
        int DarwinToFDEventType(int et)
        {
            int translated_et = 0;
            if (et & EV_RE)
                translated_et |= FDEVENT_IN;
            if (et & EV_WR)
                translated_et |= FDEVENT_OUT;

            return translated_et;
        }

        int FDEventToDarwinType(int et)
        {
            int translated_et = 0;
            if (et & FDEVENT_IN)
                translated_et |= EV_RE;
            if (et & FDEVENT_OUT)
                translated_et |= EV_WR;

            return translated_et;
        }

    private:
        fdevents               *m_pev;
        iosocket                m_IOSockets [MAX_FDS]; /* Notice here, that I am indexing by fd and NOT fd_index
                                                          what this basically means is that the max fd that the
                                                          system will be able to take would be MAX_FDS, libfdevent
                                                          on the other hand is being instructed to take a max of 
                                                          MAX_FDS, [the max fd can be much beyond that]. The same
                                                          applies for m_pCookies below. The correct way to do this
                                                          would be to use an array of MAX_FDS and have a used list
                                                          and an unused list as fdevent does. However, the laziness
                                                          principles governs this simplicity >:)
                                                          */
        OSMutex                 m_Mutex;
        int                     m_Pipes [2];
        queue<event_request_t>  m_AddQ;
        queue<int>              m_DelQ;
        void*                   m_pCookies [MAX_FDS];
        int                     m_iEventsAvailable;
};

void FDEvent::Initialize ()
{
    //m_pev = fdevent_init (MAX_FDS,FDEVENT_HANDLER_LINUX_SYSEPOLL);
    
     m_pev = fdevent_init (MAX_FDS, FDEVENT_HANDLER_POLL);
    for (int i = 0; i < MAX_FDS; i++)
    {
        m_IOSockets [i].fd = i;
        m_IOSockets [i].fde_ndx = -1;
        m_IOSockets [i].type = IOSOCKET_TYPE_SOCKET;
    }

    Assert (::pipe((int*)&m_Pipes) == 0);

    //Add the read end of the pipe to the read mask
    m_IOSockets [m_Pipes [0]].type = IOSOCKET_TYPE_PIPE;
    m_IOSockets [m_Pipes [1]].type = IOSOCKET_TYPE_PIPE;
    fdevent_event_add (
            m_pev,
            &m_IOSockets [m_Pipes [0]],
            FDEVENT_IN);
}

void FDEvent::PushAddQ (const event_request_t& er)
{
    OSMutexLocker locker (&m_Mutex);
    m_AddQ.push (er);
    Assert (::write (m_Pipes [1], "p", 1) == 1);
}

void FDEvent::PushDelQ (int fd)
{
    OSMutexLocker locker (&m_Mutex);
    m_DelQ.push (fd);
    Assert (::write (m_Pipes [1], "p", 1) == 1);
}

void FDEvent::PrepareEvents ()
{
    OSMutexLocker locker (&m_Mutex);
    // Remove pending first
    while (!m_DelQ.empty ())
    {
        int& fd             = m_DelQ.front ();
        iosocket *sock      = &m_IOSockets [fd];


        if (!sock) return;

        if (sock->fd != -1) {
            switch (sock->type) {
                case IOSOCKET_TYPE_SOCKET:
                    closesocket(sock->fd);
                    break;
                case IOSOCKET_TYPE_PIPE:
                    close(sock->fd);
                    break;
                default:
                    break;
            }
        }
        //iosocket_free(&m_IOSockets[fd]);
        fdevent_event_del (m_pev, sock);
        m_pCookies [fd]     = NULL;

        m_DelQ.pop ();
    }

    // Add pending now
    while (!m_AddQ.empty ())
    {
        event_request_t& er = m_AddQ.front ();
        int fd              = er.req->er_handle;
        iosocket *sock      = &m_IOSockets [fd];
        sock->fd            = fd;

        Assert (er.req->er_data != NULL);
        m_pCookies [fd] = er.req->er_data;

        fdevent_event_add (
                m_pev,
                sock,
                DarwinToFDEventType (er.event)
                );

        m_AddQ.pop ();
    }
}

int FDEvent::GetNextEvent (struct eventreq* req)
{
    int fde_ndx = -1;

    while (fde_ndx < 0)
    {
        if (m_iEventsAvailable > 0)
        {
            for (unsigned int event_ndx = 0; event_ndx < m_pev->used; event_ndx++)
            {
                if (
                        m_pev->pollfds[event_ndx].fd != -1 &&      // Represents a deleted fd
                        m_pev->pollfds[event_ndx].revents != 0     // Valid event recvd
                   )
                {
                    m_iEventsAvailable--; // An event consumed
                    fde_ndx = event_ndx;
                    break;
                }
            }

            if (fde_ndx > 0)
                break;
        }

        Assert (m_iEventsAvailable == 0);
        PrepareEvents ();

        int ret = fdevent_poll (m_pev, 1000);
        int err = OSThread::GetErrno();

        while (ret <= 0)
        {
            if ( 
                    err == EBADF || //this might happen if a fd is closed right before calling select
                    err == EINTR || // this might happen if select gets interrupted
                    ret == 0 // select returns 0, we've simply timed out, so recall
               ) // this might happen if select gets interrupted
            {
                ret = fdevent_poll (m_pev, 1000);
                err = OSThread::GetErrno();
                continue;
            }
            break;
        }

        if (ret > 0)
        {
            if (m_pev->pollfds [m_IOSockets [m_Pipes [0]].fde_ndx].revents & FDEVENT_IN)
            {
                // <Fasih: Copied over from Darwin select code>
                // 
                //we've gotten data on the pipe file descriptor. Clear the data.
                // increasing the select buffer fixes a hanging problem when the Darwin server is under heavy load
                // CISCO contribution
                char theBuffer[4096]; 
                (void)::read (m_Pipes[0], &theBuffer[0], 4096);
                // </Fasih>

                ret--; // Consumed one event
            }
            m_iEventsAvailable = ret;
        }
        else
        {
            break; // This is an invalid scenario. Should return an error to the caller
        }
    }

    if (fde_ndx > 0)
    {
        int fd = m_pev->pollfds [fde_ndx].fd;
        req->er_handle = fd;
        //fprintf(stdout,"fd:%d,events:%d\n",fd,m_pev->pollfds [fde_ndx].revents);
        req->er_eventbits = FDEventToDarwinType (m_pev->pollfds [fde_ndx].revents);
        req->er_data = m_pCookies [m_pev->pollfds [fde_ndx].fd];

        // don't want events on this fd until modwatch is called.
        fdevent_event_del (m_pev, &m_IOSockets [fd]);

        return 0;
    }
    else
    {
        return EINTR;
    }
}

static FDEvent sFDEvent;

void select_startevents ()
{
    sFDEvent.Initialize ();
}

int select_watchevent (struct eventreq *req, int which)
{
    return select_modwatch (req, which);
}

int select_modwatch (struct eventreq *req, int which)
{
    sFDEvent.PushAddQ (FDEvent::event_request_t (req, which));
    return 0;
}

int select_removeevent (int which)
{
    sFDEvent.PushDelQ (which);
    return 0;
}

int select_waitevent (struct eventreq *req, void* /*onlyForMOSX.. Which I have not even seen... >:)*/)
{
    return sFDEvent.GetNextEvent (req);
}

// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
