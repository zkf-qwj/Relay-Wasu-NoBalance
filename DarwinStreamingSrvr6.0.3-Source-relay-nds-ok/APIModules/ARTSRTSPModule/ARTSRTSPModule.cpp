/*
    File:       ARTSRTSPModule.cpp

    Contains:   Implements a module to fetch backend content from ARTS

    
*/
#define ADAPTATION_FLAG      0x20 
#define MPEG2_DATA           0x40
#define PCR_FLAG             0x10
#define DISCONTINUITY_FLAG   0x80
#define MIN_PACK_LEN_FOR_PCR 12
#define MP2_PKTS_TO_READ     7
#define DO_BLOCK_READ        0

#define MAXUINT32            0xffffffff

#define ARTSMAX(a,b)  ((a)>(b)?(a):(b))
#define ARTSMIN(a,b)  ((a)<(b)?(a):(b))
#define MAXBUFFERDURATION  3000
#define MAXBUFFERLEN       3000
#define MINBUFFERLEN       1000
#define MINBUFFERDURATION  1000


enum debug_level_t {
    NONE_ARTS_MODULE = 0,
    INFO_ARTS_MODULE,
    DEBUG_ARTS_MODULE
};

static debug_level_t ARTS_MODULE_DEBUG_LEVEL =DEBUG_ARTS_MODULE;
//#if DO_BLOCK_READ
#define MAX_RTP_PACKET_BUFFER_LEN 500
#define MIN_RTP_PACKET_BUFFER_LEN 1
#define TS_PACKET_SIZE            188
#define TS_TCP_PACKET_SIZE        188*7
//#endif

#ifndef __Win32__
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#endif

#include "QTSServerInterface.h"
#include "ARTSRTSPModule.h"
#include "QTSSMemoryDeleter.h"
#include "QTSSModuleUtils.h"
#include "QTSSRollingLog.h"
#include "OSMemory.h"
#include "Task.h"
#include "SDPUtils.h"
#include "SocketUtils.h"
#include "RTSPProtocol.h"
#include "RTSPRequestInterface.h"
#include "RTSPSession.h"
#include "RTPSession.h"
#include "RTCPPacket.h"
#include "RTCPAPPPacket.h"
#include "ARTSutil.h"
#include "arts_send_pkt_thread.h"
#include "ts-demux.h"
#include "multicast_util.h"
#include "live_module.h"
#include "ARTS_PH_Interface.h"

extern "C" {
#include "fdevent.h"
#include "arts_ph_common.h"
#include <string.h>

}

#include <netinet/in.h>  
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include <stdint.h>


namespace proto64{
enum ConnectionReleaseReason {
  Normal = 0,
  NotFound = 1,
  BadRequest = 2,
  Unauthorized = 3,
  Forbidden = 4,
  Aborted = 5,
  UserDisconnected = 6,
  Rejected = 7,
  MaxLimitExceeded = 8,
  MediaSetupError = 9,
  SourceUnreachable = 10,
  PHDisconnected = 11,
  AdapterDisconnected = 12,
  ScriptError = 13,
  InternalStateMismatch = 14,
  InternalGenericError = 15,
  ResourceUnavailable = 16,
  LoadBalancerReject = 17,
  LastConnectionReleaseReasonEnum = 18,
  ScriptDisabled = 19,
  AdapterTimeOut = 20,
  ScriptTimeOut  = 21
};
}

#define  MAX_HANDLE_DIR 50
#define  TYPE_TO_C(t) (((t)==2)?'a':((t)==3)?'v':'u')

//------------------------------------------------------------------------
// CONTROLLER INTERFACE CLASS
//------------------------------------------------------------------------




//------------------------------------------------------------------------
// STATIC DATA
//------------------------------------------------------------------------


FILE *inputfile;
static Bool16 sARTSJinShanModule=false;
static Bool16 sARTSszModule = false;
static debug_level_t  arts_log_level = INFO_ARTS_MODULE;
static char *couchbase_host = NULL;
static UInt32  sARTSSDPRTPLineNum =0;
static UInt32  sARTSSDPTSLineNum =0;



static QTSS_PrefsObject sServerPrefs = NULL;
static Bool16  sARTSPSICBR = false;
static UInt32  sARTSPSIDuration =1000;//ms
static Bool16  dump_input = false;
static Bool16  dump_output = false;
static Bool16  no_describe = false;  
static Bool16  sARTSSupportOtherChannel = false;
static char*   sARTSInputFile = NULL;
static UInt32  sARTSIpqamPCRInterval =40;
static UInt32  sARTSIpqamDefaultPCRInterval = 40;
static UInt32 sARTSIpqamFrequency = 195000;
static char*  sARTSIpqamHost = NULL;
static UInt16 sARTSIpqamPort = 49184;
static UInt32  sARTSIpAheadTime=500;
static Bool16  firstPlayRange = false;
static Bool16  live =false;
static UInt32  couchbase_exp=30;
static Bool16 ARTSsendBitrate = false;
static UInt64 sARTSIpqamBitrate = 6500000;  
static char*  sARTSBackends = NULL;
static char*  sDefaultARTSBackends = "127.0.0.1:9000";
static char*  sDefaultARTSBindHost = NULL;  // When NULL DArwin will bind to INADDR_ANY 
static char*  sARTSBindHost = NULL;
static char*  sARTSHandleDir[MAX_HANDLE_DIR] = {0,};
static char * sARTSSDPRTPLine[MAX_HANDLE_DIR] ={0,};
static char * sARTSSDPTSLine[MAX_HANDLE_DIR] ={0,};
static UInt32 sARTSNumHandleDir = 0;

static char*  sARTSSystemName = NULL;
static char*  sDefaultARTSSystemName = "ARTS RTSP Server ";
static char * sARTSTestUrl = NULL;
static UInt16 sARTSListenPort = 8000;
static UInt16 sDefaultARTSListenPort = 8000;



static UInt32   sDefaultRollInterval    = 7;
static Bool16   sDefaultLogTimeInGMT    = true;
static UInt32   sDefaultMaxLogBytes     = 10240000;
static Bool16   sDefaultLogEnabled  = false;


static Bool16   sDefaultEnableDiffServ  = true;
static UInt16   sDefaultDSCP = 136;
// configurable parameter to tell wehther to send RTCP bye on EOS or not
static Bool16   sDefaultSendRtcpByeOnEos  = false;
static Bool16   sARTSSupportPTS = false;

static char*    sLogName        = NULL;
static char*    sLogDir         = NULL;


static  Bool16   sARTSCBR = false;

static Bool16   sEnableDiffServ = true;
static UInt16   sARTSDSCP = 136;
static UInt32   sARTSIpqamAheadTime =0; //in microsecond
static UInt32   sARTSIpqamAfterTime =0;
//configurable parameter to tell wehther to send RTCP bye on EOS or not
static Bool16   sSendRtcpByeOnEos     = false;

static const StrPtrLen              kCacheControlHeader("must-revalidate");



static QTSS_AttributeID sARTSSessionAttr = qtssIllegalAttrID;
static QTSS_AttributeID sARTSRTSPSessionAttr = qtssIllegalAttrID;


static UInt64 MUX_RATE;


//------------------------------------------------------------------------
// FUNCTION PROTOTYPES
//------------------------------------------------------------------------

QTSS_Error ARTSRTSPModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams);
static QTSS_Error   Register(QTSS_Register_Params* inParams);
static QTSS_Error   Initialize(QTSS_Initialize_Params* inParams);
static QTSS_Error   RereadPrefs();
static QTSS_Error   ProcessRTSPRequest(QTSS_StandardRTSP_Params* inParams);
static QTSS_Error   Shutdown();

static QTSS_Error   ARTS_RequestEvent(Task **ppTask);
static QTSS_Error   ARTS_SignalStream(Task *pTask,int event);

static QTSS_Error ProcessRTCPPacket(QTSS_RTCPProcess_Params * inParams);
static QTSS_Error DoGetParameter(QTSS_StandardRTSP_Params* inParamBlock);
static QTSS_Error DoDescribe(QTSS_StandardRTSP_Params* inParamBlock);
static QTSS_Error DoSetup(QTSS_StandardRTSP_Params* inParamBlock);
static QTSS_Error DoDescribeAndSetup(QTSS_StandardRTSP_Params* inParamBlock, bool isMP2TSFlag=false);
static QTSS_Error DoPlay(QTSS_StandardRTSP_Params* inParamBlock, bool sendResponseFlag = 1);
static QTSS_Error DoPause(QTSS_StandardRTSP_Params* inParamBlock);
static QTSS_Error SendPackets(QTSS_RTPSendPackets_Params* inParams);
static QTSS_Error DestroySession(QTSS_ClientSessionClosing_Params* inParams);
static QTSS_Error CloseRTSPSession(QTSS_RTSPSession_Params* inParams);

static handler_t arts_handle_controller_fdevent(void *s, void *ctx, int revents);
static handler_t arts_handle_adapter_fdevent(void *s, void *ctx, int revents);
static handler_t arts_handle_listener_fdevent(void *s, void *ctx, int revents);
static void arts_rtsp_reg_handler(void *ctx, unsigned char regid);
static void arts_rtsp_conresp_handler(void *ctx, arts_session *p_sess, int count, arts_ph_keyValue *p_keyValuePairs);
static void arts_rtsp_conrel_handler(void *ctx, arts_session *p_sess, unsigned int cause);
static void arts_rtsp_flush_handler(void *ctx, arts_session *p_sess, unsigned int flush);
static void arts_rtsp_shutdown_handler(void *ctx, unsigned int ServerState);
static arts_system_load_state  arts_rtsp_loadstatus_handler();
static void insert_sess_buffer(arts_session *sess,uint8_t *buf,int buf_size,int64_t timestamp,bool start,bool end);


static QTSS_Error GetARTSHandleDir();
static void UpdateRTSPStats(QTSS_ClientSessionObject &inClientSession,arts_session *sess);

static QTSS_Error DoDefault(QTSS_StandardRTSP_Params* inParams);

static SInt64 GetMPEG2PCR(UInt8 *packetData, UInt32 *remainPacketLength);
int64_t GetseekStartDts(arts_session *sess);
QTSS_Error send_play_request(UInt32 callid);
static  int64_t GetFrameDuration(arts_session *sess);
//--- this function changes the timestamp of audio and video packet in such a way that they are aligned in
//absolute running time
static void SetStreamsOffset(arts_session *sess);
static void sendAnnouce(arts_session * sess);
static bool  isValidateSEI(char * l_buf,int in);
custom_struct_t * new_custom_struct(arts_session *sess);
QTSS_Error insertPktInBuf(arts_session * cur_sess,rtp_packet_buffer_type * rtp_packet_buffer);
static void get_pts(arts_session * sess);
static QTSS_Error TearDownSession(arts_session *sess,QTSS_StandardRTSP_Params* inParams,int state);
static QTSS_Error  DoTearDown(QTSS_StandardRTSP_Params* inParams);
static int  CheckProfile(QTSS_StandardRTSP_Params* inParams);

static int send_sei=0;

//**************************************************
// CLASS DECLARATIONS
//**************************************************



//------------------------------------------------------------------------
// CONTROLLER INTERFACE IMPLEMENTATION
//------------------------------------------------------------------------
ARTS_PH_Interface::ARTS_PH_Interface() :
    OSThread()
{
    
    ev = fdevent_init(4096, FDEVENT_HANDLER_LINUX_SYSEPOLL);
    control_state = ARTS_CONTROLLER_STATE_UNSET;
    controlsock = iosocket_init();
    listensock = iosocket_init();
    last_co_socket = NULL;   
    last_stop_event = NULL;
    //mult_util_list = NULL;
   // last_util = NULL; 
   
   ht_init(&multicast_ht, 4096, offsetof(multicast_t, hh_multicast));
   
    controllertimeout = 0;
    phid = 0;
    callid = 0;
    listenport = 0;
    co_socket_list = NULL;
    polltimeout =1000;
    stop_events = NULL;
    arts_handle_controller_callbacks(arts_rtsp_reg_handler, 
                                     arts_rtsp_conresp_handler, 
                                     arts_rtsp_conrel_handler, 
                                     arts_rtsp_shutdown_handler, 
                                     arts_rtsp_flush_handler, 
                                     arts_rtsp_loadstatus_handler);
}



ARTS_PH_Interface::~ARTS_PH_Interface()
{
    iosocket_free(controlsock);
    iosocket_free(listensock);
    ht_destroy(&multicast_ht);
    fdevent_free(ev);
}


ARTS_Get_Packet::~ARTS_Get_Packet()
{
    sess = NULL;
    inParams = NULL;
}
//for dump data from adapter
FILE * arts_open_file()
{
    char template_file[L_tmpnam];
    memset(template_file,0,sizeof(template_file)) ;
    strcpy(template_file, "/tmp/megXXXXXX");  
    char *ifile = mktemp(template_file);
    FILE *fb = fopen(ifile,"wb+");  
    if(fb==NULL){
       qtss_printf("open receive_pkt failed\n");
    }
    else{       
       qtss_printf("%s input open success\n",ifile);
       return fb;
    }
    
    return NULL;
}

void report_bitrate(void *isess)
{
    if(isess == NULL)
        return;
    arts_session *sess = (arts_session*)isess;
    UInt64 CurrentTime = QTSS_Milliseconds() ;
    UInt64 TimeInterval = (CurrentTime - sess->last_rtsp_bitrate_update_time);
    if(TimeInterval < 1000)
        return;
    
    custom_struct_t *custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
    UInt32 theBitRate = (sess->rtp_mpeg2_bytes_sent*8/ (TimeInterval/1000));
    if(custom_struct->last_err_num >0 && custom_struct->err_num < custom_struct-> last_err_num)
    {
        
        LogRequest(INFO_ARTS_MODULE, sess->callid,"try to change bitrate from %u ,last_err_num:%d,cur_err_num:%d",theBitRate,custom_struct->last_err_num,custom_struct->err_num );
        theBitRate += 50*8*1024;
    }
    custom_struct->last_err_num = custom_struct->err_num;
    custom_struct->err_num =0;
    arts_send_bandwith(sARTSPHInterface->controlsock->fd,sess,theBitRate,0);
    {
    OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
    sess->rtp_mpeg2_bytes_sent = 0;				  
	sess->last_rtsp_bitrate_update_time = CurrentTime;
	}
    LogRequest(INFO_ARTS_MODULE, sess->callid, "Send to Controller.current_bandwidth=%d,time=%"_64BITARG_"d ",theBitRate,CurrentTime);
}

memcached_st * conn(char *host,UInt32 callid)
{
   
    char config_string[256]={'\0'};
    sprintf(config_string,"--SERVER=%s",host);
    memcached_st *memc= memcached(config_string, strlen(config_string));
    if (memc == NULL){
             LogRequest(INFO_ARTS_MODULE,callid,"connet to %s failed,config_string:%s",host,config_string);
           return NULL;

    }
    
    return memc;
}

int set(memcached_st * memc,char *channel_id,UInt32 callid,char *value)
{
    if(channel_id == NULL || memc == NULL)
        return -1;   
          
    char key[256]={'\0'};
    sprintf(key,"channel%s_access",channel_id);	
	
	int exp_local = couchbase_exp;
	
    memcached_return_t rc= memcached_set(memc,
                                key,
                                strlen(key),
                                value,
                                strlen(value),
                                exp_local,
                                (uint32_t)0);


    if (rc != MEMCACHED_SUCCESS ){
     LogRequest(INFO_ARTS_MODULE,callid," store data failed");

    }else{

     LogRequest(INFO_ARTS_MODULE,callid," store data success");
    }
    
    return rc;
}

int get_sdp(memcached_st * memc,char *channel_id,UInt32 callid,char *value)
{
  
    if(channel_id == NULL || memc == NULL)
        return NULL;
     char key[256]={'\0'};
	 sprintf(key,"channel%s_sdp",channel_id);  
     char *outval = NULL;
     
     size_t out_size = 0;
     uint32_t flag =0;
     memcached_return_t  rc;
     outval = memcached_get(memc, key, strlen(key),
                                (size_t*)&out_size,
                                &flag,
                                &rc);
     if (rc != MEMCACHED_SUCCESS ){
        LogRequest(INFO_ARTS_MODULE,callid,"get data failed \n");
                        
     }else{
         strcpy(value,outval);       
         
         LogRequest(DEBUG_ARTS_MODULE,callid,"get data success %s \n",outval);
         free(outval);
     }
     
     return rc;

}

int get_udp_host(memcached_st * memc,char *channel_id,UInt32 callid,char *value)
{
     if(channel_id == NULL || memc == NULL)
        return NULL;
     char key[256]={'\0'};
	 sprintf(key,"channel%s_udp",channel_id);  
     char *outval = NULL;
     
     size_t out_size = 0;
     uint32_t flag =0;
     memcached_return_t  rc;
     outval = memcached_get(memc, key, strlen(key),
                                (size_t*)&out_size,
                                &flag,
                                &rc);
     if (rc != MEMCACHED_SUCCESS ){
        LogRequest(INFO_ARTS_MODULE,callid,"get data failed \n");
                        
     }else{
         strcpy(value,outval);       
         
         LogRequest(DEBUG_ARTS_MODULE,callid,"get data success %s \n",outval);
         free(outval);
     }
          
     return rc;

}

int release_conn(memcached_st * memc)
{
    if(memc != NULL)
        memcached_free(memc);
}


/*

void del_mul_list(UInt32 callid)
{
    mult_util_t *p = sARTSPHInterface->mult_util_list;
    if( p== NULL)
    return ;
    mult_util_t * prev = p;
    mult_util_t * q = NULL;
    
    while(p)
    {
        if(p->callid == callid)
        {
            break;
        }else
        prev =p;
        p = (mult_util_t *)p->next;
    }
    LogRequest(DEBUG_ARTS_MODULE, callid,"delete mulicast node:%x",p);
    if(prev == p )
    {
        sARTSPHInterface->mult_util_list = (mult_util_t *)p->next;
        if (p ==  sARTSPHInterface->last_util)
        {           
            sARTSPHInterface ->last_util = NULL;
        }
        q = p;
        q->next = NULL;
        
        free(q);
    }else
    {
        q= p;
        if( sARTSPHInterface ->last_util == p)
        {
            sARTSPHInterface ->last_util = prev;
        }        
        
        prev->next = p->next;
        q->next = NULL; 
        free(q);       
    }
    
}

void insert_mul_list(unsigned int callid, int fd)
{
    mult_util_t * node = (mult_util_t *)calloc(1,sizeof(mult_util_t));
    Assert(node != NULL);
    Assert(sARTSPHInterface != NULL );
    node->callid = callid;
    node->fd = fd;
    LogRequest(DEBUG_ARTS_MODULE, callid,"insert mulicast node:%x,fd:%d",node,fd);
    if(sARTSPHInterface->mult_util_list == NULL)
    {
        sARTSPHInterface->mult_util_list = node;   
        
    }else
    {    
       sARTSPHInterface->last_util->next = node;             
    }    
    sARTSPHInterface->last_util = node;
}
*/

/*
unsigned int get_callid( int fd)
{
    mult_util_t *p = sARTSPHInterface->mult_util_list;
    Assert(p!=NULL);
    while(p)
    {
         LogRequest(DEBUG_ARTS_MODULE, p->callid,"multicast node fd:%d\n",fd);
        if(p->fd == fd)
        {
            return p->callid;
        }else
        p = (mult_util_t *)p->next;
    }
    
    return 0;
}
*/


static void insert_stopEventsList(UInt32 callid,int events,arts_session * sess,int fd)
{
    if(callid <=0 || sess == NULL)
    {
        LogRequest(INFO_ARTS_MODULE, callid,"insert_stopEventList callid:%d",callid);
        return;
     }
     
    event_node_t *newNode=(event_node_t*)malloc(sizeof(event_node_t));
    Assert(newNode!= NULL);
    newNode->next = NULL;
    newNode->callid = callid;
    newNode->events = events;   
    newNode->unregister =1;
    newNode->sess = sess;
    newNode->fd = fd;
    
    LogRequest(INFO_ARTS_MODULE, callid,"insert_stopEventList Node:%x,fd:%d",newNode,fd);
    
    if(sARTSPHInterface->stop_events == NULL)
    {
        sARTSPHInterface->stop_events = newNode;
        sARTSPHInterface->stop_events->next = NULL;
        sARTSPHInterface->last_stop_event = sARTSPHInterface->stop_events;
        return ;
    }else
    {
        sARTSPHInterface->last_stop_event  -> next = newNode;   
        sARTSPHInterface->last_stop_event = newNode;
         return ; 
    }  
    
}

static void del_events_node(int fd)
{
    bool find_sess =0;
    event_node_t * p= sARTSPHInterface->stop_events;
    if(p == NULL ||fd <0)
        return;
    
    event_node_t * prev = p;
    LogRequest(INFO_ARTS_MODULE, 0,"Entry fd:%d",fd);
    while(p)
    {
        //arts_session * cur_sess= arts_session_find(p->callid);
        //LogRequest(INFO_ARTS_MODULE, p->callid,"cur node :%x,fd:%d",p,p->fd);
        if( p->fd == fd){
            find_sess =1;
            LogRequest(INFO_ARTS_MODULE, p->callid,"get event node ,will free it,fd:%d",fd);
            break;
        }
        prev = p;
        p=(event_node_t *)p->next;
    }
    
    if(find_sess == 0)
    {
        LogRequest(INFO_ARTS_MODULE,0,"no event node,fd:%d",fd);
        return ;
    }
    
    
    
    if(  p == prev )
    {
        if( p == sARTSPHInterface->last_stop_event)
        {
            sARTSPHInterface->last_stop_event = NULL;
        }
        
        
        sARTSPHInterface->stop_events = (event_node_t*)p->next;            
        prev->next = NULL;
        free(prev);
        LogRequest(DEBUG_ARTS_MODULE, 0,"free");
    }else
    {       
        if( p == sARTSPHInterface->last_stop_event)
        {
            sARTSPHInterface->last_stop_event = prev;
        }
        prev->next = p->next;
        p->next= NULL;
        free(p);
    }
    
}

static void set_unregister(arts_session * sess)
{
    event_node_t * p= sARTSPHInterface->stop_events;
    int find_sess =0;
    
    while(p)
    {
       
       arts_session * cur_sess=(arts_session *)p->sess;  
       if(cur_sess == NULL)
       {
            p = (event_node_t*)p->next;
            continue;    
        }    
        
        if( cur_sess->callid == sess->callid)
        {
            find_sess =1;
            LogRequest(DEBUG_ARTS_MODULE, sess->callid,"get session,we will set_unregister");
            break;
        }  
       p = (event_node_t*)p->next;
    }
    if(p != NULL && find_sess == 1)
        p->unregister = 1;
    
}


void clear_buffer(arts_session *sess)
{

    custom_struct_t *cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
    Assert(cur_struct != NULL);
    ARTS_Get_Packet *sARTSGetPacket= (ARTS_Get_Packet*) cur_struct->receive_pkts_thread;
    if(sARTSGetPacket == NULL)
        return;
    OSMutexLocker lockerBuffer(&sARTSGetPacket->bufferMux); 
    if(sess->rtp_packet_buffer)
    {
        rtp_packet_buffer_type * p = sess->rtp_packet_buffer;
        while( p )
        {
            sess->rtp_packet_buffer = (rtp_packet_buffer_type *)sess->rtp_packet_buffer -> next;
            p->next = NULL;
            
            free(p->pkt_buf);
            free(p);
            
            p=sess->rtp_packet_buffer;            
        }
    }
    
}




void insert_buffers(arts_session *sess,rtp_packet_buffer_type *rtp_packet_buffer)
{
    Assert(sess!= NULL);
    Assert(rtp_packet_buffer != NULL);
   
     if(sess->rtp_packet_buffer == NULL)
     {
        sess->rtp_packet_buffer = rtp_packet_buffer; 
        sess->rtp_packet_buffer->next = NULL; 
                           
            
     }else            
     {
        
        if(sess->last_rtp_packet != NULL){
            rtp_packet_buffer->next = NULL;
            rtp_packet_buffer_type * p_last = (rtp_packet_buffer_type*)sess->last_rtp_packet;
           
            //if(p_last->timestamp == 0 && rtp_packet_buffer->timestamp >0)
            //{
            //    p_last->timestamp = rtp_packet_buffer->timestamp;
            //}
            
            //if(rtp_packet_buffer->pkt_len >5)
            {
                 p_last->next = rtp_packet_buffer;  
                 sess->last_rtp_packet  = rtp_packet_buffer;
            }
            
            LogRequest(DEBUG_ARTS_MODULE, sess->callid,"insert after sess->last_rtp_packet");
        }          
        else
        {
     
            rtp_packet_buffer_type *p = sess->rtp_packet_buffer;
            rtp_packet_buffer_type *prev=NULL;
            while( NULL != p->next && rtp_packet_buffer->timestamp >= p->timestamp)
            {
                prev=p;
                p=(rtp_packet_buffer_type *)p->next;    
            }
                
            if(p == sess->rtp_packet_buffer)                
            {
                if(rtp_packet_buffer->timestamp < p->timestamp){
                    rtp_packet_buffer->next = sess->rtp_packet_buffer;
                    sess->rtp_packet_buffer = rtp_packet_buffer ;   
                }else {
                    rtp_packet_buffer->next = sess->rtp_packet_buffer->next;
                    sess->rtp_packet_buffer->next = rtp_packet_buffer;
                }
                                      
            }else if( p->next == NULL && rtp_packet_buffer->timestamp >= p->timestamp)
            {
                rtp_packet_buffer->next = p->next;
                p->next = rtp_packet_buffer;                                    
            }
            else
            {
                rtp_packet_buffer -> next = prev->next;
                prev->next= rtp_packet_buffer;
            }
        }         
    }  
    if(sess->transport_type == qtssRTPTransportTypeMPEG2 || no_describe == true)
    {
        sess->last_rtp_packet  = rtp_packet_buffer;  
        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"sess->last_rtp_packet:%x",sess->last_rtp_packet); 
    }   
}


int64_t GetBufferLastTimestamp(arts_session *sess)
{
    Assert(sess!= NULL)
    if(sess->rtp_packet_buffer == NULL)
    return 0;
    else
    {
        rtp_packet_buffer_type *p = sess->rtp_packet_buffer;
        while(p->next !=NULL){
            p=(rtp_packet_buffer_type *)p->next;          
         }
            
        return p->timestamp;
    }
}
void refresh_socket_list(UInt32 callid,custom_struct_t *custom_struct,bool cleanall)
{
    co_socket_t *p = sARTSPHInterface->co_socket_list;
    if(p!= NULL)
     LogRequest(DEBUG_ARTS_MODULE, callid,"co_socket_list:%x",p);
    co_socket_t *prev = p;
    while(p)
    {
        if(p->used == true && (custom_struct!= NULL && custom_struct->RTSPSessionObj == p->rtspSessObj && custom_struct->RTPSessionObj == 
        p->rtspSessObj )|| cleanall==true)
        {
            //prev = p;          
            co_socket_t *q=p;
            if(prev == p)
            {
                sARTSPHInterface->co_socket_list =(co_socket_t *) p->next;
                p= sARTSPHInterface->co_socket_list;
                if( p == sARTSPHInterface->last_co_socket)
                {
                    sARTSPHInterface->last_co_socket = NULL;
                }
              
            } else
            {
                if(sARTSPHInterface->last_co_socket == p)
                {
                    sARTSPHInterface->last_co_socket = prev;
                }
                
                prev->next = p->next;
                p=(co_socket_t *)p->next;
            }   
            LogRequest(DEBUG_ARTS_MODULE, callid,"delete socket_list:%s",q->sessionID);           
            q->next = NULL;
            free(q); 
             
            if(cleanall == false)
            {
                break;
            }       
        }
        if(cleanall == false)
        {   
            prev =p;
            p=(co_socket_t *)p->next;
        }
    }  
    
}


void stop_getpkts_thread(arts_session*sess)
{
       
     if(sess == NULL)
        return;     
     if((sess)->darwin_custom_struct != NULL)
     {
        custom_struct_t *custom_struct = ( custom_struct_t *)(sess)->darwin_custom_struct;
        if( custom_struct->receive_pkts_thread!= NULL)
        {        
            ARTS_Get_Packet *sARTSGetPacket = (ARTS_Get_Packet *) custom_struct->receive_pkts_thread;
            custom_struct->receive_pkts_thread= NULL;
            //OSMutexLocker locker(&sARTSGetPacket->bufferMux);
            LogRequest(INFO_ARTS_MODULE, sess->callid,"stop thread");
              
            delete sARTSGetPacket;  
            sess->receive_pkts_task = NULL;
        }
     }
}

static void sendAnnouce(arts_session * sess)
{
    if(sess == NULL )
    return;
    UInt32 err=0;
    custom_struct_t * custom_struct = (custom_struct_t*)sess->darwin_custom_struct;
    LogRequest(DEBUG_ARTS_MODULE,sess->callid,"Entry,custom_struct:%x,custom_struct->supportPTS:%d",custom_struct,custom_struct->supportPTS);
    if(custom_struct != NULL)
    {
        RTSPSessionInterface * theRTSPSess =(RTSPSessionInterface *)custom_struct->OwnRTSPSessionObj;
        if(theRTSPSess == NULL)
            return;
            
        RTSPResponseStream *fOutputStream = theRTSPSess->GetOutputStream();
        char annReq[2048]={'\0'};
        if(sARTSJinShanModule == true)
        {
        sprintf(annReq,"SET_PARAMETER %s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\nServer: ARTSRTSPStreamingServer\r\nx-Info:\"EOS\"\r\nx-Reason: \"EOS\"\r\nx-Code: 9\r\n\r\n",custom_struct->uri,custom_struct->cseq+1,custom_struct->sessionID);
        }
        
        
        if(sARTSszModule == true)
        {
            sprintf(annReq,"ANNOUNCE %s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\nOnDemandSessionId: %s\r\nRequire: com.comcast.ngod.c1, com.comcast.ngod.c1.decimal_npts\r\nNotice: 2101 \"End-of-Stream Reached\"  npt=%.3f\r\nStreamStatus: presentation-state=\"ready\" scale=1 event-reason=0x00\r\n\r\n",custom_struct->uri,custom_struct->cseq+1,custom_struct->sessionID,custom_struct->sessionID,sess->total_content_length*1.0);
        }
        
        if(custom_struct->supportOtherChannel==true)
        {
            sprintf(annReq,"ANNOUNCE %s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\nX-notice: EOS\r\n\r\n",custom_struct->uri,custom_struct->cseq+1,custom_struct->sessionID); 
        }else if (custom_struct->supportPTS == true)
        {
            sprintf(annReq,"SET_PARAMETER %s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\nServer: ARTSRTSPStreamingServer\r\nx-Info:\"CLOSE\"\r\nx-Reason: \"END\"\r\n\r\n",custom_struct->uri,custom_struct->cseq+1,custom_struct->sessionID);           
        }
        
        else if(custom_struct->isIpqam == true)
        {
            if(no_describe == true)
            {
            sprintf(annReq,"ANNOUNCE %s RTSP/1.0\r\nCSeq: 2\r\nSession: %s\r\nTianShan-Notice: 0001::0001 End-of-Stream Reached\r\nTianShan-NoticeParam: npt=\r\n\r\nANNOUNCE %s RTSP/1.0\r\nCSeq: 1\r\nSession: %s\r\nTianShan-Notice: 0001::0003 State Changed\r\nTianShan-NoticeParam: npt=0.000;presentation_state=stop\r\n\r\n",custom_struct->uri,custom_struct->sessionID,custom_struct->uri,custom_struct->sessionID);
            }else
            sprintf(annReq,"SET_PARAMETER %s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\nServer: ARTSRTSPStreamingServer\r\nx-Info:\"CLOSE\"\r\nx-Reason: \"END\"\r\n\r\n",custom_struct->uri,custom_struct->cseq+1,custom_struct->sessionID);
        }
        
        LogRequest(DEBUG_ARTS_MODULE,sess->callid,"%s",annReq);
        
        if(strlen(annReq)>0)
        {
            StrPtrLen  annReqStr(annReq,strlen(annReq));            
            fOutputStream->Put(annReqStr);
            err = fOutputStream->Flush();
            Assert( err == QTSS_NoErr);
        }        
    }    
}

void modify_duration(arts_session * sess)
{
    
    if(sess == NULL)
        return ;
    custom_struct_t * custom_struct = (custom_struct_t*)sess->darwin_custom_struct;
    int64_t cur_duration = GetFrameDuration(sess);
    if(cur_duration >0)
    {
         custom_struct->buf_duration -= cur_duration; 
    }
            
    if(custom_struct->buf_duration <0)
        custom_struct->buf_duration =0;
}


int get_packet(char *pkt_buf,int total_len ,void *isess)
{
     
    Assert(isess!=NULL);
    Assert(pkt_buf!=NULL);
    arts_session * sess = (arts_session *)isess;
    custom_struct_t * custom_struct = (custom_struct_t*)sess->darwin_custom_struct;
    if(custom_struct == NULL)
        return -1;
    timeout_ev_t *curSendEv = (timeout_ev_t *)custom_struct->sendEv;
    
    
    if(curSendEv->pause == true)
    {
       LogRequest(INFO_ARTS_MODULE, sess->callid,"pause is true");
        return 0;
    }
    
    if(custom_struct->pause_ready == true)
    {
        custom_struct->pause_ready= false;
        LogRequest(INFO_ARTS_MODULE, sess->callid,"pause_ready is true");
    }
    
    if(sess->head.state & ARTS_CALL_STATE_DESTROY)
    {
           custom_struct->send_idel_time += 20;
           if(custom_struct->send_idel_time >= 2000  )
           {
                LogRequest(DEBUG_ARTS_MODULE, sess->callid,"data send finished!");                 
                sendAnnouce(sess);
                return -1;
           }
           return 0;
    }
    
  
    char *buf = pkt_buf;
    int pkt_num =0,real_len=0;
    
    if(total_len <=0)
        return 0;
     
    //LogRequest(INFO_ARTS_MODULE, sess->callid,"sess->rtp_packet_buffer_len:%d,total_len:%d",sess->rtp_packet_buffer_len,total_len);
    if(sess->rtp_packet_buffer_len == 0)
    {   
        LogRequest(INFO_ARTS_MODULE, sess->callid,"sess->rtp_packet_buffer_len:%d,total_len:%d",sess->rtp_packet_buffer_len,total_len);
        return 0;
    }
       
    int left_len = ARTSMIN(total_len,TS_TCP_PACKET_SIZE); 
    
    if(custom_struct->seek == true && custom_struct->seek_start_dts >=0)
    {
        custom_struct->seek = false;
    }
    ARTS_Get_Packet *sARTSGetPacket = (ARTS_Get_Packet *)custom_struct->receive_pkts_thread;
    
    
    if(inputfile!=NULL)
    {
        
        int real_len=fread(pkt_buf,sizeof(char),7*188,inputfile);
        return real_len;
    }
    
    OSMutexLocker lockerBuffer(&sARTSGetPacket->bufferMux); 
    while(left_len&& sess->rtp_packet_buffer_len >0)
    {
       
        rtp_packet_buffer_type *cur = sess->rtp_packet_buffer;
        if(cur == NULL)
            break;
                   
        int tmp1= ARTSMIN(TS_TCP_PACKET_SIZE,left_len);
        
        int real_num = ARTSMIN(tmp1,cur->pkt_len); 
        
        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"cur_pkt_len:%d,real_num:%d",cur->pkt_len,real_num);
        
        left_len -= real_num;   
                                        
        memcpy(buf,cur->pkt_buf,real_num);
        real_len += real_num;
        buf+= real_num;
        cur->pkt_len -= real_num;
                    
        if (cur->pkt_len>0)
        {
            char tmp_pkt[TS_TCP_PACKET_SIZE];
            memcpy(tmp_pkt,cur->pkt_buf+real_num,cur->pkt_len);
            memcpy(cur->pkt_buf,tmp_pkt,cur->pkt_len);
        }
        custom_struct->send_idel_time =0;          
        
        pkt_num = real_len/TS_PACKET_SIZE;                         
        //LogRequest(DEBUG_ARTS_MODULE, sess->callid,"real_len:%d,pkt_num:%d",real_len,pkt_num);           
                       
        
        if(cur->pkt_len ==0)  
        {  
             modify_duration(sess);
                                                
                          
            if(sARTSGetPacket != NULL)
            {                                            
               
                if(sess->rtp_packet_buffer == NULL)
                {
                    return 0;
                }
                                          
                sess->rtp_packet_buffer =(rtp_packet_buffer_type *) cur->next; 
                LogRequest(DEBUG_ARTS_MODULE, sess->callid,"this packets PCR:%u,custom_struct->buf_duration:%d",cur->timestamp,custom_struct->buf_duration);
                cur->next = NULL;
                free(cur->pkt_buf);
                free(cur);
                sess->rtp_packet_buffer_len -= 1;        
            }else
            {
                sess->rtp_packet_buffer =(rtp_packet_buffer_type *) cur->next; 
                LogRequest(DEBUG_ARTS_MODULE, sess->callid,"this packets PCR:%u",cur->timestamp);
                cur->next = NULL;
                free(cur->pkt_buf);
                free(cur);
                sess->rtp_packet_buffer_len -= 1;     
            }    
        }  
                     
        if(pkt_num >= 7 )
        {                        
               break;         
        }
    }                                
           
    
