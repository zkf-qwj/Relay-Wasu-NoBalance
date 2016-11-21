#include "arts_send_pkt_thread.h"
#include <arpa/inet.h>

#include "ARTSutil.h"
extern "C" {
#include "fdevent.h"
#include "arts_ph_common.h"

}

#define TS_PACKET_SIZE  188
#define ARTSMIN(a,b)  ((a)<(b)?(a):(b))
#define TCP_PACKET_SIZE  1316

ARTS_Send_Pkt_Thread *sARTSSendPktThread;
#define MAX_INT16 ((1<<15) -1)
#define STORE_INTERVAL  29
#define SEND_BITRATE    2
int  get_min(int a,int b,int c)
{
    int t1=a;
    int t2 = b;
    int t3 = c;
    if(a<=0)
        t1 = MAX_INT16;
    if(b <=0)
        t2= MAX_INT16;
    if(c <=0)
        t3= MAX_INT16;
    double t4 = t1 < t2 ? t1:t2 ;
    return (t4<t3? t4:t3);
}


long long int usecDiff(struct timespec* time_stop, struct timespec* time_start)
{
	long long int temp = 0;
	long long int utemp = 0;
		   
	if (time_stop && time_start) {
		if (time_stop->tv_nsec >= time_start->tv_nsec) {
			utemp = time_stop->tv_nsec - time_start->tv_nsec;    
			temp = time_stop->tv_sec - time_start->tv_sec;
		} else {
			utemp = time_stop->tv_nsec + 1000000000 - time_start->tv_nsec;       
			temp = time_stop->tv_sec - 1 - time_start->tv_sec;
		}
		if (temp >= 0 && utemp >= 0) {
			temp = (temp * 1000000000) + utemp;
        	} else {
		fprintf(stderr, "start time %ld.%ld is after stop time %ld.%ld\n", time_start->tv_sec, time_start->tv_nsec, time_stop->tv_sec, time_stop->tv_nsec);
			temp = -1;
		}
	} else {
		fprintf(stderr, "memory is garbaged?\n");
		temp = -1;
	}
       return temp / 1000;
       //return temp;
}

ARTS_Send_Pkt_Thread::ARTS_Send_Pkt_Thread() :OSThread()
{
    
    loop = ev_loop_new(EVBACKEND_EPOLL);
    bitrate =0;
    udp_packet_size = TS_PACKET_SIZE;
    regist_list = NULL;
    interval_time =1.0;
    memset(&Destaddr,0,sizeof(struct sockaddr_in));   
    memset(&Destaddr1,0,sizeof(struct sockaddr_in)); 
}

bool ARTS_Send_Pkt_Thread::Configure(char *ipqamHost,UInt16 port ,UInt32 bitrate, bool return_flag)
{
    bool configok =false;
    //if(return_flag == true)
   //     return true;
    if(ipqamHost!= NULL && port >0 )
    {      
        this->Destaddr.sin_family = AF_INET;
        this->Destaddr.sin_addr.s_addr = inet_addr(ipqamHost);
        this->Destaddr.sin_port = htons(port);
        
        this->Destaddr1.sin_family = AF_INET;
        this->Destaddr1.sin_addr.s_addr = inet_addr("226.1.1.14");
        this->Destaddr1.sin_port = htons(50188);
        
        configok = true;
    }
        
    if(bitrate >0)
    {
        this->bitrate = bitrate;
    }
   
    
    return configok;    
}

void ARTS_Send_Pkt_Thread::insert_regist_node(unsigned int callid)
{
    Regist_Node *p = new Regist_Node();
    Assert(p != NULL);
    p->callid  = callid;
    fprintf(stdout,"insert regist,callid:%d\n",callid);
    p->next = NULL;
    p->registed =0;
    Regist_Node *cur = this->regist_list;
    while(cur && cur->next)
    {
        cur = cur->next;
    }
    
    if(cur == NULL)
    {
        this->regist_list = p;
    }
    else
        cur->next =p;
}


void ARTS_Send_Pkt_Thread::die_regist_node(unsigned int callid)
{
    Regist_Node * p = this->regist_list;
    
    if(p== NULL)
    {
        return;
    }
    
    Regist_Node *prev = p;
    while(p)
    {
        if(p->callid == callid)
        {          
            break;
        }
        
        prev = p;
        p=p->next;
    }
    
    p->unused =1;
}

