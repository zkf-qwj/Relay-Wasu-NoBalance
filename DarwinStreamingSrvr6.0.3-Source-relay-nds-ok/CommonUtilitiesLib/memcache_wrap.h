
#include "libmemcached/memcached.h"

inline memcached_st * create_conn(char *host)
{   
    char config_string[256]={'\0'};
    sprintf(config_string,"--SERVER=%s",host);
    memcached_st *memc= memcached(config_string, strlen(config_string));    
    return memc;
}

inline int mem_set(memcached_st * memc,const char *key,const char *value,int exp_time)
{
    if(key == NULL || memc == NULL || value == NULL)
        return -1;         
   
	int exp_local = exp_time;	
    memcached_return_t rc= memcached_set(memc,
                                key,
                                strlen(key),
                                value,
                                strlen(value),
                                exp_local,
                                (uint32_t)0);
   
    return rc;
}

inline int mem_get(memcached_st * memc,const char *key,char *value)
{
     if(key == NULL || memc == NULL || value == NULL)
        return -1;
     
     char *outval = NULL;
     
     size_t out_size = 0;
     uint32_t flag =0;
     memcached_return_t  rc;
     outval = memcached_get(memc, key, strlen(key),
                                (size_t*)&out_size,
                                &flag,
                                &rc);
     if (rc == MEMCACHED_SUCCESS ){
       strcpy(value,outval); 
       free(outval);              
     }     
     return rc;
}


inline int release_conn(memcached_st * memc)
{
    if(memc != NULL)
        memcached_free(memc);
}

