#include "ht_co_socket.h"
#include <string>
#include "QTSSARTSReflectorAccessLog.h"
#define MAXHASHNODE  4096


enum debug_level_t {
    NONE_ARTS_MODULE = 0,
    INFO_ARTS_MODULE,
    DEBUG_ARTS_MODULE
};


struct ht  co_socket_ht;

void init_co_socket_ht()
{
    ht_init(&co_socket_ht, MAXHASHNODE, offsetof(co_socket_t, hh_co_socket));
}


void destory_co_socket_ht()
{
    ht_destroy(&co_socket_ht);
}

static hthash_value  get_sum_sessID(const char * sessID)
{
    int len = strlen(sessID);
    char p[2]={'\0'};
    int i=0;
    
    hthash_value sum =0;
    while (i<len)
    {
        memcpy(p,sessID+i,1);
        sum += atoi(p);
        i++;
    }
    
    RLogRequest(INFO_ARTS_MODULE,0,"sessID:%s,sum:%d",sessID,sum);
    return sum;
}

hthash_value htfunc_co_socket(const void *item)
{
    const co_socket_t *node = (const co_socket_t*)item;   
    return get_sum_sessID(node->sessionID);
}

 int htfunc_co_socket_cmp(const void *_item1, const void *_item2_or_key)
{
    const  co_socket_t *item1 =(const  co_socket_t*) _item1;
    const  co_socket_t*item2_or_key =(const  co_socket_t*) _item2_or_key;
   
    if( !strcmp(item1->sessionID,item2_or_key->sessionID ))
    {
         RLogRequest(INFO_ARTS_MODULE,0,"sessionID is same");
	    return 0;
	}
    if ( strcmp (item1->sessionID,item2_or_key->sessionID ) <0 )
    {
        RLogRequest(INFO_ARTS_MODULE,0,"sessionID:%s,armSessID:%s",item1->sessionID,item2_or_key->sessionID);
	    return -1;
	}
    return 1;
}

void del_co_socket_node(co_socket_t *node)
{
     if(! node)
        return ;
   
    co_socket_t *node_removed = NULL;
    
    if(node != NULL)
    {       
        node_removed=(co_socket_t *)ht_remove(&co_socket_ht, node, htfunc_co_socket, htfunc_co_socket_cmp);
    }
   if(node_removed!=NULL)
   {
        RLogRequest(INFO_ARTS_MODULE, node_removed->callid,"delete co socket node:%x",node_removed);
        free(node_removed);
   }else
     RLogRequest(INFO_ARTS_MODULE,0,"can not find co socket node,sessionID:%d",node->sessionID);
   
}

void insert_co_socket_list( co_socket_t * node)
{
    if(!node )
        return ;
  
    Assert(node != NULL);   
   
    RLogRequest(INFO_ARTS_MODULE, 0,"insert co socket node:%x,sessionID:%s",node,node->sessionID);
   
    ht_insert(&co_socket_ht, node, htfunc_co_socket);
}

co_socket_t * get_co_socket_node( const char *sessionID)
{
    if(strlen(sessionID) <=0)
        return NULL;
    co_socket_t temp_node;
    strcpy(temp_node.sessionID, sessionID);
    co_socket_t *node=(co_socket_t*)ht_find(&co_socket_ht, (void*)&temp_node, htfunc_co_socket, htfunc_co_socket_cmp);
    if(node != NULL)
    {
        return node;
    }else
    {
        RLogRequest(INFO_ARTS_MODULE, 0,"can not find co_socket node,sessionID:%s",sessionID);
        return NULL;
    }
}