void ARTS_Send_Pkt_Thread::del_regist_node(unsigned int callid)
{
    Regist_Node * p = this->regist_list;
    
    if(p== NULL)
    {
        return;
    }
    
    Regist_Node *prev = p;
    while(p)
    {
        if(p->callid == callid)
        {
            fprintf(stdout,"get regist_node,callid:%d\n",callid);
            break;
        }
        
        prev = p;
        p=p->next;
        fprintf(stdout,"list regist_node, prev:%x,callid:%d,p:%x",prev,prev->callid,p);
        if(p != NULL)
        fprintf(stdout,"callid:%d\n",p->callid);
    }
    
    
    if( p == this->regist_list)
    {       
        this->regist_list =p->next;
         p->next= NULL;
    }else
    {
        if(p == NULL )
        {
            fprintf(stdout,"can not get registnode of callid:%d,prev->callid:%d\n",callid,prev->callid);
            return ;
        }
        prev->next = p->next;
        p->next = NULL;
    }
    
    delete p;
}


int  sendPacket(unsigned int callid,int bitrate, struct sockaddr_in *Destaddr,bool seek_flag,struct sockaddr_in *Destaddr1)
{
    
    int len =0,sent=0,pkt_size=0;
    int64_t total_byte = 0;
    unsigned long long int real_time =0,diff_byte =0;
    arts_session * sess = arts_session_find(callid);
    Assert(sess != NULL);
    custom_struct_t *cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
    Assert(cur_struct!= NULL);
    
    timeout_ev_t *sendEv= (timeout_ev_t *)(cur_struct->sendEv);
    
    if(cur_struct-> receive_pkts_thread == NULL && sess->rtp_packet_buffer_len == 0)
    {
       sendEv->completed == 1;
       printf("sendEv->completed == 1\n");
    }
        
    if(!sendEv->completed) 
    {    
        clock_gettime(CLOCK_MONOTONIC, &(sendEv->time_stop));
	    real_time= usecDiff(&(sendEv->time_stop), &(sendEv->time_start));
	    
	   qtss_printf("start send packet,sendEv->packet_time:%d\n",sendEv->packet_time);
	    
	    while (real_time* bitrate > sendEv->packet_time * 1000000)
	    { /* theorical bits against sent bits */
		         		    		    	    
		    len = get_packet(sendEv->send_buf,TCP_PACKET_SIZE,sess);
		    
		    total_byte  += len;		  
		    if(len < 0) {
		        fprintf(stderr, "ts file read error \n");
		        sendEv->completed = 1;
		        break;
		    } else if (len == 0) {
		        if(cur_struct->receive_pkts_thread ==NULL)
		        {
		            fprintf(stdout, "ts sent done\n");
	    	        sendEv->completed = 1;
	    	        return -1;
	    	    }else
	    	    {
	    	      break;
	    	    }
		    } else {
		       sent = sendto(sendEv->sockfd, sendEv->send_buf, len, 0, (struct sockaddr *)Destaddr, sizeof(struct sockaddr_in));
		        //sendto(sendEv->sockfd1, sendEv->send_buf, len, 0, (struct sockaddr *)Destaddr1, sizeof(struct sockaddr_in));
		        if(sent <= 0) {
			        perror("send(): error ");
			        sendEv->completed = 1;
			        break;
		        } else {
			        sendEv->packet_time += (len * 8);			        
		        }
		    }	   
		    		  
	    }	
	    
	    	    
	    if(seek_flag == true)
	    {
	        return sARTSSendPktThread->send_after_time; // zibo 100.xm 20,yulong 800
	    }    	    
	    
	   return 20;        
    }
    
    return -1;
}

