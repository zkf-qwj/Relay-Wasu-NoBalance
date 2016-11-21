#ifndef  _MULTICAST_UTIL_H
#define    _MULTICAST_UTIL_H

#include "QTSS.h"
#include "SocketUtils.h"
#include "QTSSARTSAccessLog.h"
inline int multicast_group_leave(int fd, UInt32 IPAddr)
{
    struct ip_mreq multiaddr;
    int res;
    if(fd <0 )
        return 0;
    if (!SocketUtils::IsMulticastIPAddr(IPAddr)){
        LogRequest(INFO_ARTS_MODULE,0,"IPAddr is not multicast,IPAddr:%x",IPAddr);
	    return 0;
    }

    memset(&multiaddr, 0, sizeof(multiaddr));
    multiaddr.imr_multiaddr.s_addr = htonl(IPAddr);
    multiaddr.imr_interface.s_addr = htonl(INADDR_ANY);

    res = setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &multiaddr, sizeof(multiaddr));

    if (res < 0){
        LogRequest(INFO_ARTS_MODULE, 0,"drop membership failed: %s", strerror(errno));
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
    if (!SocketUtils::IsMulticastIPAddr(IPAddr)){
        LogRequest(INFO_ARTS_MODULE,0,"IPAddr is not multicast");
	    return 0;
    }
    //LogRequest(INFO_ARTS_MODULE,0,"IPAddr:%x",IPAddr);
    memset(&multiaddr, 0, sizeof(multiaddr));
    multiaddr.imr_multiaddr.s_addr =htonl(IPAddr);
    multiaddr.imr_interface.s_addr = htonl(INADDR_ANY);

    res = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&multiaddr, sizeof(multiaddr));

    if (res < 0){
        LogRequest(INFO_ARTS_MODULE, 0,"add membership failed: %s", strerror(errno));
        return -1;
    }  
    return res;    
}


inline int create_client_fd( UInt32 IPAddr, int port)
{
    LogRequest(INFO_ARTS_MODULE, 0,"Entry");
    int fd =-1,tmp,res;
    if(port <=0 )
        return fd;
    struct sockaddr_in addr;
    if (SocketUtils::IsMulticastIPAddr(IPAddr))
     {
         LogRequest(INFO_ARTS_MODULE, 0,"IsMulticastIPAddr");
     }else
     {
        LogRequest(INFO_ARTS_MODULE, 0,"IsUnicastIPAddr");
     }
     fd = socket(PF_INET, SOCK_DGRAM, 0);
     if (fd < 0) {
         LogRequest(INFO_ARTS_MODULE, 0,"can not create recv socket: %s", strerror(errno));
	     return -1;
     }

     tmp = 1;
     if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp)) == -1){
	        LogRequest(INFO_ARTS_MODULE, 0,"can not set address reuse flag: %s", strerror(errno));
     }
        
     memset(&addr, 0, sizeof(addr));
     addr.sin_family = AF_INET;
     addr.sin_port = htons(port);
     addr.sin_addr.s_addr = htonl (IPAddr);

     res = bind(fd, (struct sockaddr*)&addr, sizeof(addr));

     if (res < 0){
	      LogRequest(INFO_ARTS_MODULE, 0,"can not bind socket: %s", strerror(errno));
	      close(fd);
	      return -1;
     }
    
    
    return fd;
}


#endif
