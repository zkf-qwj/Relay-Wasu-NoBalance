#ifndef _HT_CO_SOCKET_H
#define HT_CO_SOCKET_H

#include "QTSS.h"
#include <sys/types.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <netdb.h>

#include <unistd.h>

extern "C" {
#include "fdevent.h"
#include "arts_ph_common.h"
}
typedef struct
{
    char  sessionID[256];
    void  *sock;
    bool  used;    
    void  *rtspSessObj;
    void  *rtpSessObj;
    unsigned int callid;  
    struct hashable hh_co_socket;   
}co_socket_t;


void init_co_socket_ht();
void destory_co_socket_ht();
hthash_value htfunc_co_socket(const void *item);
int htfunc_co_socket_cmp(const void *_item1, const void *_item2_or_key);
void del_co_socket_node( co_socket_t *sessionID);
void insert_co_socket_list(co_socket_t *node);
co_socket_t * get_co_socket_node( const char *sessionID);

#endif