end:
    if(custom_struct->ofd !=NULL)
    {
        fwrite(pkt_buf,sizeof(char),real_len,custom_struct->ofd);
    }                            
    return real_len;         
}


void free_custom_struct(arts_session * sess,bool * need_free_sess)
{

     if(inputfile != NULL)
     {
        fclose(inputfile);
        inputfile  = NULL;
     }       
                
     if(sess == NULL)
     {
        LogRequest(INFO_ARTS_MODULE, 0,"session is null");
        return ;
     }   
     arts_session_head *sess_head =(arts_session_head *)sess;
     if(sess_head->sock)
     {
        del_mul_list(sess_head->sock->fd);
     }
     // LogRequest(INFO_ARTS_MODULE, 0,"sess->darwin_custom_struct=%x",sess->darwin_custom_struct);
     if(sess->darwin_custom_struct != NULL)
     {
        stop_getpkts_thread(sess);
     
        custom_struct_t * custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
       
        refresh_socket_list(sess->callid,custom_struct,false);
        
        if( custom_struct->RTSPSessionObj != NULL )
        {
            RTSPSession * theRTSPSess = (RTSPSession *) custom_struct->RTSPSessionObj;
            Assert(theRTSPSess!= NULL); 
            LogRequest(INFO_ARTS_MODULE, 0,"ARTSMOdule cleanup,obj:%x\n",custom_struct->RTSPSessionObj);              
            //delete theRTSPSess;//use delete install of killsignal dueto  first one can validate at once
            theRTSPSess->Signal(Task::kKillEvent);
            theRTSPSess->cleanObjectHolderCount(true);
            custom_struct->RTSPSessionObj = NULL;        
        }     
        
        
        if(custom_struct->adapter_unregister == 1 && need_free_sess != NULL)
        {
            (*need_free_sess) =1;
        }
           
        if(custom_struct->sendEv!= NULL)
        {
            
            free(custom_struct->sendEv);
            custom_struct->sendEv = NULL;
        }
       
        if(custom_struct->tsctx != NULL)
        {
            MpegTSContext *ts = (MpegTSContext *)custom_struct->tsctx;
            delete_ts_prg(ts);
            delete_sevice(ts);
            free(custom_struct->tsctx);
            custom_struct->tsctx = NULL;
        }
       
         if(custom_struct->ifd !=NULL)
        {   
            fclose(custom_struct->ifd);
            custom_struct->ifd= NULL;
        }
        if(custom_struct->ofd!=NULL)
        {
            fclose(custom_struct->ofd);
            custom_struct->ofd =NULL;
        }  
      
        free(sess->darwin_custom_struct);
        sess->darwin_custom_struct = NULL;        
        LogRequest(INFO_ARTS_MODULE, 0,"free sess->darwin_custom_struct=%x",sess->darwin_custom_struct);
    }
    
 }


static void *get_last_pcr(arts_session * sess,MpegTSContext *ts)
{
    int64_t last_pcr=0;
    rtp_packet_buffer_type * p = sess->rtp_packet_buffer;
    rtp_packet_buffer_type * q = NULL;
    
    while(p )
    {
        
        if(p->next)
        {
           rtp_packet_buffer_type *pnext= (rtp_packet_buffer_type *)p->next;           
           if(pnext->start == true){
                last_pcr =p->timestamp; 
                LogRequest(DEBUG_ARTS_MODULE, sess->callid,"This PKt is pcr,ts:%u,last_pcr:%u",pnext->timestamp,last_pcr);
                q =p;
           }
         }
        else if(p->next == NULL && p->start == true && p ==sess->rtp_packet_buffer)
        {
            q= sess->rtp_packet_buffer;
        }
        
        p =( rtp_packet_buffer_type *)p->next;
    }
    
    //modify the following pkts timestamp    
    
   if(q!=NULL)
   {
     ts->cur_pcrMS = last_pcr;
     rtp_packet_buffer_type * qq =(rtp_packet_buffer_type*) q->next;
     while(qq!= NULL)
     {
        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"modify timestamp from %u to %u",qq->timestamp,last_pcr);
        qq->timestamp = last_pcr;
        qq = (rtp_packet_buffer_type*)qq->next;        
     }
   }
        
        
    return q;
}



 int delete_pcr_pkt(unsigned int callid,MpegTSContext *ts)
{

    //OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex); 
    LogRequest(DEBUG_ARTS_MODULE, callid,"Entry delete_pcr_pkt");
    
    arts_session *sess = arts_session_find(callid);            
    if(sess ==NULL )
         return QTSS_RequestFailed; 
   
   if(sess->rtp_packet_buffer == NULL || sess->rtp_packet_buffer_len <=0)
        return 0;
    custom_struct_t * custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
    if(custom_struct != NULL)
     custom_struct->del_pcr_flag = true;    
        
    rtp_packet_buffer_type * p= (rtp_packet_buffer_type *)get_last_pcr(sess,ts);
    rtp_packet_buffer_type *q = NULL;
    
    if(p == NULL)
        return 0;
    if(p == sess->rtp_packet_buffer)
    {
        q= p;
        q->next = NULL;
        sess->rtp_packet_buffer = NULL;
        sess->rtp_packet_buffer_len = 0;
        free(q->pkt_buf);
        free(q);
    }else
    {
        q = (rtp_packet_buffer_type *)p->next;
        p ->next = q->next;
        q->next = NULL;
        free(q->pkt_buf);
        free(q);
        sess->rtp_packet_buffer_len --;
    }
    
   return 0;
}

//used in Ipqam CBR

int insert_one_pkt(MpegTSContext *ts,UInt32 callid,unsigned char *buf,int buf_size,int64_t pcr)
{
    arts_session * sess =arts_session_find(callid);
   Assert(sess!= NULL);
   bool finish = false;
   bool start = false;
   bool real_end=false;
   
   //when delete one pcr pkts from sess->rtp_packet_buffer, modify ts->sess_pkt_timestamp immediatily
   custom_struct_t * custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
   if(custom_struct != NULL && custom_struct->del_pcr_flag == true)
   {
       ts->sess_pkt_timestamp = ts->cur_pcrMS;
       custom_struct->del_pcr_flag =false;
   }
   LogRequest(DEBUG_ARTS_MODULE, sess->callid,"Entry,ts->buf_len:%d,pcr:%u",ts->buf_len,pcr);
   
   //new pcr pkt,need fresh ts->sess_pkt_timestamp
  
   
   while(true)
   {
   
        if(ts->buf_len >= TS_TCP_PACKET_SIZE || pcr >=0 )  
        {
            if(pcr>=0 && finish ==1)
            {
                start = true;
                real_end = true;
            }
            insert_sess_buffer(sess,ts->buf,ts->buf_len,ts->sess_pkt_timestamp,start,real_end); 
              
            ts->buf_len =0;
            
            if(start == true)
            {
                
                break;
            }
            
        }
        if(ts->cur_pcrMS >0)
            ts->sess_pkt_timestamp = pcr >1 ?pcr:ts->cur_pcrMS;
        memcpy(ts->buf+ts->buf_len,buf,buf_size);
        ts->buf_len += buf_size;        
        finish =1;
        
        if(pcr <0)
        {           
            break;
        }
               
    }
    return 0;
}

static int  handle_RTP_Pkt(arts_session * cur_sess,UInt32 callid,custom_struct_t * custom_struct,rtp_packet_buffer_type * rtp_packet_buffer)
{
    int valid_pkt =true;
    
    UInt32* theTimeStampP = NULL;
    UInt16* theSeqNumP = NULL;
    UInt32 theTimeStamp =0;
    UInt16 theSeqNum =0;
    
    UInt32* dtsP =NULL;
    UInt32 theDts =0;
    UInt8* padding_lenP = NULL;
    UInt8  padding_len =0;
    if(rtp_packet_buffer == NULL || custom_struct == NULL)
        return 0;
        
    QTSS_PacketStruct thePacket;
    thePacket.packetData = rtp_packet_buffer->pkt_buf;
   
    Assert(cur_sess!=NULL); 
    
    if(rtp_packet_buffer->pkt_len >5)
    {
        theTimeStampP = (UInt32*)thePacket.packetData;          
        theTimeStamp = ntohl(theTimeStampP[1]);                
        theSeqNumP = (UInt16*)thePacket.packetData;           
        theSeqNum = ntohs(theSeqNumP[1]);            
    }
    
    //if(!adapter_version_2 || rtp_packet_buffer->pkt_len ==5)
    {      
        padding_lenP = (UInt8*)(thePacket.packetData +rtp_packet_buffer->pkt_len-1);    
        padding_len=padding_lenP[0]; 
        LogRequest(DEBUG_ARTS_MODULE, callid ,"pad[0]:%x,padding_len:%d,pkt_len:%d",padding_len ,padding_lenP[0],rtp_packet_buffer->pkt_len);       
        dtsP =(UInt32*) ((UInt8*) ( thePacket.packetData + rtp_packet_buffer->pkt_len - padding_len ));                     
        theDts = ntohl(dtsP[0]);

    }
         
   
    
    
    if(live == true)
        rtp_packet_buffer->pkt_str =3;
    
    if(cur_sess->rtp_video_seqnum == 0xffff  && live == true)
    {
        cur_sess->rtp_video_seqnum = theSeqNum;
        cur_sess->rtp_video_timestamp = theTimeStamp;
    }
            
    //rtp_packet_buffer->timestamp =( ((SInt64)(theTimeStamp) * (SInt64) 1000) /
    //                         ( (rtp_packet_buffer->pkt_str==2) ? (SInt64)(sess->rtp_audio_clock_rate):
    //                              (SInt64)(sess->rtp_video_clock_rate)));  
                
                
    rtp_packet_buffer->timestamp = theDts;  
                
    if(custom_struct->seek== true & custom_struct->play_responsed == 0)
    {
        LogRequest(INFO_ARTS_MODULE, callid,"noplayresponse,and dts:%u ,drop it",theTimeStamp,rtp_packet_buffer->timestamp);
        free(rtp_packet_buffer->pkt_buf);
        free(rtp_packet_buffer);
        valid_pkt = false;
        return valid_pkt;
                    
    }
                              
              
    // seek low  when theTimeStamp < sess->rtp_video_timestamp,drop it
    if( ( (cur_sess->rtp_video_timestamp <cur_sess->rtp_last_video_timestamp &&  cur_sess->rtp_video_timestamp>0) ||  
            (cur_sess->rtp_audio_timestamp<cur_sess->rtp_last_audio_timestamp && cur_sess->rtp_audio_timestamp >0)) && live == false)
    {
        {
            goto end;
        }
                                                
        if( ((theTimeStamp <cur_sess->rtp_video_timestamp || theTimeStamp > cur_sess->rtp_last_video_timestamp)&& rtp_packet_buffer->pkt_str == 3 ) ||
           ((theTimeStamp <cur_sess->rtp_audio_timestamp || theTimeStamp > cur_sess->rtp_last_audio_timestamp) && rtp_packet_buffer->pkt_str == 2 ) )
        {
            LogRequest(INFO_ARTS_MODULE, callid,"seek low the rtptimestamp is %d,and dts:%u ,drop it",theTimeStamp,rtp_packet_buffer->timestamp);
            free(rtp_packet_buffer->pkt_buf);
            free(rtp_packet_buffer);
            valid_pkt = false;
            return valid_pkt;    
        }
    }
                                    
end:            
    //seek high
    if( (cur_sess->rtp_video_timestamp >cur_sess->rtp_last_video_timestamp || 
            cur_sess->rtp_audio_timestamp > cur_sess->rtp_last_audio_timestamp) && live == false)
    {
        if(theTimeStamp <cur_sess->rtp_video_timestamp && rtp_packet_buffer->pkt_str == 3 || 
                            theTimeStamp <cur_sess->rtp_audio_timestamp && rtp_packet_buffer->pkt_str == 2)
        {
            LogRequest(INFO_ARTS_MODULE, callid,"seek high the rtptimestamp is %d,and dts:%u ,drop it",theTimeStamp,rtp_packet_buffer->timestamp);
            free(rtp_packet_buffer->pkt_buf);
            free(rtp_packet_buffer);
            valid_pkt = false;
            return valid_pkt;
        }
    }
                    
    if(custom_struct->seek_start_dts < 0 && custom_struct->seek == true)
    {
        custom_struct->seek_start_dts = theDts;
        LogRequest(DEBUG_ARTS_MODULE, callid," new start dts:%"_64BITARG_"d",custom_struct->seek_start_dts);
    }          
          
    QTSS_TimeVal CurrentTime = QTSS_Milliseconds();   
    LogRequest(DEBUG_ARTS_MODULE, callid, "current_time:%"_64BITARG_"d,type = %c,buffer_len  = %d ,"
                   " Thetimestamp = %u ,dts = %u,seqNum=%u,pkt_len:%d" ,CurrentTime,TYPE_TO_C(rtp_packet_buffer->pkt_str),
                   cur_sess->rtp_packet_buffer_len,theTimeStamp,theDts,theSeqNum,rtp_packet_buffer->pkt_len);
   return valid_pkt; 
}


static bool isValidate_pkt(arts_session * sess,uint8_t *ptr,UInt32 callid,UInt32* pktLen)
{
    if(ptr == NULL)
        return false;
    Assert(sess!=NULL);  
     
    custom_struct_t * custom_struct = NULL; 
    bool error_indicator = false;
    bool priority_flag=false;
    bool is_start=false;
    int64_t timestampInMS=-1;  
    
    
    int64_t  PCR = GetMPEG2PCR(ptr,pktLen);  
    if(PCR >-1)
    {
        timestampInMS = PCR/27/1000;
        LogRequest(DEBUG_ARTS_MODULE, callid,"pcr-ms:%ld",timestampInMS);
    }  
    
    		
    custom_struct = (custom_struct_t *)sess->darwin_custom_struct; 
    
    LogRequest(DEBUG_ARTS_MODULE, callid,"ptr[0]:%X,ptr[1]:%x",ptr[0],ptr[1]);
    if(ptr[0]== 0x47)
    {                      
          if( (ptr[1]&0x80) !=0)
                error_indicator =true;
          if( (ptr[1]&0x20) != 0)
                priority_flag =true;
          if( (ptr[1]&0x40) !=0 )
                is_start =true;               
     }        
     
     if( error_indicator == true || priority_flag == true)
     {                       
            ptr[1]= (ptr[1] &0x5f);
           
            custom_struct->seek_start = true;
            
            //LogRequest(DEBUG_ARTS_MODULE, callid,"sess->mpeg2_start_time:%"_64BITARG_"d",sess->mpeg2_start_time);
            LogRequest(DEBUG_ARTS_MODULE, callid,"packet[1]:%X",ptr[1],ptr[5]);
            error_indicator = false;
            priority_flag = false;
            MpegTSContext *ts=( MpegTSContext *)(custom_struct->tsctx);
            ts->last_pcr =-1;
       } 
            
      if( (custom_struct->seek_start_dts<0 )&& custom_struct->seek == true)
        {
           if( custom_struct->seek_start ==false)
           {
              LogRequest(DEBUG_ARTS_MODULE, callid,"the PCR is %ld,drop it", timestampInMS);
              return false;
            }
        }
 
               
    if(timestampInMS>=0 && custom_struct->seek_start_dts <0 &&custom_struct->seek== true && custom_struct->seek_start==true)
    {                  
        custom_struct->seek_start_dts = timestampInMS;
        //ptr[5] |= 0x80;
        LogRequest(INFO_ARTS_MODULE, callid,"Get the new start,PCR:%u",timestampInMS);  
        custom_struct->seek_start = false;
        if(custom_struct != NULL && strlen(custom_struct->rangeHeader_global)>0)
        {   
            sess->head.state &= ~ARTS_CALL_STATE_PLAY_CMD_SENT; 
            LogRequest(DEBUG_ARTS_MODULE,callid,"will send Play");    
            send_play_request(callid);
            return false;  
        }         
    } 
  
    return true;         
}

static int handle_TS_Pkt_Ipqam(arts_session * sess,UInt32 callid,custom_struct_t * custom_struct,char * l_buf,int in)
{
    int valid_pkt = true;
    int ret =0;
    MpegTSContext *ts=( MpegTSContext *)(custom_struct->tsctx);
    UInt32 pktLen = in;
    LogRequest(DEBUG_ARTS_MODULE, callid,"in:%d",in);
    unsigned char * pkt =(unsigned char*) l_buf;
    
  
    Assert(sess!= NULL);
    
    while(pktLen >= TS_PACKET_SIZE)
    { 
           
        int pid =-1;             
        int64_t pcr=-1,pts =-1;
        int renew_bat = 0;
        int cc =0;
        bool end = false;
        bool need_get_pts = false;
        
        if(custom_struct->first_pts <0)
        {
            need_get_pts = true;
            LogRequest(DEBUG_ARTS_MODULE, callid,"will get pts");
        }
        
        if(custom_struct->seek == true && custom_struct->seek_start_dts <0)
        {
            if(isValidate_pkt(sess,pkt,callid,&pktLen)== false)
            {
                pktLen -= TS_PACKET_SIZE; 
                pkt += TS_PACKET_SIZE;               
                ts->mux_pos += TS_PACKET_SIZE; 
                continue;                
            }else
            {
                ts->cur_pcrMS =-1;               
            }
                 
        }
            
        if(custom_struct->seek == true && custom_struct->seek_start_dts >=0) 
        {   
             ts->sess_pkt_timestamp = custom_struct->seek_start_dts;
             //LogRequest(DEBUG_ARTS_MODULE, callid,"ts->seek_pkt_timestamp:%d",ts->sess_pkt_timestamp);
        }
        
        
        cc=handle_packet(ts, pkt,&pid,&pcr,NULL,NULL,(unsigned int )callid,&pts,need_get_pts);
         
        //LogRequest(DEBUG_ARTS_MODULE, callid,"pts:%d,first_pts:%d\n",pts,custom_struct->first_pts);
        if(pts>=0 && custom_struct->first_pts <0)
        {
            custom_struct->first_pts = pts;
            LogRequest(INFO_ARTS_MODULE, callid,"custom_struct->first_pts:%d",pts);
        } 
         
           
        ret=insert_pkts(ts,pcr,callid);
        
        
        if(ret != QTSS_NoErr)
            return ret;
         
        if(pid == PAT_PID)
        {
            add_service(ts,callid);
            write_new_psi(ts,pid,callid,NULL);
            renew_bat =1;
        }   
       
         
        if(pcr >-1)
        {   
            end = true;
        }
                
        if (renew_bat == 0 || (ts->pat_copy == 0 || (ts->pat_copy == 1&& pid !=0x00))  && ts->nb_services >0 && ts->hasKeyframe )
        {          
            ret=insert_one_pkt(ts,callid,pkt,TS_PACKET_SIZE,pcr);
            LogRequest(DEBUG_ARTS_MODULE, callid,"can install");
           
         }
        if(ret != QTSS_NoErr)
            return ret;
                   
        if(pcr>=0 )
        {
            ts->pcr_pkts_count = 1;  
            ts->need_pkts_num =0; 
            LogRequest(DEBUG_ARTS_MODULE, callid,"pcr:%d,ts->pcr_pkts_count:%d",pcr,1);          
         } 
     
         
        if(ts->last_pcr>=0 )
        {            
            modify_count(ts);
            LogRequest(DEBUG_ARTS_MODULE, callid,"last_pcr:%d,ts->pcr_pkts_count:%d,cur_pcrMS:%d",pcr,1,ts->cur_pcrMS);
        }
        
       
        pktLen -= TS_PACKET_SIZE; 
        pkt += TS_PACKET_SIZE;               
        ts->mux_pos += TS_PACKET_SIZE; 
       
        ts->cur_pcrMS = ARTSMAX(ts->cur_pcrMS,ts->last_pcr);   //new-insert-method  end  
        LogRequest(DEBUG_ARTS_MODULE, callid,"ts->cur_pcrMs:%d",ts->cur_pcrMS);
    }   
    
   
    if(sess->rtp_packet_buffer_len >0 && sess->rtp_packet_buffer!= NULL)
    {
        int64_t duration = ts->cur_pcrMS - sess->rtp_packet_buffer->timestamp;
        if(duration > MAXBUFFERDURATION)
        {
            custom_struct->buf_duration = duration;
            LogRequest(DEBUG_ARTS_MODULE, callid,"buf duration:%u",custom_struct->duration);
        }
    }   
    
   return 0;     
}


int64_t getLastPts(rtp_packet_buffer_type * rtp_buf,custom_struct_t * custom_struct)
{       
    return custom_struct->last_keyframe_pts;
}


rtp_packet_buffer_type * newRtpBuf(char * buffer,int len,int stream_type,arts_session *sess)
{
    char *first_packet = (char *) malloc(len);
    
    Assert(first_packet != NULL);
    memcpy(first_packet,buffer,len);           
    //LogRequest(INFO_ARTS_MODULE,sess->callid,"newrtpbuf:%x,len:%d",first_packet,len);         
    rtp_packet_buffer_type *rtp_packet_buffer =(rtp_packet_buffer_type *) malloc(sizeof(rtp_packet_buffer_type));
    rtp_packet_buffer->pkt_buf = first_packet;           
    rtp_packet_buffer->pkt_str = stream_type;
    rtp_packet_buffer->pkt_len = len ;
    rtp_packet_buffer->next = NULL;
    rtp_packet_buffer->start = false;
    rtp_packet_buffer->pkt_org_len = len;
     
    rtp_packet_buffer->timestamp =0;
    return rtp_packet_buffer;
}

rtp_packet_buffer_type *GetRtpBuf(char *buffer,int len,int stream_type,arts_session *sess)
{
    
    custom_struct_t *cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
    Assert(cur_struct != NULL);
    ARTS_Get_Packet *sARTSGetPacket= (ARTS_Get_Packet*) cur_struct->receive_pkts_thread;
    if(sARTSGetPacket == NULL)
    { 
        return NULL;          
    }
    
    
    {       
        OSMutexLocker lockerBuffer(&sARTSGetPacket->bufferMux); 
   
        if(sess->rtp_packet_buffer_pool && sess->rtp_packet_buffer_pool->used == false && sess->rtp_packet_buffer_pool->pkt_buf != NULL && sess->rtp_packet_buffer_pool->pkt_org_len >=len)
        {
            rtp_packet_buffer_type *node = sess->rtp_packet_buffer_pool;
            sess->rtp_packet_buffer_pool =(rtp_packet_buffer_type*) sess->rtp_packet_buffer_pool->next; 
      
            node->pkt_len = len;
            node->next =NULL;
            node->pkt_str= stream_type;
          
            memcpy(node->pkt_buf,buffer,len);
            //LogRequest(INFO_ARTS_MODULE, sess->callid,"get packet from pool,org_len:%d,pkt_buf:%x,pkt_len:%d,node:%x",node->pkt_org_len,node->pkt_buf,len,node);
            return node;
       
        }
    }
    
    
    if(!sess->rtp_packet_buffer_pool)
    {
            LogRequest(INFO_ARTS_MODULE, sess->callid,"pool is null");
    }      
    else 
        LogRequest(INFO_ARTS_MODULE, sess->callid,"used:%d,org_len:%d,pkt_buf:%x",sess->rtp_packet_buffer_pool->used,sess->rtp_packet_buffer_pool->pkt_org_len,sess->rtp_packet_buffer_pool->pkt_buf);
    
    rtp_packet_buffer_type *newNode = newRtpBuf(buffer,len,stream_type,sess);
    return newNode;
    
}

int64_t getcurTs(int64_t timestampInMS,custom_struct_t * custom_struct)
{
    if( timestampInMS >=0)
        custom_struct ->last_recv_PCR = timestampInMS;            
            
    return  ARTSMAX(timestampInMS,custom_struct ->last_recv_PCR); 
}


static void insert_sess_buffer(arts_session *sess,uint8_t *buf,int buf_size,int64_t timestamp,bool start,bool end)
{
     LogRequest(DEBUG_ARTS_MODULE, sess->callid,"insert buffer:%d,timestamp:%u,start:%d,end:%d",buf_size,timestamp,start,end);
    
    if(buf == NULL || buf_size <=0 || sess == NULL)
        return;
        
    rtp_packet_buffer_type *rtp_packet_buffer = newRtpBuf((char *)buf,buf_size,2,sess);
        
	rtp_packet_buffer->start = start;
	rtp_packet_buffer->timestamp =timestamp;
	
	insertPktInBuf(sess,rtp_packet_buffer);
	
}



QTSS_Error insertPktInBuf(arts_session * cur_sess,rtp_packet_buffer_type * rtp_packet_buffer)
{

    if(cur_sess == NULL || rtp_packet_buffer  == NULL)
        return QTSS_NoErr;
        
    if(cur_sess->transport_type == qtssRTPTransportTypeMPEG2)
    {
        LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid, "current_time:%"_64BITARG_"d,type = %c,buffer_len  = %d ,"
                   "PCR: %"_64BITARG_"d,pkt_len:%d,Pts:%"_64BITARG_"d" ,QTSS_Milliseconds(),TYPE_TO_C(rtp_packet_buffer->pkt_str),
                   cur_sess->rtp_packet_buffer_len,rtp_packet_buffer ->timestamp,rtp_packet_buffer->pkt_len,rtp_packet_buffer->pts);
    }
    custom_struct_t * custom_struct= (custom_struct_t *)cur_sess->darwin_custom_struct;
    Assert(custom_struct != NULL);
    ARTS_Get_Packet *sARTSGetPacket = (ARTS_Get_Packet *)custom_struct->receive_pkts_thread;
    rtp_packet_buffer_type *p = (rtp_packet_buffer_type *)cur_sess->rtp_packet_buffer;
    int64_t duration = 0;
    if(p!= NULL)
        duration = rtp_packet_buffer->timestamp  - p->timestamp;
         
    if (sARTSGetPacket != NULL)
    {
        OSMutexLocker lockerBuffer(&sARTSGetPacket->bufferMux);
        insert_buffers(cur_sess,rtp_packet_buffer);             
        cur_sess->rtp_packet_buffer_len++;
    }
                
    if( cur_sess->rtp_packet_buffer != NULL && duration >= MAXBUFFERDURATION )
    {
        custom_struct->buf_duration = duration;                   
        LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"buf_duration:%u",custom_struct->buf_duration);                  
                   
    }
               
    return QTSS_NoErr;  
    
}
static bool  isValidateSEI(char * l_buf,int in)
{
    char *ptr = l_buf;
    if(ptr == NULL)
    return false;
    LogRequest(DEBUG_ARTS_MODULE,0,"ptr[5]:%x,ptr[1]:%x,ptr[2]:%x",ptr[5] ,ptr[1],ptr[2]);
    if((ptr[2] == 0x00 || ptr[5]== 0x02 || ptr[5]== 0x42) && ((ptr[1] &0x40)!=0)  )
    {
        LogRequest(DEBUG_ARTS_MODULE,0,"ptr[5]:%x,ptr[1]:%x",ptr[5] ,ptr[1]);
        return true;
    }
    return false;
}



static int handle_TS_Pkt_PTS(arts_session *cur_sess,UInt32 callid,char * l_buf,int in)
{
    QTSS_TimeVal timestampInMS = -1;
    SInt64 PCR = -1,tmpPCR =-1;
    
    UInt32 pktLen =in;
    char* ptr =l_buf;
    char *lastKeyframe = ptr;
    bool error_indicator =false;
    bool priority_flag =false;
    bool is_start=false;
                
    SInt64 PTS =-1,lastPTS=-1;
    int offset =0;
    bool drop_flag =false;
    int64_t dts = -1;
    bool seek_flag = false;
    char tmp_buf[8*TS_PACKET_SIZE];
      
    custom_struct_t * custom_struct = (custom_struct_t *)cur_sess->darwin_custom_struct;
    int stream_type = 1;
    MpegTSContext*ts = NULL;
    while(pktLen >= TS_PACKET_SIZE)
    {  
        PTS = -1;
        tmpPCR =-1;
        dts =-1;
         
        ts = (MpegTSContext*)custom_struct->tsctx;
        seek_flag = false;
        
        if(custom_struct->first_pts <0)
        {
            seek_flag = true;
            LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"first_pts :%d,seek_flag:%d,buf_duration:%d",custom_struct->first_pts,seek_flag,custom_struct->buf_duration);
        }
              
        
        dts=handle_pkt_simple(ts,(unsigned char*)ptr,TS_PACKET_SIZE,&tmpPCR,&PTS,seek_flag);
        
        if(ptr[5]==0 && ptr[0]==0x47 && ptr[1]== 0x40)
        {
            add_service(ts,0);
        }
        if(tmpPCR>-1)
        {
            LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"PCR-MS:%u",tmpPCR);
        }
        if(tmpPCR >-1 && PCR ==-1)
        {
            PCR = tmpPCR;
        } 
                  
                    // get timestamp and set some param
        if(PCR != -1)      
        {
            timestampInMS = PCR;     
                    
            if(cur_sess->first_mpeg2_timestamp <=0)
                cur_sess->first_mpeg2_timestamp = timestampInMS;  
                        
                        //no seek get first pcr
            if(custom_struct->seek_start_dts < 0 && custom_struct->seek == false)
            {
                cur_sess -> mpeg2_start_time =0;
                custom_struct->seek_start_dts = timestampInMS;
                LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"custom_struct->seek_start_dts:%u",custom_struct->seek_start_dts);
            }  
                
        }else
            timestampInMS = -1;
                                                                                     
            
            // check wether seek start flag
            if(ptr[0] != 0x47)
            {
                LogRequest(INFO_ARTS_MODULE,cur_sess->callid,"ptr[0]:%X,ptr[1]:%x,ptr[2]:%x,ptr[3]:%x,ptr[4]:%x,ptr[5]:%x,ptr[6]:%x,pktLen:%d",ptr[0],ptr[1],ptr[2],ptr[3],ptr[4],ptr[5],ptr[6],pktLen);
            }
            
            LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"ptr[0]:%X,ptr[1]:%x,ptr[2]:%x,ptr[3]:%x,ptr[4]:%x,ptr[5]:%x,ptr[6]:%x,pktLen:%d",ptr[0],ptr[1],ptr[2],ptr[3],ptr[4],ptr[5],ptr[6],pktLen);
            if(ptr[0]== 0x47)
            {                      
                if( (ptr[1]&0x80) !=0)
                    error_indicator =true;
                if( (ptr[1]&0x20) != 0)
                    priority_flag =true;
                if( (ptr[1]&0x40) !=0 )
                    is_start =true;               
            }        
     
            if( error_indicator == true || priority_flag == true)
            {                       
                ptr[1]= (ptr[1] &0x5f);  
                            
                custom_struct->seek_start = true;                 
                LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"packet[1]:%X",ptr[1]);
                error_indicator = false;
                priority_flag = false;
            }      
                    
                    
                    //seek start but no receive validate TS packet
            if( (custom_struct->seek_start_dts<0 )&& custom_struct->seek == true)
            {
                if( custom_struct->seek_start ==false  )
                {
                    
                    LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"the PCR is %ld,drop it,buf_duration:%d", timestampInMS,custom_struct->buf_duration);
                    PCR = -1;
                    drop_flag = true;
                    goto  next;
                 }
                 
                  bool flag1 = isValidateSEI(ptr,TS_PACKET_SIZE);
                 
                 if ( flag1==false && (timestampInMS<0 ) )
                 {
                   
                    LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"the PCR is %ld,drop it,buf_duration:%d", timestampInMS,custom_struct->buf_duration);
                    PCR =-1;
                    drop_flag = true;
                    goto  next;
                 }
                 
                 
                 else if(custom_struct->seek_start == true && drop_flag == true )
                 {
                    
                       /*
                        uint8_t  packet[TS_PACKET_SIZE];
                        if(ptr[0] == 0x47 && (ptr[2] == 0x00 || ptr[5] ==0x02 ))
                        {
                            // pat or pmt 
                            memcpy(packet,ptr,TS_PACKET_SIZE);
                        }
                        
                        rtp_packet_buffer_type *pkt_buf = GetRtpBuf(ptr,TS_PACKET_SIZE,stream_type,cur_sess);
          
                        pkt_buf->pts = lastPTS;
                        pkt_buf->used = true;
                        pkt_buf->timestamp = getcurTs(timestampInMS,custom_struct);             
                        insertPktInBuf(cur_sess,pkt_buf);
                        */
                        /*
                        mpegts_write_sdt(ts,cur_sess->callid,packet);                        
                        if(ptr-l_buf >= TS_PACKET_SIZE)
                        {
                              memcpy(ptr-TS_PACKET_SIZE,packet,TS_PACKET_SIZE);
                              lastKeyframe = ptr -TS_PACKET_SIZE;
                         }else
                         {
                              memcpy(tmp_buf,packet,TS_PACKET_SIZE);
                              ptr = tmp_buf;
                              memcpy(ptr + TS_PACKET_SIZE,l_buf,in);
                              lastKeyframe = ptr;                              
                              ptr += 2*TS_PACKET_SIZE;                              
                         }                        
                      
                     */ 
                        drop_flag = false;
                        lastKeyframe = ptr;         
                   
                 }
            }           
                    
            if(timestampInMS>=0 && custom_struct->seek_start_dts <0 &&custom_struct->seek== true && custom_struct->seek_start==true)
            {                  
                custom_struct->seek_start_dts = timestampInMS;
                LogRequest(INFO_ARTS_MODULE, cur_sess->callid,"Get the new start,PCR:%ld",timestampInMS);  
                //custom_struct->seek_start=false;
                 
                if(custom_struct != NULL && strlen(custom_struct->rangeHeader_global)>0)
                {   
                    cur_sess->head.state &= ~ARTS_CALL_STATE_PLAY_CMD_SENT; 
                    LogRequest(INFO_ARTS_MODULE, cur_sess->callid,"will send Play");    
                    send_play_request(callid);  
                }     
                    
            }   
                         
            if(PCR != -1 && custom_struct->first_pts >=0)
            {                          
                  if( !(custom_struct->seek == true && custom_struct->seek_start_dts<0)){                            
                          LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"drop other packets"); 
                          ptr = l_buf + in;               
                          break;
                   }                  
            }    
                    
            if(PTS>-1)
            {      
                LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"PTS:%d",PTS/90);
               if(custom_struct->first_keyframe_pts <0)
               {
                    custom_struct->first_keyframe_pts = PTS;
                    LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"custom_struct->first_keyfame_pts:%d",PTS);
               }      
                      
                                 
                if(custom_struct->first_pts <0)
                {
                    
                    if(custom_struct->seek == true && custom_struct->seek_start == true || custom_struct->seek ==false)
                    {
                        custom_struct->first_pts = PTS;
                        custom_struct->seek_start = false;
                        LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"custom_struct->first_pts:%d",PTS);
                    }
                    
                }  
                custom_struct->pause = false;
            }
                                          