static void
timeout_cb(EV_P_ ev_timer *w,int revents)
{   
       
    timeout_ev_t * sendEv = (timeout_ev_t*)w->data;
    Assert(sendEv != NULL);
    
    double nsec =0.0;
    int nsec_live =0;
    int nsec_ipqam = 0;
    int nsec_sendbitrate =0;
    
    //qtss_printf("Entry timeout_cb,cur_time:%"_64BITARG_"d\n",QTSS_Milliseconds());
    bool seek_flag = false;
  
    unsigned int callid = sendEv->callid;
    OSMutexLocker registListMutex(&sARTSSendPktThread->regist_list_Mutex);
    arts_session * sess = arts_session_find(callid);
    if(sess == NULL)
    {
        qtss_printf("this sess is close\n");
        return;
    }
    custom_struct_t *cur_struct = (custom_struct_t*)sess->darwin_custom_struct;
    

    
    if(cur_struct->live == true)
    {
            
        if(cur_struct->live_idel >=  STORE_INTERVAL*1000 || cur_struct->isIpqam == false)
        {
            memcached_st * memc= conn(sess->couchbase_host,sess->callid);
            if(memc != NULL)
            {
                set(memc,sess->channel_id,sess->callid,"1");
                release_conn(memc);
            }                       
        
            cur_struct->live_idel =0;
        }      
        
        nsec_live = STORE_INTERVAL*1000;
        
    }else if (cur_struct->sendBitrate == true)
    {   
            if(sess->rtp_mpeg2_bytes_sent >0)
            {
               report_bitrate(sess);
            }
            
            nsec_sendbitrate = SEND_BITRATE*1000;
    }
        
        
    if(cur_struct->isIpqam == true)
    {
    
        if(cur_struct!=NULL && cur_struct->seek == true && cur_struct->seek_start_dts >0 || cur_struct->pause_ready == true) 
        {
            clock_gettime(CLOCK_MONOTONIC, &(sendEv->time_start));
            sendEv->packet_time =0;
        
            if(cur_struct!=NULL && cur_struct->seek == true)
            {
                cur_struct->seek=false;
                seek_flag = true;
            }
        
            if(cur_struct->pause_ready == true)
            {
                cur_struct->pause_ready =false;
            }
        }  
    
         
        int sleep_time = sendPacket(callid,sARTSSendPktThread->bitrate,&(sARTSSendPktThread->Destaddr),seek_flag,&(sARTSSendPktThread->Destaddr1));
        if(sleep_time == -1)
        {
            return;
        }
        
        nsec_ipqam = (sleep_time-sARTSSendPktThread->send_ahead_time);   
    }
      
   int nsec_total = get_min(nsec_ipqam,nsec_live,nsec_sendbitrate);
  
    nsec = nsec_total/1000.0;
   
    if(sendEv->completed != 1)
    {
        ev_timer_stop(sARTSSendPktThread->loop,w);
        w->data = (void*)sendEv;
        ev_timer_init(w,timeout_cb,nsec,0);    
        ev_timer_start(sARTSSendPktThread->loop,w);
       
    }
    
     if(cur_struct->live == true)
     {
        cur_struct->live_idel += nsec_total;
        //qtss_printf("live_idel:%d\n",cur_struct->live_idel);
     }
  
    //qtss_printf("nsec_total:%d\n",nsec_total);
}


void ARTS_Send_Pkt_Thread::del_sendEv(unsigned int callid)
{
    arts_session * sess = arts_session_find(callid);
    Assert(sess!= NULL);
    
    custom_struct_t * cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
    Assert(cur_struct != NULL);
    
    timeout_ev_t * sendEv = (timeout_ev_t *)cur_struct->sendEv;
    if(sendEv!=NULL)
    {
        ev_timer_stop(sARTSSendPktThread->loop,&(sendEv->timeout_watcher));    
    }
    this->del_regist_node(callid);
}

void ARTS_Send_Pkt_Thread::del_event(unsigned int callid)
{
    arts_session * sess = arts_session_find(callid);
    Assert(sess!= NULL);
    
    custom_struct_t * cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
    Assert(cur_struct != NULL);
    
    timeout_ev_t * sendEv = (timeout_ev_t *)cur_struct->sendEv;
    Assert(sendEv != NULL);
    ev_timer_stop(sARTSSendPktThread->loop,&(sendEv->timeout_watcher));
    sendEv->pause=true;
}

