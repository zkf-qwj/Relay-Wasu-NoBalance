#ifndef  _ARTS_SEND_PKT_THREAD_H
#define  _ARTS_SEND_PKT_THREAD_H
#include <time.h>
#include <sys/time.h>
#include "QTSServerInterface.h"
#include <stdlib.h>
#include <stdio.h>


#include "ev.h"
extern "C" {
#include "libmemcached/memcached.h"
}

#define PACKET_SIZE  7*188

//extern "C" {
typedef struct 
{   
    
    int completed;     
    int sockfd;      
    int sockfd1;
    struct timespec time_start;
    struct timespec time_stop;  
    struct timespec send_start;
    struct timespec send_stop;  
    ev_timer  timeout_watcher;
    char send_buf[PACKET_SIZE];
    bool pause;
    unsigned int callid;     
    unsigned long long int packet_time;  
}timeout_ev_t;
//}




typedef struct Regist_Node
{
    unsigned int callid;
    struct Regist_Node * next;
    int registed;
    int unused;
}Regist_Node;


class ARTS_Send_Pkt_Thread : public OSThread
{
public:
        ARTS_Send_Pkt_Thread();
        ~ARTS_Send_Pkt_Thread();
        bool Configure(char *ipqamHost,UInt16 port ,UInt32 bitrate,bool return_flag);
        int udp_packet_size;
        unsigned char send_buf[PACKET_SIZE];
        int bitrate;  
        
        void insert_regist_node(unsigned int callid);
        void del_regist_node(unsigned int callid);   
        void die_regist_node(unsigned int callid);
        struct ev_loop *loop;
        Regist_Node * regist_list;
        struct sockaddr_in Destaddr;
         struct sockaddr_in Destaddr1;
        ev_timer interval_watcher; 
        double interval_time;
        OSMutex regist_list_Mutex;
       
        void del_sendEv(unsigned int callid);
        int send_ahead_time;
        int send_after_time;
        void del_event(unsigned int callid);
        void playAgain(unsigned int callid);
       
 private:
        virtual void Entry();        
        void Trigger();
        
       
         
         
        struct timeval  interval_tv;
        
         
};

class ARTS_Get_Packet :public Task
{
    public:
    
       
        // this Task for getting packets from adapter
        ARTS_Get_Packet() : Task() {
                                                       
                            this->callid = -1; 
                            this->inParams = NULL;       
                            this->stream_type = 0;
                            this->buffer_len =0;
                           
                         }
        virtual ~ARTS_Get_Packet();
        
        UInt32 callid; 
        void* inParams; 
        OSMutex bufferMux;              
        char *buffer;
        int  buffer_len;
        int  stream_type; 
        void *sess;       
        
        virtual SInt64 Run();
    private:   
        
        enum
        {
            kProxyTaskPollIntervalMsec = 10
        };
};

int get_packet(char *pkt_buf,int left_len ,void *sess);
extern ARTS_Send_Pkt_Thread *sARTSSendPktThread;
int get_udp_host(memcached_st * memc,char *channel_id,UInt32 callid,char *value);
int get_sdp(memcached_st * memc,char *channel_id,UInt32 callid,char *value);
int set(memcached_st * memc,char *channle_id,unsigned int callid,char *value);
memcached_st * conn(char *host, unsigned int callid);
int release_conn(memcached_st * memc);
void report_bitrate(void *sess);
#endif