next:                   
            ptr += TS_PACKET_SIZE;
            pktLen -= TS_PACKET_SIZE;
                 
        }
 

        if( ts!= NULL && ts->pat_copy ==1 && ts->pmt_copy == 1 && custom_struct->psicbr == true && custom_struct->seek_start_dts >=0)    
        {
           ts->pcr_pkts_period = (ts->mux_rate * ts->pcr_retransmit_time)/(TS_PACKET_SIZE * 8 * 1000);
	       LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"ts->pcr_pkts_period:%d\n",ts->pcr_pkts_period);
	   
            int64_t old_pcr = ts->cur_pcrMS;
            //int64_t first_pcr = custom_struct->seek_start_dts-1000;
            //ts->cur_pcrMS = first_pcr <0 ? 0: first_pcr ;
            ts->cur_pcrMS =0;
            unsigned char buf[TS_PACKET_SIZE];           
            write_new_psi(ts,PAT_PID,cur_sess->callid,buf);
            insert_one_pkt(ts,callid,buf,TS_PACKET_SIZE,-1);
            write_new_psi(ts,0x02,cur_sess->callid,buf);
            insert_one_pkt(ts,callid,buf,TS_PACKET_SIZE,-1);
            
            
            insert_pcr_only(ts,ts->pcr_pid,0,buf,ts->cur_pcrMS);
            insert_one_pkt(ts,callid,buf,TS_PACKET_SIZE,-1);
            
            while (ts->cur_pcrMS <=  sARTSPSIDuration )
            {
                write_new_psi(ts,PAT_PID,cur_sess->callid,buf);
                insert_one_pkt(ts,callid,buf,TS_PACKET_SIZE,-1);
                write_new_psi(ts,0x02,cur_sess->callid,buf);
                insert_one_pkt(ts,callid,buf,TS_PACKET_SIZE,-1);
                if(ts->pcr_pkts_count >= ts->pcr_pkts_period && ts->pcr_pkts_period >0)
                {
                    insert_pcr_only(ts,ts->pcr_pid,0,buf,-1);
                    insert_one_pkt(ts,callid,buf,TS_PACKET_SIZE,-1);
                    ts->pcr_pkts_count =1;
                }
                ts->pcr_pkts_count ++;               
            }
            
            if(ts->buf_len >0)
            {
                rtp_packet_buffer_type *pkt_buf = newRtpBuf((char*)ts->buf,ts->buf_len,stream_type,cur_sess);
                pkt_buf->timestamp = ts->cur_pcrMS;
                insertPktInBuf(cur_sess,pkt_buf);
                ts->buf_len =0;
            }
            
            ts->cur_pcrMS = old_pcr;
            
            custom_struct->psicbr = false;
        }   
                
                // offset ==0 --> nokeyframe in whole buf                       
        if(drop_flag == false &&(offset == 0  || ptr-lastKeyframe >0 )) 
        {
            offset = ptr-lastKeyframe;            
            rtp_packet_buffer_type *pkt_buf = GetRtpBuf(lastKeyframe,offset,stream_type,cur_sess);
          
            pkt_buf->pts = lastPTS;
            pkt_buf->used = true;
            pkt_buf->timestamp = getcurTs(timestampInMS,custom_struct);             
            insertPktInBuf(cur_sess,pkt_buf);
        }  
        
        
}
static int handle_TS_Pkt_NoIpqam(arts_session *cur_sess,UInt32 callid ,custom_struct_t * custom_struct,rtp_packet_buffer_type * rtp_packet_buffer)
{
    if(custom_struct == NULL || rtp_packet_buffer == NULL)
        return 0;
        
    int valid_pkt = true;
    QTSS_PacketStruct thePacket;
    QTSS_TimeVal timestampInMS;
    SInt64 PCR = -1,tmpPCR =-1;
    thePacket.packetData = rtp_packet_buffer->pkt_buf;
    UInt32 pktLen = rtp_packet_buffer->pkt_len;
    UInt8* ptr = (UInt8*)thePacket.packetData;
    bool error_indicator =false;
    bool priority_flag =false;
    bool is_start=false;
    bool new_start=false;
    
    
    Assert(cur_sess!=NULL);    
            
    while(pktLen >= TS_PACKET_SIZE)
    {                       
        //LogRequest(DEBUG_ARTS_MODULE, sess->callid," first:%X,orig:%X",ptr[0],ptr[1]);                    
        tmpPCR = GetMPEG2PCR(ptr, &pktLen); 
        if(tmpPCR>-1)
        {
            LogRequest(DEBUG_ARTS_MODULE, callid,"PCR-MS:%u",tmpPCR/27/1000);
        }
        
        if(tmpPCR >-1 && PCR ==-1)
        {
            PCR = tmpPCR;
        }
                    
        LogRequest(DEBUG_ARTS_MODULE, callid,"ptr[0]:%X,ptr[1]:%x,ptr[2]:%x,ptr[3]:%x,ptr[4]:%x,ptr[5]:%x",ptr[0],ptr[1],ptr[2],ptr[3],ptr[4],ptr[5]);
        if(ptr[0]== 0x47)
        {                      
            if( (ptr[1]&0x80) !=0)
                error_indicator =true;
            if( (ptr[1]&0x20) != 0)
                priority_flag =true;
            if( (ptr[1]&0x40) !=0 )
                is_start =true;               
        }        
     
        if( error_indicator == true || priority_flag == true)
        {                       
            ptr[1]= (ptr[1] &0x5f);
            if(tmpPCR == -1)
                PCR = -1;
                            
            custom_struct->seek_start = true; 
            //cur_sess->mpeg2_start_time = 0;
            //LogRequest(DEBUG_ARTS_MODULE, callid,"sess->mpeg2_start_time:%"_64BITARG_"d",cur_sess->mpeg2_start_time);
            LogRequest(DEBUG_ARTS_MODULE, callid,"packet[1]:%X",ptr[1]);
            error_indicator = false;
            priority_flag = false;
        }                       
                    
                    
                    
        if(PCR != -1)
        {                          
            if( !(custom_struct->seek == true && custom_struct->seek_start_dts<0)){                            
                LogRequest(DEBUG_ARTS_MODULE, callid,"drop other packets");                
                break;
            }                  
        }
        ptr += TS_PACKET_SIZE;
        pktLen -= TS_PACKET_SIZE;
                 
    }
    
    if(PCR != -1)
    {
        timestampInMS = PCR / 27 / 1000 ;                    
        if(cur_sess->first_mpeg2_timestamp <=0)
            cur_sess->first_mpeg2_timestamp = timestampInMS;  
                        
        if(custom_struct->seek_start_dts < 0 && custom_struct->seek == false)
        {
            custom_struct->seek_start_dts = timestampInMS;
            LogRequest(DEBUG_ARTS_MODULE, callid,"custom_struct->seek_start_dts:%ld",custom_struct->seek_start_dts);
        }  
                
    }else
        timestampInMS = -1;           
                     

    if( (custom_struct->seek_start_dts<0 )&& custom_struct->seek == true)
    {
        if( custom_struct->seek_start ==false)
        {
            LogRequest(DEBUG_ARTS_MODULE, callid,"the PCR is %ld,drop it", timestampInMS);
            free(rtp_packet_buffer->pkt_buf);
            free(rtp_packet_buffer);
            valid_pkt = false;
            return valid_pkt;
        }
    }
 
               
    if(timestampInMS>=0 && custom_struct->seek_start_dts <0 &&custom_struct->seek== true && custom_struct->seek_start==true)
    {                  
        custom_struct->seek_start_dts = timestampInMS;
        LogRequest(DEBUG_ARTS_MODULE, callid,"Get the new start,PCR:%ld",timestampInMS);  
        custom_struct->seek_start = false;
        if(custom_struct != NULL && strlen(custom_struct->rangeHeader_global)>0)
        {   
            cur_sess->head.state &= ~ARTS_CALL_STATE_PLAY_CMD_SENT; 
            LogRequest(DEBUG_ARTS_MODULE,callid,"will send Play");    
            send_play_request(callid);  
        }     
                    
    }           
                              
               
    if( timestampInMS >=0)
        custom_struct ->last_recv_PCR = timestampInMS;                
                
    int64_t lasttimestampInMS = custom_struct ->last_recv_PCR;                      
    rtp_packet_buffer ->timestamp = ARTSMAX(timestampInMS,lasttimestampInMS); 
                
            
   
    QTSS_TimeVal CurrentTime = QTSS_Milliseconds();                      
    LogRequest(DEBUG_ARTS_MODULE, callid, "current_time:%"_64BITARG_"d,type = %c,buffer_len  = %d ,"
        "PCR: %"_64BITARG_"d,pkt_len:%d" ,CurrentTime,TYPE_TO_C(rtp_packet_buffer->pkt_str),
        cur_sess->rtp_packet_buffer_len,rtp_packet_buffer ->timestamp,rtp_packet_buffer->pkt_len);  
        
    return valid_pkt;
}




SInt64 ARTS_Get_Packet::Run()
{
    LogRequest(DEBUG_ARTS_MODULE,this->callid," ARTS_Get_Packet,cuttime: %"_64BITARG_"d,buf_len:%d,stream_type:%d",QTSS_Milliseconds(),this->buffer_len,this->stream_type);  
    if(this->buffer_len <=0)
    {       
        return 0;
    }  
    
    int ret =1;
    //int real_len = this->buffer_len /TS_PACKET_SIZE * TS_PACKET_SIZE;
    //int real_len  = this->buffer_len;
   
        
    custom_struct_t *custom_struct = NULL;
    arts_session *cur_sess = NULL;
    
    cur_sess = (arts_session *)this->sess;
         
    if(cur_sess == NULL)
    {
        LogRequest(DEBUG_ARTS_MODULE,this->callid,"cur_sess == NULL");
          return  -1;
    }
    Assert(cur_sess != NULL);
    custom_struct= (custom_struct_t *)cur_sess->darwin_custom_struct;
    rtp_packet_buffer_type * rtp_packet_buffer = NULL;
     
    
    if(custom_struct->isIpqam == false && custom_struct->supportPTS==false  && custom_struct->psicbr  == false  )
    {
        rtp_packet_buffer = newRtpBuf(this->buffer,this->buffer_len, this->stream_type,cur_sess);
    }
    
    
    if(this->buffer_len>0 && custom_struct->ifd!= NULL)
    {        
        fwrite(this->buffer,sizeof(char),this->buffer_len,custom_struct->ifd);                 
    }
    
    if(cur_sess->transport_type != qtssRTPTransportTypeMPEG2)
    {        
        ret = handle_RTP_Pkt(cur_sess,this->callid,custom_struct,rtp_packet_buffer);
    }else
    {
        if(custom_struct->isIpqam == false)
        {
            if(custom_struct->supportPTS==true || custom_struct->psicbr == true)
            {
                LogRequest(DEBUG_ARTS_MODULE,this->callid,"Entry handle_TS_Pkt_PTS");
                ret = handle_TS_Pkt_PTS(cur_sess,this->callid,this->buffer,this->buffer_len);
            }
            
            else if(sARTSCBR == true)
            {
                LogRequest(DEBUG_ARTS_MODULE,this->callid,"Entry handle_TS_Pkt_Ipqam");
                ret =handle_TS_Pkt_Ipqam(cur_sess,this->callid,custom_struct,this->buffer,this->buffer_len);
            }            
            else if(live == false)
            {
               LogRequest(DEBUG_ARTS_MODULE,this->callid,"Entry handle_TS_Pkt_NoIpqam");
               ret = handle_TS_Pkt_NoIpqam(cur_sess,this->callid,custom_struct,rtp_packet_buffer);
            }
        }else 
        {           
             LogRequest(DEBUG_ARTS_MODULE,this->callid,"Entry handle_TS_Pkt_Ipqam");
            ret =handle_TS_Pkt_Ipqam(cur_sess,this->callid,custom_struct,this->buffer,this->buffer_len);
        }
    }
    
    
    if(cur_sess->aud_discont == 1 && cur_sess->vid_discont == 1 && cur_sess->flush_discont == 1)
    {
        clear_buffer(cur_sess);              
        LogRequest(DEBUG_ARTS_MODULE, cur_sess->callid,"clear buffer list due to sess->discont");
    }           
                          
       //ret is true is validate pkts when seek

    if(rtp_packet_buffer != NULL && custom_struct->isIpqam == false && ret== 1 && custom_struct->supportPTS==false)
    {    
        insertPktInBuf(cur_sess,rtp_packet_buffer);    
        
    }
    
    this->buffer_len =0;
    return 0;    
}






bool ARTS_PH_Interface::Configure(char *pBackends,char * pBindHost, UInt16 lport)
{
   
    char *hostname = NULL;
    int portnum = 0;

    // Get the listenport and start listening to it
    if (lport)
    {
        listenport = lport;
        listensock->fd = arts_adapter_listen( pBindHost ,listenport);
        if(-1 == listensock->fd)
        {
            LogRequest(INFO_ARTS_MODULE, 0, "arts_adapter_listen error");
            return false;
        }
    }
    else
    {
        LogRequest(INFO_ARTS_MODULE, 0, "no listenport specified");
        //return false;
    }

    // Get the controller configuration
    portnum = get_host_port(pBackends,&hostname); 
    
    LogRequest(INFO_ARTS_MODULE, 0, "controller address: IP = %s, port = %d", hostname, portnum);
    
    if(portnum <=0)
    {
        LogRequest(INFO_ARTS_MODULE, 0, "bad controller address specified %s",  pBackends);
        return false;
    }

    bzero( (void *)&servaddr, sizeof(servaddr) );
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(portnum);
    servaddr.sin_addr.s_addr = inet_addr(hostname);

    free(hostname);
    if(  sARTSSupportOtherChannel == true || sARTSSupportPTS== true || no_describe == true || sARTSJinShanModule == true || sARTSszModule == true  || sARTSIpqamHost != NULL )
    {
          tmpbuf_len =1316;
    }else
        tmpbuf_len =sizeof(tmpbuffer);

    return true;
}

void ARTS_PH_Interface::Entry()
{
    fdevent_revents *revents = fdevent_revents_init();
    int poll_errno;
    int n;

    QTSS_TimeVal prevTs = QTSS_Milliseconds(), curTs;
    
    while(IsStopRequested() == false)
    {        
        n = fdevent_poll(ev, this->polltimeout);         
        poll_errno = errno;
        
        if (n > 0) 
        {
         
            /* n is the number of events */
            size_t i;
            fdevent_get_revents(ev, n, revents);           
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
                    LogRequest(INFO_ARTS_MODULE, 0, "event loop returned: %d", (int) r);
                    break;
               }
           }
        } 
        else if (n < 0 && poll_errno != EINTR) 
        {
                 LogRequest(INFO_ARTS_MODULE, 0, "event loop: fdevent_poll failed: %s", strerror(poll_errno)); 
        }
        
        event_node_t *p = this->stop_events;
        event_node_t * prev =p;
        //LogRequest(DEBUG_ARTS_MODULE, 0,"Entry, stop_events:%x",p);
        while(p)
        {         
                            
            arts_session * sess = (arts_session *)p->sess;
            if ( p->unregister ==1 && sess!= NULL && sess->darwin_custom_struct != NULL)
            {             
                           
                custom_struct_t * custom_struct = (custom_struct_t *)sess->darwin_custom_struct;   
                 LogRequest(DEBUG_ARTS_MODULE, sess->callid,"custom_struct->buf_duration:%d",custom_struct->buf_duration);        
                if( (  (custom_struct->buf_duration >=0 && custom_struct->buf_duration <MINBUFFERDURATION && live==false) ||
                    (sess->rtp_packet_buffer_len < MINBUFFERLEN && sess->rtp_packet_buffer_len >=0 && live == true) )                
                    && custom_struct->adapter_unregister == true)
				{			 
                  fdevent_event_add(this->ev, sess->head.sock, FDEVENT_IN | FDEVENT_HUP );	                  
                  p->unregister = 0;
                  custom_struct->adapter_unregister = false;   
                  LogRequest(INFO_ARTS_MODULE, sess->callid,"event_add,fd:%d",sess->head.sock->fd);               		
				}            
            }  
            
            p = (event_node_t *) p->next;      
        }

       
        curTs = QTSS_Milliseconds();
        if(curTs >= prevTs + this->polltimeout && live == false) 
        {
            Trigger();
            prevTs = curTs;
        }      
        
    }
    fdevent_revents_free(revents);
}

void ARTS_PH_Interface::Trigger()
{
	char  l_buf[128];  //must be big enough to hold ipv6 address
	char  l_portBuf[8];
	
	
	//LogRequest(INFO_ARTS_MODULE, 0,"Entry Triugger:%"_64BITARG_"d",QTSS_Milliseconds());
    //LogRequest(INFO_ARTS_MODULE, 0,"Trigger Entry,control_state:%d",control_state);
    /* the trigger handle cares about the SCTP connection with the controller */
    switch(control_state)
    {
    case ARTS_CONTROLLER_STATE_UNSET:
    /* This is the first time in after init so add the listen socket as well*/
    
    if(listensock->fd != -1 && live == false)
    {
        fdevent_register(ev, listensock, arts_handle_listener_fdevent, NULL);
        fdevent_event_add(ev, listensock, FDEVENT_IN);
    }

    /* Fall Through */
    case ARTS_CONTROLLER_STATE_IDLE:
    /* The controller is down so try connecting with the controller*/
        if(controllertimeout != 0)
        {
             controllertimeout --;
             break;
        }

        switch(arts_controller_connect(&servaddr, &controlsock->fd))
        {
        case HANDLER_WAIT_FOR_FD:
            break;

        case HANDLER_WAIT_FOR_EVENT:
     
            LogRequest(INFO_ARTS_MODULE, 0, "controller connection establishing");
     
            fdevent_register(ev, controlsock, arts_handle_controller_fdevent, NULL);
            fdevent_event_add(ev, controlsock, FDEVENT_OUT | FDEVENT_HUP);
            control_state = ARTS_CONTROLLER_STATE_CONNECTING;
            break;
     
        case HANDLER_GO_ON:
            
            LogRequest(INFO_ARTS_MODULE, 0, "controller connection established");
     
			sprintf(l_portBuf,":%d",listenport);
            arts_send_registration(controlsock->fd, sARTSSystemName, ARTS_PH_TYPE_RTSP, strcat(strcpy(l_buf,sARTSBindHost),l_portBuf) );
            fdevent_register(ev, controlsock, arts_handle_controller_fdevent, NULL);
            fdevent_event_add(ev, controlsock, FDEVENT_IN | FDEVENT_HUP);
            control_state = ARTS_CONTROLLER_STATE_READ_REG_RESPONSE;
            break;
     
        default:
            LogRequest(INFO_ARTS_MODULE, 0, "controller connection error ");
            break;
        }
       break;

    default:
           break;
    }
}

// Handle Controller events
handler_t arts_handle_controller_fdevent(void *s, void *ctx, int revents) 
{
    ARTS_PH_Interface *srv = (ARTS_PH_Interface *) s;
	char l_buf[128];
	char l_portBuf[8];
    int flags = 0;
    
	LogRequest(INFO_ARTS_MODULE, 0, "controller connection event=%d: %d", revents, srv->control_state);

    if(revents & FDEVENT_IN)
    {
        //Read event
        OSMutexLocker l_autoMutex(&srv->sessionMutex);
        flags = arts_handle_controller_resp(srv->controlsock->fd, srv);
    }

    if ((flags == 0) && (revents & FDEVENT_OUT))
    {
        // Outbound connection succeeded
        //arts_send_registration(srv->controlsock->fd, sARTSSystemName, ARTS_PH_TYPE_RTSP, srv->listenport);inet_ntoa()
		sprintf(l_portBuf ,":%d",srv->listenport);
		arts_send_registration(srv->controlsock->fd, sARTSSystemName, ARTS_PH_TYPE_RTSP, strcat(strcpy(l_buf,sARTSBindHost),l_portBuf));
        srv->control_state = ARTS_CONTROLLER_STATE_READ_REG_RESPONSE;

        LogRequest(INFO_ARTS_MODULE, 0, "controller connection established");

        fdevent_event_add(srv->ev, srv->controlsock, FDEVENT_IN | FDEVENT_HUP);
    }

    if((flags == -1) ||(revents & FDEVENT_HUP) || (revents & FDEVENT_ERR))
    {        // Controller Socket is down
        srv->controllertimeout = 10;
        srv->control_state = ARTS_CONTROLLER_STATE_IDLE;

        LogRequest(INFO_ARTS_MODULE, 0, "controller connection down");

        fdevent_event_del(srv->ev, srv->controlsock);        
        close(srv->controlsock->fd);
        // Also need to put code to cleanup session structs
    }
  
    return HANDLER_GO_ON;
}


void del_adapter_fdevent(arts_session * sess,int fd)
{
    if(sess!=NULL)
    {
        custom_struct_t * custom_struct =(custom_struct_t *)  sess->darwin_custom_struct;
        custom_struct->adapter_unregister = true; 
        LogRequest(INFO_ARTS_MODULE, sess->callid, "unregister adapter ");
        stop_getpkts_thread(sess);
    }  
      
    del_events_node(fd);
  
}

// Handle SCTP Connections from the adapter
handler_t arts_handle_adapter_fdevent(void *s, void *ctx, int revents) 
{

    LogRequest(DEBUG_ARTS_MODULE, 0, "arts_handle_adapter_fdevent: entering");

    ARTS_PH_Interface *srv = (ARTS_PH_Interface *) s;
    
    OSMutexLocker l_autoMutex(&srv->sessionMutex);

    int in = 0;
    arts_session_head *sess_head = (arts_session_head *) ctx;
    arts_session *sess = NULL;
    
    int release_flag =0;
   
    if(sess_head->state != ARTS_CALL_STATE_UNSET)
    {
        // This is a real session and not a session head, 
        sess = (arts_session *) ctx;
    }      

    //OSMutexLocker l_autoMutex(&srv->sessionMutex);
    if(revents & FDEVENT_IN)
    {
        int flags = 0;
        //int flags = MSG_CONFIRM;
        struct sctp_sndrcvinfo sndrcvinfo;
        
        
        if(sess!=NULL && (sess->head.state & ARTS_CALL_STATE_DESTROY_NOW) )
        {                    
            fdevent_event_del(srv->ev, sess_head->sock);        
            fdevent_unregister(srv->ev, sess_head->sock);
            
            if(sess->head.sock!=NULL)
            {            
                multicast_group_leave(sess->head.sock->fd,sess->head.addr);               
                del_events_node(sess->head.sock->fd);                
            }
            free_custom_struct(sess,NULL);
            LogRequest(INFO_ARTS_MODULE, sess->callid,"free sess,sess->fd:%d",sess->head.sock->fd);
             
            arts_session_free(sess,1);   
            srv->polltimeout = 1000;              
            return HANDLER_GO_ON;
        }
             
                
        //sARTSSupportOtherChannel -->yangzhou-hd TsOverIp
        //sARTSSupportPTS          -->wasu        TsOverIp
        //no_describe         -->zibo        Ipqam
               
        in = sctp_recvmsg( sess_head->sock->fd, (void *)sARTSPHInterface->tmpbuffer, sARTSPHInterface->tmpbuf_len,
                (struct sockaddr *)NULL, 0, &sndrcvinfo, &flags );      
                
        if(in != 1316)
        {
            LogRequest(DEBUG_ARTS_MODULE, sndrcvinfo.sinfo_ppid, "arts_handle_adapter_fdevent: data received: data_len = %d",in);
        }

        // If in < 1 then Connection went down
        if( (in > 0) && (!sess) )
        {
            // First Data received on socket. We need to associate with a real session struct
            // Remove the fd from the event loop.
            fdevent_event_del(srv->ev, sess_head->sock);        
            fdevent_unregister(srv->ev, sess_head->sock);
            sess = arts_session_find(sndrcvinfo.sinfo_ppid);
            
            if(!sess || (sess->head.state & ARTS_CALL_STATE_DESTROY_NOW))
            {
                // Session not found. Cleanup and return.
                LogRequest(INFO_ARTS_MODULE, sndrcvinfo.sinfo_ppid, "adapter unexpected connection");                                            
                arts_session_head_free(sess_head);
                return HANDLER_GO_ON;
            }
            else
            {   
                sess ->callid = sndrcvinfo.sinfo_ppid;            
                // Session found.
                if(live == false)
                {
                    sess->head.state |= ARTS_CALL_STATE_CONNECTED;  
                }         
                
                sess->head.sock = sess_head->sock;
                LogRequest(DEBUG_ARTS_MODULE, sess->callid,"sess->head.sock:%x",sess->head.sock);
                sess_head->sock = NULL;
                 
                arts_session_head_free(sess_head);                
                        
                fdevent_register(srv->ev, sess->head.sock, arts_handle_adapter_fdevent, sess);   
                if(sess->head.state & ARTS_CALL_STATE_PLAY)
				{	//we must wait until we received response from Adapter and then 
				    //we can move on to process data from adapter
					// before that we only listen to HUP,ERR events, data will be hold. 
                  fdevent_event_add(srv->ev, sess->head.sock, FDEVENT_IN | FDEVENT_HUP );
                  LogRequest(DEBUG_ARTS_MODULE, sess->callid,"event_add when sess->head.state == play");
					
				}                 
                  //first packet received from adapter ,need initialize some struct   
                  
                 if (sess->darwin_custom_struct ==NULL)
                 {              
                        custom_struct_t *custom_struct = new_custom_struct(sess);
                        
                        MpegTSContext * tsctx = (MpegTSContext*) malloc(sizeof( MpegTSContext));                        
                        Assert(tsctx!=NULL);   
                        memset(tsctx,0,sizeof(MpegTSContext));                     
                        init_tscontext(tsctx,MUX_RATE,sARTSIpqamPCRInterval);                        
                        custom_struct->tsctx = tsctx; 
                        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"cur_pcr:%d,last_pcr:%d,mux-rate:%d,"
                        "delay:%d,first_pts:%d",tsctx->cur_pcrMS,tsctx->last_pcr,tsctx->mux_rate,
                        tsctx->pcr_retransmit_time,custom_struct->first_pts);                      
                                                       
                        sess->darwin_custom_struct =custom_struct;
                        
                        
                        inputfile = NULL;
                        if(sARTSInputFile != NULL)
                           inputfile = fopen(sARTSInputFile,"rb");                        
                      
                        custom_struct->adapter_unregister = true;                         
                        insert_stopEventsList(sess->callid,FDEVENT_IN | FDEVENT_HUP,sess,sess->head.sock->fd);   
                        LogRequest(INFO_ARTS_MODULE, sess->callid,"unregister adapter util receive play request");                                          
                        srv->polltimeout =500;                   
                    }                 
                    
               }    
        }
             
        if(in >0)
        {   
            srv->polltimeout =150;
            custom_struct_t * custom_struct =(custom_struct_t *)  sess->darwin_custom_struct;                    
             
            if(sess->head.state & ARTS_CALL_STATE_PLAY)
            {      
                 ARTS_Get_Packet * sARTSGetPacket =(ARTS_Get_Packet *) custom_struct->receive_pkts_thread;
                   
                 if(sARTSGetPacket != NULL&& in >0)
                 {                                
                    // used in local file test
                     if(inputfile != NULL)
                     {
                           int64_t total_len = 0;
                           int64_t real_size=fread(sARTSPHInterface->tmpbuffer,sizeof(char),1316,inputfile);
                           sARTSGetPacket->buffer = sARTSPHInterface->tmpbuffer;
                           sARTSGetPacket->buffer_len = real_size;                               
                               
                           if(real_size <=0 && sess->rtp_packet_buffer_len ==0)
                           {                                        
                                TearDownSession(sess,NULL,ARTS_CALL_STATE_DESTROY_NOW);
                                in = real_size;          
                           }                      
                        
                     }else{                           
                            sARTSGetPacket->buffer = sARTSPHInterface->tmpbuffer;
                            sARTSGetPacket->buffer_len = in;  
                     }
                     sARTSGetPacket->stream_type = sndrcvinfo.sinfo_stream;
                     sARTSGetPacket->sess = sess; 
                     sARTSGetPacket->Run();                        
                 }
                    
                    
                 if( (custom_struct->buf_duration >MAXBUFFERDURATION && live == false)  || (
                    (sess->rtp_packet_buffer_len > MAXBUFFERLEN) && live == true))
                 {
                        fdevent_event_del(srv->ev, sess->head.sock);                                                                     
                        set_unregister(sess);       
                        custom_struct->adapter_unregister = true;
                        LogRequest(INFO_ARTS_MODULE, sess->callid,"buffer is full,unregister adapter,fd:%d",sess->head.sock->fd);
                 }              
            }            
            else
            {                          
                 if(custom_struct->isIpqam==0  && inputfile == NULL)
                 {
                        
                        memcpy(sess->first_packet.pkt_buf, sARTSPHInterface->tmpbuffer, in );
                        sess->first_packet.pkt_str = sndrcvinfo.sinfo_stream;
                        sess->first_packet.pkt_len = in ;
                 }
            }                
                           
          
                
           if( (  (!(sess->head.state & ARTS_CALL_STATE_PLAY))  && (sess->head.state & ARTS_CALL_STATE_READ_CON_RESPONSE)) 
                         || ( live == true && !(sess->head.state & ARTS_CALL_STATE_CONNECTED) )   )
            
            {
                Task *l_Task = (Task *)sess->task_ptr;
                if(l_Task)
                {
                      LogRequest(INFO_ARTS_MODULE, sess->callid, "Signal data Received Event");
                      ARTS_SignalStream(l_Task,Task::kReadEvent);
                }                 
                sess->head.state |= ARTS_CALL_STATE_CONNECTED;                 
             }
                
            LogRequest(DEBUG_ARTS_MODULE, sess->callid, "adapter call connected ");
        } 
    }
    
    if((in <=0) || (revents & FDEVENT_HUP) || (revents & FDEVENT_ERR))
    {
        // Connection went down
        if(release_flag ==0)
        {
            multicast_group_leave(sess_head->sock->fd,sess_head->addr);
            del_adapter_fdevent(sess,sess_head->sock->fd);
        }  
       
        fdevent_event_del(srv->ev, sess_head->sock);        
        fdevent_unregister(srv->ev, sess_head->sock);
        LogRequest(INFO_ARTS_MODULE, 0, "arts_handle_adapter_fdevent: No data or Socket closed for fd = %d", sess_head->sock->fd);
        srv->polltimeout=1000;       
        return HANDLER_GO_ON;
    }
        
    return HANDLER_GO_ON;      
}
custom_struct_t * new_custom_struct(arts_session *sess)
{
    custom_struct_t * custom_struct = (custom_struct_t *)malloc(sizeof(custom_struct_t));
    memset(custom_struct ,0,sizeof(custom_struct_t));
                        
    if(dump_input == true)
    {
        custom_struct->ifd = arts_open_file();
        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"dump input readyed");
    }
                        
    if(dump_output == true)
    {
        custom_struct->ofd = arts_open_file();
        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"dump output readyed");
    }
    sess->rtp_packet_buffer_len = -1;
    custom_struct->supportPTS = sARTSSupportPTS;
    custom_struct->first_pts =-1;
    custom_struct->last_keyframe_pts =-1;
    custom_struct->first_keyframe_pts =-1;
    custom_struct->buf_duration = -1;
    custom_struct->play_idel = false;
    
    return custom_struct;

}