void ARTS_Send_Pkt_Thread::playAgain( unsigned int callid)
{
    arts_session * sess = arts_session_find(callid);
    Assert(sess!= NULL);
    
    custom_struct_t * cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
    Assert(cur_struct != NULL);
    
    timeout_ev_t * sendEv = (timeout_ev_t *)cur_struct->sendEv;
    Assert(sendEv != NULL);

    int sleep_time = sendPacket(callid,sARTSSendPktThread->bitrate,&(sARTSSendPktThread->Destaddr),false,&(sARTSSendPktThread->Destaddr1));
    double nsec = sleep_time/1000.0;  
    ev_timer_init(&(sendEv->timeout_watcher),timeout_cb,nsec,0);    
    ev_timer_start(sARTSSendPktThread->loop,&(sendEv->timeout_watcher));
       
}





 void interval_cb( EV_P_ ev_timer *w,int revents)
{
    int nsec_ipqam = 0;
    int nsec_live =0;
    int nsec_sendbitrate = 0;
    int nsec_total =0;
    double nsec =0.0;
        
    if(sARTSSendPktThread->IsStopRequested() == false)
    {
        
        int flags =0;
        Regist_Node * p = sARTSSendPktThread->regist_list;
        Regist_Node *prev = p;
        //qtss_printf("regist_list:%x\n",p);
        while(p)
        {            
            arts_session * sess = arts_session_find (p->callid); 
            if(sess != NULL && p->registed ==0)
            {
                //
                custom_struct_t* cur_struct = (custom_struct_t *)sess->darwin_custom_struct;
                
                if(cur_struct != NULL && cur_struct->sendEv != NULL)
                {
                    timeout_ev_t * sendEv = (timeout_ev_t *)cur_struct->sendEv;
                    
                    if(cur_struct ->isIpqam == 1)
                    {
                        clock_gettime(CLOCK_MONOTONIC, &(sendEv->time_start));
                         int sleep_time = sendPacket(p->callid,sARTSSendPktThread->bitrate,&(sARTSSendPktThread->Destaddr),false,&(sARTSSendPktThread->Destaddr1));
                         nsec_ipqam = sleep_time;  
                    }       
                    
                    
                    if(cur_struct->live == true)
                    {
                         //qtss_printf("store data\n");
                         memcached_st * memc= conn(sess->couchbase_host,sess->callid);
                         if(memc != NULL)
                         {
                            set(memc,sess->channel_id,sess->callid,"1");
                            release_conn(memc);
                         }                       
                        
                        nsec_live = STORE_INTERVAL *1000;
                    }
                    
                    if (cur_struct->sendBitrate == true)
                    {
                        if(sess->rtp_mpeg2_bytes_sent >0)
                        {
                            report_bitrate(sess);
                        }
            
                        nsec_sendbitrate = SEND_BITRATE*1000;
                    }
                    
                    nsec_total = get_min(nsec_ipqam,nsec_live,nsec_sendbitrate);
                    nsec = nsec_total/1000.0;
                    
                    
                    sendEv->timeout_watcher.data = (void*)sendEv;
                    ev_timer_init(&(sendEv->timeout_watcher),timeout_cb,nsec,0);    
                    ev_timer_start(sARTSSendPktThread->loop,&(sendEv->timeout_watcher));                  
                    
                    qtss_printf("regist session sendPacket,cur_time:%"_64BITARG_"d\n,nsec_total:%d",QTSS_Milliseconds(),nsec_total);                   
                    p->registed =1;
                }
                             
            } 
            prev = p;           
            p = p->next;
        }      
    }
}



void ARTS_Send_Pkt_Thread::Entry()
{
    qtss_printf("start Entry ARTS_Send_pkt\n");
    
    ev_timer_init(&interval_watcher,interval_cb,interval_time,2.0);
    ev_timer_start(this->loop,&interval_watcher);    
    ev_run(this->loop,0);
    ev_loop_destroy(this->loop);
}


/*
void  ARTS_Send_Pkt_Thread::beActived()
{
    event_active(&interval, EV_TIMEOUT, 1);
    qtss_printf("event_active start\n");
}
*/

ARTS_Send_Pkt_Thread::~ARTS_Send_Pkt_Thread()
{
    if(this->loop != NULL)
        ev_loop_destroy(this->loop);
}



    
