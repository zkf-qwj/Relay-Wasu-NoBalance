#ifndef  _LIVE_MODULE_H
#define  _LIVE_MODULE_H
#include "QTSS.h"
#include "QTSSARTSAccessLog.h"
#include "ARTS_PH_Interface.h"
extern "C" {

#include "arts_ph_common.h"


}
int  register_adapter_handler(char *host ,int port,unsigned int callid);
void del_mul_list(unsigned int callid);
void insert_mul_list(unsigned int callid, int fd);
extern ARTS_PH_Interface *sARTSPHInterface;

inline int get_host_port(char *pBackends,char **host_name )
{
    char *colon;
    char *hostname = NULL, *port = NULL;
    int portnum =0;
    if (NULL != (colon = strrchr(pBackends, ':')))
    {
        int len = colon - pBackends;
        hostname = new char[len+1];
        strncpy(hostname, pBackends, len );
        hostname[len] = 0;
        (*host_name) = hostname;
        port = new char[strlen(colon + 1) + 1];
        strcpy(port, colon + 1);

        portnum = atoi(port);
        free(port);
     }
    return portnum;

}

inline QTSS_Error get_channel_id (QTSS_StandardRTSP_Params* inParams,arts_session *sess)
{
    if(sess == NULL || inParams == NULL)
        return QTSS_NoErr;
    char FileName[1024]={'\0'};   
   
    char * channel_str = NULL;
    char * theRequestAttributes = NULL;    
    QTSS_RTSPRequestObject  theRequest = inParams->inRTSPRequest;
    QTSS_Error err = QTSS_GetValueAsString(theRequest, qtssRTSPReqFileName, 0, &theRequestAttributes);
    LogRequest(INFO_ARTS_MODULE, 0,"File-Name:%s",theRequestAttributes);
    strcpy(FileName,theRequestAttributes);
    QTSS_Delete(theRequestAttributes);
    
    if(sess->transport_type==qtssRTPTransportTypeMPEG2)
        channel_str= strstr(FileName,".ts");
    else
        channel_str= strstr(FileName,".mp4");
    if(channel_str != NULL)
    {
        memcpy(sess->channel_id,FileName,channel_str-FileName);
        LogRequest(INFO_ARTS_MODULE, 0,"channel_id:%s",sess->channel_id);
        return QTSS_NoErr;
    }else
    {
        LogRequest(INFO_ARTS_MODULE, 0,"channel_id is null\n");
        return   QTSS_BadArgument;
    }
    
   
}



inline  int register_data_sock(char *couchbase_host,unsigned int l_callid,char * channel_id,char *sdp)
{
       
     memcached_st * memc = NULL;
     if(strlen(couchbase_host) == 0 )
     {
            return QTSS_BadArgument;
     }
         
     char multi_addr[1024]={'\0'};
     
     if(strstr(couchbase_host,"226.1.1.1")!= NULL)
     {
        strcpy(multi_addr,"226.1.1.1:49184");
     }
     else
     {
        memc = conn(couchbase_host,l_callid);
        if(memc == NULL)
        {
           return QTSS_BadArgument;
        }
        get_udp_host(memc,channel_id,l_callid,multi_addr); 
     }
     //strcpy(multi_addr,"127.0.0.1:49184");
     
     LogRequest(DEBUG_ARTS_MODULE,l_callid,"multi_addr:%s",multi_addr);
     
     if(sdp != NULL)
     {         
        get_sdp(memc,channel_id,l_callid,sdp);
        LogRequest(DEBUG_ARTS_MODULE,l_callid,"sdp:%s\n",sdp);
     }
     
     
     if(strlen(multi_addr) == 0)
     {
        if(memc != NULL)
        {             
            release_conn(memc);
        }
        return QTSS_ValueNotFound;
     }else
     {
        set(memc,channel_id,l_callid,"1");        
        release_conn(memc);
     }
     
     char *multi_host = NULL;
     int port = get_host_port(multi_addr,&multi_host);
     LogRequest(DEBUG_ARTS_MODULE,l_callid,"port:%d,host:%s\n",port,multi_host);  
     int newfd =register_adapter_handler(multi_host,port,l_callid);    
     
     free(multi_host);
     
     return QTSS_NoErr;
}

#endif