// Handle SCTP Connections from the adapter
handler_t arts_handle_adapter_fdevent_io(void *s, void *ctx, int revents) 
{

    LogRequest(DEBUG_ARTS_MODULE, 0, "arts_handle_adapter_fdevent_io: entering");

    ARTS_PH_Interface *srv = (ARTS_PH_Interface *) s;
    
    OSMutexLocker l_autoMutex(&srv->sessionMutex);

    int in = 0;
    arts_session_head *sess_head = (arts_session_head *) ctx;
    arts_session *sess = NULL;
       
    if(sess_head->state != ARTS_CALL_STATE_UNSET)
    {
        // This is a real session and not a session head, 
        sess = (arts_session *) ctx;
    }      

    //OSMutexLocker l_autoMutex(&srv->sessionMutex);
    if(revents & FDEVENT_IN)
    {
        int flags = 0;
        //int flags = MSG_CONFIRM;
        struct sctp_sndrcvinfo sndrcvinfo;
        
          if(sess != NULL)
             LogRequest(DEBUG_ARTS_MODULE, sess->callid,"sess->head.state:%d",sess->head.state);
          if(sess!=NULL && (sess->head.state == ARTS_CALL_STATE_DESTROY_NOW) )
          {
             LogRequest(DEBUG_ARTS_MODULE, sess->callid,"addr:%x",sess->head.addr);
            fdevent_event_del(srv->ev, sess_head->sock);        
            fdevent_unregister(srv->ev, sess_head->sock);
            
            if(sess->head.sock!=NULL)
            {           
                multicast_group_leave(sess->head.sock->fd,sess->head.addr); 
                {
                   
                    del_events_node(sess->head.sock->fd);
                    LogRequest(DEBUG_ARTS_MODULE, sess->callid,"free event_node,sess->head->fd:%d",sess->head.sock->fd);
                }
            }
            free_custom_struct(sess,NULL);
            LogRequest(INFO_ARTS_MODULE, sess->callid,"free sess,sess->fd:%d",sess->head.sock->fd);
            arts_session_free(sess,1);   
            srv->polltimeout = 1000;  
            return HANDLER_GO_ON;
          }
                
        int len = sizeof(sARTSPHInterface->tmpbuffer);
       
        
        //sARTSSupportOtherChannel -->yangzhou-hd TsOverIp
        //sARTSSupportPTS          -->wasu        TsOverIp
        //no_describe         -->zibo        Ipqam
        if( (sess!= NULL && sess->transport_type==qtssRTPTransportTypeMPEG2) || sARTSSupportOtherChannel == true || sARTSSupportPTS== true || no_describe == true  )
        {
            len =1316;
            //len = 1504;
        }
       
        //in = recv(sess_head->sock->fd,(void*)tmpbuffer,len,MSG_DONTWAIT);
        struct sockaddr_in  fMsgAddr;
        ::memset(&fMsgAddr, 0, sizeof(fMsgAddr));
        socklen_t addrlen = sizeof(fMsgAddr);
        
        
        in = recvfrom(sess_head->sock->fd, (void *)sARTSPHInterface->tmpbuffer, len, 0, (sockaddr*)&fMsgAddr, &(addrlen));
                
        LogRequest(DEBUG_ARTS_MODULE, 0, "arts_handle_adapter_fdevent: data received: data_len = %d,fd:%d",in,sess_head->sock->fd);
       

        // If in < 1 then Connection went down
        if( (in > 0) && (!sess) )
        {
            // First Data received on socket. We need to associate with a real session struct
            // Remove the fd from the event loop.
            fdevent_event_del(srv->ev, sess_head->sock);        
            fdevent_unregister(srv->ev, sess_head->sock);
            //sess = arts_session_find(sndrcvinfo.sinfo_ppid);g
            unsigned int sess_callid = get_multicast_callid(sess_head->sock->fd);
            sess = arts_session_find(sess_callid);
            if(!sess || (sess->head.state & ARTS_CALL_STATE_DESTROY_NOW))
            {
                // Session not found. Cleanup and return.
                if(!sess){
                    LogRequest(INFO_ARTS_MODULE, sess_callid, "adapter unexpected connection,sock->fd:%d",sess_head->sock->fd); 
                }
                else
                {
                    LogRequest(INFO_ARTS_MODULE, sess_callid, "head.state:%d",sess->head.state);
                }     
                multicast_group_leave(sess_head->sock->fd,sess_head->addr);                                           
                arts_session_head_free(sess_head);
                return HANDLER_GO_ON;
            }
            else
            {
               
                // Session found.
                           
                sess->head.sock = sess_head->sock;
                sess->head.addr = sess_head->addr;
                LogRequest(DEBUG_ARTS_MODULE, 0,"sess->head.sock:%x",sess->head.sock);
                sess_head->sock = NULL;
                 
                arts_session_head_free(sess_head);                
                        
                fdevent_register(srv->ev, sess->head.sock, arts_handle_adapter_fdevent_io, sess);  
                
                if(sess->head.state & ARTS_CALL_STATE_PLAY)
				{	//we must wait until we received response from Adapter and then we can move on to process data from adapter
					// before that we only listen to HUP,ERR events, data will be hold. 
                  fdevent_event_add(srv->ev, sess->head.sock, FDEVENT_IN | FDEVENT_HUP );
                  LogRequest(DEBUG_ARTS_MODULE, sess->callid,"event_add when sess->head.state == play");
					
				}               
                  //first packet received from adapter ,need initialize some struct         
                 if (sess->darwin_custom_struct ==NULL)
                 {              
                        custom_struct_t *custom_struct = new_custom_struct(sess);
                        
                        MpegTSContext * tsctx = (MpegTSContext*) malloc(sizeof( MpegTSContext));
                        Assert(tsctx!=NULL);
                        memset(tsctx ,0,sizeof(MpegTSContext));
                        init_tscontext(tsctx,MUX_RATE,sARTSIpqamPCRInterval);
                        
                        custom_struct->tsctx = tsctx; 
                        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"cur_pcr:%d,last_pcr:%d,mux-rate:%d,"
                        "delay:%d",tsctx->cur_pcrMS,tsctx->last_pcr,tsctx->mux_rate,tsctx->pcr_retransmit_time);                      
                                                       
                        sess->darwin_custom_struct =custom_struct;                        
                        custom_struct->adapter_unregister = true;
                        insert_stopEventsList(sess->callid,FDEVENT_IN | FDEVENT_HUP,sess,sess->head.sock->fd);   
                        LogRequest(INFO_ARTS_MODULE, sess->callid,"unregister adapter util receive play request");  
                       
                        srv->polltimeout =500;
                   
                    }  
                    
               }    
        }
             
        if(in >0)
        {   
             srv->polltimeout =500;
             custom_struct_t * custom_struct =(custom_struct_t *)  sess->darwin_custom_struct;
             
             if(sess == NULL || sess->darwin_custom_struct == NULL)
             {
                LogRequest(INFO_ARTS_MODULE, sess->callid,"receive_pkts stop,we will unregist adapter");
                fdevent_event_del(srv->ev, sess_head->sock);        
                fdevent_unregister(srv->ev, sess_head->sock); 
            
                return HANDLER_GO_ON;
             }
             
            if(sess->head.state & ARTS_CALL_STATE_PLAY)
            {                                          
                   
                   ARTS_Get_Packet * sARTSGetPacket =(ARTS_Get_Packet *) custom_struct->receive_pkts_thread;
                   
                   if(sARTSGetPacket != NULL&& in >0)
                   {                                
                      sARTSGetPacket->buffer=sARTSPHInterface->tmpbuffer ;                          
                      sARTSGetPacket->buffer_len = in;     
                                                    
                      sARTSGetPacket->stream_type = sndrcvinfo.sinfo_stream;
                      sARTSGetPacket->sess = sess;
                      sARTSGetPacket->Run();               
                                            
                    }
                    
                    
                    if( (custom_struct->buf_duration >MAXBUFFERDURATION && live == false)  || (
                    (sess->rtp_packet_buffer_len > MAXBUFFERLEN) && live == true))
                    {
                        fdevent_event_del(srv->ev, sess->head.sock);
                                                                     
                        set_unregister(sess);       
                        custom_struct->adapter_unregister = true;
                        LogRequest(INFO_ARTS_MODULE, sess->callid,"buffer is full,unregister adapter,fd:%d",sess->head.sock->fd);
                    }              
            }
            
            else
            {         
                    
                    if(custom_struct->isIpqam==0  && inputfile == NULL)
                    {
                        memcpy(sess->first_packet.pkt_buf, sARTSPHInterface->tmpbuffer, in );
                        sess->first_packet.pkt_str = sndrcvinfo.sinfo_stream;
                        sess->first_packet.pkt_len = in ;                      
                    }
            }  
           
                    
            OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);    
            if(  ( ( !(sess->head.state & ARTS_CALL_STATE_PLAY))  && 
                    (sess->head.state & ARTS_CALL_STATE_READ_CON_RESPONSE) ) || (live==true &&(!( sess->head.state & ARTS_CALL_STATE_CONNECTED))))
            {
            
               
                Task *l_Task = (Task *)sess->task_ptr;
                        if(l_Task)
                        {
                            LogRequest(INFO_ARTS_MODULE, sess->callid, "Signal data Received Event");
           
                            ARTS_SignalStream(l_Task,Task::kReadEvent);
                        }
                        else
                            Assert(false);
                            
              
               sess->head.state |= ARTS_CALL_STATE_CONNECTED;    
               LogRequest(INFO_ARTS_MODULE, sess->callid,"sess head ARTS_CALL_STATE_CONNECTED");           
             }
                
            LogRequest(DEBUG_ARTS_MODULE, sess->callid, "adapter call connected,sess->head.state:%d",sess->head.state);
        }     
       
        
    }
    if((in <=0) || (revents & FDEVENT_HUP) || (revents & FDEVENT_ERR))
    {
        // Connection went down
        
        del_adapter_fdevent(sess,sess_head->sock->fd);             
        fdevent_event_del(srv->ev, sess_head->sock);        
        fdevent_unregister(srv->ev, sess_head->sock);
        LogRequest(INFO_ARTS_MODULE, 0, "arts_handle_adapter_fdevent: No data or Socket closed for fd = %d", sess_head->sock->fd);
        srv->polltimeout=1000;   
     }
    return HANDLER_GO_ON;  
      
}


int  register_adapter_handler(char *host ,int port,unsigned int l_callid)
{
    LogRequest(INFO_ARTS_MODULE, 0,"Entry");
    int newfd =-1;
    if(host == NULL || port <=0)
        return -1;
    UInt32 addr = SocketUtils::ConvertStringToAddr(host);
    newfd = create_client_fd(addr,port);
    
    ARTS_PH_Interface *srv  = sARTSPHInterface;
    
    
    if(newfd != -1)
    {
        multicast_group_join(newfd, addr);
        arts_session_head *sess_head = arts_session_head_init();
        sess_head->sock->fd = newfd;
        sess_head->addr = addr;
        
        {
           OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
           insert_mul_list(l_callid,newfd);         
        } 
             
        fdevent_register(srv->ev, sess_head->sock, arts_handle_adapter_fdevent_io, sess_head);
        fdevent_event_add(srv->ev, sess_head->sock, FDEVENT_IN | FDEVENT_HUP);
                        
        LogRequest(INFO_ARTS_MODULE, 0, "arts_handle_listener_fdevent: listener new connection fd = %d", sess_head->sock->fd);
    }
    return newfd;
}


handler_t arts_handle_listener_fdevent(void *s, void *ctx, int revents) 
{
    ARTS_PH_Interface *srv = (ARTS_PH_Interface *) s;
    
    LogRequest(INFO_ARTS_MODULE, 0, "arts_handle_listener_fdevent: listener connection event = %d", revents);

    if(revents & FDEVENT_IN)
    {
        int newfd = arts_adapter_accept(srv->listensock->fd, srv->control_state);
        if(newfd != -1)
        {
            arts_session_head *sess_head = arts_session_head_init();
            sess_head->sock->fd = newfd;
            fdevent_register(srv->ev, sess_head->sock, arts_handle_adapter_fdevent, sess_head);
            fdevent_event_add(srv->ev, sess_head->sock, FDEVENT_IN | FDEVENT_HUP);
                        
            LogRequest(INFO_ARTS_MODULE, 0, "arts_handle_listener_fdevent: listener new connection fd = %d", sess_head->sock->fd);
        }
    }
    return HANDLER_GO_ON;
}

// Handle Registration response
static void arts_rtsp_reg_handler(void *ctx, unsigned char regid)
{
    ARTS_PH_Interface *srv = (ARTS_PH_Interface *) ctx;
    if(srv->control_state == ARTS_CONTROLLER_STATE_READ_REG_RESPONSE)
    {
        srv->control_state = ARTS_CONTROLLER_STATE_ACTIVE;
        srv->phid = regid;

        LogRequest(INFO_ARTS_MODULE, 0, "registration received id=%d", regid);
		qtss_printf("ARTS RTSP Module: registration received id=%d\n", regid);
    }
}

// Handle Connection Response
static void arts_rtsp_conresp_handler(void *ctx, arts_session *p_sess, int count, arts_ph_keyValue *p_keyValuePairs)
{
    LogRequest(INFO_ARTS_MODULE, p_sess->callid, "connection response received,p->callid:%d",p_sess->callid);
    
    p_sess->keyValuePairs = arts_ph_create_keyvalues(count);
    p_sess->numKeyValuePairs = count;
    for(int i=0; i < count; i++)
    {
        arts_ph_calloc_keyvalue(p_sess->keyValuePairs, i ,  p_keyValuePairs[i].name, p_keyValuePairs[i].value);
    }
    Task *l_Task = (Task *)p_sess->task_ptr;

    if(l_Task)
        ARTS_SignalStream(l_Task,Task::kReadEvent);
    else
        Assert(false);
}

// Handle Connection Release
static void arts_rtsp_conrel_handler(void *ctx, arts_session *p_sess, unsigned int cause)
{
    //ARTS_PH_Interface *srv = (ARTS_PH_Interface *) ctx;
    LogRequest(INFO_ARTS_MODULE, p_sess->callid, "connection release received");

    if(cause)
    {
    // This is an abort. Be careful, the enum for cause may change with change in xml file
    // Remove the fd from the event loop
        LogRequest(INFO_ARTS_MODULE, p_sess->callid, "Cause = %d", cause);
        QTSS_ClientSessionObject theClientSession = p_sess->remote_con;
        p_sess->head.state = ARTS_CALL_STATE_DESTROY_NOW;
        qtss_printf("p_sess->head.state = ARTS_CALL_STATE_DESTROY_NOW;\n");
        p_sess->ReleaseCause = cause;
        
                
        if(theClientSession)
	    {
			if(  (  (!(p_sess->head.state & ARTS_CALL_STATE_CONNECTED))  &&
				    (!(p_sess->head.state & ARTS_CALL_STATE_READ_CON_RESPONSE)) ) || //Ex Adapter Resource not Available
				    ( !(p_sess->head.state & ARTS_CALL_STATE_PLAY)) )                //Ex URI Not found
			{
				Task *l_Task = (Task *)p_sess->task_ptr;
				if(l_Task)
				{
					LogRequest(INFO_ARTS_MODULE, p_sess->callid, "Signal Stream");
				
					ARTS_SignalStream(l_Task,Task::kReadEvent);
				}
				else
					Assert(false);
			}
			else
			{   
			    LogRequest(DEBUG_ARTS_MODULE, p_sess->callid,"QTSS_Teardown");
				(void)QTSS_Teardown(theClientSession);
            }
	    }
    }
    else if(!(p_sess->head.state & ARTS_CALL_STATE_CONNECTED))
    {
        LogRequest(INFO_ARTS_MODULE, p_sess->callid, "Call State = %d", p_sess->head.state);
    
        // We are not connected, then just delete the session and send Teardown
        p_sess->head.state = ARTS_CALL_STATE_DESTROY_NOW;
        qtss_printf("p_sess->head.state = ARTS_CALL_STATE_DESTROY_NOW;123\n");
        p_sess->ReleaseCause = proto64::Normal;
        QTSS_ClientSessionObject theClientSession = p_sess->remote_con;
        if(theClientSession){
            (void)QTSS_Teardown(theClientSession);
        }
    }
    else
    {
        p_sess->head.state |= ARTS_CALL_STATE_DESTROY;
    }
}

static void arts_rtsp_shutdown_handler(void *ctx, unsigned int ServerState)
{
    LogRequest(INFO_ARTS_MODULE, 0, "arts_rtsp_shutdown_handler: Shutdown Received ");

    QTSS_Error theErr = QTSS_NoErr;
    if(sServer) 
        theErr = QTSS_SetValue(sServer, qtssSvrState , 0, &ServerState, sizeof(ServerState));
    Assert(theErr == QTSS_NoErr);
}


// Handle Flush
static void arts_rtsp_flush_handler(void *ctx, arts_session *p_sess, unsigned int flush)
{
    //ARTS_PH_Interface *srv = (ARTS_PH_Interface *) ctx;
    LogRequest(INFO_ARTS_MODULE, p_sess->callid, "Flush Received");

    if(flush)
    {
       p_sess->vid_discont = 1;
       p_sess->aud_discont = 1;
       p_sess->flush_discont = 1;
          
    }
}

static arts_system_load_state arts_rtsp_loadstatus_handler()
{

   QTSServerInterface* theServer = QTSServerInterface::GetServer();
   QTSS_ServerState theServerState = theServer->GetServerState();
          
   //we may want to deny this connection for a couple of different reasons
   //if the server is refusing new connections
   if ((theServerState == qtssRefusingConnectionsState) ||
                      (theServerState == qtssIdleState) ||
                (theServerState == qtssFatalErrorState) ||
                  (theServerState == qtssShuttingDownState))
   return OVERLOADED;    

   //if the max connection limit has been hit    
   SInt32 maxConns = theServer->GetPrefs()->GetMaxConnections();
   
   if (maxConns > -1) // limit connections
   { 
    UInt32 maxConnections = (UInt32) maxConns;
    if  ( (theServer->GetNumRTPSessions() >= maxConnections)|| 
          ( theServer->GetNumRTSPSessions() + theServer->GetNumRTSPHTTPSessions() >= maxConnections )) 
    {
     return OVERLOADED;         
    }
   }  
            
   //if the max bandwidth limit has been hit
   SInt32 maxKBits = theServer->GetPrefs()->GetMaxKBitsBandwidth();
   if ( (maxKBits > -1) && (theServer->GetCurBandwidthInBits() >= ((UInt32)maxKBits*1024)) )
   return OVERLOADED;

   return NOTOVERLOADED;                           

}    


//------------------------------------------------------------------------
// MODULE FUNCTIONS IMPLEMENTATION
//------------------------------------------------------------------------


QTSS_Error ARTS_RequestEvent(Task **ppTask)
{
    // First thing to do is to alter the thread's module state to reflect the fact
    // that an event is outstanding.
    QTSS_ModuleState* theState = (QTSS_ModuleState*)OSThread::GetMainThreadData();
    if (OSThread::GetCurrent() != NULL)
        theState = (QTSS_ModuleState*)OSThread::GetCurrent()->GetThreadData();

    if (theState == NULL)
        return QTSS_RequestFailed;

    if (theState->curTask == NULL)
        return QTSS_OutOfState;

    theState->eventRequested = true;
    *ppTask = theState->curTask;
    return QTSS_NoErr;
}

QTSS_Error ARTS_SignalStream(Task *pTask,int event)
{
    if (pTask != NULL)
        pTask->Signal(event);
    return QTSS_NoErr;
}

QTSS_Error ARTSRTSPModule_Main(void* inPrivateArgs)
{
    return _stublibrary_main(inPrivateArgs, ARTSRTSPModuleDispatch);
}

// Dispatch this module's role call back.
QTSS_Error ARTSRTSPModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams)
{
    switch (inRole)
    {
        case QTSS_Register_Role:
            return Register(&inParams->regParams);
        case QTSS_Initialize_Role:
            return Initialize(&inParams->initParams);
        case QTSS_RereadPrefs_Role:
            return RereadPrefs();
        case QTSS_Shutdown_Role:
            return Shutdown();
        case QTSS_RTSPPreProcessor_Role:
            return ProcessRTSPRequest(&inParams->rtspPreProcessorParams);
		case QTSS_RTSPSessionClosing_Role:
			return CloseRTSPSession(&inParams->rtspSessionClosingParams);
        case QTSS_RTPSendPackets_Role:
            return SendPackets(&inParams->rtpSendPacketsParams);
        case QTSS_ClientSessionClosing_Role:{
            //qtss_printf("call DestroySession\n");
            return DestroySession(&inParams->clientSessionClosingParams);
         }
		case QTSS_RTCPProcess_Role:
		    return ProcessRTCPPacket(&inParams->rtcpProcessParams);
	    
    }
    return QTSS_NoErr;
}

// Handle the QTSS_Register role call back.
QTSS_Error Register(QTSS_Register_Params* inParams)
{
   // Do role & attribute setup
    (void)QTSS_AddRole(QTSS_Initialize_Role);
    (void)QTSS_AddRole(QTSS_RereadPrefs_Role);
    (void)QTSS_AddRole(QTSS_Shutdown_Role);
    (void)QTSS_AddRole(QTSS_RTSPPreProcessor_Role);
	(void)QTSS_AddRole(QTSS_RTSPSessionClosing_Role);
    (void)QTSS_AddRole(QTSS_RTPSendPackets_Role);   
    (void)QTSS_AddRole(QTSS_ClientSessionClosing_Role);
	(void)QTSS_AddRole(QTSS_RTCPProcess_Role);
    
    // Add an Client session attribute for tracking ARTSSession objects
    static char* sARTSSessionName    = "ARTSRTSPModuleSession";

    (void)QTSS_AddStaticAttribute(qtssClientSessionObjectType, sARTSSessionName, NULL, qtssAttrDataTypeUInt32);
    (void)QTSS_IDForAttr(qtssClientSessionObjectType, sARTSSessionName, &sARTSSessionAttr);

	//added by lijie, 2010.09.30
    (void)QTSS_AddStaticAttribute(qtssRTSPSessionObjectType, sARTSSessionName, NULL, qtssAttrDataTypeUInt32);
    (void)QTSS_IDForAttr(qtssRTSPSessionObjectType, sARTSSessionName, &sARTSRTSPSessionAttr);
	//jieli
	
    // Tell the server our name!
    static char* sModuleName = "ARTSRTSPModule";
    ::strcpy(inParams->outModuleName, sModuleName);

    return QTSS_NoErr;
}

// Handle the QTSS_Initialize role call back.
QTSS_Error Initialize(QTSS_Initialize_Params* inParams)
{
    QTSS_Error err = QTSS_NoErr;
     
    
    sARTSPHInterface = new ARTS_PH_Interface();
   
    
    // Setup module utils
    QTSSModuleUtils::Initialize(inParams->inMessages, inParams->inServer, inParams->inErrorLogStream);

    // Get prefs object
    sPrefs = QTSSModuleUtils::GetModulePrefsObject(inParams->inModule);
    sServerPrefs = inParams->inPrefs;
    sServer = inParams->inServer;

    sARTSAccessLog = NEW QTSSARTSAccessLog();
    
    err = RereadPrefs();
   
    if (sARTSAccessLog != NULL && sLogEnabled)
        sARTSAccessLog->EnableLog();

	WriteStartupMessage();
   
	LogRequest(INFO_ARTS_MODULE, 0, "Role = Initialize: System Name = %s", sARTSSystemName);
	qtss_printf("Role = Initialize: System Name = %s", sARTSSystemName);
    
    LogRequest(INFO_ARTS_MODULE, 0, "Role = Initialize: Backends = %s", sARTSBackends);
    qtss_printf("ARTS RTSP Module: Role = Initialize: Backends = %s\n", sARTSBackends);

    LogRequest(INFO_ARTS_MODULE, 0, "Role = Initialize: BindHost = %s", sARTSBindHost);
    qtss_printf("ARTS RTSP Module: Role = Initialize: BindHost = %s\n", sARTSBindHost);
    
    LogRequest(INFO_ARTS_MODULE, 0, "Role = Initialize: ListenPort = %d", sARTSListenPort);
    qtss_printf("ARTS RTSP Module: Role = Initialize: ListenPort = %d\n", sARTSListenPort);

	LogRequest(INFO_ARTS_MODULE, 0, "Role = Initialize: Number HandleDir = %d", sARTSNumHandleDir);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: Enable Diffserv = %d",sEnableDiffServ);  

    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: Send RTCP Bye on EOS = %d", sSendRtcpByeOnEos);
    qtss_printf("ARTS RTSP Module: Role = Initialize: Send RTCP Bye on EOS = %d\n", sSendRtcpByeOnEos);
    
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: Inputfile =%s",sARTSInputFile);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: Ipqam_Host =%s",sARTSIpqamHost);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: Ipqam_Port=%d",sARTSIpqamPort);
   
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: Ipqam_Bitrate=%d",sARTSIpqamBitrate);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: suppotherChannel=%d",sARTSSupportOtherChannel);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: no-describe=%d",no_describe);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize: Ip_ahead_time=%d",sARTSIpAheadTime);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize:  couchbase_host=%s",couchbase_host);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize:  First_Play_Range:%d",firstPlayRange);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize:  SupportPTS=%d",sARTSSupportPTS);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize:  sARTSCBR=%d",sARTSCBR);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize:  live=%d",live);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize:  sARTSPSIDuration=%d",sARTSPSIDuration);
    LogRequest(INFO_ARTS_MODULE, 0,"Role = Initialize:   ARTSsendBitrate=%d", ARTSsendBitrate);
   
  
    
    switch (arts_log_level)
    {
        case 0:
            LogRequest(INFO_ARTS_MODULE, 0,"loglevel:NON");
            break;
        case 1:
            LogRequest(INFO_ARTS_MODULE, 0,"loglevel:INFO");
            break;
        case 2:
            LogRequest(INFO_ARTS_MODULE, 0,"loglevel:DEBUG");
            break;
        default:
            break;
    }
    
    ARTS_MODULE_DEBUG_LEVEL = arts_log_level;

	for (UInt32 theIndex = 0; theIndex < sARTSNumHandleDir; theIndex++)
    {
	    qtss_printf("ARTS RTSP Module: Role = Initialize: HandleDir = %s\n", sARTSHandleDir[theIndex]);
		LogRequest(INFO_ARTS_MODULE, 0, "Role = Initialize: HandleDir = %s", sARTSHandleDir[theIndex]);
    }
    
    for (UInt32 theIndex = 0; theIndex < sARTSSDPRTPLineNum; theIndex++)
    {
	    qtss_printf("ARTS RTSP Module: Role = Initialize: SDP-rtp-Line = %s\n", sARTSSDPRTPLine[theIndex]);
		LogRequest(INFO_ARTS_MODULE, 0, "Role = Initialize: SDP-rtp-Line = %s", sARTSSDPRTPLine[theIndex]);
    }

    for (UInt32 theIndex = 0; theIndex < sARTSSDPTSLineNum; theIndex++)
    {
	    qtss_printf("ARTS RTSP Module: Role = Initialize: SDP-ts-Line = %s\n", sARTSSDPTSLine[theIndex]);
		LogRequest(INFO_ARTS_MODULE, 0, "Role = Initialize: SDP-ts-Line = %s", sARTSSDPTSLine[theIndex]);
    }

    if(!sARTSPHInterface->Configure(sARTSBackends, sARTSBindHost, sARTSListenPort))
        return QTSS_RequestFailed;
       
    if(  (sARTSIpqamHost!=NULL && strlen(sARTSIpqamHost))>0 || live == true || ARTSsendBitrate == true || sARTSCBR == true)
    //if(  (sARTSIpqamHost!=NULL && strlen(sARTSIpqamHost))>0  || ARTSsendBitrate == true || sARTSCBR == true) //no used in wasu con-test ,delete "live ==true"
    {
         sARTSSendPktThread = new ARTS_Send_Pkt_Thread();
        
         if(sARTSIpqamHost!=NULL  || live == true || ARTSsendBitrate == true)
         {
            bool flag = false;
            
            if(live == true || ARTSsendBitrate == true)
                flag = true;
            sARTSSendPktThread->Configure(sARTSIpqamHost,sARTSIpqamPort,sARTSIpqamBitrate,flag);
            sARTSSendPktThread->send_ahead_time = sARTSIpqamAheadTime;
            sARTSSendPktThread->send_after_time = sARTSIpqamAfterTime;
           
            
         }
         
         
         sARTSSendPktThread->Start();
         
        
       
    }
    
    if(sARTSIpqamBitrate >0)
        MUX_RATE = sARTSIpqamBitrate;
    
    
    
    sARTSPHInterface->Start();

    // Report to the server that this module handles DESCRIBE, SETUP, PLAY, PAUSE, and TEARDOWN
    //static QTSS_RTSPMethod sSupportedMethods[] = { qtssDescribeMethod, qtssSetupMethod, qtssTeardownMethod, 
      //                      qtssPlayMethod, qtssPauseMethod,qtssGetParameterMethod};
	static QTSS_RTSPMethod sSupportedMethods[] = { qtssDescribeMethod, qtssSetupMethod, qtssTeardownMethod, 
                            qtssPlayMethod, qtssGetParameterMethod};
    QTSSModuleUtils::SetupSupportedMethods(inParams->inServer, sSupportedMethods, 5);

    return err;
}

QTSS_Error GetARTSHandleDir()
{
    QTSS_Object theAttrInfo = NULL;
    QTSS_Error theErr = QTSS_GetAttrInfoByName(sPrefs, "arts_handledir", &theAttrInfo);
    if (theErr != QTSS_NoErr)
	    return qtssIllegalAttrID;
   
    QTSS_AttrDataType theAttributeType = qtssAttrDataTypeUnknown;
    UInt32 theLen = sizeof(theAttributeType);
   
    QTSS_AttributeID theID = qtssIllegalAttrID; 
    theLen = sizeof(theID);
    theErr = QTSS_GetValue(theAttrInfo, qtssAttrID, 0 , &theID, &theLen);
    Assert(theErr == QTSS_NoErr);

    QTSS_GetNumValues(sPrefs, theID, &sARTSNumHandleDir);
    Assert(theErr == QTSS_NoErr);    

    if (sARTSNumHandleDir == 0)
        return NULL;

    for (UInt32 theIndex = 0; theIndex < sARTSNumHandleDir; theIndex++)
    {
        theErr = QTSS_NoErr;
        (void)QTSS_GetValueAsString((QTSS_Object)sPrefs, theID, theIndex, &sARTSHandleDir[theIndex]);
        Assert(theErr == QTSS_NoErr);   
    }
	return QTSS_NoErr;
}


QTSS_Error GetARTSSDPRTPLine()
{
    QTSS_Object theAttrInfo = NULL;
    QTSS_Error theErr = QTSS_GetAttrInfoByName(sPrefs, "arts_sdp_line_rtp", &theAttrInfo);
    if (theErr != QTSS_NoErr)
	    return qtssIllegalAttrID;
   
    QTSS_AttrDataType theAttributeType = qtssAttrDataTypeUnknown;
    UInt32 theLen = sizeof(theAttributeType);
   
    QTSS_AttributeID theID = qtssIllegalAttrID; 
    theLen = sizeof(theID);
    theErr = QTSS_GetValue(theAttrInfo, qtssAttrID, 0 , &theID, &theLen);
    Assert(theErr == QTSS_NoErr);

    QTSS_GetNumValues(sPrefs, theID, &sARTSSDPRTPLineNum);
    Assert(theErr == QTSS_NoErr);    

    if (sARTSSDPRTPLineNum == 0)
        return NULL;

    for (UInt32 theIndex = 0; theIndex < sARTSSDPRTPLineNum; theIndex++)
    {
        theErr = QTSS_NoErr;
        (void)QTSS_GetValueAsString((QTSS_Object)sPrefs, theID, theIndex, &sARTSSDPRTPLine[theIndex]);
        Assert(theErr == QTSS_NoErr);   
    }
	return QTSS_NoErr;
}

QTSS_Error GetARTSSDPTSLine()
{
    QTSS_Object theAttrInfo = NULL;
    QTSS_Error theErr = QTSS_GetAttrInfoByName(sPrefs, "arts_sdp_line_ts", &theAttrInfo);
    if (theErr != QTSS_NoErr)
	    return qtssIllegalAttrID;
   
    QTSS_AttrDataType theAttributeType = qtssAttrDataTypeUnknown;
    UInt32 theLen = sizeof(theAttributeType);
   
    QTSS_AttributeID theID = qtssIllegalAttrID; 
    theLen = sizeof(theID);
    theErr = QTSS_GetValue(theAttrInfo, qtssAttrID, 0 , &theID, &theLen);
    Assert(theErr == QTSS_NoErr);

    QTSS_GetNumValues(sPrefs, theID, &sARTSSDPTSLineNum);
    Assert(theErr == QTSS_NoErr);    

    if (sARTSSDPTSLineNum == 0)
        return NULL;
    
    qtss_printf("sARTSSDPTSLineNum:%d\n",sARTSSDPTSLineNum);

    for (UInt32 theIndex = 0; theIndex < sARTSSDPTSLineNum; theIndex++)
    {
        theErr = QTSS_NoErr;
        (void)QTSS_GetValueAsString((QTSS_Object)sPrefs, theID, theIndex, &sARTSSDPTSLine[theIndex]);
        qtss_printf("sARTSSDPTSLine:%s\n",sARTSSDPTSLine[theIndex]);
        Assert(theErr == QTSS_NoErr);   
    }
	return QTSS_NoErr;
}


// Handle the QTSS_RereadPrefs_Role role call back.
QTSS_Error RereadPrefs()
{
    delete [] sDefaultLogDir;
    (void)QTSS_GetValueAsString(sServerPrefs, qtssPrefsErrorLogDir, 0, &sDefaultLogDir);

    delete [] sLogName;
    sLogName = QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_request_logfile_name", sDefaultLogName);
    
    delete [] sLogDir;
    sLogDir  =  QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_request_logfile_dir", sDefaultLogDir);

    delete [] sARTSSystemName;
    sARTSSystemName = QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_systemname", sDefaultARTSSystemName);

    delete [] sARTSBackends;
    sARTSBackends = QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_backends", sDefaultARTSBackends);

    delete [] sARTSBindHost;
    sARTSBindHost = QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_bindhost", sDefaultARTSBindHost);
    
    delete [] couchbase_host;
    couchbase_host = QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_couchbasehost", sDefaultARTSBindHost);
    
    
    delete [] sARTSIpqamHost;
    sARTSIpqamHost = QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_ipqam_host", sDefaultARTSBindHost);
    
    
    delete [] sARTSInputFile;
    sARTSInputFile = QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_inputfile", sDefaultARTSBindHost);
    
    delete [] sARTSTestUrl;
    sARTSTestUrl = QTSSModuleUtils::GetStringAttribute(sPrefs, "arts_test_url", sDefaultARTSBindHost);
    
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_log_level",  qtssAttrDataTypeUInt16,
                                &arts_log_level, &sDefaultARTSListenPort, sizeof(arts_log_level));
    
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_ipqam_port",  qtssAttrDataTypeUInt16,
                                &sARTSIpqamPort, &sDefaultARTSListenPort, sizeof(sARTSIpqamPort));
      
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_ipqam_frequency", qtssAttrDataTypeUInt32,
                                &sARTSIpqamFrequency, &sDefaultRollInterval, sizeof(sARTSIpqamFrequency)); 
    
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_ipqam_send_bitrate", qtssAttrDataTypeUInt32,
                                &sARTSIpqamBitrate, &sDefaultRollInterval, sizeof(sRollInterval));
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_ip_ahead_time", qtssAttrDataTypeUInt32,
                                &sARTSIpAheadTime, &sDefaultRollInterval, sizeof(sARTSIpAheadTime));  
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_psi_duration", qtssAttrDataTypeUInt32,
                                &sARTSPSIDuration, &sDefaultRollInterval, sizeof(sARTSPSIDuration));
                                 
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_ipqam_send_ahead_time", qtssAttrDataTypeUInt32,
                                &sARTSIpqamAheadTime, &sDefaultRollInterval, sizeof(sARTSIpqamAheadTime));
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_ipqam_send_after_time", qtssAttrDataTypeUInt32,
                                &sARTSIpqamAfterTime, &sDefaultRollInterval, sizeof(sARTSIpqamAfterTime));                            
    
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_listenport",  qtssAttrDataTypeUInt16,
                                &sARTSListenPort, &sDefaultARTSListenPort, sizeof(sARTSListenPort));

    QTSSModuleUtils::GetAttribute(sPrefs, "arts_request_logging", qtssAttrDataTypeBool16,
                                &sLogEnabled, &sDefaultLogEnabled, sizeof(sLogEnabled));
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_sz_module", qtssAttrDataTypeBool16,
                                &sARTSszModule, &sDefaultLogEnabled, sizeof(sARTSszModule));
    
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_send_bitrate_to_controller", qtssAttrDataTypeBool16,
                                &ARTSsendBitrate, &sDefaultLogEnabled, sizeof(ARTSsendBitrate));                            
          
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_jinshan_module", qtssAttrDataTypeBool16,
                                &sARTSJinShanModule, &sDefaultLogEnabled, sizeof(sARTSJinShanModule));
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_live_mode", qtssAttrDataTypeBool16,
                                &live, &sDefaultLogEnabled, sizeof(live));
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_support_pts", qtssAttrDataTypeBool16,
                                &sARTSSupportPTS, &sDefaultLogEnabled, sizeof(sARTSSupportPTS));   
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_cbr", qtssAttrDataTypeBool16,
                                &sARTSCBR, &sDefaultLogEnabled, sizeof(sARTSCBR));   
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_psi_cbr", qtssAttrDataTypeBool16,
                                &sARTSPSICBR, &sDefaultLogEnabled, sizeof(sARTSPSICBR));                      
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_no_describe", qtssAttrDataTypeBool16,
                                &no_describe, &sDefaultLogEnabled, sizeof(no_describe));
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_dump_input_flag", qtssAttrDataTypeBool16,
                                &dump_input, &sDefaultLogEnabled, sizeof(dump_input));
                                
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_dump_output_flag", qtssAttrDataTypeBool16,
                                &dump_output, &sDefaultLogEnabled, sizeof(dump_output));
                                
                       
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_allow_frist_play_range", qtssAttrDataTypeBool16,
                                &firstPlayRange, &sDefaultLogEnabled, sizeof(firstPlayRange));         
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_request_logfile_size", qtssAttrDataTypeUInt32,
                                &sMaxLogBytes, &sDefaultMaxLogBytes, sizeof(sMaxLogBytes));

    QTSSModuleUtils::GetAttribute(sPrefs, "arts_request_logfile_interval", qtssAttrDataTypeUInt32,
                                &sRollInterval, &sDefaultRollInterval, sizeof(sRollInterval));

    QTSSModuleUtils::GetAttribute(sPrefs, "arts_request_logtime_in_gmt", qtssAttrDataTypeBool16,
                                &sLogTimeInGMT, &sDefaultLogTimeInGMT, sizeof(sLogTimeInGMT));

    QTSSModuleUtils::GetAttribute(sPrefs, "arts_enable_diffserv", qtssAttrDataTypeBool16,
                                &sEnableDiffServ, &sDefaultEnableDiffServ, sizeof(sEnableDiffServ));
           
   QTSSModuleUtils::GetAttribute(sPrefs, "arts_support_other_channel", qtssAttrDataTypeBool16,
                                &sARTSSupportOtherChannel, &sDefaultLogEnabled, sizeof(sARTSSupportOtherChannel));                     
                                
    
    QTSSModuleUtils::GetAttribute(sPrefs, "arts_dscp",  qtssAttrDataTypeUInt16,
                                            &sARTSDSCP, &sDefaultDSCP, sizeof(sARTSDSCP));
   QTSSModuleUtils::GetAttribute(sPrefs, "arts_ipqam_pcr_retransmit_time", qtssAttrDataTypeUInt32,
                                &sARTSIpqamPCRInterval, &sARTSIpqamDefaultPCRInterval, sizeof(sARTSIpqamPCRInterval));

    QTSSModuleUtils::GetAttribute(sPrefs,"arts_send_rtcp_bye_on_eos", qtssAttrDataTypeBool16,
                                &sSendRtcpByeOnEos, &sDefaultSendRtcpByeOnEos, sizeof(sSendRtcpByeOnEos));
    // handle changing the sLogEnabled state of the access log.
    if (sARTSAccessLog != NULL)
    {
        if (sLogEnabled)
        {
            if (!sARTSAccessLog->IsLogEnabled())
                sARTSAccessLog->EnableLog();
        }
        else
        {
            if (sARTSAccessLog->IsLogEnabled())
                sARTSAccessLog->CloseLog();
        }
    }

    GetARTSHandleDir();
    GetARTSSDPRTPLine();
    GetARTSSDPTSLine();

    return QTSS_NoErr;
}

