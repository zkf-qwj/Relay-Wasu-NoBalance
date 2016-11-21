#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h> 
#include "fdevent.h"
#include "arts_ph_common.h"



 fdevents *ev = NULL;


int multicast_group_leave(int fd, UInt32 IPAddr)
{
    struct ip_mreq multiaddr;
    int res;
    if(fd <0 )
        return 0;
   

    memset(&multiaddr, 0, sizeof(multiaddr));
    multiaddr.imr_multiaddr.s_addr = IPAddr;
    multiaddr.imr_interface.s_addr = htonl(INADDR_ANY);

    res = setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &multiaddr, sizeof(multiaddr));

    if (res < 0){
          printf("drop membership failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

inline int multicast_group_join(int fd, UInt32 IPAddr)
{
    struct ip_mreq multiaddr;
    int res;
     if(fd <0 )
        return 0;
   
    
    memset(&multiaddr, 0, sizeof(multiaddr));
    multiaddr.imr_multiaddr.s_addr =htonl(IPAddr);
    multiaddr.imr_interface.s_addr = htonl(INADDR_ANY);

    res = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&multiaddr, sizeof(multiaddr));

    if (res < 0){
       printf("add membership failed: %s\n", strerror(errno));
        return -1;
    }  
    return res;    
}


int multicast_client_fd( UInt32 IPAddr, int port)
{
    LogRequest(INFO_ARTS_MODULE, 0,"Entry");
    int fd =-1,tmp,res;
    if(port <=0 )
        return fd;
    struct sockaddr_in addr;
    
     {
          printf("IsMulticastIPAddr");
         fd = socket(PF_INET, SOCK_DGRAM, 0);
         if (fd < 0) {
           printf("can not create recv socket: %s\n", strerror(errno));
	       return -1;
        }

        tmp = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp)) == -1){
	        printf("can not set address reuse flag: %s\n", strerror(errno));
        }
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl (INADDR_ANY);

        res = bind(fd, (struct sockaddr*)&addr, sizeof(addr));

        if (res < 0){
	        printf("can not bind socket: %s\n", strerror(errno));
	        close(fd);
	        return -1;
        }
    }
    
    return fd;
}

handler_t arts_handle_adapter_fdevent_io(void *s, void *ctx, int revents) 
{

    printf("arts_handle_adapter_fdevent_io: entering\n");
}


int  register_adapter_handler(char *host ,int port)
{
    
    int newfd =-1;
    if(host == NULL || port <=0)
        return -1;
    UInt32 addr = ntohl(inet_addr(host));
    newfd = multicast_client_fd(addr,port);
    
    
    if(newfd != -1)
    {
        multicast_group_join(newfd, addr);
        arts_session_head *sess_head = arts_session_head_init();
        sess_head->sock->fd = newfd;
        sess_head->addr = addr;
        fdevent_register(ev, sess_head->sock, arts_handle_adapter_fdevent_io, sess_head);
        fdevent_event_add(ev, sess_head->sock, FDEVENT_IN | FDEVENT_HUP);
                        
        printf("arts_handle_listener_fdevent: listener new connection fd = %d\n", sess_head->sock->fd);
    }
    return newfd;
}


int main()
{
     ev=fdevent_init(4096, FDEVENT_HANDLER_POLL);
     
     if(register_adapter_handler("226.1.1.1",49184)< 0)
     {
        exit(1);
     }
     
     fdevent_revents *revents = fdevent_revents_init();
     int poll_errno;
     int n;

   
    while( true)
    {
        // Do the poll
              
        n = fdevent_poll(ev, 1000);        
       
        poll_errno = errno;
        
        if (n > 0) 
        {
            size_t i;
            fdevent_get_revents(ev, n, revents);
            printf("fdevent_get_revents\n");
            for (i = 0; i < revents->used; i++) 
            {
               fdevent_revent *revent = revents->ptr[i];
               handler_t r;
      
               switch (r = (*(revent->handler))(this, revent->context, revent->revents)) 
               {
                case HANDLER_FINISHED:
                case HANDLER_GO_ON:
                case HANDLER_WAIT_FOR_EVENT:
                    break;
                case HANDLER_ERROR:
                    /* should never happen */
                    Assert(false);
                    break;
                default:
                    printf("event loop returned: %d", (int) r);
                    break;
               }
           }
        } 
        else if (n < 0 && poll_errno != EINTR) 
        {
                 printf("event loop: fdevent_poll failed: %s", strerror(poll_errno)); 
                 break;
        }
        
     }
     
     fdevent_revents_free(revents);
}

