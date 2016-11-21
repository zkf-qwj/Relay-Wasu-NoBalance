#ifndef _ARTS_PH_Interface_H
#define _ARTS_PH_Interface_H

extern "C" {
#include "fdevent.h"
#include "arts_ph_common.h"
}

typedef struct
{
    char sessionID[256];
    TCPSocket *sock;
    bool     used;
    void     *next;
    RTSPSession  *rtspSessObj;
    RTPSession   *rtpSessObj;   
}co_socket_t;

typedef struct 
{
    int callid;
    int fd;
    void *next;
    struct hashable hh_multicast;
} multicast_t;//multicast_struct






typedef struct event_node
{
    int callid;
    int events;
    int64_t stop_time;   //at the time of this obj->fd unregister
    void *next;
    bool unregister ;
    void *sess;
    int fd;
}event_node_t;


class ARTS_PH_Interface : public OSThread
{
public:
    ARTS_PH_Interface();
    ~ARTS_PH_Interface();
    bool Configure(char *pBackends,char * pBindHost ,UInt16 lport);
    virtual void Entry();

    fdevents *ev;           /* Event handlers */
    arts_controlfd_state_t control_state;/* State of Controller Connection */
    int callid;                         /* Call Id for incoming call */
    unsigned char phid;                 /* PH ID assigned by the controller */
    iosocket *controlsock;              /* iosocket for SCTP to Controller */
    int controllertimeout;              /* Number of seconds to wait before retrying controller */
    int listenport;                     /* Port for Listening to SCTP connections */
    iosocket *listensock;               /* iosocket for Listening */   
    int polltimeout; 
    char tmpbuffer[ARTS_IPC_MAX_ENCODED_PDU_SIZE+1];
    int tmpbuf_len;
    //char tmpbuffer[3948];
    OSMutex sessionMutex;
    
    struct ht multicast_ht;
  
    co_socket_t* co_socket_list;
    co_socket_t* last_co_socket;
    
    event_node_t * stop_events;
    event_node_t * last_stop_event;
    //live_ott_t     live_ott_info[MAX_INFO];
    
    bool lock;
private:
    void Trigger();
    struct sockaddr_in servaddr;    /* ARTS Controller Address */
    
    
};


ARTS_PH_Interface *sARTSPHInterface = NULL;

inline hthash_value htfunc_multicast(const void *item)
{
    const multicast_t *node = (const multicast_t*)item;   
    return node->fd ^ (node->fd >> 32);
}

inline int htfunc_multicast_cmp(const void *_item1, const void *_item2_or_key)
{
    const  multicast_t *item1 =(const  multicast_t*) _item1;
    const  multicast_t*item2_or_key =(const  multicast_t*) _item2_or_key;
   
    if (likely(item1->fd == item2_or_key->fd))
	return 0;
    if (item1->fd < item2_or_key->fd)
	return -1;
    return 1;
}

inline void del_mul_list(int fd)
{
    multicast_t temp_node;
    temp_node.fd=fd;
    multicast_t *node_removed = NULL;
    multicast_t *node=(multicast_t*)ht_find(&sARTSPHInterface->multicast_ht, (void*)&temp_node, htfunc_multicast, htfunc_multicast_cmp);
    if(node != NULL)
    {
        OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
        node_removed=(multicast_t *)ht_remove(&sARTSPHInterface->multicast_ht, node, htfunc_multicast, htfunc_multicast_cmp);
    }
   if(node_removed!=NULL)
   {
        LogRequest(INFO_ARTS_MODULE, node_removed->callid,"delete mulicast node:%x",node_removed);
        free(node_removed);
   }else
     LogRequest(INFO_ARTS_MODULE,0,"can not find multicast node,fd:%d",fd);
   
}

inline void insert_mul_list(unsigned int callid, int fd)
{
    multicast_t * node = (multicast_t *)calloc(1,sizeof(multicast_t));
    Assert(node != NULL);
    Assert(sARTSPHInterface != NULL );
    node->callid = callid;
    node->fd = fd;
    LogRequest(INFO_ARTS_MODULE, callid,"insert multicast node:%x,fd:%d",node,fd);
    OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
    ht_insert(&sARTSPHInterface->multicast_ht, node, htfunc_multicast);
}

inline unsigned int get_multicast_callid( int fd)
{
    multicast_t temp_node;
    temp_node.fd=fd;
    multicast_t *node=(multicast_t*)ht_find(&sARTSPHInterface->multicast_ht, (void*)&temp_node, htfunc_multicast, htfunc_multicast_cmp);
    if(node != NULL)
    {
        return node->callid;
    }else
    {
        LogRequest(INFO_ARTS_MODULE, 0,"can not find callid,fd:%d",fd);
        return 0;
    }
}

#endif