QTSS_Error ProcessRTSPRequest(QTSS_StandardRTSP_Params* inParams)
{
    QTSS_RTSPMethod* theMethod = NULL;
    UInt32 theMethodLen = 0;
    if ((QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqMethod, 0,
            (void**)&theMethod, &theMethodLen) != QTSS_NoErr) || (theMethodLen != sizeof(QTSS_RTSPMethod)))
    {
        Assert(false);
        return QTSS_RequestFailed;
    }

    switch (*theMethod)
    {
        case qtssDescribeMethod:
            return DoDescribe(inParams);
        case qtssSetupMethod:
            return DoSetup(inParams);
        case qtssPlayMethod:
            return DoPlay(inParams);
        case qtssTeardownMethod:
            // Tell the server that this session should be killed, and send a TEARDOWN response
            //qtss_printf("QTSS_Teardown--ProcessRTSPRequest\n");
            return DoTearDown(inParams);
            //qtss_printf("sendrtspResponse\n");
            
            break;
        case qtssPauseMethod:
             return DoPause(inParams);
             break;
        case qtssGetParameterMethod:
            //(void)QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
            return DoGetParameter(inParams);
            break;
            
        case qtssValidateMethod:
             return DoDefault(inParams);
            
        default:
            break;
    }

    return QTSS_NoErr;
}


QTSS_Error DoDefault(QTSS_StandardRTSP_Params* inParams)
{
    LogRequest(INFO_ARTS_MODULE, 0,"entry");
    
     if (CheckProfile(inParams) < 0 && !sARTSszModule)
    {
        return QTSS_NoErr;
    }
    
    if(inParams->inRTSPHeaders != NULL)
    {
        QTSSDictionary *headerDict = (QTSSDictionary* )inParams->inRTSPHeaders;
        StrPtrLen *theKeySessionID=headerDict->GetValue(qtssVaryHeader);
        RTSPSessionInterface * theRTSPSess =(RTSPSessionInterface *) inParams->inRTSPSession;
         
        LogRequest(DEBUG_ARTS_MODULE, 0,"RTSPSession:%x",theRTSPSess);
        RTPSession * theRtpSession  = (RTPSession *) inParams->inClientSession;
        Assert(theRTSPSess !=NULL && theRtpSession !=NULL);
            
           
       
            
        
        if(theKeySessionID!= NULL && theKeySessionID->Len >0)
        {
                      
            co_socket_t *sockstruct = (co_socket_t *)malloc(sizeof(co_socket_t));
            memset(sockstruct,0,sizeof(co_socket_t));
           
            char *sessionCStr = theKeySessionID->GetAsCString();
            memcpy(sockstruct->sessionID,sessionCStr,strlen(sessionCStr));
            
            sockstruct->sock =theRTSPSess->GetOutputStream()->GetSocket();          
            sockstruct->used = false; 
            sockstruct->next = NULL;
            sockstruct->rtspSessObj=(RTSPSession*)  inParams->inRTSPSession; 
            sockstruct->rtpSessObj =(RTPSession*) inParams->inClientSession;     
            
            LogRequest(DEBUG_ARTS_MODULE, 0,"validate header sessionID:%s",sessionCStr);
            delete sessionCStr;
            //OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
            if(sARTSPHInterface->co_socket_list == NULL)
            {
                sARTSPHInterface->co_socket_list = sockstruct;
                sARTSPHInterface->last_co_socket = sockstruct;
                LogRequest(DEBUG_ARTS_MODULE, 0,"sARTSPHInterface->co_socket_list:%x",sARTSPHInterface->co_socket_list);
            }else
            {
                sARTSPHInterface->last_co_socket ->next = sockstruct;          
              
                sARTSPHInterface->last_co_socket = sockstruct;           
            }
        }
    }
    
    
    QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
    LogRequest(INFO_ARTS_MODULE, 0,"exit");
    
    return QTSS_NoErr;    
}

QTSS_Error DoGetParameter(QTSS_StandardRTSP_Params* inParams)
{

   // LogRequest(INFO_ARTS_MODULE,0, "DoGetParameter: Entering ");
    QTSS_Error err = QTSS_NoErr;
    QTSS_ClientSessionObject theClientSession = inParams->inClientSession;   
    QTSS_RTSPRequestObject theRequest = inParams->inRTSPRequest;
   
   
    if (CheckProfile(inParams) < 0 && !sARTSszModule &&no_describe == false)
    {
        return QTSS_NoErr;
    }
   
    UInt32 l_callid = 0, l_len = sizeof(UInt32);
    arts_session *sess = NULL;
    err = QTSS_GetValue(theClientSession, sARTSSessionAttr, 0, (void *)&l_callid, &l_len);

    LogRequest(INFO_ARTS_MODULE, l_callid, "DoGetParameter: Entering ");	

    if(err == QTSS_NoErr)
    {
      //OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
      sess = arts_session_find(l_callid);  
    }
	
    if(!sess)
      return QTSS_NoErr;  

    if(sess)
    {
        sess->LastGetParameterTime = QTSS_MilliSecsTo1970Secs(QTSS_Milliseconds());;   
    }    
    iovec theDescribeVec[3] = { {0 }};
    ResizeableStringFormatter editedSDP(NULL,0);
    UInt32 sessLen = 0;
    UInt32 mediaLen = 0;
    char url[256]={'\0'};
   /* IF(SESs->transport_type == qtssRTPTransportTypeMPEG2 && custom_struct->first_pts >=0)*/
   
   UInt32 theLen = 0;
   UInt32* theContentLenP = NULL;
   err = QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqContentLen, 0, (void**)&theContentLenP, &theLen);
   if(err != QTSS_NoErr)
   {
        LogRequest(INFO_ARTS_MODULE, sess->callid,"get qtssRTSPReqContentLen failed");
        return QTSS_NoErr;  
   }
  
   LogRequest(DEBUG_ARTS_MODULE, sess->callid,"content_len:%d",theContentLenP,*theContentLenP);
   if(sess->transport_type == qtssRTPTransportTypeMPEG2 && sess->last_mpeg2_timestamp >=0 && sARTSszModule == true)
   {
   // strlen("connection_timeout") == 18
	if( *theContentLenP == 18)
	{
	    sprintf(url,"connection_timeout: 60\r\n\r\n");
	}
	// strlen("position") ==8
	if(*theContentLenP == 8)
	{
        sprintf(url,"position:%.3f\r\n\r\n",sess->last_mpeg2_timestamp/1000.0); 
    }   
    LogRequest(INFO_ARTS_MODULE, sess->callid,"%s,sess->last_mpeg2_timestamp:%d",url,sess->last_mpeg2_timestamp);
    editedSDP.Put(url);
   }

   if(strlen(url)>0)
   {
      theDescribeVec[1].iov_base = editedSDP.GetBufPtr();
      theDescribeVec[1].iov_len = editedSDP.GetBytesWritten();
      
      sessLen = editedSDP.GetBytesWritten();
      mediaLen = 0;
      char *theFullRequest = NULL;
     err = QTSS_GetValueAsString(theRequest, qtssRTSPReqFullRequest, 0, &theFullRequest);

     if (err != QTSS_NoErr)
     {
        return QTSS_NoErr;
     }
      if(strstr(theFullRequest,"text/parameters"))
     {
        StrPtrLen contentTypeHeader("text/parameters");
        (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssContentTypeHeader,
                                            contentTypeHeader.Ptr, contentTypeHeader.Len);
     }
      QTSSModuleUtils::SendDescribeResponse(inParams->inRTSPRequest, inParams->inClientSession,
                                                                  &theDescribeVec[0], 3, sessLen + mediaLen ); 
   }else
   {
    (void)QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
   }  
    
   // LogRequest(INFO_ARTS_MODULE, l_callid, "DoGetParameter: Exiting ");    
    return QTSS_NoErr;
}

int  CheckProfile(QTSS_StandardRTSP_Params* inParams)
{
    char       *theRequestAttributes = NULL;
    UInt32     attributeLen          = 0;
    QTSS_Error err                   = QTSS_NoErr;
     
    QTSS_RTSPRequestObject  theRequest  = inParams->inRTSPRequest;
     
    err = QTSS_GetValueAsString(theRequest, qtssRTSPReqFilePathTrunc, 0, &theRequestAttributes);
    if (err != QTSS_NoErr)
    {        
       return -1;
    }

    //LogRequest(INFO_ARTS_MODULE, 0, "DoDescribe: Role = QTSS_RTSPPreProcessor_Role: Path = %s", theRequestAttributes);

    if(sARTSNumHandleDir == 0 )
    {
		LogRequest(INFO_ARTS_MODULE, 0, "DoDescribe: Role = No Handle Dir In Pref");
		err =-1;
		goto finish;
    }
	
    for (UInt32 theIndex = 0; theIndex < sARTSNumHandleDir; theIndex++)
    {
		if(sARTSHandleDir[theIndex]!= NULL)
		{
			int len = strlen( sARTSHandleDir[theIndex] );// + 1;
		
			if(! strncmp(theRequestAttributes, sARTSHandleDir[theIndex], len))
				break;
		}
		if(theIndex == (sARTSNumHandleDir - 1))
		{
		    err =-1;
			goto finish;
         }	
    }

    //LogRequest(INFO_ARTS_MODULE, 0, "Role = OpenFilePreprocess: Ourfile");
finish:
    QTSS_Delete(theRequestAttributes);
    
    return err;
}

arts_session * NewSess( UInt32 *l_callid)
{
     sARTSPHInterface->callid += 16; // Last 4 bits should always be zero
           
     if(sARTSPHInterface->callid >= 0xffff0) //20 bits only
            sARTSPHInterface->callid = 16;

     
     LogRequest(DEBUG_ARTS_MODULE,0,"callid:%d,phid:%d",sARTSPHInterface->callid,sARTSPHInterface->phid);
     (*l_callid) =(UInt32) sARTSPHInterface->callid | sARTSPHInterface->phid << 24;
     unsigned int callid = (unsigned int)(*l_callid);
     arts_session *sess = arts_session_init(callid);
     LogRequest(DEBUG_ARTS_MODULE, sess->callid,"callid:%u,sess->callid:%u",*l_callid, sess->callid);
     return sess;
}


void prepare_params(QTSS_StandardRTSP_Params* inParams,arts_ph_callparams *callparams, UInt32 l_callid,arts_session *sess)
{
    QTSS_Error err = QTSS_NoErr;
    char       *theRequestAttributes = NULL;
    UInt32     attributeLen          = 0;
    QTSS_RTSPHeaderObject       theHeader = inParams->inRTSPHeaders;
    QTSS_RTSPSessionObject      theRTSPSession = inParams->inRTSPSession;
    QTSS_RTSPRequestObject      theRequest         = inParams->inRTSPRequest;
    QTSS_ClientSessionObject    theClientSession   = inParams->inClientSession;
    memset(callparams, 0, sizeof(arts_ph_callparams));
    callparams->callid = l_callid;
    callparams->listenport = sARTSPHInterface->listenport; 
    
    err = QTSS_GetValuePtr(theRequest, qtssRTSPReqFullRequest, 0, (void **)&theRequestAttributes, &attributeLen);   

    if(theRequestAttributes)
    {
        if(strstr(theRequestAttributes,"MP2T") || no_describe == true  ||strstr(theRequestAttributes,".ts") )
        {
            sess->transport_type=qtssRTPTransportTypeMPEG2;
            LogRequest(INFO_ARTS_MODULE, sess->callid,"chose MPEG2,theRequestAttributes:%s",theRequestAttributes);
        }
     }
    
    
    if(sARTSTestUrl && strlen(sARTSTestUrl))
    {
    
        strncpy(callparams->requestURI,sARTSTestUrl,strlen(sARTSTestUrl));
    }else
    {
    
        err = QTSS_GetValuePtr(theRequest, qtssRTSPReqAbsoluteURL, 0, (void **)&theRequestAttributes, &attributeLen);

        if(err == QTSS_NoErr)
        {
            char url[1024]={'\0'};
            strncpy(url,theRequestAttributes,attributeLen);
            char *start=strstr(url,";");
            if(start != NULL)
            {            
                strncpy(callparams->requestURI, url, start-url);
            }else     
            {               
                strncpy(callparams->requestURI, url, strlen(url));
            }
        }
    }

    err = QTSS_GetValuePtr(theRequest, qtssRTSPReqQueryString, 0, (void **)&theRequestAttributes, &attributeLen);

    if(err == QTSS_NoErr)
        strncpy(callparams->queryString, theRequestAttributes, attributeLen);
        
    err = QTSS_GetValuePtr(theRTSPSession, qtssRTSPSesRemoteAddrStr, 0, (void **)&theRequestAttributes, &attributeLen);

    if(err == QTSS_NoErr)
        strncpy(callparams->clientIP, theRequestAttributes, attributeLen);

    err = QTSS_GetValuePtr(theClientSession, qtssCliSesFirstUserAgent, 0, (void **)&theRequestAttributes, &attributeLen);

    if(err == QTSS_NoErr)
        strncpy(callparams->userAgent, theRequestAttributes, attributeLen);

    err = QTSS_GetValuePtr(sServer, qtssSvrServerName, 0, (void **)&theRequestAttributes, &attributeLen);

    if(err == QTSS_NoErr)
        strncpy(callparams->serverName, theRequestAttributes, attributeLen);

    err = QTSS_GetValuePtr(theRTSPSession, qtssRTSPSesLocalAddrStr, 0, (void **)&theRequestAttributes, &attributeLen);

    if(err == QTSS_NoErr)
        strncpy(callparams->serverHost, theRequestAttributes, attributeLen);

    UInt16 localPort=0;
    attributeLen = sizeof(localPort);
    err = QTSS_GetValue(theRTSPSession, qtssRTSPSesLocalPort, 0, (void *)&localPort, &attributeLen);

    if(err == QTSS_NoErr)
        sprintf(callparams->serverPort, "%d", localPort);

    strcpy(callparams->serverProtocol, "RTSP 1.0");

    err = QTSS_GetValuePtr(theHeader, qtssBandwidthHeader, 0, (void**)&theRequestAttributes, &attributeLen);
        
    if(err == QTSS_NoErr)
        strncpy(callparams->Bandwidth, theRequestAttributes, attributeLen);
    
        
    err = QTSS_GetValuePtr(theHeader, qtssXWapProfileHeader, 0,(void**)&theRequestAttributes,&attributeLen);
        
    if(err == QTSS_NoErr)
    {
        strncpy(callparams->xWapProfile,theRequestAttributes,attributeLen);
        callparams->xWapProfile[attributeLen]='\0';
    }

    err = QTSS_GetValuePtr(theHeader, qtssXWapProfileDiffHeader, 0,(void **)&theRequestAttributes,&attributeLen);
      
    if(err == QTSS_NoErr)
    {
        strncpy(callparams->xWapProfileDiff,theRequestAttributes,attributeLen);
        callparams->xWapProfileDiff[attributeLen]='\0';
    }
	   
}


QTSS_Error TearDownSession(arts_session *sess,QTSS_StandardRTSP_Params* inParams,int state)
{
    if( sess->RTSP_Session_Type == qtssRTSPSession && inParams != NULL)
    {

        QTSS_RTSPStatusCode  sCode = RTSPProtocol::GetQTSS_RTSPStatusCode(sess->ReleaseStatusCode);
        if(sCode != 0 )
        {
             LogRequest(INFO_ARTS_MODULE, sess->callid, "Error code:%d",sCode);  
            QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest,sCode , 0);
        }
        else
            QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest,qtssServerUnavailable , 0);

    }    
		
    if( sess->RTSP_Session_Type == qtssRTSPHTTPSession)
        QTSSModuleUtils::SendHTTPErrorResponse(inParams->inRTSPRequest,qtssClientNotFound,true, NULL);
	    
	if(state & ARTS_CALL_STATE_PLAY && inParams)
	{
        (void)QTSS_Teardown(inParams->inClientSession);
    }else    
    {
       
		OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
		if(sess->head.sock!= NULL && live == true )
            multicast_group_leave(sess->head.sock->fd,sess->head.addr);
        if(sess->head.sock != NULL)
        {
            OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);            
            del_events_node(sess->head.sock->fd);           
        }
        free_custom_struct(sess,NULL);
		arts_session_free(sess,1);
	}
}


QTSS_Error DoDescribe(QTSS_StandardRTSP_Params* inParams)
{
    
    LogRequest(INFO_ARTS_MODULE, 0, "DoDescribe: Entering "); 
    char rtp_sdp[1024]={'\0'}; 

    QTSS_Error err = QTSS_NoErr;
    QTSS_RTSPRequestObject       theRequest         = inParams->inRTSPRequest;
    QTSS_ClientSessionObject     theClientSession   = inParams->inClientSession;    
    RTSPRequestInterface         *theRTSPRequest    = (RTSPRequestInterface *)inParams->inRTSPRequest;
    QTSS_RTSPSessionObject      theRTSPSession = inParams->inRTSPSession;
    
    char * theRequestAttributes=NULL;
    UInt32 attributeLen=0;
    
    //check profile if not our, pass this request to other module
    if (CheckProfile(inParams) < 0 && sARTSJinShanModule == false) 
    {       
            LogRequest(INFO_ARTS_MODULE, 0,"No ARTS Module");
            return QTSS_NoErr;
    }
    	
    // This is important. Check controller state and proceed only if state ACTIVE.
    // Now it is possible that by the time the connnection request is sent, the 
    // controller will go down. But retries are done only after 10s, so we should be
    // ok, in that the connection request will fail but the send on the socket doesnt
    // need to be protected.
    QTSS_RTSPSessionType theSessionType = qtssRTSPSession;
	UInt32 theSessionLen = sizeof(theSessionType);
	
	QTSS_GetValue(inParams->inRTSPSession, qtssRTSPSesType, 0, (void*)&theSessionType, &theSessionLen);
    
    
    if ( (sARTSPHInterface->control_state != ARTS_CONTROLLER_STATE_ACTIVE) && live==false)
    {
    	if( theSessionType == qtssRTSPSession)
			 QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest, qtssServerUnavailable, 0);
	
		if( theSessionType == qtssRTSPHTTPSession)
			QTSSModuleUtils::SendHTTPErrorResponse(inParams->inRTSPRequest,qtssServerUnavailable,true, NULL);
        return QTSS_RequestFailed;
    }

    UInt32 l_callid = 0, l_len = sizeof(UInt32);
    arts_session *sess = NULL;
    err = QTSS_GetValue(theClientSession, sARTSSessionAttr, 0, (void *)&l_callid, &l_len);


    if(err == QTSS_NoErr)
    {      
        sess = arts_session_find(l_callid);  
        //LogRequest(INFO_ARTS_MODULE, l_callid, "Found session");
    }
    
    if(!sess)
    {
        arts_ph_callparams callparams;      /* Call Params Struct */                  
        sess =  NewSess(&l_callid);   
      
        if ( sess == NULL )
        {
            LogRequest(INFO_ARTS_MODULE, l_callid, "sess == NULL:1286");
            return QTSS_RequestFailed;
        }
        
        prepare_params(inParams,&callparams,l_callid,sess);
        
        sess->RTSP_Session_Type = theSessionType;
	      
      
		//added by lijie, 2010.09.30	  
		err = QTSS_SetValue(theRTSPSession, sARTSRTSPSessionAttr, 0, (void *)&l_callid, sizeof(UInt32));	  
		Assert(err == QTSS_NoErr);
	    //jieli
        err = QTSS_SetValue(theClientSession, sARTSSessionAttr, 0, (void *)&l_callid, sizeof(UInt32));
        Assert(err == QTSS_NoErr);
        sess->remote_con = theClientSession;
        sess->RTSPRequest = theRequest;
              
              
       
        if(live == true)
        {
            memcpy(sess->couchbase_host,couchbase_host,strlen(couchbase_host));
            get_channel_id(inParams,sess);
            if(sess->transport_type == qtssRTPTransportTypeMPEG2)
            {
                err = register_data_sock(sess->couchbase_host,l_callid,sess->channel_id,NULL);
            }else
            {
                err = register_data_sock(sess->couchbase_host,l_callid,sess->channel_id,rtp_sdp);
            }
                
           if(err  != QTSS_NoErr)
           {
                 TearDownSession(sess,inParams,ARTS_CALL_STATE_DESTROY_NOW);
                 LogRequest(INFO_ARTS_MODULE, l_callid,"live mode get multicase add failed ");
                 return QTSS_RequestFailed;
           }
        }
        else
        { 
            if(arts_send_conreq(sARTSPHInterface->controlsock->fd, &callparams) == -1)
            {
			    TearDownSession(sess,inParams,ARTS_CALL_STATE_DESTROY_NOW);
                LogRequest(INFO_ARTS_MODULE, l_callid, "arts_send_conreq(sARTSPHInterface->controlsock->fd, &callparams) == -1");
	            return QTSS_RequestFailed;
            }
            ARTS_RequestEvent((Task **)&sess->task_ptr);
            LogRequest(INFO_ARTS_MODULE, l_callid, "if(!sess):1392");
            return QTSS_NoErr;
        }     
    }
      
    
	if(sess->head.state == ARTS_CALL_STATE_DESTROY_NOW)
	{	
	    TearDownSession(sess,inParams,ARTS_CALL_STATE_DESTROY_NOW);		
        LogRequest(INFO_ARTS_MODULE, l_callid, "sess->head.state == ARTS_CALL_STATE_DESTROY_NOW:1412");
		return QTSS_RequestFailed;
	}
 
   

    iovec theDescribeVec[3] = { {0 }};
    ResizeableStringFormatter editedSDP(NULL,0);  
    
    bool isIpqam =  theRTSPRequest->GetIsIpqam();   
    
    if(  live== true )
    {
        
       // if(isIpqam == true)
        {
        editedSDP.Put("v=0\r\n");
    
        
        char tempBuff[256]= "";
        tempBuff[255] = 0;
        qtss_snprintf(tempBuff,sizeof(tempBuff) - 1, "%lu", (UInt32) sARTSPHInterface->callid);

   
        
        editedSDP.Put("o=OnewaveUServerEagle ");
        editedSDP.Put(tempBuff);
        editedSDP.Put(" ");
        QTSS_TimeVal curTs = QTSS_Milliseconds();
        qtss_snprintf(tempBuff, sizeof(tempBuff) - 1, "%"_64BITARG_"d", (SInt64) (curTs/1000) + 2208988800LU);
        editedSDP.Put(tempBuff);

        editedSDP.Put(" IN IP4 ");
        UInt32 buffLen = sizeof(tempBuff) -1;
        (void)QTSS_GetValue(inParams->inClientSession, qtssCliSesHostName, 0, &tempBuff, &buffLen);
        editedSDP.Put(tempBuff, buffLen);
        editedSDP.PutEOL();
       
       
        char* theSDPName = NULL;
        (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqAbsoluteURL, 0, &theSDPName);
        QTSSCharArrayDeleter thePathStrDeleter(theSDPName);
        editedSDP.Put("s=");
        editedSDP.Put(theSDPName); 
        err = QTSS_GetValuePtr(theRequest, qtssRTSPReqQueryString, 0, (void **)&theRequestAttributes, &attributeLen);
        if(err == QTSS_NoErr)
        {   editedSDP.Put("?",1);
            editedSDP.Put(theRequestAttributes,attributeLen);
            editedSDP.Put("\r\n");
        }
        }
        if(isIpqam == true)
        {  
            char ctx_len[128]={'\0'};
            LogRequest(DEBUG_ARTS_MODULE, l_callid,"sess->total_content_length:%d",sess->total_content_length);
            
            editedSDP.Put("a=range:npt=0.000000-");
            
            if(sess->total_content_length >0)
            {
                sprintf(ctx_len,"%.6f",sess->total_content_length*1.0);
                editedSDP.Put(ctx_len);
            }else
            {
                sprintf(ctx_len,"%.6f",36000*1.0);
                editedSDP.Put(ctx_len);
            }                
            
            editedSDP.Put("\r\n");
            editedSDP.Put("a=bitrate:12032000\r\n");
            editedSDP.Put("a=x-frequency:");
            char frequency[128]={'\0'};
            sprintf(frequency,"%d\r\n",sARTSIpqamFrequency);
            editedSDP.Put(frequency);
            editedSDP.Put("a=x-pid:32\r\n");
            editedSDP.Put("m=video 0 MP2T mpgv\r\n");
            editedSDP.Put("m=audio 0 MP2T mpga\r\n");
            editedSDP.Put("a=control:trackID=1\r\n");
        }else if(live == true)
        {   
            char **sdp_line=NULL;
            int sdp_line_num =0;
            if( sess->transport_type == qtssRTPTransportTypeMPEG2)
            {
                sdp_line = sARTSSDPTSLine;
                sdp_line_num = sARTSSDPTSLineNum;
            }else
            {
                sdp_line = sARTSSDPRTPLine;
                sdp_line_num = sARTSSDPRTPLineNum;
            }
            for (UInt32 theIndex = 0; theIndex < sdp_line_num; theIndex++)
            {
                editedSDP.Put(sdp_line[theIndex]);
                editedSDP.Put("\r\n");	           
            }
       
            if(strlen(rtp_sdp) >0)
            {
                editedSDP.Put("a=fmtp:96 ");
                editedSDP.Put(rtp_sdp);
                editedSDP.Put("\r\n");
            }
        }    
       
   }else   
   {
        static char *l_sdpName = "Content-Body";
    
        for ( int l_Index = 0 ; l_Index < sess->numKeyValuePairs; l_Index++)
        {
            if(sess->keyValuePairs[l_Index].name && 
                sess->keyValuePairs[l_Index].value)
            {
                if(strcmp(l_sdpName,sess->keyValuePairs[l_Index].name) == 0) 
                {
                    editedSDP.Put(sess->keyValuePairs[l_Index].value);       
                }
            }
        }
        
      
 }
 
    StrPtrLen editedSDPSPL(editedSDP.GetBufPtr(),editedSDP.GetBytesWritten());
    LogRequest(DEBUG_ARTS_MODULE,l_callid,"SDP:%s",editedSDPSPL.Ptr);
    SDPContainer checkedSDPContainer;
    checkedSDPContainer.SetSDPBuffer( &editedSDPSPL );
    if (!checkedSDPContainer.IsSDPBufferValid())
    {
        LogRequest(INFO_ARTS_MODULE, l_callid, "!checkedSDPContainer.IsSDPBufferValid():1469");
        return QTSS_RequestFailed;
    }

    SDPLineSorter sortedSDP(&checkedSDPContainer);
    UInt32 sessLen = sortedSDP.GetSessionHeaders()->Len;
    UInt32 mediaLen = sortedSDP.GetMediaHeaders()->Len;

    theDescribeVec[1].iov_base = sortedSDP.GetSessionHeaders()->Ptr;
    theDescribeVec[1].iov_len = sortedSDP.GetSessionHeaders()->Len;

    theDescribeVec[2].iov_base = sortedSDP.GetMediaHeaders()->Ptr;
    theDescribeVec[2].iov_len = sortedSDP.GetMediaHeaders()->Len;

	// Append the Last Modified header to be a good caching proxy citizen before sending the Describe
	
	(void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssCacheControlHeader,
                                kCacheControlHeader.Ptr, kCacheControlHeader.Len);
                                
	
    QTSSModuleUtils::SendDescribeResponse(inParams->inRTSPRequest, inParams->inClientSession,
                                            &theDescribeVec[0], 3, sessLen + mediaLen );
    arts_ph_delete_keyvalues(sess->keyValuePairs, sess->numKeyValuePairs);
    sess->keyValuePairs = NULL;
    sess->numKeyValuePairs = 0;
    //if(live == true)
    sess->head.state |= ARTS_CALL_STATE_READ_CON_RESPONSE;
    LogRequest(INFO_ARTS_MODULE, l_callid, "Exiting,sess->head.state:%d",sess->head.state);

    return QTSS_NoErr;
}

QTSS_Error  DoTearDown(QTSS_StandardRTSP_Params* inParams)
{
     LogRequest(INFO_ARTS_MODULE, 0,"Entry");
    
    if (CheckProfile(inParams) < 0)
    {
        return QTSS_NoErr;
    }
      (void)QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
      (void)QTSS_Teardown(inParams->inClientSession);      
    LogRequest(INFO_ARTS_MODULE, 0,"exit"); 
    return QTSS_NoErr;
}


QTSS_Error DoSetup(QTSS_StandardRTSP_Params* inParams)
{
     LogRequest(INFO_ARTS_MODULE, 0,"Entry");
    
    if (CheckProfile(inParams) < 0)
    {
        return QTSS_NoErr;
    }
     
    QTSS_Error theErr = QTSS_NoErr;
    UInt32 callid, theLen = sizeof(callid);
    char *theRequestAttributes = NULL;
    UInt32 attributeLen = 0;
    QTSS_RTSPHeaderObject       theHeader;
    QTSS_RTSPRequestObject theRequest = inParams->inRTSPRequest;

    QTSS_Error err = QTSS_GetValuePtr(theRequest, qtssRTSPReqFullRequest, 0, (void **)&theRequestAttributes, &attributeLen);
   

    if(theRequestAttributes)
    {
        if(strstr(theRequestAttributes,"MP2T") || no_describe == true )
        {
            LogRequest(DEBUG_ARTS_MODULE, 0, "Entering MP2T ");
            return DoDescribeAndSetup(inParams,true);
        }
    }

    theErr = QTSS_GetValue(inParams->inClientSession, sARTSSessionAttr, 0, &callid, &theLen);
    Assert(theErr == QTSS_NoErr);
	arts_session *sess = NULL;
    {
		OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
	    sess = arts_session_find(callid);
    	if(!sess) return QTSS_NoErr;
	}
	


    LogRequest(INFO_ARTS_MODULE, callid, "Entering ");


    //unless there is a digit at the end of this path (representing trackID), don't
    //even bother with the request
    char* theDigitStr = NULL;
    (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqFileDigit, 0, &theDigitStr);
    QTSSCharArrayDeleter theDigitStrDeleter(theDigitStr);
    UInt32 theTrackID;
    if(!(sess->transport_type == qtssRTPTransportTypeMPEG2))
    {
        if (theDigitStr == NULL)
        {
            StrPtrLen theErrResponse("ARTS RTSP Module: No TrackID");
             LogRequest(INFO_ARTS_MODULE, callid, "ErrorCode: qtssClientBadRequest");
            return QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest,
                                                                 qtssClientBadRequest, &theErrResponse);
        }
        theTrackID = ::strtol(theDigitStr, NULL, 10);
    }
    else
    {
        theTrackID = 1;
    }
    QTSS_RTPPayloadType thePayloadType = qtssUnknownPayloadType;

    //Create a new RTP stream
    QTSS_RTPStreamObject newStream = NULL;
    
     LogRequest(DEBUG_ARTS_MODULE, callid, "--begin addstream");
    theErr = QTSS_AddRTPStream(inParams->inClientSession, inParams->inRTSPRequest, &newStream, 0);
    LogRequest(DEBUG_ARTS_MODULE, callid, "--end addstream,err:%d",theErr);
    if (theErr != QTSS_NoErr)
        return theErr;

    theHeader = inParams->inRTSPHeaders; 
    arts_rtsp_additional_headers rtspHeader;

    memset(&rtspHeader, 0, sizeof(rtspHeader));

    rtspHeader.callid = callid;
    
    strcpy(rtspHeader.method,"Setup");

    err = QTSS_GetValuePtr(theHeader, qtssXWapProfileHeader, 0,(void **)&theRequestAttributes,&attributeLen);
        
    if(err == QTSS_NoErr)
      {
          strncpy(rtspHeader.xWapProfile,theRequestAttributes,attributeLen);
          rtspHeader.xWapProfile[attributeLen] = '\0';
      }      
    err = QTSS_GetValuePtr(theHeader, qtssXWapProfileDiffHeader, 0,(void **)&theRequestAttributes,&attributeLen);
    
    if(err == QTSS_NoErr)
      {
          strncpy(rtspHeader.xWapProfileDiff,theRequestAttributes,attributeLen);
          rtspHeader.xWapProfileDiff[attributeLen] = '\0';
      }    
    err = QTSS_GetValuePtr(theHeader, qtss3GPPLinkCharHeader, 0,(void **)&theRequestAttributes,&attributeLen);

    if(err == QTSS_NoErr)
      {
          strncpy(rtspHeader._3gppLinkChar,theRequestAttributes,attributeLen);
          rtspHeader._3gppLinkChar[attributeLen] = '\0';
      }    
    err = QTSS_GetValuePtr(theHeader, qtss3GPPAdaptationHeader, 0,(void **)&theRequestAttributes,&attributeLen);

    if(err == QTSS_NoErr)
      { 
          strncpy(rtspHeader._3gppAdaptationHeader,theRequestAttributes,attributeLen);
          rtspHeader._3gppAdaptationHeader[attributeLen] = '\0';
      }    
	arts_send_additional_rtsp_Headers(sARTSPHInterface->controlsock->fd, &rtspHeader); //send rtsp headers to controller

           
    switch(theTrackID)
    {
    
    case 1:
        thePayloadType = qtssVideoPayloadType;
        sess->video_str = newStream;
        theErr = QTSS_SetValue(newStream, qtssRTPStrSSRC, 0, &sess->rtp_video_SSRC, sizeof(sess->rtp_video_SSRC));
        
        if(sEnableDiffServ)
        theErr = QTSS_Enable_DiffServ(newStream,sARTSDSCP);  
        
        Assert(theErr == QTSS_NoErr);

        break;
    case 2:
        thePayloadType = qtssAudioPayloadType;
        sess->audio_str = newStream;
        theErr = QTSS_SetValue(newStream, qtssRTPStrSSRC, 0, &sess->rtp_audio_SSRC, sizeof(sess->rtp_audio_SSRC));
       
        if(sEnableDiffServ)
        theErr = QTSS_Enable_DiffServ(newStream,sARTSDSCP);
       
        Assert(theErr == QTSS_NoErr);

        break;
    default:
        return QTSS_RequestFailed;
    }

    theErr = QTSS_SetValue(newStream, qtssRTPStrPayloadType, 0, &thePayloadType, sizeof(thePayloadType));
    Assert(theErr == QTSS_NoErr);

    theErr = QTSS_SetValue(newStream, qtssRTPStrTrackID, 0, &theTrackID, sizeof(theTrackID));
    Assert(theErr == QTSS_NoErr);

    //send the setup response
    char *cacheCStr = "no-cache";
    StrPtrLen cachStr(cacheCStr,strlen(cacheCStr));
    (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssCacheControlHeader,
                                cachStr.Ptr, cachStr.Len);
	
    QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, newStream, 0);
    
    LogRequest(INFO_ARTS_MODULE, callid, "Exiting ");

    return  QTSS_NoErr; 
}

