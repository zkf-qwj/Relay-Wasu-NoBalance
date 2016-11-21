#include "ts-demux.h"
#include <assert.h>
#define TS_PACKET_SIZE          188
#define PAT_PID                 0x0000

 int64_t   handle_pkt_simple(MpegTSContext *ts, unsigned char *packet, int pkt_len,int64_t *pcr_val,int64_t *pts,bool need_get_pts)
{	
	int len, pid=-1, cc,afc, is_start, has_adaptation, has_payload;
	unsigned char *p, *p_end;	
	int random_access_flag=0;
	int pmt_flag=0;
	int i=0;
	int ret=0;
	int ada_len;
	int video_flag = 0;
	int64_t dts =-1;
	pid = AV_RB16(packet + 1) & 0x1fff;
	
	if(pid<0 || (ts->nb_prg<=0 &&pid !=0)){		
		return 0;
    }
    
	
	is_start = packet[1] & 0x40;
	afc = (packet[3] >> 4) & 3;
    if (afc == 0) /* reserved value */{			
			return 0;
	}			
	has_adaptation = afc & 2;
	has_payload = afc & 1;
	
	p = packet + 4;
	
	cc = (packet[3] & 0xf);
	if(pid != ts->pcr_pid)
	{
	    cc =-5;
	}
	if(has_adaptation)
	{
	    ada_len =p[0];
	}

	p_end = packet + TS_PACKET_SIZE;
	if (p >= p_end)
		return 0;  	
	
	if(is_session(ts,pid) ){		     
         
		for(i=0;i<ts->nb_prg;i++){
			if (pid==ts->prg[i].prg_id)
				pmt_flag=1;
		}
		if ( (pid==PAT_PID || pmt_flag) && (ts->nb_prg <=0 || ts->prg[0].nb_video_pids <=0 || ts->prg[0].nb_audio_pids <=0 )  ) {
			if (is_start) {
				/* pointer field present */
				
				len = *p++;
				if (p + len > p_end)
					return 0;
				if (len ) {
					/* write remaining section bytes */
					if ( (ret =demux_patpmt(ts,p,len,pmt_flag,packet,0))<0 )
					    return 0;			
				}
				p += len;				
				if (p < p_end) {
				    //has more than one pmt or pat in one ts-packet
				    if(( ret= demux_patpmt(ts,p,p_end-p,pmt_flag,packet,0))<0)
				    return 0;				                         
				}
			} else { // no start
			    if(( ret= demux_patpmt(ts,p,p_end-p,pmt_flag,packet,0))<0)
				    return 0;					
			}
		
		}  
	}	
	else
	{
	    is_start = packet[1] & 0x40;
	
		afc = (packet[3] >> 4) & 3;
		
		has_adaptation = afc & 2;
		has_payload = afc & 1;
		
		p = packet + 4;	
		
		int ada_len = p[0];	   
	         
		if (has_adaptation &&  ada_len >0 ) {
		    //printf("pid:%d,ts->pcr_pid:%d\n",pid,ts->pcr_pid);
		    if( pid  == ts->pcr_pid)
		    {
		        int64_t PCR=GetMPEG2PCR(packet,TS_PACKET_SIZE);	
	        	           
	            int64_t cur_pcr= PCR/27/1000;
	            if(PCR >=0 && pcr_val != NULL)
	            {
	                (*pcr_val) = cur_pcr;
	               // printf("cur_pcr:%u\n",cur_pcr);
	            }
		    }		  
		    	
		   random_access_flag = p[1]&0x40;	    		
			p += p[0] + 1;	
		}	
		
		 if( need_get_pts == true)
		 { 	
		    get_av_flag(ts,pid,&video_flag,NULL);	
	     }
		
		//if(is_start && random_access_flag && video_flag ==1){	
		if(is_start && video_flag == 1){	
		    if(pts!= NULL)
		         dts=pes_cb(ts, p, p_end - p,pts); 
		    
		}
		//printf("pid:%d,is_start:%d,video_flag:%d,packet[1]:%d,pts:%d\n",pid,is_start,video_flag,packet[1],*pts);
	}
  
    return dts;
}