co_socket_t * new_sock_t(QTSS_StandardRTSP_Params* inParams,TCPSocket *insock)
{

    char *sessionCStr =  NULL;
    if(inParams == NULL || inParams->inRTSPHeaders == NULL)
    return NULL;
    QTSSDictionary *headerDict = (QTSSDictionary* )inParams->inRTSPHeaders;
    StrPtrLen *theKeySessionID=headerDict->GetValue(qtssOnDemandSessionId);
    
    if(theKeySessionID!= NULL && theKeySessionID->Len >0)
    {
          sessionCStr = theKeySessionID->GetAsCString();
         
    } else
    {
         LogRequest(DEBUG_ARTS_MODULE, 0,"Invalidate header sessionID");
         return NULL;
    }         
    
    co_socket_t *sockstruct = (co_socket_t *)malloc(sizeof(co_socket_t));
    if(sockstruct == NULL)
        return  NULL;
    memset(sockstruct,0,sizeof(co_socket_t));
    memcpy(sockstruct->sessionID,sessionCStr,strlen(sessionCStr));
    sockstruct->sock =insock;
    
    sockstruct->used = false; 
    sockstruct->next = NULL;
    sockstruct->rtspSessObj=(RTSPSession*)  inParams->inRTSPSession; 
    sockstruct->rtpSessObj =(RTPSession*) inParams->inClientSession;  
    delete  sessionCStr;
    return sockstruct; 
}


void insert_sock_list(co_socket_t * sockstruct)
{
    if(sockstruct == NULL)
        return;
    if(sARTSPHInterface->co_socket_list == NULL)
    {
        sARTSPHInterface->co_socket_list = sockstruct;
        sARTSPHInterface->last_co_socket = sockstruct;
        LogRequest(DEBUG_ARTS_MODULE, 0,"sARTSPHInterface->co_socket_list:%x",sARTSPHInterface->co_socket_list);
    }else
    {
       sARTSPHInterface->last_co_socket ->next = sockstruct;
       sARTSPHInterface->last_co_socket = sockstruct; 
    }
}

QTSS_Error recordSess(QTSS_StandardRTSP_Params* inParams)
{
    LogRequest(INFO_ARTS_MODULE, 0,"entry");
    co_socket_t * sock_t = new_sock_t(inParams, NULL);
    insert_sock_list(sock_t);
    return QTSS_NoErr; 
}

QTSS_Error generate_sdp(QTSS_StandardRTSP_Params* inParams,char *sdp,arts_session *sess)
{
    if(sdp == NULL || inParams == NULL || sess == NULL)
        return -1;
    char *sessionCStr = NULL;
    char *theRequestAttributes=0;
    UInt32 attributeLen =0;
    QTSS_Error err;
    RTSPSession * theRTSPSession = (RTSPSession*)inParams->inRTSPSession;
    RTPSessionInterface * theRTPSession = (RTPSessionInterface *)inParams->inClientSession;
    StrPtrLen* sessionIDStr= theRTPSession->GetValue(qtssCliSesRTSPSessionID);
    char *sessIDCStr = sessionIDStr->GetAsCString();
    
    char serverHost[256]={'\0'};
    err = QTSS_GetValuePtr(theRTSPSession, qtssRTSPSesLocalAddrStr, 0, (void **)&theRequestAttributes, &attributeLen);
    if(err == QTSS_NoErr)
    {
        strncpy(serverHost,theRequestAttributes, attributeLen);
    }
    
    sprintf(sdp,"v=0\r\no=- %s %lu IN IP4 1.2.3.4\r\ns=\r\nt=0 0\r\na=control:rtsp://%s:554/%s\r\nc= IN IP4 2.2.2.2\r\nm=video 45 udp MP2T\r\n",sessIDCStr,(UInt32) sess->callid,serverHost,sessIDCStr); 
      
    delete sessIDCStr;
    LogRequest(DEBUG_ARTS_MODULE, sess->callid,"sdp:%s",sdp);
}


QTSS_Error DoDescribeAndSetup(QTSS_StandardRTSP_Params* inParams,bool isMP2TSFlag)
{
    LogRequest(INFO_ARTS_MODULE, 0, "DoDescribeAndSetup: Entering ");
    UInt32 sessLen=0;
    UInt32 mediaLen =0;  
    iovec theDescribeVec[3] = { {0 }};
    ResizeableStringFormatter editedSDP(NULL,0);
    
    custom_struct_t *custom_struct = NULL;
    
    QTSS_Error err = QTSS_NoErr;
    QTSS_RTSPRequestObject    theRequest         = inParams->inRTSPRequest;
    RTSPRequestInterface      *theRTSPRequest    = (RTSPRequestInterface*)inParams->inRTSPRequest;    
    QTSS_ClientSessionObject  theClientSession   = inParams->inClientSession;
    RTSPSessionInterface      *theRTSPSess       = (RTSPSessionInterface*)inParams->inRTSPSession;
    QTSS_RTSPSessionObject      theRTSPSession = inParams->inRTSPSession;
    
    if(no_describe == true)
    {
        theRTSPRequest->SetNoDescribe();
    }
    if(sARTSSupportOtherChannel == true)
    {
        LogRequest(INFO_ARTS_MODULE, 0, "Entry double rtsp connection  mode");
        if(theRTSPSess!=NULL)
            theRTSPSess->SetSupportOtherChannel();
    }

    // Get the full RTSP request from the server's attribute.
        

    if (CheckProfile(inParams) < 0 && sARTSJinShanModule ==false)
    {
        return QTSS_NoErr;
    }
	
	QTSS_RTSPSessionType theSessionType = qtssRTSPSession;
	UInt32 theSessionLen = sizeof(theSessionType);
	
	QTSS_GetValue(inParams->inRTSPSession, qtssRTSPSesType, 0, (void*)&theSessionType, &theSessionLen);
  

    // This is important. Check controller state and proceed only if state ACTIVE.
    // Now it is possible that by the time the connnection request is sent, the 
    // controller will go down. But retries are done only after 10s, so we should be
    // ok, in that the connection request will fail but the send on the socket doesnt
    // need to be protected.custom_st
    if ( (sARTSPHInterface->control_state != ARTS_CONTROLLER_STATE_ACTIVE) && live ==false)
    {
    	if( theSessionType == qtssRTSPSession)
			 QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest, qtssServerUnavailable, 0);
	
		if( theSessionType == qtssRTSPHTTPSession)
			QTSSModuleUtils::SendHTTPErrorResponse(inParams->inRTSPRequest,qtssServerUnavailable,true, NULL);
        return QTSS_RequestFailed;
    }

    UInt32 l_callid = 0, l_len = sizeof(UInt32);
    arts_session *sess = NULL;
    err = QTSS_GetValue(theClientSession, sARTSSessionAttr, 0, (void *)&l_callid, &l_len);


    if(err == QTSS_NoErr)
    {
        OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
        sess = arts_session_find(l_callid);  
        LogRequest(DEBUG_ARTS_MODULE, l_callid, "Found session");
        /*
        if(strlen(sess->channel_id) >0 && live == true)
        {
            goto sendResp;
        } */
    }

    if(!sess)
    {
        arts_ph_callparams callparams;       /* Call Params Struct */
        sess=NewSess(&l_callid); 
        Assert(sess != NULL)
              
        if(isMP2TSFlag)
        {
            strcat(callparams.queryString,"&mux=mp2ts");
            sess->transport_type = qtssRTPTransportTypeMPEG2;
        }

        prepare_params(inParams,&callparams,l_callid,sess);

		//added by lijie, 2010.09.30	  
		err = QTSS_SetValue(theRTSPSession, sARTSRTSPSessionAttr, 0, (void *)&l_callid, sizeof(UInt32));	  
		Assert(err == QTSS_NoErr);
	    //jieli        
        err = QTSS_SetValue(theClientSession, sARTSSessionAttr, 0, (void *)&l_callid, sizeof(UInt32));
        Assert(err == QTSS_NoErr);
		
        sess->remote_con = theClientSession;
        sess->RTSPRequest = theRequest;
             
        sess->RTSP_Session_Type = theSessionType;
	    LogRequest(DEBUG_ARTS_MODULE, l_callid, "session Type = %d", sess->RTSP_Session_Type);
	    
	    
	    	    

         if(live == true)
        {
            memcpy(sess->couchbase_host,couchbase_host,strlen(couchbase_host));
            get_channel_id(inParams,sess);
            if( register_data_sock(sess->couchbase_host,l_callid,sess->channel_id,NULL) != QTSS_NoErr)
           {
                 TearDownSession(sess,inParams,ARTS_CALL_STATE_DESTROY_NOW);
                 LogRequest(INFO_ARTS_MODULE, l_callid,"live mode get multicase add failed ");
                 return QTSS_RequestFailed;
           }
        }
        else
        { 
            if(arts_send_conreq(sARTSPHInterface->controlsock->fd, &callparams) == -1)
            {
			    TearDownSession(sess,inParams,ARTS_CALL_STATE_DESTROY_NOW);
                LogRequest(INFO_ARTS_MODULE, l_callid, "arts_send_conreq(sARTSPHInterface->controlsock->fd, &callparams) == -1");
	            return QTSS_RequestFailed;
            }
            ARTS_RequestEvent((Task **)&sess->task_ptr);
            LogRequest(INFO_ARTS_MODULE, l_callid, "if(!sess):1392");
            return QTSS_NoErr;
        }     
    }
    
    if(isMP2TSFlag)
    {
        sess->transport_type = qtssRTPTransportTypeMPEG2;
    }
       

    if(sess->head.state == ARTS_CALL_STATE_DESTROY_NOW)
    {
        TearDownSession(sess,inParams,ARTS_CALL_STATE_DESTROY_NOW);
        LogRequest(INFO_ARTS_MODULE, l_callid,"sess->head.state == ARTS_CALL_STATE_DESTROY_NOW");
        return QTSS_RequestFailed;
    }
   
   custom_struct  = (custom_struct_t *)sess->darwin_custom_struct;
    if(custom_struct != NULL )
    {
        MpegTSContext*ts = (MpegTSContext*)custom_struct->tsctx;
        
        custom_struct->isIpqam =theRTSPRequest->GetIsIpqam();
        if(ts!= NULL)
            ts->isIpqam = custom_struct->isIpqam;        
	    LogRequest(DEBUG_ARTS_MODULE, sess->callid,"isIpqam:%d",custom_struct->isIpqam);
    }
    
    LogRequest(DEBUG_ARTS_MODULE, l_callid, "generating sdp");
    //changes reqd for thomson/stb where an sdp needs to be sent back as an response to setup
   
    
  
    
    if(sARTSszModule == true)
    {
        theRTSPSess->szModule = true;
        char sdp[4096]={'\0'};
        generate_sdp(inParams,sdp,sess);
        editedSDP.Put(sdp);
        theDescribeVec[1].iov_base = editedSDP.GetBufPtr();
        theDescribeVec[1].iov_len = editedSDP.GetBytesWritten();
        sessLen = editedSDP.GetBytesWritten();
    }
  
    //unless there is a digit at the end of this path (representing trackID), don't
    //even bother with the request
    /*
    char* theDigitStr = NULL;
    (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqFileDigit, 0, &theDigitStr);
    QTSSCharArrayDeleter theDigitStrDeleter(theDigitStr);
    */
 sendResp:   
    UInt32 theTrackID = 1;
    QTSS_RTPPayloadType thePayloadType = qtssUnknownPayloadType;

    //Create a new RTP stream
    QTSS_RTPStreamObject newStream = NULL;
    qtss_printf("--begin add stream");
    err = QTSS_AddRTPStream(inParams->inClientSession, inParams->inRTSPRequest, &newStream, 1);
    qtss_printf("--end add stream,err:%d\n",err);
    if (err != QTSS_NoErr)
        return err;

    thePayloadType = qtssVideoPayloadType;
    sess->video_str = newStream;
    err = QTSS_SetValue(newStream, qtssRTPStrSSRC, 0, &sess->rtp_video_SSRC, sizeof(sess->rtp_video_SSRC));
      
    Assert(err == QTSS_NoErr);

    if(sEnableDiffServ)
        err = QTSS_Enable_DiffServ(newStream, sARTSDSCP);     
    Assert(err == QTSS_NoErr);


    err = QTSS_SetValue(newStream, qtssRTPStrPayloadType, 0, &thePayloadType, sizeof(thePayloadType));
    Assert(err == QTSS_NoErr);

    err = QTSS_SetValue(newStream, qtssRTPStrTrackID, 0, &theTrackID, sizeof(theTrackID));
    Assert(err == QTSS_NoErr);
    char *theFullRequest = NULL;
    err = QTSS_GetValueAsString(theRequest, qtssRTSPReqFullRequest, 0, &theFullRequest);

    if (err != QTSS_NoErr)
    {
        return QTSS_NoErr;
    }
    if(strstr(theFullRequest,"x-playNow")) //If Request contains x-playNow call DoPlay from here only
        DoPlay(inParams, 0);

   
    if(strstr(theFullRequest,"application/sdp") || sARTSszModule == true)
    {
        StrPtrLen contentTypeHeader("application/sdp");
        (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssContentTypeHeader,
                                            contentTypeHeader.Ptr, contentTypeHeader.Len);
    }
   delete  theFullRequest;
              
    QTSSModuleUtils::SendDescribeResponse(inParams->inRTSPRequest, newStream,
                                                    &theDescribeVec[0], 3, sessLen + mediaLen );
    arts_ph_delete_keyvalues(sess->keyValuePairs, sess->numKeyValuePairs);
    sess->keyValuePairs = NULL;
    sess->numKeyValuePairs = 0; 
    
     if(sARTSszModule == true)
    {      
        recordSess(inParams);
    }  
    
    
  
    
    
    
    sess->head.state |= ARTS_CALL_STATE_READ_CON_RESPONSE;
    
    LogRequest(INFO_ARTS_MODULE, l_callid, "Exiting  sess->head.state:%d,[%x]szModule:%d",sess->head.state,theRTSPSess,theRTSPSess->szModule );

    return  QTSS_NoErr; 
}
//init some date when start play
void init_thread(arts_session *sess)
{
    if(sess == NULL)
        return ;
    OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);   
    LogRequest(INFO_ARTS_MODULE, sess->callid,"clean buffer");
	clear_buffer(sess); 
	custom_struct_t * custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
	custom_struct->seek_start_dts =-1; 
	
	//custom_struct->buf_duration = 0; 
	sess->rtp_packet_buffer = NULL;
	sess->rtp_packet_buffer_len = 0 ;
	
	
	if(custom_struct->isIpqam == true && custom_struct->tsctx != NULL)
	{
	    MpegTSContext *ts=( MpegTSContext *)(custom_struct->tsctx);
	    ts->pcr_pkts_count =0;
	    ts->sdt_pkts_count =0;
	    ts->bat_pkts_count =0;
	    ts->sdt_bat_count =0;
	    ts->nit_pkts_count =0;
	    ts->first_pcr =-1;
	} 
    
}

int64_t  append_range_header(arts_session* sess,QTSS_StandardRTSP_Params* inParams)
{
     if(sess == NULL)
        return 0;
        
     custom_struct_t *custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
     Assert(custom_struct != NULL);
     double range_npt_start =0.0;
     static char *l_sdpName = "Range";
     char rangeCstr[256] ={'\0'}; 
     StrPtrLen rangeStr;  
     int l_Index = 0;
     for ( l_Index = 0 ; l_Index < sess->numKeyValuePairs; l_Index++)
     {
           if(sess->keyValuePairs[l_Index].name && 
                        sess->keyValuePairs[l_Index].value)
           {
                if(strcmp(l_sdpName,sess->keyValuePairs[l_Index].name) == 0) 
                {                        
                     char range[128]={'\0'};
                     memcpy(range,sess->keyValuePairs[l_Index].value,strlen(sess->keyValuePairs[l_Index].value));
                     LogRequest(INFO_ARTS_MODULE,sess->callid,"range:%s",range);
                     range_npt_start= atoi(range) /1000.0;   
                     
                     custom_struct->play_responsed=1;                                      
                }
           }
     }
        
     double sdp_len = sess->total_content_length;
     char rangeHeader[128]={'\0'}; 
     int64_t pts =-1;  
         
     /*
     if(live == true && custom_struct->isIpqam == true)
     {
        if(custom_struct->first_pts >0)
        {
            pts = custom_struct->first_pts;
            range_npt_start =pts/90000.0; 
        }else
        return -1;      
       
     }*/
     
     //|| (live == true && custom_struct->first_pts >0)
     if( l_Index >0 && custom_struct->copy_range == false   )
     {
        if(inputfile != NULL)
        return 0;
         if(sdp_len >0 &&live == false && sess->transport_type == qtssRTPTransportTypeMPEG2  )                          
            qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "npt=%.6f-%.6f",range_npt_start,sdp_len) ;
         else
             qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "npt=%.6f-",range_npt_start) ;      
         StrPtrLen rangeHeaderPtr(rangeHeader);
         (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssRangeHeader,
                                    rangeHeaderPtr.Ptr, rangeHeaderPtr.Len); 
                                    
         LogRequest(INFO_ARTS_MODULE,sess->callid,"rangeresponse:%s",rangeHeader);
         custom_struct->copy_range = true  ;
         if(custom_struct->supportPTS ==false || custom_struct->supportOtherChannel == true  )
         {
            return 0;
         }                           
                                   
    }
    else
    {   
    
        if(inputfile != NULL)
        {
            sdp_len = 264.0;
        }
        
    
        if (custom_struct->copy_range == false)
        {
            memset(rangeHeader,0,sizeof(rangeHeader))   ;     
            if(sdp_len >0 && live == false && sess->transport_type == qtssRTPTransportTypeMPEG2)               
                qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "npt=0.000000-%.6f",sdp_len) ;
            else
                qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "npt=0.000000-") ;
                
            StrPtrLen rangeHeaderPtr(rangeHeader);
            (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssRangeHeader,
                                    rangeHeaderPtr.Ptr, rangeHeaderPtr.Len);
            LogRequest(INFO_ARTS_MODULE,sess->callid,"rangeappend:%s",rangeHeader);
            custom_struct->copy_range = true;
        }
        
        if(inputfile != NULL)
        return 0;
        
        if(custom_struct->supportPTS == true )
        { 
          pts =custom_struct->first_pts ;
          if(pts <0)
            return pts;
       }else
         return 0;
                                           
     } 
     
     
     if(custom_struct->supportPTS==true )
     {
              
        pts =custom_struct->first_pts*100/9; //wasu pts in us
        
        LogRequest(INFO_ARTS_MODULE,sess->callid,"pts:%lu,custom_struct->first_pts:%lu",pts,custom_struct->first_pts);
       
        if(pts >=0)
        {
            
            memset(rangeHeader,0,sizeof(rangeHeader)) ; 
            if(custom_struct->supportOtherChannel == false)
            {
                qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "%lu",pts) ;
                StrPtrLen rangeHeaderPtr1(rangeHeader);
            (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssXTSInfoHeader,
                                    rangeHeaderPtr1.Ptr, rangeHeaderPtr1.Len);
            }else
            {
                double pts_in_s = custom_struct->first_pts/90000.0;
                qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "%.6f",pts_in_s);
                StrPtrLen rangeHeaderPtr1(rangeHeader);
                
                (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssXPositionHeader,
                                    rangeHeaderPtr1.Ptr, rangeHeaderPtr1.Len); 
                
            }         
           
         }
     }                  
    
    return pts;
}
void init_play(arts_session * sess,UInt32 callid,QTSS_StandardRTSP_Params* inParams)
{
  
    if(callid <=0)
        return;
        
  
    if(sess == NULL)
        return;
    
    
    if(sess->darwin_custom_struct!=NULL)
    {
        custom_struct_t * custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
        
       
        
        custom_struct->need_send_packet_num =0;              
           
        custom_struct->last_pts =0;     
        custom_struct->play_responsed = 0;   
           
        
        if(custom_struct->receive_pkts_thread == NULL)
        {           
           ARTS_Get_Packet * sARTSGetPacket = NEW ARTS_Get_Packet(); 
           if(inputfile == NULL)                                
                init_thread(sess);    
           sARTSGetPacket->callid = callid;
	       sARTSGetPacket->inParams = inParams;	
	       custom_struct->receive_pkts_thread =  sARTSGetPacket; 
	       
	       if(inputfile == NULL && firstPlayRange == true)
	       {
	            sess->head.state &= ~ARTS_CALL_STATE_READ_CON_RESPONSE;
	            
	       }
	      
	       if(sess->first_packet.pkt_len >0 && inputfile == NULL)
	       {
	            //memcpy(sARTSGetPacket->buffer,sess->first_packet.pkt_buf,sess->first_packet.pkt_len);
	            sARTSGetPacket->buffer =  sess->first_packet.pkt_buf;
                sARTSGetPacket->buffer_len = sess->first_packet.pkt_len;
                sARTSGetPacket->stream_type = sess->first_packet.pkt_str;
                sARTSGetPacket->sess = sess;
                 if( sARTSPSICBR == true)
                {
                    custom_struct->psicbr = true;
                } 
                sARTSGetPacket->Run();  
               
                sess->first_packet.pkt_len =0;    
	       } 	           
         }
           
    }
    
    sess->last_rtsp_bitrate_update_time = 0;
}

QTSS_Error send_play_request(UInt32 callid)
{
    if(callid <0)
     return QTSS_NoErr;
     
   arts_session  * sess = arts_session_find(callid);
   custom_struct_t * custom_struct = (custom_struct_t*)sess->darwin_custom_struct;
   LogRequest(DEBUG_ARTS_MODULE, sess->callid,"sess->head.state:%x",sess->head.state);
  if(sess == NULL || custom_struct == NULL)
    return QTSS_NoErr; 
  if ( !(sess->head.state & ARTS_CALL_STATE_PLAY_CMD_SENT) && strlen(custom_struct->rangeHeader_global) >0 ) 
  {
      arts_rtsp_additional_headers rtspHeader;
      memset(&rtspHeader, 0, sizeof(rtspHeader));
	
      rtspHeader.callid = callid;
      strcpy(rtspHeader.method,"Play");
      
      int numKeyValues = 0;
	  int numMoreHeaders = 1; //only Range header	now
	
	  rtspHeader.keyValuePairs = arts_ph_create_keyvalues(numMoreHeaders);
	  arts_ph_calloc_keyvalue(rtspHeader.keyValuePairs, numKeyValues, "Range", custom_struct->rangeHeader_global );
      numKeyValues++;
     
      rtspHeader.numKeyValuePairs = numKeyValues;
	  
	
	  arts_send_additional_rtsp_Headers(sARTSPHInterface->controlsock->fd, &rtspHeader); //send rtsp headers to controller
	
	  arts_ph_delete_keyvalues(rtspHeader.keyValuePairs, numMoreHeaders);
	  LogRequest(INFO_ARTS_MODULE, callid,"posted play request,range:%s\n",custom_struct->rangeHeader_global);
	  sess->head.state |= ARTS_CALL_STATE_PLAY_CMD_SENT ;
	  
	  memset(custom_struct->rangeHeader_global,0,sizeof(custom_struct->rangeHeader_global))	;
	  
	  custom_struct->seek = true;
	  custom_struct->seek_start_dts = -1;  
     
   }
   return 0;
}


static void getCseq(arts_session * sess,QTSS_StandardRTSP_Params* inParams)
{
     if(sess == NULL || inParams == NULL)
        return ;
    char *theRequestAttributes=NULL;
    UInt32 attributeLen = 0,callid=sess->callid,err=0;
    QTSS_RTSPHeaderObject theHeader = inParams->inRTSPHeaders;
    custom_struct_t * custom_stru =(custom_struct_t *) sess->darwin_custom_struct;
	Assert(custom_stru != NULL);
	
	err = QTSS_GetValuePtr(theHeader, qtssCSeqHeader, 0, (void **)&theRequestAttributes, &attributeLen);
	if(err == QTSS_NoErr && attributeLen >0)
	{
	    custom_stru->cseq = atoi(theRequestAttributes); 
	    LogRequest(DEBUG_ARTS_MODULE, callid,"cur cseq:%d",custom_stru->cseq);	                
	}	
}

static void SetOtherChannel(arts_session * sess,QTSS_StandardRTSP_Params* inParams)
{
    if(sess == NULL || inParams == NULL)
        return ;
    char *theRequestAttributes=NULL;    
    UInt32 attributeLen = 0,callid=sess->callid,err=0;
   
	custom_struct_t * custom_stru =(custom_struct_t *) sess->darwin_custom_struct;
	
	if(custom_stru == NULL)
	{
	    abort();
	}
	
	LogRequest(DEBUG_ARTS_MODULE, callid,"custom_stru->sendEv:%x",custom_stru->sendEv);
	RTSPRequestInterface * theRTSPRequest = (RTSPRequestInterface*)inParams->inRTSPRequest;
	
	
	custom_stru->isIpqam =theRTSPRequest->GetIsIpqam();   
	
	//custom_stru->isIpqam =1;
	           
	LogRequest(DEBUG_ARTS_MODULE, callid,"isIpqam:%d",custom_stru->isIpqam); 
	
	
	RTSPSession *theRTSPSession = (RTSPSession *)inParams->inRTSPSession;
	if(sARTSszModule == true)
	{
	    theRTSPSession->szModule =true;
	}
	custom_stru->OwnRTSPSessionObj = theRTSPSession;	
	LogRequest(DEBUG_ARTS_MODULE, callid,"LastRTSPSessionObj:%x",custom_stru->OwnRTSPSessionObj);
	getCseq(sess,inParams);
	
	
	RTPSessionInterface * theRTPSession = (RTPSessionInterface *)inParams->inClientSession;	    
	    
    StrPtrLen* sessionIDStr= theRTPSession->GetValue(qtssCliSesRTSPSessionID);	
    char *sessIDCStr = sessionIDStr->GetAsCString();
    memcpy(custom_stru->sessionID,sessIDCStr,strlen(sessIDCStr)); 
    LogRequest(DEBUG_ARTS_MODULE, callid,"sessionID:%s",custom_stru->sessionID);   
   
    QTSS_RTSPRequestObject theRequest = inParams->inRTSPRequest;//zlj add modfiy
   //if(strlen(custom_stru->uri) ==0 )
   {
    QTSS_RTSPRequestObject theRequest = inParams->inRTSPRequest;//zlj 这个是原有的
	err = QTSS_GetValuePtr(theRequest, qtssRTSPReqAbsoluteURL, 0, (void **)&theRequestAttributes, &attributeLen);
    if(err == QTSS_NoErr)
        strncpy(custom_stru->uri, theRequestAttributes, attributeLen);
   }
	
    if(sess->transport_type == qtssRTPTransportTypeMPEG2 && sARTSPHInterface->co_socket_list!=NULL && custom_stru->RTPSessionObj == NULL)
	{	    
	    
	    co_socket_t *p = sARTSPHInterface->co_socket_list;    
	    
	     while(p&&p->sessionID[0]!='\0')
	     {	          
	           if(strncmp(sessIDCStr,p->sessionID,strlen(p->sessionID)) == 0 && p->used == false)
	           {	                 
	           
	                LogRequest(DEBUG_ARTS_MODULE, callid,"find rtspsessionObj"); 
	                if(p->sock != NULL)      
	                {
	                    theRTSPSession->GetOutputStream()->SetDataSocket(p->sock);
	                    custom_stru->supportOtherChannel= true;
	                }
	                
	                
                    bool flag = theRTSPSession->GetSupportOtherChannel();
                    LogRequest(DEBUG_ARTS_MODULE, callid,"set supportOtherChannel:%d",flag);
	                
	                
	                custom_stru->RTSPSessionObj= p->rtspSessObj;
	                LogRequest(DEBUG_ARTS_MODULE, callid,"FirstRTSP:%x",custom_stru->RTSPSessionObj);
	                custom_stru->RTPSessionObj = p->rtpSessObj;
	                     
                     err = QTSS_GetValuePtr(theRequest, qtssRTSPReqQueryString, 0, (void **)&theRequestAttributes, &attributeLen);
                     if(err == QTSS_NoErr)
                     {
                       strncpy(custom_stru->uri+strlen(custom_stru->uri),"?",1), 
                        strncpy(custom_stru->uri+strlen(custom_stru->uri), theRequestAttributes, attributeLen);
                     }
	                                          
	                strcpy(custom_stru->sessionID,sessIDCStr);	                            
	                
	                p->used = true;
	                
	                break;
	            }	            
	            p=(co_socket_t *)p->next;
	     }	    
	}
	
	 delete sessIDCStr;
	
	if(custom_stru->RTPSessionObj != NULL)
	{
	    SInt64 timeoutInMs = 5*3600*1000;  
	    RTPSessionInterface * theRTPsession = (RTPSessionInterface *)custom_stru->RTPSessionObj;  
        theRTPsession->SetTimeOuts(timeoutInMs);
	}
	
}

int sock_init()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0) {
	    perror("socket(): error ");	 
	    abort();  
    } 
    
    return fd;
}

static int replay(arts_session * sess,QTSS_StandardRTSP_Params* inParams)
{
  
    if(sess == NULL || sess->darwin_custom_struct == NULL )
        return 0;
    custom_struct_t *custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
    if(custom_struct->supportPTS == false)
        return 0;
    double sdp_len = sess->total_content_length;
    int64_t pts = getLastPts(sess->rtp_packet_buffer,custom_struct) - custom_struct->first_keyframe_pts;// wasu diff  is needed  for some STB
    //int64_t pts = getLastPts(sess->rtp_packet_buffer,custom_stru);// wasu no diff this is right
    if(pts>=0)
    {   
        double ptsInS = pts /90000.0;
       
        if(ptsInS <0)
          ptsInS =0;
        
        char rangeHeader[128]={'\0'};          
        if(sdp_len >0 )        
            qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "npt=%.6f-%.6f",ptsInS,sdp_len);
        else
            qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "npt=%.6f-",ptsInS);
        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"range-pts:%s,cur_pts:%lu,first_pts:%d,sdp_len:%f,pts:%.6f",rangeHeader,pts,custom_struct->first_pts,sdp_len,pts);
        StrPtrLen rangeHeaderPtr(rangeHeader);
        (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssRangeHeader,
                                                            rangeHeaderPtr.Ptr, rangeHeaderPtr.Len); 
                                            
        memset(rangeHeader,0,sizeof(rangeHeader));
        qtss_snprintf(rangeHeader,sizeof(rangeHeader) -1, "%lu",pts*100/9);  
        StrPtrLen rangeHeaderPtr1(rangeHeader);                                 
        (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssXTSInfoHeader,
                                    rangeHeaderPtr1.Ptr, rangeHeaderPtr1.Len); 
                     
     }
     return 0;
}

#if 1
/* new DoPlay logic */
QTSS_Error DoPlay(QTSS_StandardRTSP_Params* inParams, bool sendResponseFlag)
{
    QTSS_Error err = QTSS_NoErr;
    char *theRequestAttributes=NULL;
    UInt32 callid, attributeLen = 0,theLen = sizeof(callid);
    QTSS_RTSPHeaderObject theHeader;
    err = QTSS_GetValue(inParams->inClientSession, sARTSSessionAttr, 0, &callid, &theLen);
    Assert(err == QTSS_NoErr);
	arts_session *sess = NULL;
	{
    	OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
	    sess = arts_session_find(callid);
    	if(!sess) return QTSS_NoErr;
	}
    LogRequest(INFO_ARTS_MODULE, callid, "Entering Play");
	qtss_printf("Entering Play, state:0x%x,  %"_64BITARG_"d\n", sess->head.state,QTSS_Milliseconds() );
	
	if (CheckProfile(inParams) < 0  && !sARTSszModule )
    {
        return QTSS_NoErr;
    }
	
	
	
	int seek_flag =0;


	if( sess->head.state == ARTS_CALL_STATE_DESTROY_NOW )
	{
	    TearDownSession(sess,inParams,ARTS_CALL_STATE_PLAY);
		return QTSS_RequestFailed;
	}
          
     
    RTPSessionInterface * theRTPSession = (RTPSessionInterface *)inParams->inClientSession;		    
    StrPtrLen* sessionIDStr= theRTPSession->GetValue(qtssCliSesRTSPSessionID);	
    char *sessIDCStr = sessionIDStr->GetAsCString();   
    LogRequest(INFO_ARTS_MODULE, callid,"sessionID:%s",sessIDCStr); 
    delete  sessIDCStr;
     
   {
        OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
        if(! (sess->head.state & ARTS_CALL_STATE_CONNECTED))
        {
            LogRequest(INFO_ARTS_MODULE, callid, "ARTS_CALL_STATE_NOT_CONNECTED");

            ARTS_RequestEvent((Task **)&sess->task_ptr);  
              
            LogRequest(INFO_ARTS_MODULE, callid, "Wait for Request Event,sess->head.state:%d",sess->head.state);// raw-data didn't arrived,The session hold this till data arrive           
            return QTSS_NoErr;
        }
    }
    
    
    SetOtherChannel(sess,inParams);
    init_play(sess,callid,inParams); 
    
     
   
    
	LogRequest(INFO_ARTS_MODULE, callid,"sess->head.state:%d",sess->head.state);
	
    if (! ( sess->head.state & ARTS_CALL_STATE_READ_CON_RESPONSE )  && live == false) 
	{
		if ( !(sess->head.state & ARTS_CALL_STATE_PLAY_CMD_SENT) ) 
	    {
	        theHeader = inParams->inRTSPHeaders; 
	        arts_rtsp_additional_headers rtspHeader;
            memset(&rtspHeader, 0, sizeof(rtspHeader));
	
            rtspHeader.callid = callid;
            strcpy(rtspHeader.method,"Play");
    
            err = QTSS_GetValuePtr(theHeader, qtss3GPPLinkCharHeader, 0,(void **)&theRequestAttributes,&attributeLen);
	
            if(err == QTSS_NoErr)
            {
                strncpy(rtspHeader._3gppLinkChar,theRequestAttributes,attributeLen);
                rtspHeader._3gppLinkChar[attributeLen]='\0';
            }    
	
            err = QTSS_GetValuePtr(theHeader, qtss3GPPAdaptationHeader, 0,(void **)&theRequestAttributes,&attributeLen);
	
            if(err == QTSS_NoErr)
            {
                strncpy(rtspHeader._3gppAdaptationHeader,theRequestAttributes,attributeLen);
                rtspHeader._3gppAdaptationHeader[attributeLen] = '\0';
            }    
	
	        //LiJie modified on 2012.09.19
	        //add play range support, or rtsp seek support for ARTS
	        int numKeyValues = 0;
	        int numMoreHeaders = 1; //only Range header	now
	
	        rtspHeader.keyValuePairs = arts_ph_create_keyvalues(numMoreHeaders);
	
	        err = QTSS_GetValuePtr(theHeader, qtssRangeHeader, 0,(void **)&theRequestAttributes,&attributeLen);
	        custom_struct_t * custom_stru =(custom_struct_t *) sess->darwin_custom_struct;
	                
	
	        if(err == QTSS_NoErr  || attributeLen == 0)
            {
		        char sRangeHeader[256];
                strncpy(sRangeHeader,theRequestAttributes,attributeLen);                
                sRangeHeader[attributeLen] = '\0'; 
                Float64 fStartTime = 0.0;  
                StrPtrLen rangeHeader(sRangeHeader,strlen(sRangeHeader));     
	            if(attributeLen >0)
	            {
		            arts_ph_calloc_keyvalue(rtspHeader.keyValuePairs, numKeyValues, "Range", sRangeHeader);	            
                    StringParser  theRangeParser(&rangeHeader);               
                    theRangeParser.GetThru(NULL, '=');//consume "npt="
                    theRangeParser.ConsumeWhitespace();
                    fStartTime=(Float64)theRangeParser.ConsumeNPT(); 
                    LogRequest(INFO_ARTS_MODULE, callid,"rangeHeader:%s,receive_pkt_thread:%X:",rangeHeader.Ptr,custom_stru->receive_pkts_thread);                    
                }
                
                seek_flag = 1;                 
                
                 
                if(strcmp(rangeHeader.Ptr,"npt=-") == 0 || attributeLen==0 || strcmp(rangeHeader.Ptr,"npt=now-") ==0|| (custom_stru != NULL && custom_stru->receive_pkts_thread == NULL)  )
                {   
                    custom_stru->copy_range = false;     
                    get_pts(sess);       
                    replay(sess,inParams);
                    StrPtrLen scale_str("1.0");
                    (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest,qtssScaleHeader,scale_str.Ptr,scale_str.Len);
                    
                    if (sess->transport_type == qtssRTPTransportTypeMPEG2){
                            ((RTSPRequestInterface*)(inParams->inRTSPRequest))->SetTransportType(qtssRTPTransportTypeMPEG2);
                        err = QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
                    }else{
                        err = QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, qtssPlayRespWriteTrackInfo);
                    } 
                    
                    //Ipqam has special send Packet method instead of sendPacket callback of rtpsession       
                    if(custom_stru->isIpqam ==false)
                        err = QTSS_Play(inParams->inClientSession, inParams->inRTSPRequest, qtssPlayFlagsSendRTCP);
                        
                    Assert(err == QTSS_NoErr);  
                    sess->mpeg2_start_time = 0;
                    if(custom_stru->isIpqam ==false)
                        err = QTSS_Play(inParams->inClientSession, inParams->inRTSPRequest, qtssPlayFlagsSendRTCP);
                    Assert(err == QTSS_NoErr); 
                    //else
                        // sARTSSendPktThread->beActived();
                    timeout_ev_t *curSendEv = (timeout_ev_t *)custom_stru->sendEv;
                    if(curSendEv!=NULL && curSendEv->pause==true)
                    {    
                        custom_stru->pause_ready = true;         
                        curSendEv->pause=false;
                        LogRequest(INFO_ARTS_MODULE, callid,"pause-play");
                    }           
                                       
                    return QTSS_NoErr; 
                }     
                 
                 
                if( ((custom_stru->seek ==true && custom_stru->seek_start_dts <0) ||strlen(custom_stru->rangeHeader_global)>0 )
                    && strcmp(rangeHeader.Ptr,"npt=-")!=0   )
                {                  
                    strncpy(custom_stru->rangeHeader_global,sRangeHeader,sizeof(custom_stru->rangeHeader_global));
                    LogRequest(INFO_ARTS_MODULE, callid,"queue this range play");
                    RTSPSessionInterface * theRTSPSess =(RTSPSessionInterface *) inParams->inRTSPSession;
                    theRTSPSess->GetOutputStream()->SetBytesWritten();
                    return QTSS_NoErr;    
                }            
              
                if(fStartTime >= 0.0)
                {    
                    init_thread(sess); 
                    sess->first_packet.pkt_len =0;                   
                                   
                    if(custom_stru != NULL)
                    {                       
                        custom_stru->seek = true;                                              
                        sess->first_packet.pkt_len =0;
                        custom_stru ->last_recv_PCR = 0;                                      
                       
                        custom_stru->buf_duration =0; 
                        LogRequest(DEBUG_ARTS_MODULE, callid,"custom_stru ->last_recv_PCR = 0;");                            
                        custom_stru->pause_start_time =0; 
                        custom_stru->first_pts = -1; 
                        custom_stru->copy_range = false;  
                       
                        if(firstPlayRange == true)
                        {  
                            sess->head.state |= ARTS_CALL_STATE_PLAY;
                        }
                        
                        send_sei =0;
                    }
                }else
                    custom_stru->seek_start  = false;    
                    
                          
		        
		        numKeyValues++;
            }  		
	
	        rtspHeader.numKeyValuePairs = numKeyValues;
	  
	
	        arts_send_additional_rtsp_Headers(sARTSPHInterface->controlsock->fd, &rtspHeader); //send rtsp headers to controller
	
	        arts_ph_delete_keyvalues(rtspHeader.keyValuePairs, numMoreHeaders);
	        LogRequest(INFO_ARTS_MODULE, callid,"posted play request");
	        sess->head.state |= ARTS_CALL_STATE_PLAY_CMD_SENT ;
	        
		}
	   // If we are doing RTP-Meta-Info stuff, we might be asked to get called again here.
	   // This is simply because seeking might be a long operation and we don't want to
	   // monopolize the CPU, but there is no other reason to wait, so just set a timeout of 0

	   err = QTSS_SetIdleTimer(200);	   
	   LogRequest(INFO_ARTS_MODULE, callid,"QTSS_SetIdleTimer(200)");
	   Assert(err == QTSS_NoErr);	   
	   return err;	   
	}
	
	
	if( (sess->rtp_media_type & ARTS_VIDEO_SESSION) || live == true)	
	{
	
	    err = QTSS_SetValue(sess->video_str, qtssRTPStrFirstSeqNumber, 0, &sess->rtp_video_seqnum, sizeof(sess->rtp_video_seqnum));
    	Assert(err == QTSS_NoErr);

    	err = QTSS_SetValue(sess->video_str, qtssRTPStrFirstTimestamp, 0, &sess->rtp_video_timestamp, sizeof(sess->rtp_video_timestamp));
    	Assert(err == QTSS_NoErr);

	    LogRequest(DEBUG_ARTS_MODULE, callid, "video sequence no = %u time stamp = %u " ,sess->rtp_video_seqnum, sess->rtp_video_timestamp);
		
	}

	if(sess->rtp_media_type & ARTS_AUDIO_SESSION)
	{
    	err = QTSS_SetValue(sess->audio_str, qtssRTPStrFirstSeqNumber, 0, &sess->rtp_audio_seqnum, sizeof(sess->rtp_audio_seqnum));
    	Assert(err == QTSS_NoErr);

	    err = QTSS_SetValue(sess->audio_str, qtssRTPStrFirstTimestamp, 0, &sess->rtp_audio_timestamp, sizeof(sess->rtp_audio_timestamp));
    	Assert(err == QTSS_NoErr);

		LogRequest(INFO_ARTS_MODULE, callid, "audio sequence no = %u time stamp = %u " ,sess->rtp_audio_seqnum, sess->rtp_audio_timestamp);   
	}
		
 
    sess->head.state |= ARTS_CALL_STATE_PLAY;    
    LogRequest(DEBUG_ARTS_MODULE, callid,"After setting up, seqnum_v: %d,seqnum_a: %d, rtp_vt: %u, rtp_at: %u \n",sess->rtp_video_seqnum,    sess->rtp_audio_seqnum,
				sess->rtp_video_timestamp, sess->rtp_audio_timestamp );
    custom_struct_t * custom_struct = (custom_struct_t *)sess->darwin_custom_struct; 
    
    
    
     
 
    if(  (sess->head.state & ARTS_CALL_STATE_PLAY) &&  (custom_struct->adapter_unregister == true) && ( (sess->rtp_packet_buffer_len == -1  && live==true)|| (custom_struct->buf_duration ==-1 && live ==false) )&& custom_struct->play_idel == false )
	{	      
        if(live == false)
            custom_struct->buf_duration =0;
        else
           sess->rtp_packet_buffer_len =0;     	
    }
    
 
    if(sendResponseFlag)
    {               
        if(append_range_header(sess,inParams)<0 )
        {
            custom_struct->waite_time += 100;
            if(custom_struct->waite_time >=20000)
            {
                LogRequest(INFO_ARTS_MODULE, callid,"waite 20s no data received");
                (void)QTSS_Teardown(inParams->inClientSession);
                return QTSS_NoErr;
            }
            QTSS_SetIdleTimer(100);	   
	        LogRequest(INFO_ARTS_MODULE, callid,"QTSS_SetIdleTimer(100)");
	        custom_struct->play_idel = true;
	        Assert(err == QTSS_NoErr);	   
	        return err;	  
        }
    
        custom_struct->waite_time  =0;
        custom_struct->play_idel = false;

        StrPtrLen scale_str("1.0");
        (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest,qtssScaleHeader,scale_str.Ptr,scale_str.Len);
    
        //err = QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, qtssPlayRespWriteTrackInfo);
        if (sess->transport_type == qtssRTPTransportTypeMPEG2){
            qtss_printf("-________MPEG2\n");
            ((RTSPRequestInterface*)(inParams->inRTSPRequest))->SetTransportType(qtssRTPTransportTypeMPEG2);
            err = QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
        }else{
            err = QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, qtssPlayRespWriteTrackInfo);
        }
		sess->head.state &= ~ARTS_CALL_STATE_READ_CON_RESPONSE;
		
        Assert(err == QTSS_NoErr);   
        
    }
    

   if(custom_struct->isIpqam ==false && live ==false && ARTSsendBitrate == false)
  // if(custom_struct->isIpqam ==false  && ARTSsendBitrate == false) //delete "live==false" noused in wasu-con-test
   {// common mode
        err = QTSS_Play(inParams->inClientSession, inParams->inRTSPRequest, qtssPlayFlagsSendRTCP);
        Assert(err == QTSS_NoErr);   
  
   } 
   else
   {
   
       if(custom_struct->sendEv == NULL && sARTSSendPktThread!= NULL )
       {
        OSMutexLocker registListMutex(&sARTSSendPktThread->regist_list_Mutex);
        sARTSSendPktThread->insert_regist_node(sess->callid);
        LogRequest(DEBUG_ARTS_MODULE, callid, "insert regist_node:%x", sARTSSendPktThread->regist_list);
      
           
        timeout_ev_t *curSendEv = (timeout_ev_t *)malloc(sizeof(timeout_ev_t));
        Assert(curSendEv != NULL);
        memset(curSendEv,0,sizeof(timeout_ev_t));
        if( custom_struct->isIpqam==1 )
        {
            curSendEv->sockfd = sock_init();
            curSendEv->sockfd1 = sock_init();
        }
        curSendEv->callid =callid;
       
        custom_struct->sendEv = curSendEv;
        
        RTPSessionInterface * theRTPSession = (RTPSessionInterface *)inParams->inClientSession;
                             
        LogRequest(DEBUG_ARTS_MODULE, callid,"active ipqam send packets,live:%d,custom_struct->isIpqam:%d,ARTSsendBitrate:%d\n",live,custom_struct->isIpqam,ARTSsendBitrate); 
        if(live == true )
        {
            custom_struct->live = true;          
        }
       
        if(ARTSsendBitrate == true)
        {
            custom_struct->sendBitrate = true;           
        }
        
        if( custom_struct->isIpqam == false)
        {
            err = QTSS_Play(inParams->inClientSession, inParams->inRTSPRequest, qtssPlayFlagsSendRTCP);
            Assert(err == QTSS_NoErr);   
        } 
        
      }      
      else
      {   
           if(custom_struct->isIpqam == 1)
           {
                timeout_ev_t *curSendEv = (timeout_ev_t *)custom_struct->sendEv;
                if(curSendEv !=NULL && curSendEv->pause==true)
                {              
                    curSendEv->pause=false;
                }
           }           
      }
          
     
                  
   }
    sess->first_packet_send_time = QTSS_Milliseconds();
    
    sess->mpeg2_start_time = 0;
    if(custom_struct!= NULL)
        custom_struct ->pkt_timestamp = 0;
	
	sess->head.state &= ~ARTS_CALL_STATE_PLAY_CMD_SENT;  // ready to receive new play requests 
	sess->rtp_mpeg2_bytes_sent =0;
	LogRequest(INFO_ARTS_MODULE, callid, "first_packet_send_time = %"_64BITARG_"d,mpeg2_start_time:%d" ,sess->first_packet_send_time,sess->mpeg2_start_time);
    LogRequest(INFO_ARTS_MODULE, callid, "Exiting " );

    return err;
}

#endif 



Bool8 IsDiscontinuityFlag(UInt8 *packetData, UInt32 remainPacketLength)
{
    while(remainPacketLength > 188)
    {
        if(!(*(packetData+3)& ADAPTATION_FLAG))
        {
            packetData += 188;
            remainPacketLength -= 188;
            continue; 
        }
        if(*(packetData+5)& DISCONTINUITY_FLAG)
        {
            return true;
        }
        else
        {
            packetData += 188;
            remainPacketLength -= 188;
            continue; 
        }
    }
    return false;
}


SInt64 GetMPEG2PCR(UInt8 *packetData, UInt32 *remainPacketLength)
{
    
        SInt64 PCR = -1;
        SInt64 PCR_base = 0;
        SInt64 PCR_ext = 0;

        if(*remainPacketLength < MIN_PACK_LEN_FOR_PCR || !(*(packetData+3)& ADAPTATION_FLAG))
            return PCR;
        int adp_len =packetData[4];
        if(adp_len <=0 )
        return PCR;
        
        
        if(*(packetData+5)& PCR_FLAG)
        {
            int i;
            for(i = 6; i < 10; i++)
            {
                PCR_base = PCR_base * 256 + *(packetData+i);
		//printf("PCR Base is %lld\n",PCR_base);
            }
            PCR_base *= 2;
            PCR_base = packetData[i] & 0x80 ? PCR_base + 1:PCR_base;
            PCR_ext = packetData[i++] & 0x7F;
            PCR_ext *= 2;
            PCR_ext = packetData[i] & 0x80 ? PCR_ext + 1:PCR_ext;
            PCR_ext *= 2;
            PCR_ext = packetData[i] & 0x40 ? PCR_ext + 1:PCR_ext;
            PCR = PCR_base * 300 + PCR_ext;
        }
        return PCR;
}



static  int64_t GetFrameDuration(arts_session *sess)
{
   rtp_packet_buffer_type  *p = (rtp_packet_buffer_type *)sess->rtp_packet_buffer;
   rtp_packet_buffer_type *nextone =NULL;
   if(sess == NULL)
    return 0;
    
   
   if ( p->next!=NULL)
   {
      nextone = (rtp_packet_buffer_type *)p->next;
      //LogRequest(DEBUG_ARTS_MODULE, 0,"the next time_stamp:%u,cur_timestamp:%u",nextone->timestamp,p->timestamp);
      return nextone->timestamp - p->timestamp;
   }
    else 
    {
        custom_struct_t *cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
        if(cur_struct != NULL)
            return cur_struct->duration;
        else
          return 0;
    }
   return 0;
}


QTSS_Error SendMpeg2Packets(QTSS_RTPSendPackets_Params* inParams,arts_session *sess)
{   
    QTSS_Error theErr = QTSS_NoErr;
    UInt32 callid, theLen = sizeof(callid);	
	UInt32 theSendInterval = QTSServerInterface::GetServer()->GetPrefs()->GetSendIntervalInMsec();	
	
#if ! DO_BLOCK_READ
    SInt32 in = 0;  
#endif
   // theErr = QTSS_GetValue(inParams->inClientSession, sARTSSessionAttr, 0, &callid, &theLen);
    //Assert(theErr == QTSS_NoErr); 
    
    
   // arts_session *sess = NULL;
   // {
        //OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
      //  sess = arts_session_find(callid);
   // }
     //LogRequest(DEBUG_ARTS_MODULE, callid,"sess:%x",sess);   
     
    if(!sess) return QTSS_NoErr;    
    
    while(true)
    {
        QTSS_TimeVal curtime =0;    
        custom_struct_t * custom_struct = (custom_struct_t*)sess->darwin_custom_struct;   
        
send:             
         
        curtime = QTSS_Milliseconds();
        
         if(sess->mpeg2_start_time <=0){
            sess->mpeg2_start_time = curtime; 
            LogRequest(DEBUG_ARTS_MODULE, callid,"sess->mpeg2_start_time:%"_64BITARG_"d",sess->mpeg2_start_time);                            
         }
         
         
         if(sess->last_rtsp_bitrate_update_time <=0)
         {
            sess->last_rtsp_bitrate_update_time = curtime;
         }
         
       LogRequest(DEBUG_ARTS_MODULE, callid,"sess->bufer_len:%d,sess->mpeg2_start_time:%"_64BITARG_"d,pkt_len:%d,buf_duration:%d,bitrate_update_time:%"_64BITARG_"d",sess->rtp_packet_buffer_len,sess->mpeg2_start_time,sess->first_packet.pkt_len,custom_struct->buf_duration,sess->last_rtsp_bitrate_update_time);
       
        if(sess->first_packet.pkt_len >0  && sess->first_packet.pkt_len % TS_PACKET_SIZE ==0 )
        {
            QTSS_PacketStruct thePacket;           
                
           
            thePacket.packetData = sess->first_packet.pkt_buf;  
            if(custom_struct == NULL)
	        {
	            LogRequest(DEBUG_ARTS_MODULE, callid,"custom_struct is null");
	            return QTSS_NoErr;
	        }    
           
            if(live == false)
            {
                int64_t pkt_transformat = custom_struct->pkt_timestamp;	                        
                custom_struct->send_idel_time = 0;    
         
                int64_t startDts = GetseekStartDts(sess);
             
                LogRequest(DEBUG_ARTS_MODULE, callid,"startDts:%d,pkt_transformat:%d",startDts,pkt_transformat);
                int64_t sendTime = 0;
          
                if(startDts >0 && pkt_transformat ==0)
                {
                    sendTime = sess->mpeg2_start_time;
                }else
                    sendTime =sess->mpeg2_start_time + pkt_transformat-startDts;   
            
                if(sendTime <=0)
                    thePacket.packetTransmitTime = curtime;
                else
                    thePacket.packetTransmitTime = sendTime;
                
                //if(inputfile != NULL)
                 //   thePacket.packetTransmitTime = curtime;
             
       
                int ahead_time = thePacket.packetTransmitTime -curtime;
                if(ahead_time >0 && ahead_time > sARTSIpAheadTime )
                {
              
                    inParams->outNextPacketTime = theSendInterval;
                    LogRequest(DEBUG_ARTS_MODULE, callid,"cur pkts transmittime:%"_64BITARG_"d,outNextPacketTime:%"_64BITARG_"d,ahead_time:%d,need sleep 50 ms",thePacket.packetTransmitTime,inParams->outNextPacketTime,ahead_time); 
                    return   QTSS_NoErr;
                             
                }     
            }else                 
                thePacket.packetTransmitTime = curtime;     
            
              theErr = QTSS_Write(sess->video_str, &thePacket, sess->first_packet.pkt_len, NULL, qtssWriteFlagsIsMpeg2);  
             
            LogRequest(DEBUG_ARTS_MODULE, callid,"send,transmittime:%"_64BITARG_"d,theErr:%d,curTime:%"_64BITARG_"d,pkt_len:%d",
                thePacket.packetTransmitTime, theErr,curtime,sess->first_packet.pkt_len);
            
           
            if(theErr==QTSS_NoErr)
            {   
                if(custom_struct->supportOtherChannel == true)    
                {     
                    LogRequest(DEBUG_ARTS_MODULE, callid,"set channel Num");   
                    RTPStream *theRTPStream = (RTPStream *)sess->video_str;                    
                    UInt8 channel_num = theRTPStream->GetRTPChannelNum();
                    if(channel_num >=0)
                    {
                        channel_num += 1;
                        theRTPStream->SetRTPChannelNum(channel_num);
                    }
                }
               
                if(custom_struct ->ofd!= NULL)
                {
                   ::fwrite(sess->first_packet.pkt_buf,sizeof(char),sess->first_packet.pkt_len,custom_struct->ofd);            
                 }             
                OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
                sess->rtp_mpeg2_bytes_sent += sess->first_packet.pkt_len;
                sess->first_packet.pkt_len = 0;   
                
                
                inParams->outNextPacketTime = 0;
           }else  if(theErr==QTSS_WouldBlock)
		   {
		         
	             QTSS_TimeVal  CurrentTime = QTSS_Milliseconds();
                 QTSS_TimeVal  FlowControlProbeInterval;
                 
                   
                 FlowControlProbeInterval = thePacket.suggestedWakeupTime == -1?(thePacket.packetTransmitTime  - CurrentTime) :
                                                (thePacket.suggestedWakeupTime  - CurrentTime);
                                                
                 if(FlowControlProbeInterval > 0)
                          inParams->outNextPacketTime = FlowControlProbeInterval >10?(FlowControlProbeInterval-10):FlowControlProbeInterval;   
                                     // for buffering, try me again in # MSec
                 else
                          inParams->outNextPacketTime = theSendInterval;//50;    // for buffering, try me again in # MSec                       
                 
                 if(inParams->outNextPacketTime >theSendInterval)
                    inParams->outNextPacketTime =  theSendInterval;
                 
                  custom_struct->err_num ++;
                 LogRequest(DEBUG_ARTS_MODULE, callid, "Flow control Probe interval = %"_64BITARG_"d", inParams->outNextPacketTime);  
                
                 return QTSS_NoErr;   
                 
           }else
           {
                if(theErr == QTSS_NotConnected){   
                    LogRequest(DEBUG_ARTS_MODULE, callid,"QTSS_Teardown");              
                    (void)QTSS_Teardown(inParams->inClientSession);
                    return QTSS_NoErr;
                }
                inParams->outNextPacketTime = theSendInterval/5;
                return QTSS_NoErr;
            }   
           
        }else  if(sess->rtp_packet_buffer)
        {
        // get packet from buffers        
           //LogRequest(DEBUG_ARTS_MODULE, callid,"get packet from buffers");
           custom_struct_t * custom_struct = (custom_struct_t*)sess->darwin_custom_struct;
	       Assert(custom_struct != NULL);   
	       
	       custom_struct->send_idel_time =0;
		  
           if(custom_struct->need_send_packet_num ==0)
           {                 
              custom_struct->need_send_packet_num = 7;              
           }
           if(send_sei <2)
           {
                custom_struct->need_send_packet_num  =1;
                send_sei ++;
           }
                     
                     
           inParams->outNextPacketTime  = 0;   
           while( custom_struct->need_send_packet_num >0)
           {               
                //char * buf = sess->first_packet.pkt_buf +4;
                //sess->first_packet.pkt_len = 4;
                char * buf = sess->first_packet.pkt_buf;                
                sess->first_packet.pkt_len  =0;
                
                int pkt_num = 0;
                
                while(true)
                {
                    rtp_packet_buffer_type *cur = sess->rtp_packet_buffer;
                    if(cur == NULL)
                        break;
                                     
                      
                    if(pkt_num==0){
                        sess->last_mpeg2_timestamp = cur->timestamp;
                        custom_struct->pkt_timestamp = cur->timestamp;
                    }
                    
                   // int tmp1= ARTSMIN(TS_TCP_PACKET_SIZE,custom_struct->need_send_packet_num *TS_PACKET_SIZE);
                    //tmp1 -= sess->first_packet.pkt_len;
                    int tmp1 = custom_struct->need_send_packet_num *TS_PACKET_SIZE -sess->first_packet.pkt_len;
                    int real_num = ARTSMIN(tmp1,cur->pkt_len);    
                                        
                    memcpy(buf,cur->pkt_buf,real_num);
                    sess->first_packet.pkt_len += real_num;
                  
                    
                    buf+= real_num;
                    cur->pkt_len -= real_num;
                    
                    if (cur->pkt_len>0)
                    {
                        char tmp_pkt[TS_TCP_PACKET_SIZE];
                        memcpy(tmp_pkt,cur->pkt_buf+real_num,cur->pkt_len);
                        memcpy(cur->pkt_buf,tmp_pkt,cur->pkt_len);
                    }
                    
                    LogRequest(DEBUG_ARTS_MODULE, callid,"real_num:%d,sess->first_pkt_len:%d,cur->pkt_len:%d",real_num,sess->first_packet.pkt_len,cur->pkt_len);
                    pkt_num = sess->first_packet.pkt_len/TS_PACKET_SIZE;                         
                   
                       
                    ARTS_Get_Packet *sARTSGetPacket = (ARTS_Get_Packet *)custom_struct->receive_pkts_thread;
                    if(cur->pkt_len ==0)  
                    {  
                          if(live == false)
                            modify_duration(sess);
                          if(sARTSGetPacket != NULL)
                          {                
                                                           
                            OSMutexLocker lockerBuffer(&sARTSGetPacket->bufferMux);                           
                                                                               
                            sess->rtp_packet_buffer =(rtp_packet_buffer_type *) cur->next; 
                            LogRequest(DEBUG_ARTS_MODULE, callid,"this packets PCR:%u,cur_pts:%d,sess->buffer:%x,cur:%x,sess->last_rtp:%x,pkt_buf:%x,org_len:%d",cur->timestamp,cur->pts,sess->rtp_packet_buffer,cur,sess->last_rtp_packet,cur->pkt_buf,cur->pkt_org_len);
                            
                            cur->next = sess->rtp_packet_buffer_pool;
                            cur->used = false;
                            sess->rtp_packet_buffer_pool =cur;
                            
                            //cur->next = NULL;
                            //free(cur->pkt_buf);
                            //free(cur);
                            sess->rtp_packet_buffer_len -= 1;    
                             
                            if(cur == sess->last_rtp_packet)
                            {
                                sess->last_rtp_packet = NULL;                 
                                LogRequest(DEBUG_ARTS_MODULE, callid,"cur_rtp:%x",cur);
                            }     
                        }else
                        {
                            sess->rtp_packet_buffer =(rtp_packet_buffer_type *) cur->next; 
                            LogRequest(DEBUG_ARTS_MODULE, callid,"this packets PCR:%u",cur->timestamp);
                            cur->next = sess->rtp_packet_buffer_pool;
                            cur->used = false;
                            sess->rtp_packet_buffer_pool =cur;
                            
                            /*
                            cur->next = NULL;
                            free(cur->pkt_buf);
                            free(cur);
                            */
                            sess->rtp_packet_buffer_len -= 1;  
                             if(cur == sess->last_rtp_packet)
                            {
                                sess->last_rtp_packet = NULL;                 
                                LogRequest(DEBUG_ARTS_MODULE, callid,"cur_rtp:%x",cur);
                            }      
                        }    
                     }  
                     
                     if(pkt_num >= 21 || custom_struct->need_send_packet_num ==pkt_num)
                     {                        
                        break;
                     }                                
                                        
                }
                custom_struct->need_send_packet_num -= pkt_num;  
                // LogRequest(DEBUG_ARTS_MODULE, callid,"left_pkt:%d",custom_struct->need_send_packet_num);
                if(sess->rtp_packet_buffer_len ==0)
                      custom_struct->need_send_packet_num =0;   
                 
                                                
                goto send;               
           }
           
          
        }else
        {   // no packet in 2sec buffer
        
            if(sess->head.state & ARTS_CALL_STATE_DESTROY || (custom_struct && custom_struct->seek_start_dts >=0) )
            {
                custom_struct->send_idel_time += theSendInterval/5;
                if(custom_struct->send_idel_time >= 2000  &&  sess->head.state & ARTS_CALL_STATE_DESTROY )
                {
                 
                        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"data send finished!");                 
                        sendAnnouce(sess);
                        return QTSS_NoErr;
                }                    
                else if(custom_struct->send_idel_time >= 20000)
                {
                        LogRequest(DEBUG_ARTS_MODULE, sess->callid,"20s after no data!"); 
                        TearDownSession(sess,NULL,ARTS_CALL_STATE_PLAY);
                        return QTSS_NoErr;
                         
                }
                    
                    
               
               
            }
            inParams->outNextPacketTime = theSendInterval/5;  
           
           return QTSS_NoErr; 
           
        }  
    }
}


/**************************************************************************************
* This function is used to align the endpoint of the clip that was being played. Lets *
* take a look at the problem. Say the two streams are audio and video have duration of*
* 300ms and 66ms respectively.Then the  streams will be something as follows          *
*  audio streams : |----------------300ms-----------|----------------300ms-----------|*
*  video streams : |-66ms-|-66ms-|-66ms-|-66ms-|-66ms-|-66ms-|-66ms-|                 *
* Now it may so happen that the last packet sent in both the stream does not have the *
* There can be various reasons(Can be figured out on your own)                        *
*                                                                                     *
* So assuming that the beginning of first packets are aligned and the duration played *
* should be same so that the clip played after this have their begining aligned.      *
*                                                                                     *
*     if(audio_duration_played > video_duration_played)                               *
*          video_duration_played = audio_duration_played                              *
*     else                                                                            *
*          audio_duration_played = video_duration_played                              *
*                                                                                     *
**************************************************************************************/

void SetStreamsOffset ( arts_session *sess )
{
    UInt32 aud_ts,vid_ts;
    UInt64 aud_duration_played, vid_duration_played;
    UInt32 aud_clock_rate, vid_clock_rate;
    UInt32 diff_correction = 0;
    UInt32 aud_last_pkt_duration , vid_last_pkt_duration;
    UInt32 aud_last_pkt_timestamp , vid_last_pkt_timestamp;

    aud_ts = sess->aud_ts;
    vid_ts = sess->vid_ts;

    aud_clock_rate = sess->rtp_audio_clock_rate;
    vid_clock_rate = sess->rtp_video_clock_rate;

    aud_last_pkt_duration = sess->lastSent_aud_packet_timestamp - sess->lastlastSent_aud_packet_timestamp ;
    vid_last_pkt_duration = sess->lastSent_vid_packet_timestamp - sess->lastlastSent_vid_packet_timestamp ;

    aud_last_pkt_timestamp = sess->lastSent_aud_packet_timestamp ;
    vid_last_pkt_timestamp = sess->lastSent_vid_packet_timestamp ;

    aud_duration_played = (UInt64)( aud_last_pkt_timestamp - aud_ts + aud_last_pkt_duration ) * (UInt64)1000 / (UInt64)aud_clock_rate ;
    vid_duration_played = (UInt64)( vid_last_pkt_timestamp - vid_ts + vid_last_pkt_duration ) * (UInt64)1000 / (UInt64)vid_clock_rate ;

    LogRequest(DEBUG_ARTS_MODULE,sess->callid,"audio duration played : %"_64BITARG_"d video duration played :%"_64BITARG_"d",aud_duration_played,vid_duration_played);


    if( aud_duration_played > vid_duration_played ){
        diff_correction = aud_duration_played - vid_last_pkt_duration*1000 / vid_clock_rate ;

        LogRequest(DEBUG_ARTS_MODULE,sess->callid,"The diff_correction for dif_duration before scaling is:%u",diff_correction);

        // diff_correction = darwin_util_uint64_scale_int ( diff_correction , (SInt32)vid_clock_rate , (SInt32)1000);
        diff_correction = diff_correction * vid_clock_rate / 1000 ;

        LogRequest(DEBUG_ARTS_MODULE,sess->callid,"The diff_correction for diff_duration after scaleing is:%u",diff_correction);

        vid_ts = vid_ts + diff_correction;
        aud_ts = sess->lastSent_aud_packet_timestamp;

    }else if( aud_duration_played < vid_duration_played ){
         diff_correction = vid_duration_played - (aud_last_pkt_duration)*1000 / aud_clock_rate ;

         LogRequest(DEBUG_ARTS_MODULE,sess->callid,"The diff_correction for dif_duration before scaling is:%u",diff_correction);
         
         //diff_correction = darwin_util_uint64_scale_int ( diff_correction , (SInt32)aud_clock_rate , (SInt32)1000);
         diff_correction = diff_correction * aud_clock_rate / 1000 ;
         
         LogRequest(DEBUG_ARTS_MODULE,sess->callid,"The diff_correction for diff_duration after scaleing is:%u",diff_correction);

         aud_ts = aud_ts + diff_correction;
         vid_ts = sess->lastSent_vid_packet_timestamp;
    }

    sess->vid_ts = vid_ts ;
    sess->aud_ts = aud_ts ;
    LogRequest(DEBUG_ARTS_MODULE,sess->callid,"The reset timestamp value for audio stream : %u and for video stream : %u",sess->aud_ts, sess->vid_ts);

}


int64_t GetseekStartDts(arts_session *sess)
{
    if(sess!=NULL&&sess->darwin_custom_struct != NULL)
    {
        custom_struct_t *p = (custom_struct_t*) sess->darwin_custom_struct;
        LogRequest(DEBUG_ARTS_MODULE,sess->callid,"seek_start_dts:%d,pause_start_time:%d,last_recv_PCR:%d",p->seek_start_dts,p->pause_start_time,p->last_recv_PCR);        
       return  ARTSMAX(p->seek_start_dts,p->pause_start_time);
    }    
    return 0;
}

void RefreshDuration(arts_session **sess,int64_t theDts,int theSendInterval)
{
    if(sess == NULL || (*sess) ==NULL || theDts <0)
        return;
    custom_struct_t * cur_struct = (custom_struct_t *)(*sess)->darwin_custom_struct;
     if(cur_struct != NULL )    
     {  
          if(cur_struct->seek == false)               
          {
                int64_t diffTime=theDts - cur_struct->last_pts;
                //if(theDts < (cur_struct->last_pts -theSendInterval*20)  || (theDts > (cur_struct->last_pts -theSendInterval*20)) )
               if( (diffTime >0 && diffTime> theSendInterval*20) || (diffTime <0 && diffTime < (-1*theSendInterval*20)))
               {
                    cur_struct->seek_start_dts = theDts;
                    (*sess)->first_packet_send_time = QTSS_Milliseconds();
                    LogRequest(DEBUG_ARTS_MODULE, (*sess)->callid,"fresh sess->first_packet_send_time: %"_64BITARG_"d, cur dts:%u,last->dts:%u",(*sess)->first_packet_send_time,theDts,cur_struct->last_pts);
                     cur_struct->last_pts =theDts;
               }else    
                               
                cur_struct->last_pts =theDts;                      
            
           }else
           {
                (*sess)->first_packet_send_time = QTSS_Milliseconds();
                LogRequest(DEBUG_ARTS_MODULE, (*sess)->callid,"after seek new first_send_time:%"_64BITARG_"d",(*sess)->first_packet_send_time);
                cur_struct->seek = false;                         
           }                  
      }
}




QTSS_Error SendPackets(QTSS_RTPSendPackets_Params* inParams)
{	
    QTSS_Error theErr = QTSS_NoErr;
    UInt32 callid, theLen = sizeof(callid); 
	UInt32 theSendInterval = QTSServerInterface::GetServer()->GetPrefs()->GetSendIntervalInMsec();
	bool isBeginningOfWriteBurst = true;
	QTSS_WriteFlags theFlags = qtssWriteFlagsIsRTP;

    theErr = QTSS_GetValue(inParams->inClientSession, sARTSSessionAttr, 0, &callid, &theLen);
    Assert(theErr == QTSS_NoErr);
    LogRequest(DEBUG_ARTS_MODULE, callid, "SendPackets: Entering ,current_time:%"_64BITARG_"d" ,QTSS_Milliseconds());
    arts_session *sess = NULL;
    {
        //LogRequest(DEBUG_ARTS_MODULE,callid,"start get session");
        //OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
        sess = arts_session_find(callid);
        //LogRequest(DEBUG_ARTS_MODULE, callid,"this->sess:%x",sess);
    }
    if(!sess)
    { 
        LogRequest(DEBUG_ARTS_MODULE, callid,"sess is null");
        return QTSS_NoErr;
    }
    
    
    if(sess->transport_type == qtssRTPTransportTypeMPEG2)
    {
        return SendMpeg2Packets(inParams,sess);
    }
	
	
    while(true)
    {
    
         LogRequest(DEBUG_ARTS_MODULE, callid, "sess->first_packet.pkt_len:%d ,sess->buffer_len:%d,pkt_str:%d",sess->first_packet.pkt_len,sess->rtp_packet_buffer_len,sess->first_packet.pkt_str);
        if(sess->first_packet.pkt_len > 0)
        {
            // There was a packet, so send it to the client          
            
            UInt32* theTimeStampP = NULL;
            
            UInt32 theTimeStamp =0;
            UInt8 padding_len =0;
            
            UInt32 theDts =0;
            
          
            QTSS_PacketStruct thePacket;   
            UInt8 padding_flag =sess->first_packet.pkt_buf[0]&0x20;
           
            memset(sess->first_packet.pkt_buf,sess->first_packet.pkt_buf[0]&0xdf,1);           
            thePacket.packetData = sess->first_packet.pkt_buf; 
              
            if(sess->first_packet.pkt_len >5 )
            {
                theTimeStampP = (UInt32*)thePacket.packetData;          
                theTimeStamp = ntohl(theTimeStampP[1]); 
            }    
            
            //if(!adapter_version_2)
            {
                UInt8 *padding_lenP = (UInt8*)(thePacket.packetData +sess->first_packet.pkt_len-1);    
                padding_len=padding_lenP[0]; 
                LogRequest(DEBUG_ARTS_MODULE, callid ,"padding_len:%d",padding_len );       
                UInt32* dtsP =(UInt32*) ((UInt8*) ( thePacket.packetData + sess->first_packet.pkt_len - padding_len ));                     
                theDts = ntohl(dtsP[0]);
            }     
                                           
            
            if(sess->first_packet.pkt_str == 2)
            {				
            
                /*
                if(theSeqNum != sess->lastSent_aud_packet_seqnum)
                {                   
					if( (sess->aud_discont == 1))
					{  
                        sess->aud_seqnumOffset = sess->lastSent_aud_packet_seqnum; //(no delta required since first seqnum is 1)
                      
                        sess->aud_discont = 0;                      
						qtss_printf("Resetting Audio Timestamp and SeqNum to %u, %u \n", sess->aud_ts, sess->aud_seqnumOffset);
                    }


                    *(theSeqNumP+1) =    htons( theSeqNum) ;//htons(sess->aud_seqnumOffset + theSeqNum);
                    *(theTimeStampP+1) =  htonl( theTimeStamp);//htonl(sess->aud_ts + theTimeStamp) ;

                    theTimeStamp = ntohl(theTimeStampP[1]);
                    theSeqNum = ntohs(theSeqNumP[1]);
					
					if ( sess->lastSent_aud_packet_seqnum > (theSeqNum + 0x7fff)) 
						sess->audio_rtp_seqnum_warp_offset += 0xffff; 
                    
                    sess->lastSent_aud_packet_seqnum = theSeqNum;
                }
				
				realSeqNum = theSeqNum + sess->audio_rtp_seqnum_warp_offset;
				*/			
				QTSS_TimeVal  CurrentTime = QTSS_Milliseconds();							
				
				RefreshDuration(&sess,theDts,theSendInterval);				
				thePacket.packetTransmitTime = sess->first_packet_send_time + (theDts -  GetseekStartDts(sess) ) ;
				if( live == true)
				    thePacket.packetTransmitTime = CurrentTime;
				/*pts
				if ( theTimeStamp > sess->rtp_audio_timestamp )
                  thePacket.packetTransmitTime = sess->first_packet_send_time + ( ((SInt64)(theTimeStamp - sess->rtp_audio_timestamp) * (SInt64) 1000) /(SInt64)(sess->rtp_audio_clock_rate));
			    else
				  thePacket.packetTransmitTime = sess->first_packet_send_time;
				  //	pts	
				  */
				
	            if(thePacket.packetTransmitTime -CurrentTime >  sARTSIpAheadTime)
	            {
	                inParams->outNextPacketTime = theSendInterval ;
	                LogRequest(DEBUG_ARTS_MODULE, callid,"cur pkts transmittime:%"_64BITARG_"d,outNextPacketTime:%"_64BITARG_"d",thePacket.packetTransmitTime,inParams->outNextPacketTime);
                    return QTSS_NoErr; 
	            }
	            		
				if (isBeginningOfWriteBurst)
					theFlags |= qtssWriteFlagsWriteBurstBegin;
				
				//if ( realSeqNum >=  (sess->real_rtp_audio_seqnum) )
				{	    
				
                   theErr = QTSS_Write(sess->audio_str, &thePacket, sess->first_packet.pkt_len-padding_len, NULL, qtssWriteFlagsIsRTP);                  
                   
                   LogRequest(DEBUG_ARTS_MODULE, callid,"--send type = a, theTimeStamp = %u, theErr:%d,CurrentTime %"_64BITARG_"d,Pkt_transtime = %"_64BITARG_"d,delay:%"_64BITARG_"d,dts:%u,pkt_len:%d",theTimeStamp,theErr,CurrentTime,thePacket.packetTransmitTime, (CurrentTime-thePacket.packetTransmitTime),theDts,sess->first_packet.pkt_len-padding_len);
                }
			    //else 
				//	qtss_printf("packet dropped seq: %d, first-seq:%d ,real-seq:%"_64BITARG_"d, real_first_seq:%"_64BITARG_"d, CurrentTime %"_64BITARG_"d \n", 
				//				theSeqNum, sess->rtp_audio_seqnum ,realSeqNum, sess->real_rtp_audio_seqnum, QTSS_Milliseconds());
          
                if(!theErr)
                {
                    //sess->lastlastSent_aud_packet_timestamp = sess->lastSent_aud_packet_timestamp;
                    sess->lastSent_aud_packet_timestamp = theDts;               
                }              

            }
            else if(sess->first_packet.pkt_str == 3)
            {
                /*
                if(theSeqNum != sess->lastSent_vid_packet_seqnum)
                {
                   if( (sess->vid_discont == 1))
                   {
                        sess->vid_seqnumOffset = sess->lastSent_vid_packet_seqnum; //(no delta required since first seqnum is 1)                         
                        sess->vid_discont = 0;                       
						qtss_printf( "Resetting Video Timestamp and SeqNum to %u, %u \n", sess->vid_ts, sess->vid_seqnumOffset);
                    }

                    *(theSeqNumP+1) = htons( theSeqNum);//htons(sess->vid_seqnumOffset + theSeqNum);
                    *(theTimeStampP+1) = htonl(theTimeStamp);//htonl(sess->vid_ts + theTimeStamp) ;                    
                    theTimeStamp = ntohl(theTimeStampP[1]);
                    theSeqNum = ntohs(theSeqNumP[1]);                  
					if ( sess->lastSent_vid_packet_seqnum > (theSeqNum + 0x7fff)) 
						sess->video_rtp_seqnum_warp_offset += 0xffff; 					
                    sess->lastSent_vid_packet_seqnum = theSeqNum;                  
                }
				
				realSeqNum = theSeqNum + sess->video_rtp_seqnum_warp_offset;
				*/
				RefreshDuration(&sess,theDts,theSendInterval);									
				thePacket.packetTransmitTime = sess->first_packet_send_time +  (theDts -  GetseekStartDts(sess)) ;
				
			
				
				QTSS_TimeVal  CurrentTime = QTSS_Milliseconds();					
				if(thePacket.packetTransmitTime -CurrentTime >  sARTSIpAheadTime)
	            {
	                inParams->outNextPacketTime = theSendInterval;
	                 LogRequest(DEBUG_ARTS_MODULE, callid,"cur pkts transmittime:%"_64BITARG_"d,startdts:%u,first_packet_send_time:%"_64BITARG_"d,dts:%u",inParams->outNextPacketTime, GetseekStartDts(sess),sess->first_packet_send_time,theDts);
	                
                    return QTSS_NoErr; 
	            }			
				
				if (isBeginningOfWriteBurst)
					theFlags |= qtssWriteFlagsWriteBurstBegin;
               
				//if ( realSeqNum >=  (sess->real_rtp_video_seqnum ))
				{				    
				   theErr = QTSS_Write(sess->video_str, &thePacket, sess->first_packet.pkt_len-padding_len, NULL, theFlags);				   
				   LogRequest(DEBUG_ARTS_MODULE, callid,"--send type = v, theTimeStamp = %u,theErr:%d.CurrentTime %"_64BITARG_"d,Pkt_transtime = %"_64BITARG_"d,delay:%"_64BITARG_"d,dts:%u,pkt_len:%d",theTimeStamp,theErr,CurrentTime,thePacket.packetTransmitTime,(CurrentTime-thePacket.packetTransmitTime),theDts,sess->first_packet.pkt_len-padding_len);
				}
			   // else 
				//{
				 //  qtss_printf("packet dropped seq: %d, first-seq:%d ,real-seq:%"_64BITARG_"d,real-first-seq:%"_64BITARG_"d, CurrentTime %"_64BITARG_"d \n", 
				//			   theSeqNum, sess->rtp_video_seqnum , realSeqNum, sess->real_rtp_video_seqnum, QTSS_Milliseconds());				   
				//}             
				
                if(!theErr)
                {
                    //sess->lastlastSent_vid_packet_timestamp = sess->lastSent_vid_packet_timestamp;
                    sess->lastSent_vid_packet_timestamp = theDts;                  
                }                
            }         
          
			isBeginningOfWriteBurst = false;

#if DO_BLOCK_READ
            {
                QTSS_TimeVal  CurrentTime = QTSS_Milliseconds();              
                if (thePacket.packetTransmitTime < CurrentTime)
                    LogRequest(DEBUG_ARTS_MODULE, callid, "Packet should have been sent earlier by %d msecs", CurrentTime - thePacket.packetTransmitTime);
            }
#endif
            if(theErr != QTSS_NoErr) 
            {
                if(theErr == QTSS_WouldBlock)
                {
                   QTSS_TimeVal  CurrentTime = QTSS_Milliseconds();
                   QTSS_TimeVal  FlowControlProbeInterval;
                  
                   
                   FlowControlProbeInterval = thePacket.suggestedWakeupTime == -1?(thePacket.packetTransmitTime  - CurrentTime) :
                                                (thePacket.suggestedWakeupTime  - CurrentTime);
                                                
                    if(FlowControlProbeInterval > 0)
                            inParams->outNextPacketTime = FlowControlProbeInterval >10?(FlowControlProbeInterval-10):FlowControlProbeInterval;   
                                     // for buffering, try me again in # MSec
                    else
                            inParams->outNextPacketTime = theSendInterval;//50;    // for buffering, try me again in # MSec                       

                    LogRequest(DEBUG_ARTS_MODULE, callid, "Flow control Probe interval = %"_64BITARG_"d", inParams->outNextPacketTime);  
                    
                    return QTSS_NoErr;       
                }
                else
                {
                
                    if(theErr == QTSS_NotConnected){ 
                        LogRequest(DEBUG_ARTS_MODULE, callid,"QTSS_Teardown");                   
                        (void)QTSS_Teardown(inParams->inClientSession);
                        return QTSS_NoErr; 
                    }
                    
                    inParams->outNextPacketTime = theSendInterval/5;
                    return QTSS_NoErr; 
                }

            }
            else
            {
                if(sess->first_packet.pkt_str == 2)
                {
                    sess->rtp_audio_bytes_sent += sess->first_packet.pkt_len;
                    sess->rtp_last_audio_timestamp = theTimeStamp;
                }
                if(sess->first_packet.pkt_str == 3)
                {
                    sess->rtp_video_bytes_sent += sess->first_packet.pkt_len;
                    sess->rtp_last_video_timestamp = theTimeStamp;
                }
                sess->first_packet.pkt_len = 0; 
               
            }      
        }
        
 

#if DO_BLOCK_READ
        if(sess->rtp_packet_buffer && sess->rtp_packet_buffer_len > MIN_RTP_PACKET_BUFFER_LEN )
#else
        
        if(sess->rtp_packet_buffer )        
#endif
        {
            ARTS_Get_Packet *sARTSGetPacket = NULL;
            
            if(sess->darwin_custom_struct != NULL)
            {
               custom_struct_t *custom_struct = ( custom_struct_t *)sess->darwin_custom_struct;            
                sARTSGetPacket = (ARTS_Get_Packet *)custom_struct->receive_pkts_thread;
                if(sARTSGetPacket != NULL)
                {                    
                    OSMutexLocker lockerBuffer(&sARTSGetPacket->bufferMux);
                    sess->rtp_packet_buffer_len--;
                    
                 }else
                    sess->rtp_packet_buffer_len--;         
              
                 
                 modify_duration(sess);
                           
                                       
                 LogRequest(DEBUG_ARTS_MODULE, sess->callid, "get packet from buffer,curtime:%"_64BITARG_"d,Duration:%u,buf_len:%d", custom_struct->buf_duration,QTSS_Milliseconds(),sess->rtp_packet_buffer_len);
                              
            }else
            {
                sess->rtp_packet_buffer_len --;
                LogRequest(INFO_ARTS_MODULE, sess->callid,"sess->darwin_custom_struct is null");
            }           

                             

            rtp_packet_buffer_type  *rtp_packet_buffer = sess->rtp_packet_buffer;
            memcpy(sess->first_packet.pkt_buf, sess->rtp_packet_buffer->pkt_buf, sess->rtp_packet_buffer->pkt_len);
            sess->first_packet.pkt_len = sess->rtp_packet_buffer->pkt_len;
            sess->first_packet.pkt_str = sess->rtp_packet_buffer->pkt_str;
            //sess->first_packet.timestamp = sess->rtp_packet_buffer->timestamp; 
           
            
             if(sARTSGetPacket != NULL)
            {           
                OSMutexLocker lockerBuffer(&sARTSGetPacket->bufferMux);
                sess->rtp_packet_buffer = (rtp_packet_buffer_type  *)sess->rtp_packet_buffer->next;
            }else
            {
                sess->rtp_packet_buffer = (rtp_packet_buffer_type  *)sess->rtp_packet_buffer->next;
            }

            rtp_packet_buffer->next = NULL;
            free(rtp_packet_buffer->pkt_buf);
            free(rtp_packet_buffer);
        }
        
        else
        {
            if(sess->head.state & ARTS_CALL_STATE_DESTROY)
            {
                custom_struct_t *custom_struct = NULL;
                if(sess->darwin_custom_struct != NULL)
                {
                    custom_struct= ( custom_struct_t *)sess->darwin_custom_struct;
                } 
                custom_struct->send_idel_time += 10;
                if(custom_struct->send_idel_time >= 2000  )
                {
                    LogRequest(DEBUG_ARTS_MODULE, sess->callid,"data send finished!");                 
                    sendAnnouce(sess);
                    return QTSS_NoErr;
                }
               
            }
            inParams->outNextPacketTime = theSendInterval/5;  
           
           return QTSS_NoErr;          
        }
    }
    LogRequest(DEBUG_ARTS_MODULE, callid, "Exiting");

    return QTSS_NoErr; 
}
static void get_pts(arts_session * sess)
{
    if(sess->rtp_packet_buffer == NULL || sess->rtp_packet_buffer_len ==0 )
    {
        return ;
    }
    custom_struct_t * custom_struct = (custom_struct_t *)sess->darwin_custom_struct;
    if(custom_struct != NULL &&  custom_struct->last_keyframe_pts >=0)
    {
        return;
    }
    rtp_packet_buffer_type * p  = sess->rtp_packet_buffer;
    while(p!=NULL && p->pkt_len >0)
    {
        char * ptr = p->pkt_buf;
        int pkt_len = p->pkt_len;
        int64_t PTS,tmpPCR,dts;
        
        while(pkt_len )
        {
            
            PTS = -1;
            tmpPCR =-1;
            dts =-1;
                    //tmpPCR = GetMPEG2PCR(ptr, &pktLen,&PTS,&(custom_struct->ts));
            MpegTSContext*ts = (MpegTSContext*)custom_struct->tsctx;
            dts=handle_pkt_simple(ts,(unsigned char*)ptr,TS_PACKET_SIZE,&tmpPCR,&PTS,true);
            if(PTS>0)
            {
                if(custom_struct->last_keyframe_pts == -1)
                {
                    custom_struct->last_keyframe_pts = PTS;
                     LogRequest(INFO_ARTS_MODULE, sess->callid,"get PTS-MS:%d", PTS/90);
                    return ;
                }
            }
            ptr += TS_PACKET_SIZE;
            pkt_len -= TS_PACKET_SIZE;
        }
        
         p= (rtp_packet_buffer_type * )p->next;
    }
}

QTSS_Error DoPause(QTSS_StandardRTSP_Params* inParams)
{
    QTSS_Error err = QTSS_NoErr;
   
    UInt32 callid, attributeLen = 0,theLen = sizeof(callid);

    err = QTSS_GetValue(inParams->inClientSession, sARTSSessionAttr, 0, &callid, &theLen);
    Assert(err == QTSS_NoErr);
	arts_session *sess = NULL;
    {
		OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
	    sess = arts_session_find(callid);
    }
    if(!sess) return QTSS_NoErr;

      
    custom_struct_t *custom_struct = (custom_struct_t*)sess->darwin_custom_struct;
   
    if(!custom_struct) return QTSS_NoErr;
    
    custom_struct ->pause = true;
    if(custom_struct->isIpqam == true || sess->transport_type == qtssRTPTransportTypeMPEG2)
    {
        custom_struct->pause_start_time =sess->last_mpeg2_timestamp;
    }else
    {
        custom_struct->pause_start_time = ARTSMAX(sess->lastSent_vid_packet_timestamp,sess->lastSent_aud_packet_timestamp);
    }
    
    
    
    custom_struct->last_keyframe_pts = -1;
    custom_struct->pkt_timestamp = 0;
    sess->rtp_mpeg2_bytes_sent =0; 
    if(custom_struct->isIpqam == true)
    {
         timeout_ev_t *curSendEv = (timeout_ev_t *)custom_struct->sendEv;
         curSendEv->pause = true;
    
    }else
    {

        err = QTSS_Pause(inParams->inClientSession);
        Assert(err == QTSS_NoErr); 
    } 
    
    err =  QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
    Assert(err == QTSS_NoErr);
    if(custom_struct->supportPTS == true)
    {
        get_pts( sess);
    }
    LogRequest(INFO_ARTS_MODULE, callid, "Exiting");     

    return err;
}

//added by lijie, 2010.09.30
//close rtsp sessions established by Describe request (adapter pipeline created), 
//but disconnected by client before send SETUP request (no rtp session established).

QTSS_Error CloseRTSPSession(QTSS_RTSPSession_Params* inParams)
{
    QTSS_Error err = QTSS_NoErr;
    UInt32 callid, theLen = sizeof(callid);
	
    err = QTSS_GetValue(inParams->inRTSPSession, sARTSRTSPSessionAttr, 0, &callid, &theLen);
    Assert(err == QTSS_NoErr);
    Assert(sARTSRTSPSessionAttr != qtssIllegalAttrID);    
	
    OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
    arts_session *sess = arts_session_find(callid);
    if(!sess) return QTSS_NoErr;
	
	//no action if any rtp session is available for this rtsp session
	if (sess->video_str || sess->audio_str) return QTSS_NoErr;  
    
    LogRequest(INFO_ARTS_MODULE, callid, "Entering CloseRTSPSession");
	
    LogRequest(INFO_ARTS_MODULE, callid, "Audio RTP Bytes Sent = %u", sess->rtp_audio_bytes_sent);
	
    LogRequest(INFO_ARTS_MODULE, callid, "Video RTP Bytes Sent = %u", sess->rtp_video_bytes_sent);
	
    LogRequest(INFO_ARTS_MODULE, callid, "Mpeg2 TS Bytes Sent = %u", sess->rtp_mpeg2_bytes_sent);
	
    if(!(sess->head.state & ARTS_CALL_STATE_DESTROY_NOW))
    {		
        sess->ReleaseCause = proto64::UserDisconnected;
    }
	
    LogRequest(INFO_ARTS_MODULE, callid, "Release Cause = %d", sess->ReleaseCause);
	
    // Check the controller state before sending to avoid contention for fd write
    if ((sARTSPHInterface->control_state == ARTS_CONTROLLER_STATE_ACTIVE) &&
        (!(sess->head.state & ARTS_CALL_STATE_RELEASE_SENT)))
    {
        arts_send_conrel(sARTSPHInterface->controlsock->fd, sess, ARTS_PH_TYPE_RTSP);
		//sess->head.state |= ARTS_CALL_STATE_RELEASE_SENT;
        LogRequest(INFO_ARTS_MODULE, callid, "Release Sent to Controller. Cause = %d", sess->ReleaseCause);
    }
    // Signal the other task to cleanup the connection
    
    custom_struct_t *cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
   
     
   if(cur_struct != NULL && sARTSSendPktThread!= NULL )
    {    
        OSMutexLocker registListMutex(&sARTSSendPktThread->regist_list_Mutex);
        sARTSSendPktThread->del_sendEv(sess->callid);
    }
    
    if( (cur_struct!=NULL && cur_struct->receive_pkts_thread == NULL) ||  sess->head.sock == NULL || cur_struct->adapter_unregister == true)
    { 
        // adapter close first
        free_custom_struct(sess,NULL);  
        if(sess->head.sock!= NULL )
        {
			multicast_group_leave(sess->head.sock->fd,sess->head.addr);		
			
            del_events_node(sess->head.sock->fd);
            
		}
        arts_session_free(sess,1);
        LogRequest(INFO_ARTS_MODULE, callid,"free session");
    }else
    {
        //client close first
        
        cur_struct->buf_duration =0;
        sess->head.state = ARTS_CALL_STATE_DESTROY_NOW;
    }
	
    return QTSS_NoErr;
}
//jieli



QTSS_Error DestroySession(QTSS_ClientSessionClosing_Params* inParams)
{
    QTSS_Error err = QTSS_NoErr;
    UInt32 callid, theLen = sizeof(callid);
    

    err = QTSS_GetValue(inParams->inClientSession, sARTSSessionAttr, 0, &callid, &theLen);  
    
    if(callid ==0 || err != QTSS_NoErr)
    {
        return QTSS_NoErr;
    }
    Assert(sARTSSessionAttr != qtssIllegalAttrID);

    OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
    arts_session *sess = arts_session_find(callid);
    if(!sess)
    { 
        LogRequest(INFO_ARTS_MODULE, 0,"sess is null");
        return QTSS_NoErr;
    }
	LogRequest(INFO_ARTS_MODULE, callid, "Entering DestroySession");
	LogRequest(DEBUG_ARTS_MODULE, callid, "Audio RTP Bytes Sent = %u,sess->addr:%x", sess->rtp_audio_bytes_sent,sess->head.addr);
	
    UpdateRTSPStats(inParams->inClientSession,sess);  

    

    LogRequest(DEBUG_ARTS_MODULE, callid, "Video RTP Bytes Sent = %u", sess->rtp_video_bytes_sent);

    LogRequest(DEBUG_ARTS_MODULE, callid, "Mpeg2 TS Bytes Sent = %u", sess->rtp_mpeg2_bytes_sent);

    if(!(sess->head.state & ARTS_CALL_STATE_DESTROY_NOW))
    {

        if(inParams->inReason == qtssCliSesCloseClientTeardown)
            sess->ReleaseCause = proto64::Normal;
                  
        if(inParams->inReason == qtssCliSesCloseTimeout)
            sess->ReleaseCause = proto64::PHDisconnected;
                  
        if(inParams->inReason == qtssCliSesCloseClientDisconnect)
            sess->ReleaseCause = proto64::UserDisconnected;
    }

    LogRequest(INFO_ARTS_MODULE, callid, "Release Cause = %d,addr:%x", sess->ReleaseCause,sess->head.addr);

    // Check the controller state before sending to avoid contention for fd write
    
    if ((sARTSPHInterface->control_state == ARTS_CONTROLLER_STATE_ACTIVE) &&
        (!(sess->head.state & ARTS_CALL_STATE_RELEASE_SENT)) && live == false)
    {
        arts_send_conrel(sARTSPHInterface->controlsock->fd, sess, ARTS_PH_TYPE_RTSP);
		//sess->head.state |= ARTS_CALL_STATE_RELEASE_SENT;
        LogRequest(DEBUG_ARTS_MODULE, callid, "Release Sent to Controller. Cause = %d", sess->ReleaseCause);
    }   
    custom_struct_t * cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
    
    if(cur_struct != NULL && sARTSSendPktThread!= NULL )
    {    
        OSMutexLocker registListMutex(&sARTSSendPktThread->regist_list_Mutex);
        sARTSSendPktThread->del_sendEv(sess->callid);     
    }
    
    
    
    if( (cur_struct!=NULL && cur_struct->receive_pkts_thread == NULL) ||  sess->head.sock == NULL )
    { 
        // adapter close first  or this sess has no receive data or unregister for fullbuffer
        free_custom_struct(sess,NULL); 
        if(sess->head.sock!= NULL )
        {
		    multicast_group_leave(sess->head.sock->fd,sess->head.addr); 		    
		    del_events_node(sess->head.sock->fd);		    
		}
        arts_session_free(sess,1);
        LogRequest(INFO_ARTS_MODULE, callid,"free session");
    }else
    {
        //client close first
        cur_struct->buf_duration =0;
        if(live == true)
        {
            rtp_packet_buffer_type  *rtp_packet_buffer = NULL;
	        while(sess->rtp_packet_buffer)
	        {   
       	        rtp_packet_buffer = sess->rtp_packet_buffer;
		        sess->rtp_packet_buffer = (rtp_packet_buffer_type  *)sess->rtp_packet_buffer->next;
		        rtp_packet_buffer->next = NULL;
		        free(rtp_packet_buffer->pkt_buf);
		        free(rtp_packet_buffer);
	        }
	        sess->rtp_packet_buffer_len =0;
        }
        sess->head.state = ARTS_CALL_STATE_DESTROY_NOW;
    }
  
    
    LogRequest(INFO_ARTS_MODULE, callid,"Exiting");
     
    return QTSS_NoErr;
}

QTSS_Error ProcessRTCPPacket(QTSS_RTCPProcess_Params * inParams)
{
	QTSS_ClientSessionObject theClientSession = NULL;
    QTSS_Error err = QTSS_NoErr;
    UInt32 callid, theLen = sizeof(callid);

    err = QTSS_GetValue(inParams->inClientSession, sARTSSessionAttr, 0, &callid, &theLen);
    Assert(err == QTSS_NoErr);
    Assert(sARTSSessionAttr != qtssIllegalAttrID);
	
	OSMutexLocker l_autoMutex(&sARTSPHInterface->sessionMutex);
	arts_session *sess = arts_session_find((unsigned int)callid);
	
	if (sess)
		theClientSession = sess->remote_con;
	else
		return QTSS_Unimplemented;
	
	UInt32 packetlossrate;
	RTCPPacket rtcppacket;
	LogRequest(INFO_ARTS_MODULE, callid, "Starting Process RTCP Packet %s", "");
	UInt8 *ptr = (UInt8 *)(inParams->inRTCPPacketData);
	if (rtcppacket.ParsePacket(ptr,inParams->inRTCPPacketDataLen))
	{
		LogRequest(INFO_ARTS_MODULE, callid,"Packet type is %d",rtcppacket.GetPacketType());
		RTCPReceiverPacket rtcprpacket;
		if(rtcprpacket.ParseReport((UInt8*)(inParams->inRTCPPacketData),inParams->inRTCPPacketDataLen))
		{	
			if(sARTSPHInterface->control_state == ARTS_CONTROLLER_STATE_ACTIVE && (sess->head.state & ARTS_CALL_STATE_CONNECTED) 
			   && (! (sess->head.state & ARTS_CALL_STATE_DESTROY))&&(! (sess->head.state & ARTS_CALL_STATE_DESTROY_NOW))) 
	        {
		       packetlossrate = rtcprpacket.GetFractionLostPackets(0);
			   
			   	UInt32 theBitRate = 0;
				UInt32 *theByteSent = 0;
				UInt32 theLen = 0;	
			    (void)QTSS_GetValuePtr(theClientSession, qtssCliSesCurrentBitRate, 0, (void **)&theBitRate, &theLen);	   
			   
			   QTSS_GetValuePtr(theClientSession, qtssCliSesRTPBytesSent, 0, (void **)&theByteSent, &theLen);
			   LogRequest(INFO_ARTS_MODULE, callid, "RTP Byte Sent : %d , Last Byte Sent : %d",*theByteSent,sess->last_rtsp_bytes_sent);
			   
			   UInt64 CurrentTime = QTSS_Milliseconds();
			   float TimeInterval = (CurrentTime - sess->last_rtsp_bitrate_update_time)/1000.0;
			   if(TimeInterval < 1.0)
			   return QTSS_NoErr;
			   
			   LogRequest(INFO_ARTS_MODULE, callid, "Time from last RR report : %f",TimeInterval);
               theBitRate = (UInt32)((*theByteSent - sess->last_rtsp_bytes_sent)/ TimeInterval);			                
			   arts_send_bandwith(sARTSPHInterface->controlsock->fd,sess,theBitRate,packetlossrate);
			   sess->last_rtsp_bytes_sent = *theByteSent;
			   sess->last_rtsp_bitrate_update_time = CurrentTime;
               LogRequest(INFO_ARTS_MODULE, callid, "Send packetlossrate to Controller.current_bandwidth=%d , packetlossrate = %d",theBitRate, packetlossrate);
			   return QTSS_NoErr;
	        }
	        else
			{
			   LogRequest(INFO_ARTS_MODULE, callid, "Nothing to do with RTCP packet! %s", "");
			   return QTSS_OutOfState;
		    }
		}
		else		      
		LogRequest(INFO_ARTS_MODULE, callid, "The RTCP Packet is not APP RTCP Packet !%s", "");
		return QTSS_Unimplemented;
		
	}
	return QTSS_Unimplemented;
	
	
}

static void UpdateRTSPStats(QTSS_ClientSessionObject &inClientSession,arts_session *sess)
{ 
     
     QTSS_RTPStreamObject* streamObject=NULL; 
     long* ptrRtpPacketLost = NULL;
     long* ptrRtpPacketJitter = NULL;
     UInt32 theLen =0;


    /*    All of the attributes of QTSS_ClientSessionObject are preemptive safe, 
       so they can be read by calling QTSS_GetValue, QTSS_GetValueAsString, or QTSS_GetValuePtr.
    */
    /* 
       All of the attributes of QTSS_RTPStreamObject are preemptive safe, 
       so they can be read by calling QTSS_GetValue, QTSS_GetValueAsString, or QTSS_GetValuePtr.
    */


     if ( (sess->head.state == ARTS_CALL_STATE_UNSET )||
          !(sess->head.state & ARTS_CALL_STATE_CONNECTED ) ||
           (sess->rtp_audio_bytes_sent + sess->rtp_video_bytes_sent) == 0)
        
     {
         return;
     }

    QTSS_Error err = QTSS_GetValuePtr(inClientSession, qtssCliSesStreamObjects, 0, (void**)&streamObject, &theLen);
    Assert ((err != QTSS_NoErr) || (theLen != sizeof(QTSS_RTPStreamObject*)) || (streamObject == NULL));
       


    theLen = sizeof(sess->rtp_packets_lost);
    ptrRtpPacketLost = &sess->rtp_packets_lost;

    
    err = QTSS_GetValuePtr(*streamObject, qtssRTPStrTotalLostPackets, 0, (void**)&ptrRtpPacketLost, &theLen);
    Assert ((err != QTSS_NoErr) || (theLen != sizeof(long*)) || (ptrRtpPacketLost == NULL));
       
 

    ptrRtpPacketJitter = &sess->rtp_jitter;
    err = QTSS_GetValuePtr(*streamObject, qtssRTPStrJitter, 0,(void**) &ptrRtpPacketJitter, &theLen);
    Assert((err != QTSS_NoErr) || (theLen != sizeof(long*)) || (ptrRtpPacketJitter == NULL));
      
    return;
}


static QTSS_Error Shutdown()
{
    delete [] sARTSSystemName;
    delete [] sARTSBackends;
    delete [] sARTSBindHost;
    delete [] sARTSIpqamHost;
    delete [] sARTSInputFile;
    refresh_socket_list(0,NULL,true);
    WriteShutdownMessage();
   
    sARTSPHInterface->StopAndWaitForThread();
   
    arts_sessions_free();
   
    delete sARTSPHInterface;   
    return QTSS_NoErr;
}
