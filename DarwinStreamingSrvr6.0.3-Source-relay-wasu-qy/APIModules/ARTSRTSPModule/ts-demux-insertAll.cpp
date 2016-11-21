#include "ts-demux.h"

#include <assert.h>
#define DEBUG 0

#if DEBUG
#include "file.h"
#endif
//#include "util.h"

#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))
#define FFERRTAG(a, b, c, d) (-(int)MKTAG(a, b, c, d))
#define AVERROR_INVALIDDATA        FFERRTAG( 'I','N','D','A') ///< Invalid data found when processing input
#define TS_FEC_PACKET_SIZE 204
#define TS_DVHS_PACKET_SIZE 192
#define TS_PACKET_SIZE 188
#define TS_MAX_PACKET_SIZE 204

#define TS_TCP_PACKET_SIZE 188*7

#define ADAPTATION_FLAG      0x20 
#define MPEG2_DATA           0x40
#define PCR_FLAG             0x10
#define DISCONTINUITY_FLAG   0x80
#define MIN_PACK_LEN_FOR_PCR 12
#define MP2_PKTS_TO_READ     7

#define NB_PID_MAX 8192
#define MAX_SECTION_SIZE 4096
#define AV_NOPTS_VALUE          ((int64_t)UINT64_C(0x8000000000000000))
#define PCR_TIME_BASE 27000000

/* pids */
#define PAT_PID                 0x0000
#define SDT_PID                 0x0011
#define TDT_PID                 0x0014


#define PAT_TID   0x00
#define PMT_TID   0x02
#define SDT_TID   0x42
#define BAT_TID   0x4A
#define NIT_TID   0x40


#define  A_BR   188000
#define  V_BR   2820000

#define DEFAULT_PROVIDER_NAME   "Cnstream"
#define DEFAULT_SERVICE_NAME    "Service01"

static size_t max_alloc_size= INT_MAX;


const  int max_delay = 40; 

static int BCD[10]={0x0000,0x0001,0x0010,0x0011,0x0100,0x0101,0x0110,0x0111,0x1000,0x1001};
/*
enum AVRounding {
    AV_ROUND_ZERO     = 0, ///< Round toward zero.
    AV_ROUND_INF      = 1, ///< Round away from zero.
    AV_ROUND_DOWN     = 2, ///< Round toward -infinity.
    AV_ROUND_UP       = 3, ///< Round toward +infinity.
    AV_ROUND_NEAR_INF = 5, ///< Round to nearest and halfway cases away from zero.
    AV_ROUND_PASS_MINMAX = 8192, ///< Flag to pass INT64_MIN/MAX through instead of rescaling, this avoids special cases for AV_NOPTS_VALUE
};
*/

#define  AV_ROUND_ZERO  0
#define  AV_ROUND_INF   1
#define  AV_ROUND_DOWN  2
#define  AV_ROUND_UP    3
#define  AV_ROUND_NEAR_INF  5
#define  AV_ROUND_PASS_MINMAX  8192



static const StreamType ISO_types[] = {
    { 0x01, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_MPEG2VIDEO },
    { 0x02, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_MPEG2VIDEO },
    { 0x03, AVMEDIA_TYPE_AUDIO,        AV_CODEC_ID_MP3 },
    { 0x04, AVMEDIA_TYPE_AUDIO,        AV_CODEC_ID_MP3 },
    { 0x0f, AVMEDIA_TYPE_AUDIO,        AV_CODEC_ID_AAC },
    { 0x10, AVMEDIA_TYPE_VIDEO,      AV_CODEC_ID_MPEG4 },
    /* Makito encoder sets stream type 0x11 for AAC,
     * so auto-detect LOAS/LATM instead of hardcoding it. */
#if !CONFIG_LOAS_DEMUXER
    { 0x11, AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_AAC_LATM }, /* LATM syntax */
#endif
    { 0x1b, AVMEDIA_TYPE_VIDEO,       AV_CODEC_ID_H264 },
    { 0xd1, AVMEDIA_TYPE_VIDEO,      AV_CODEC_ID_DIRAC },
    { 0xea, AVMEDIA_TYPE_VIDEO,        AV_CODEC_ID_VC1 },
    { 0 },
};

static const StreamType HDMV_types[] = {
    { 0x80, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_PCM_BLURAY },
    { 0x81, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_AC3 },
    { 0x82, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_DTS },
    { 0x83, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_TRUEHD },
    { 0x84, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_EAC3 },
    { 0x85, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_DTS }, /* DTS HD */
    { 0x86, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_DTS }, /* DTS HD MASTER*/
    { 0xa1, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_EAC3 }, /* E-AC3 Secondary Audio */
    { 0xa2, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_DTS },  /* DTS Express Secondary Audio */
    { 0x90, AVMEDIA_TYPE_SUBTITLE, AV_CODEC_ID_HDMV_PGS_SUBTITLE },
    { 0 },
};

/* ATSC ? */
static const StreamType MISC_types[] = {
    { 0x81, AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_AC3 },
    { 0x8a, AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_DTS },
    { 0 },
};

static const StreamType REGD_types[] = {
    { MKTAG('d','r','a','c'), AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_DIRAC },
    { MKTAG('A','C','-','3'), AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_AC3 },
    { MKTAG('B','S','S','D'), AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_S302M },
    { MKTAG('D','T','S','1'), AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_DTS },
    { MKTAG('D','T','S','2'), AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_DTS },
    { MKTAG('D','T','S','3'), AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_DTS },
    { MKTAG('K','L','V','A'), AVMEDIA_TYPE_DATA,    AV_CODEC_ID_SMPTE_KLV },
    { MKTAG('V','C','-','1'), AVMEDIA_TYPE_VIDEO,   AV_CODEC_ID_VC1 },
    { 0 },
};

/* descriptor present */
static const StreamType DESC_types[] = {
    { 0x6a, AVMEDIA_TYPE_AUDIO,             AV_CODEC_ID_AC3 }, /* AC-3 descriptor */
    { 0x7a, AVMEDIA_TYPE_AUDIO,            AV_CODEC_ID_EAC3 }, /* E-AC-3 descriptor */
    { 0x7b, AVMEDIA_TYPE_AUDIO,             AV_CODEC_ID_DTS },
    { 0x56, AVMEDIA_TYPE_SUBTITLE, AV_CODEC_ID_DVB_TELETEXT },
    { 0x59, AVMEDIA_TYPE_SUBTITLE, AV_CODEC_ID_DVB_SUBTITLE }, /* subtitling descriptor */
    { 0 },
};


static int write_pkts(MpegTSContext *ts,unsigned char * packet,int len);
void init_sec(SectionHeader *sec,int tid,int tsid,int version,int sec_num,int last_sec_num);
static void modify_desc_len(uint8_t * len_ptr,uint8_t*data);



#if DEBUG

int insert_one_pkt(MpegTSContext *ts,unsigned int callid,unsigned char *buf,int buf_size,int64_t pcr);
void modify_count(MpegTSContext *ts);
#endif

void put16(uint8_t **q_ptr, int val)
{
    uint8_t *q;
    q = *q_ptr;
    *q++ = val >> 8;
    *q++ = val;
    *q_ptr = q;
}

static inline int get8(unsigned char **pp,  unsigned char *p_end)
{
    	unsigned char *p;
    	int c;

    	p = *pp;
    	if (p >= p_end)
        	return -1;
    	c = *p++;
    	*pp = p;
   	return c;
}

static inline int get16(unsigned char **pp, unsigned char *p_end)
{
          unsigned char *p;
    	int c;

    	p = *pp;
    	if ((p + 1) >= p_end)
       		return -1;
    	c = AV_RB16(p);
    	p += 2;
    	*pp = p;
    	return c;
}




void *av_malloc(size_t size)
{
    void *ptr = NULL;
#if CONFIG_MEMALIGN_HACK
    long diff;
#endif

    /* let's disallow possible ambiguous cases */
    if (size > (max_alloc_size - 32))
        return NULL;

#if CONFIG_MEMALIGN_HACK
    ptr = malloc(size + ALIGN);
    if (!ptr)
        return ptr;
    diff              = ((~(long)ptr)&(ALIGN - 1)) + 1;
    ptr               = (char *)ptr + diff;
    ((char *)ptr)[-1] = diff;
#elif HAVE_POSIX_MEMALIGN
    if (size) //OS X on SDK 10.6 has a broken posix_memalign implementation
    if (posix_memalign(&ptr, ALIGN, size))
        ptr = NULL;
#elif HAVE_ALIGNED_MALLOC
    ptr = _aligned_malloc(size, ALIGN);
#elif HAVE_MEMALIGN
#ifndef __DJGPP__
    ptr = memalign(ALIGN, size);
#else
    ptr = memalign(size, ALIGN);
#endif
    /* Why 64?
     * Indeed, we should align it:
     *   on  4 for 386
     *   on 16 for 486
     *   on 32 for 586, PPro - K6-III
     *   on 64 for K7 (maybe for P3 too).
     * Because L1 and L2 caches are aligned on those values.
     * But I don't want to code such logic here!
     */
    /* Why 32?
     * For AVX ASM. SSE / NEON needs only 16.
     * Why not larger? Because I did not see a difference in benchmarks ...
     */
    /* benchmarks with P3
     * memalign(64) + 1          3071, 3051, 3032
     * memalign(64) + 2          3051, 3032, 3041
     * memalign(64) + 4          2911, 2896, 2915
     * memalign(64) + 8          2545, 2554, 2550
     * memalign(64) + 16         2543, 2572, 2563
     * memalign(64) + 32         2546, 2545, 2571
     * memalign(64) + 64         2570, 2533, 2558
     *
     * BTW, malloc seems to do 8-byte alignment by default here.
     */
#else
    ptr = malloc(size);
#endif
    if(!ptr && !size) {
        size = 1;
        ptr= av_malloc(1);
    }
#if CONFIG_MEMORY_POISONING
    if (ptr)
        memset(ptr, 0x2a, size);
#endif
    return ptr;
}

void *av_mallocz(size_t size)
{
    void *ptr = av_malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

void *av_realloc(void *ptr, size_t size)
{
#if CONFIG_MEMALIGN_HACK
    int diff;
#endif

    /* let's disallow possible ambiguous cases */
    if (size > (max_alloc_size - 32))
        return NULL;

#if CONFIG_MEMALIGN_HACK
    //FIXME this isn't aligned correctly, though it probably isn't needed
    if (!ptr)
        return av_malloc(size);
    diff = ((char *)ptr)[-1];
    av_assert0(diff>0 && diff<=ALIGN);
    ptr = realloc((char *)ptr - diff, size + diff);
    if (ptr)
        ptr = (char *)ptr + diff;
    return ptr;
#elif HAVE_ALIGNED_MALLOC
    return _aligned_realloc(ptr, size + !size, ALIGN);
#else
    return realloc(ptr, size + !size);
#endif
}

/* add one element to a dynamic array */
void av_dynarray_add(void *tab_ptr, int *nb_ptr, void *elem)
{
    /* see similar ffmpeg.c:grow_array() */
    int nb, nb_alloc;
    intptr_t *tab;

    nb = *nb_ptr;
    tab = *(intptr_t**)tab_ptr;
    if ((nb & (nb - 1)) == 0) {
        if (nb == 0)
            nb_alloc = 1;
        else
            nb_alloc = nb * 2;
        tab = (intptr_t *)av_realloc(tab, nb_alloc * sizeof(intptr_t));
        *(intptr_t**)tab_ptr = tab;
    }
    tab[nb++] = (intptr_t)elem;
    *nb_ptr = nb;
}

char *av_strdup(const char *s)
{
    char *ptr = NULL;
    if (s) {
        int len = strlen(s) + 1;
        ptr =(char*) av_malloc(len);
        if (ptr)
            memcpy(ptr, s, len);
    }
    return ptr;
}

 void putstr8(uint8_t **q_ptr, const char *str)
{
    uint8_t *q;
    int len;

    q = *q_ptr;
    if (!str)
        len = 0;
    else
        len = strlen(str);
    *q++ = len;
    memcpy(q, str, len);
    q += len;
    *q_ptr = q;
}

void get_av_flag(MpegTSContext *ts,int pid,int *v_flag,int *a_flag)
{

    if(ts== NULL || ts->nb_prg<=0 || ts->prg[ts->nb_prg-1].nb_video_pids <=0 )
    {
        return;
    }
    
    if( v_flag == NULL && a_flag == NULL)
        return;
        
    int i,j;
    
    if(v_flag != NULL)
    {
        for(i=0;i<ts->nb_prg;i++)
        {       
            for(j=0;j<ts->prg[i].nb_video_pids;j++)
            {
                if(pid == ts->prg[i].video_pids[j])
                {                        
                    (*v_flag) = 1;                 
                    return ;
                } 
            }
        }
    }
    
    if(a_flag!= NULL)
    {
        for(i=0;i<ts->nb_prg;i++)
        {       
            for(j=0;j<ts->prg[i].nb_audio_pids;j++)
            {
                if(pid == ts->prg[i].audio_pids[j])
                {                        
                    (*v_flag) = 1;                 
                    return ;
                } 
            }
        }
    }
    
    
}
int is_session(MpegTSContext *ts,int pid)
{
	int i=0;
	if(pid==PAT_PID || pid==SDT_PID)
		return 1;
	//fprintf(stdout,"ts->nb_prg:%d\n",ts->nb_prg);
	for(i=0;i<ts->nb_prg;i++){
		if(pid == ts->prg[i].prg_id)
			return 1;
	}
	return 0;
}

static int64_t getMJD(int year,int mon,int day)
{
    int l=0;
    if (mon == 1 || mon ==2)
    l=1;
     
    int64_t mjd = 14956 + day + (int)((year-l)*365.25) + (int)((mon +1+l*12)*30.6001);  
    return mjd;
}


static int parse_section_header(SectionHeader *h,
                                unsigned char **pp,  unsigned char *p_end)
{
    	int val;

   	 val = get8(pp, p_end);
    	if (val < 0)
       		return -1;
    	h->tid = val;  //table_id

    	*pp += 2;
    	val = get16(pp, p_end);
    	if (val < 0)
        	return -1;
    	h->id = val;//transport_stream_id

    	val = get8(pp, p_end);
    	if (val < 0)
        	return -1;
    	h->version = (val >> 1) & 0x1f;

    	val = get8(pp, p_end);
    	if (val < 0)
        	return -1;
    	h->sec_num = val;

   	 val = get8(pp, p_end);
    	if (val < 0)
        	return -1;
    	h->last_sec_num = val;
    	return 0;
}



void *grow_array(void *array, int elem_size, int *size, int new_size)
{
	if (new_size >=  INT_MAX / elem_size) {
		fprintf(stderr, "Array too big.\n");
		exit(1);
	}

	if (*size < new_size) {
		unsigned char *old=(unsigned char *)array;
		unsigned char *tmp  = (unsigned char*) malloc(new_size*elem_size);
		if (!tmp) {
			fprintf(stderr, "Could not alloc buffer.\n");
			exit(1);
		}
		memcpy(tmp,old,(*size)*elem_size);
		memset(tmp + *size*elem_size, 0, (new_size-*size) * elem_size);
		*size  =  new_size;
		free(old);
		return tmp;
	}
    
	return array;
}





int get_maxmin_pid(MpegTSContext *ts,int64_t*max_stream_pid,int64_t *min_stream_pid)
{
    int64_t max_pid =0,min_pid= 0xfffffff;
    int i,j;
    if(ts == NULL )
        return 0;
    for(i=0;i<ts->nb_prg;i++){
        for(j=0;j<ts->prg[i].nb_video_pids;j++) {
            if(ts->prg[i].video_pids[j] >max_pid)
            {
                max_pid = ts->prg[i].video_pids[j];
            }
            
            if(ts->prg[i].video_pids[j] <min_pid)
            {
                min_pid = ts->prg[i].video_pids[j];
            }          
            
        }       
    }
    
    if(max_pid <= 0xffffffff || min_pid >= 0)
    {
        (*max_stream_pid) = max_pid;
        (*min_stream_pid) = min_pid;
    }
    
    
    return 0;
}

static int modify_pcr_pid(MpegTSContext *ts,int64_t new_pcr_pid,unsigned char *section,int len,unsigned char *pcr_ptr,int64_t old_pcr_pid)
{
    if(new_pcr_pid>0 || ts->pcr_pid >0){
	    if(ts->pcr_pid >0)
	        new_pcr_pid = ts->pcr_pid;
        //fprintf(stdout,"new_pcr:%d\n",new_pcr_pid);        
	    put16(&pcr_ptr,old_pcr_pid&0xe000 | new_pcr_pid&0x1fff);	    
	    ts->pcr_pid = new_pcr_pid >0? new_pcr_pid: ts->pcr_pid;
	}

    uint32_t crc = av_bswap32(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, section, len - 4));
    section[len - 4] = (crc >> 24) & 0xff;
    section[len - 3] = (crc >> 16) & 0xff;
    section[len - 2] = (crc >> 8) & 0xff;
    section[len - 1] = (crc) & 0xff;
    
    return 0;
}


int get_bcd( int sec)
{
    if(sec >100)
        return -1;
    int first = sec/10;
    int second = sec%10;
    
    int res = (first <<4) + (second&0xf);
    return res;
}


 void mpegts_write_tdt(MpegTSContext *ts, unsigned int callid,uint8_t * pkt)
{
   unsigned char buf[188];
   buf[0]= 0x47;
   buf[1]= 0x40;
   buf[2]= 0x14;
   ts->tdt.count += 1;
   
   buf[3]= 0x10 | (ts->tdt.count & 0xf) ;
   buf[4]=0x00;
   buf[5]=0x70;
   buf[6]= 0x70;
   buf[7]= 0x05;
   unsigned char * p =buf+8;
   put16(&p,getMJD(114,6,13));
   char  tmp[8];
   //sprintf(tmp,"%d%d%d",ts->tdt.hour,ts->tdt.min,ts->tdt.sec);
   //memcpy(p,tmp,6);
   //p += 6;
   *p ++ = 0x17;
   *p++ = 0x10;
   *p++ = get_bcd(ts->tdt.sec);
   
   
   ts->tdt.sec += 2;
   if(ts->tdt.sec == 60)
   {
      ts->tdt.min ++;
      ts->tdt.sec =0;
   }
   
   if(ts->tdt.min ==60)
   {
      ts->tdt.hour ++;
      ts->tdt.min =0;
   }
   
   
   memset(p,0xff,188 -(p-buf));
   if(pkt!= NULL)
   {
        memcpy(pkt,buf,TS_PACKET_SIZE);
   }else
      insert_one_pkt(ts,callid,buf,TS_PACKET_SIZE,-1);
}
static int mpegts_find_stream_type( uint32_t stream_type, const StreamType *types,StreamType **target_type)
{
	(*target_type)->codec_type=AVMEDIA_TYPE_UNKNOWN;
	(*target_type)->codec_id= AV_CODEC_ID_NONE;
	for (; types->stream_type; types++) {
		if (stream_type == types->stream_type) {
			(*target_type)->codec_type = types->codec_type;
			(*target_type)->codec_id   = types->codec_id;           
			return 0;
		}
	}
}
static void init_codec(enum AVMediaType codec_type, enum AVCodecID codec_id,char *codec_type_name,char *codec_id_name,StreamType **target_type){
	(*target_type)->codec_type=codec_type;
	(*target_type)->codec_id=codec_id;
	(*target_type)->codec_type_name=codec_type_name;
	(*target_type)->codec_id_name=codec_id_name;
}

static int quick_find_codec_info(uint32_t stream_type,StreamType *target_type)
{
	(target_type)->codec_type=AVMEDIA_TYPE_UNKNOWN;
	(target_type)->codec_id= AV_CODEC_ID_NONE;
	switch (stream_type){
		case 0x01:			
			init_codec(AVMEDIA_TYPE_VIDEO,AV_CODEC_ID_MPEG1VIDEO,
					"video","mpeg1video",&target_type);
			break;
		case 0x02:
			init_codec(AVMEDIA_TYPE_VIDEO,AV_CODEC_ID_MPEG2VIDEO,
					"video","mpeg2video",&target_type);
			break;
		case 0x1b:
			init_codec(AVMEDIA_TYPE_VIDEO,AV_CODEC_ID_H264,
					"video","h264",&target_type);
			break;
		case 0xea:	
			init_codec(AVMEDIA_TYPE_VIDEO,AV_CODEC_ID_VC1,
					"video","vc1",&target_type);
			break;
		//audio
		case 0x03:
			//mpeg1audio
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_MP3,
					"audio","mp3",&target_type);
			break;
		case 0x04://mpeg2audio			
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_MP3,
					"audio","mp2a",&target_type);
			break;
		case 0x0f:			
		case 0x11:
			
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_AAC,
					"audio","aac",&target_type);
			break;
//LPCM none
		case 0x80:
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_NONE,
					"audio","LPCM",&target_type);			
			break;

		case 0x06:
		case 0x81:
		init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_AC3,
					"audio","ac3",&target_type);
		
			break;
		case 0x82:
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_DTS,
					"audio","dts",&target_type);
			
			break;
		case 0x83:
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_NONE,
					"audio","dolby trueHD",&target_type);
			
			break;
		case 0x84:
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_AC3,
					"audio","ac3-plus",&target_type);
			
			break;
		case 0x85:
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_DTS,
					"audio","dts-hd",&target_type);
			break;
		case 0x86:
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_DTS,
					"video","dts-ma",&target_type);
			break;
		case 0xa1:
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_AC3,
					"audio","ac3-plus_SEC",&target_type);
			break;
		case 0xa2:
			init_codec(AVMEDIA_TYPE_AUDIO,AV_CODEC_ID_DTS,
					"audio","dts_hd_SEC",&target_type);
			break;
		case 0x90:
	
			init_codec(AVMEDIA_TYPE_SUBTITLE,AV_CODEC_ID_HDMV_PGS_SUBTITLE,
					"subtitle","pgs",&target_type);	
			break;	
		case 0x92:
			init_codec(AVMEDIA_TYPE_SUBTITLE,AV_CODEC_ID_TEXT,
					"subtitle","text",&target_type);
			break;	
		default:
			init_codec(AVMEDIA_TYPE_UNKNOWN, AV_CODEC_ID_NONE,NULL,NULL,&target_type);
	}
	
}

static int mpegts_find_stream_info(uint32_t stream_type,StreamType *target_type)
{
	enum AVMediaType codec_type;
	enum AVCodecID codec_id= AV_CODEC_ID_NONE;
	mpegts_find_stream_type( stream_type, ISO_types,&target_type);
   
	if(target_type->codec_id == AV_CODEC_ID_NONE)   
		mpegts_find_stream_type(stream_type, HDMV_types,&target_type);
		
	if (stream_type == 0x83) {
            // HDMV TrueHD streams also contain an AC3 coded version of the
            // audio track - add a second stream for this           
           target_type-> codec_type= AVMEDIA_TYPE_AUDIO;
           target_type->codec_id   = AV_CODEC_ID_AC3;          
        }   
	if (target_type->codec_id == AV_CODEC_ID_NONE)
         mpegts_find_stream_type(stream_type, MISC_types,&target_type);
   
	
	return 0;
}



static int pmt_cb(MpegTSContext *ts, unsigned char *section, int section_len,int demux_flag,unsigned char * pkt_start)
{    
	SectionHeader h1, *h = &h1;   
	 unsigned char *p, *p_end, *desc_list_end,*pcr_ptr;
	int program_info_length, pcr_pid, pid, stream_type,old_pcr_pid;
	int desc_list_len =0;
	int64_t new_pcr_pid=0;
	
	
	int i,j;   
	
	
	
	int len = (AV_RB16(section + 1) & 0xfff) + 3;
	 
	if(len<0) 
	{
	    fprintf(stdout,"invalidate len\n");
	    return -1;
	}
	
	p_end = section + len - 4;
	p = section;
	if (parse_section_header(h, &p, p_end) < 0)
	{
        fprintf(stdout,"parse_section_header failed\n");
        return -1;
    }
      

	if (h->tid != PMT_TID)
	{
	    fprintf(stdout,"h->tid:%d\n",h->tid);
		return -1;
    }
    
    pcr_ptr =p;
     
	pcr_pid = get16(&p, p_end);
	
	if (pcr_pid < 0)
	{
        fprintf(stdout,"pcr_pid <0\n");
		return -1;
    }
		
    old_pcr_pid = pcr_pid;
	pcr_pid &= 0x1fff;  
    //fprintf(stdout,"LOG:old_pcr_pid:%x,pcr_pid=%d\n",old_pcr_pid,pcr_pid);
    
    if(ts->pcr_pid >0  )
    {
        if(ts->isIpqam == true)
        {
            return modify_pcr_pid(ts,0,section,len,pcr_ptr,old_pcr_pid);
         }
        else return 0;
    }
       
        

	program_info_length = get16(&p, p_end);
	if (program_info_length < 0)
		return 0;
	program_info_length &= 0xfff;
	
	p += program_info_length;
	if (p >= p_end)
	{
	    fprintf(stdout,"p>= p_end\n");
		return -1;
     }

   
	for(;;) {     
		
		stream_type = get8(&p, p_end);
		if (stream_type < 0){		   
			break;
		}
		pid = get16(&p, p_end);
		if (pid < 0)
		break;
		
		pid &= 0x1fff;  
		
		StreamType target;	
		quick_find_codec_info(stream_type,&target);
		
		if(target.codec_type==AVMEDIA_TYPE_UNKNOWN &&target.codec_type_name==NULL){
			mpegts_find_stream_info(stream_type,&target);	
		}
		if(target.codec_type==AVMEDIA_TYPE_VIDEO){
			for(i=0;i<ts->nb_prg;i++){
			    if(ts->prg[i].prg_no == h->id){
				    ts->prg[i].video_pids[ts->prg[i].nb_video_pids]=pid;
				    ts->prg[i].pcr_pid=pcr_pid;
				    ts->prg[i].nb_video_pids++;
				    int v_index=ts->prg[i].nb_video_pids;
				    ts->prg[i].video_info[v_index-1].codec_id=target.codec_id;
				    ts->prg[i].video_info[v_index-1].codec_name=target.codec_id_name;
				  
				}
			}				
			
		}else if(target.codec_type==AVMEDIA_TYPE_AUDIO){
			for(i=0;i<ts->nb_prg;i++){
			    if(ts->prg[i].prg_no == h->id){
				    ts->prg[i].audio_pids[ts->prg[i].nb_audio_pids]=pid;				
				    ts->prg[i].nb_audio_pids++;
				    ts->prg[i].audio_info[ts->prg[i].nb_audio_pids-1].codec_id=target.codec_id;
				    ts->prg[i].audio_info[ts->prg[i].nb_audio_pids-1].codec_name=target.codec_id_name;
				  
				}
			}
			
		}
     
       
       /*
		for(i=0;i<ts->nb_prg;i++){
			if(ts->prg[i].prg_no == h->id){
			
			        
			        for (j=0;j<ts->prg[i].nb_video_pids;j++)
			        {
			            if (ts->prg[i].video_pids[j] == pid)
			            {
			               return modify_pcr_pid(ts,0,section,len,pcr_ptr,old_pcr_pid);
			            }	            
			        }
				    ts->prg[i].video_pids[ts->prg[i].nb_video_pids]=pid;				    
				    ts->prg[i].nb_video_pids++;
				    fprintf(stdout,"pid:%d\n",pid);
				    
		        }
			}				
			*/
		
		desc_list_len = get16(&p, p_end);
		if (desc_list_len < 0)
			break;
		desc_list_len &= 0xfff;
		desc_list_end = p + desc_list_len;
		if (desc_list_end > p_end)
			break;    
		
        p = desc_list_end;
    }

           
    int64_t max_pid=0 ,min_pid=0;
    get_maxmin_pid(ts,&max_pid,&min_pid);
	
    if(max_pid >0 && min_pid >0 )
    {
        if(max_pid +1 <0xffff)
        {
            new_pcr_pid = max_pid+1;
        }
        else if(min_pid -1 >0)
        {
            new_pcr_pid = min_pid-1;
        }  
    }
	if(ts->isIpqam == true)
	{
	    ts->pcr_pid = new_pcr_pid;
        modify_pcr_pid(ts,new_pcr_pid,section,len,pcr_ptr,old_pcr_pid);
    }else
    {
        if(pcr_pid >0 && ts->pcr_pid <=0)
           ts->pcr_pid = pcr_pid;
    }	
   	
    return 0;
}

static void add_pat_entry(MpegTSContext *ts, unsigned int programid,unsigned int program_no)
{
    struct Program *p;
    void *tmp=malloc((ts->nb_prg+1)*sizeof( Program));    
    if(!tmp)
       		return;
	memcpy(tmp,ts->prg,(ts->nb_prg)*sizeof(Program));
	if(ts->prg)
		free(ts->prg);

    ts->prg = (Program *)tmp;
    p = &ts->prg[ts->nb_prg];
	memset(p,0,sizeof( Program));
    p->prg_id = programid;
    p->prg_no = program_no;
    p->nb_video_pids = 0;  
    p->nb_audio_pids=0;    
  	ts->nb_prg++;	
}

MpegTSService *mpegts_add_service(MpegTSContext *ts,
                                         int sid,
                                         const char *provider_name,
                                         const char *name)
{
    MpegTSService *service;

    service =(MpegTSService *) av_mallocz(sizeof(MpegTSService));
    if (!service)
        return NULL;
    
    service->sid = sid;
    service->provider_name = av_strdup(provider_name);
    service->name = av_strdup(name);
    
    ts->services[ts->nb_services]=service;
    ts->nb_services ++;
    
    return service;
}


void delete_sevice(MpegTSContext *ts)
{
    if(ts != NULL)
    {
        if(ts->services != NULL)
        {   
            int i=0;
            for(i=0;i<ts->nb_services;i++)
            {
                MpegTSService *service =ts->services[i];
                if(service !=NULL)
                {
                    if(service->name != NULL)
                    {
                        free(service->name);
                        service->name = NULL;
                    }
                    
                    if(service->provider_name!= NULL)
                    {
                        free(service->provider_name);
                        service->provider_name = NULL;
                    }
                    
                    free(service);
                }
            }
        }
    }
    
   
    ts->nb_services= 0;
}

//write TS Header and crc
static void mpegts_write_section(MpegTSContext *ts,MpegTSSection *s, uint8_t *buf, int len,int callid,uint8_t * pkt)
{
    unsigned int crc;
    unsigned char packet[TS_PACKET_SIZE];
    const unsigned char *buf_ptr;
    unsigned char *q;
    int first, b, len1, left;

    crc = av_bswap32(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, buf, len - 4));
    buf[len - 4] = (crc >> 24) & 0xff;
    buf[len - 3] = (crc >> 16) & 0xff;
    buf[len - 2] = (crc >> 8) & 0xff;
    buf[len - 1] = (crc) & 0xff;

    /* send each packet */
    buf_ptr = buf;
    while (len > 0) {
        first = (buf == buf_ptr);
        q = packet;
        *q++ = 0x47;//start indicator
        b = (s->pid >> 8);
        if (first)
            b |= 0x40;
        *q++ = b; //is random_access
        *q++ = s->pid;//pid
        s->cc = (s->cc + 1) & 0xf;
        *q++ = 0x10 | s->cc; //counter
        if (first)
            *q++ = 0; /* 0 offset *///first_section
        len1 = TS_PACKET_SIZE - (q - packet);
        if (len1 > len)
            len1 = len;
        memcpy(q, buf_ptr, len1);
        q += len1;
        /* add known padding data */
        left = TS_PACKET_SIZE - (q - packet);
        if (left > 0)
            memset(q, 0xff, left);
     
        if(pkt!=NULL)
        {
            memcpy(pkt,packet,TS_PACKET_SIZE);
        }
       
       else insert_one_pkt(ts,callid,packet,TS_PACKET_SIZE,-1);

        buf_ptr += len1;
        len -= len1;
    }
}


static int mpegts_write_section1(MpegTSContext *ts,MpegTSSection *s,uint8_t *buf,int len, unsigned int callid,uint8_t *pkt)
{
    uint8_t section[1024], *q;
    unsigned int tot_len;
    int tid = s->sec_header.tid;
    int id = s->sec_header.id;
    int version = s->sec_header.version;
    int sec_num = s->sec_header.sec_num;
    int last_sec_num = s->sec_header.last_sec_num;
    /* reserved_future_use field must be set to 1 for SDT */
    //unsigned int flags = tid == SDT_TID ? 0xf000 : 0xb000;
   unsigned int flags = 0xf000;
    tot_len = 3 + 5 + len + 4;
    /* check if not too big */
    if (tot_len > 1024)
        return AVERROR_INVALIDDATA;

    q = section;
    *q++ = tid;
    put16(&q, flags | (len + 5 + 4)); /* 5 byte header + 4 byte CRC */
    put16(&q, id);//transport_stream_id
    *q++ = 0xc1 | (version << 1); /* current_next_indicator = 1 */
    *q++ = sec_num;
    *q++ = last_sec_num;
    memcpy(q, buf, len);

    mpegts_write_section(ts,s, section, tot_len,callid,pkt);
    return 0;
}

//write section table
 void mpegts_write_sdt(MpegTSContext *ts, unsigned int callid,uint8_t * pkt)
{
   //printf("---write_sdt-");
    uint8_t data[1012], *q, *desc_list_len_ptr, *desc_len_ptr;
    int i, running_status, free_ca_mode, val;
    MpegTSService *service = NULL;
    q = data;
   
   
    put16(&q, ts->onid);
    *q++ = 0xff;
    for(i = 0; i < ts->nb_services; i++) {
        service = ts->services[i];
        
        put16(&q, service->sid);
        *q++ = 0xff | 0x00; /* currently no EIT info */
        desc_list_len_ptr = q;
        q += 2;
        running_status = 4; /* running */
        free_ca_mode = 0;

        /* write only one descriptor for the service name and provider */
        *q++ = 0x48;
        desc_len_ptr = q;
        q++;
        *q++ = 0x01; /* digital television service */
        putstr8(&q, service->provider_name);
        putstr8(&q, service->name);
        desc_len_ptr[0] = q - desc_len_ptr - 1;

        /* fill descriptor length */
        val = (running_status << 13) | (free_ca_mode << 12) |
            (q - desc_list_len_ptr - 2);
        desc_list_len_ptr[0] = val >> 8;
        desc_list_len_ptr[1] = val;
    }
          
    ts->sdt.cc =ts->sdt_bat_count;  
   
    mpegts_write_section1(ts,&ts->sdt,data, q - data,callid,pkt);
    ts->sdt_bat_count =(ts->sdt_bat_count+1) &0xf;
}
void write_Linkage_desc(int tag,uint8_t **packet,int tsid,int onid,int sid)
{
    uint8_t * p = NULL;
    p = *packet;
    *p++=tag;
    *p++=07;
    put16(&p,tsid);//transport_stream_id
    put16(&p,onid);//orignal_network_id
    put16(&p,sid);//server_id
    *p++=160; //linktype
    *packet = p;
}

void write_desc(int tag,uint8_t *data,int len,uint8_t **packet)
{
   uint8_t *p = NULL;
    p = *packet;
    *p ++ = tag;
    *p ++  = len;
    memcpy(p,data,len); 
    p+= len;   
    *packet = p;
}

void write_ts_desc(uint8_t ** packet,int tsid,int onid)
{
    uint8_t *p = NULL;
    p = *packet;
   
    put16(&p,tsid);//transport_stream_id
    put16(&p,onid);//orignal_network_id
    uint8_t *ts_desc_len_ptr = p;
    p += 2;
        
    // frequency 0x03070000
    //symbol rate  0x006875
    //FEC_inner 0x05
    uint8_t frequency_desc[]={0x03,0x07,0x00,0x00,0xFF,0xF2,0x03,0x00,0x68,0x75,0x05};
    write_desc(0x44,frequency_desc,sizeof(frequency_desc),&p);
    
   
    modify_desc_len(ts_desc_len_ptr,p);
  
    *packet = p;
}

void delete_ts_prg(MpegTSContext *ts)
{
    if(ts== NULL)  return;
    int i=0;
    if(ts->prg!=NULL){
        free(ts->prg);
        ts->prg == NULL;
    }
    ts->nb_prg =0;
}




void mpegts_write_nit(MpegTSContext *ts, unsigned int callid,uint8_t * pkt)
 {
    //printf("--mpets_write_nit--\n");
    uint8_t data[1012], *q, *desc_list_len_ptr, *desc_len_ptr;
    int i, running_status, free_ca_mode, val;
    MpegTSService *service = NULL;
    q = data;
   
    uint8_t *n_desc_len_ptr = q;
    q += 2;
   
        
   
    char cdata[9] ="Cnstream";
    write_desc(0x40,(uint8_t*)cdata,strlen(cdata),&q);
    
   
    uint8_t  tmp_buf[29]={0x63,0x68,0x69,0x08,0xB8,0xE8,0xBB,0xAA,
                               0xD3,0xD0,0xcf,0xDe,0x65,0x6E,0x67,0x0D,0x67,0x65,
                               0x68,0x75,0x61,0x20,0x63,0x75,0x6c,0x74,0x75,0x72,
                               0x65};
   // write_desc(0x5B,tmp_buf,sizeof(tmp_buf),&q);
    write_Linkage_desc(0x4A,&q,ts->tsid,ts->onid,ts->service_id);
    modify_desc_len(n_desc_len_ptr,q);
    uint8_t * ts_loop_len_ptr = q;
    q += 2;    
    
    write_ts_desc(&q,ts->tsid,ts->onid);  
    modify_desc_len(ts_loop_len_ptr,q);
    
    
      
    //nit-table-id 0x40,transport_stream_id 0x01
    mpegts_write_section1(ts,&ts->nit,data, q - data,callid,pkt);
 
 }
 
void modify_desc_len(uint8_t *desc_len_ptr,uint8_t * data_ptr)
{
   int len = data_ptr - desc_len_ptr -2;
   int64_t val = 0xf000 | len;
   desc_len_ptr[0]= val>>8;
   desc_len_ptr[1]= val;
}

 
 void mpegts_write_bat(MpegTSContext *ts,unsigned int callid,uint8_t * pkt)
 {
   
    if(ts->nb_services <=0)
        return ;
        
    //printf("--write_bat--\n");
    uint8_t data[1012], *q; 
    int64_t val; 
    memset(data,0xff,sizeof(data)) ;
    q = data;    
   // bouquet_descriptors
    uint8_t* bq_decs_len_ptr = q;
    q+=2;   
    char cdata[9]="Cnstream";    
    //char cdata[9]="PRIVBAT";
    write_desc(0x47,(uint8_t *)cdata,strlen(cdata),&q)  ;   
    modify_desc_len(bq_decs_len_ptr,q); 
   
    uint8_t *ts_loop_len_ptr = q;//transport_stream_loop_len
    q+=2;
   
    //tsid 03
    put16(&q,ts->tsid);
    put16(&q,ts->onid); 
    uint8_t *ts_desc_len_ptr = q;
    q+= 2;      
    
    
    *q++=0x41;//des_tag
    *q++=0x03;//des_len    
   
    put16(&q,ts->service_id);//server_id
   *q++=ts->services[ts->nb_services-1]->type;//service_type 
   
    *q++ = 0x88;
    *q++ = 0x23;
    put16(&q,ts->service_id);
    
    uint8_t tag_88[]={0x00,0x00,0x1e,0x04,0x88,0x00,
                               0x00,0x2a,0x03,0x7a,0x00,0x00,0x44,0x03,0x2a,0x00,
                               0x00,0x48,0x04,0x7e,0x00,0x00,0x49,0x04,0x92,0x00,
                               0x00,0x6b,0x04,0x74,0x00,0x00,0x6e};
                               
    uint8_t tag_89[] = {0x89,0x1c,0x04,0x6a,0x01,0x11,0x04,0x88,0x03,0x19,0x03,0x7a,0x01,
                               0x10,0x03,0x2a,0x01,0x10,0x04,0x7e,0x03,0x10,0x04,
                               0x92,0x03,0x19,0x04,0x74,0x03,0x10};
    /*
    unsigned char tmp_buf[67]={0x88,0x23,0x04,0x6a,0x00,0x00,0x1e,0x04,0x88,0x00,
                               0x00,0x2a,0x03,0x7a,0x00,0x00,0x44,0x03,0x2a,0x00,
                               0x00,0x48,0x04,0x7e,0x00,0x00,0x49,0x04,0x92,0x00,
                               0x00,0x6b,0x04,0x74,0x00,0x00,0x6e,0x89,0x1c,0x04,
                               0x6a,0x01,0x11,0x04,0x88,0x03,0x19,0x03,0x7a,0x01,
                               0x10,0x03,0x2a,0x01,0x10,0x04,0x7e,0x03,0x10,0x04,
                               0x92,0x03,0x19,0x04,0x74,0x03,0x10};
                               */
  // memcpy(q,tmp_buf,sizeof(tmp_buf));
   memcpy(q,tag_88,sizeof(tag_88));
   q+= sizeof(tag_88);
   
   
    modify_desc_len(ts_desc_len_ptr,q);
    modify_desc_len(ts_loop_len_ptr,q);
  
   ts->bat.pid = 0x11;
    ts->bat.cc = ts->sdt_bat_count;
    //ts->bat->sec_header->id --> bouquet_id 0x6100
    mpegts_write_section1(ts,&ts->bat,data,q - data,callid,pkt);
    
    ts->sdt_bat_count =(ts->sdt_bat_count+1) &0xf;
    
 
 }

static int pat_cb(MpegTSContext *ts,  unsigned char *section, int section_len,int copy_pat_flag)
{
    if(ts->pat_copy == 1)
        return 0;
        
	SectionHeader h1, *h = &h1;
	uint8_t *p, *p_end;
	int sid, pmt_pid;   
	int crc_valid=1;
    int nit_flag =0;
	
	uint8_t *sec_len_ptr = section +1;
	
	int len = (AV_RB16(section + 1) & 0xfff) + 3;
	if(len<0){
	    fprintf(stdout,"pat len <0\n");
	    return -1;
	 }
	
	
	uint8_t *pat_str =ts->pat_pkt + ts->pat_pkt_len;
	
	p_end = section +len - 4;
	p = section;
	if (parse_section_header(h, &p, p_end) < 0)
	{
	    fprintf(stderr,"pat parse_section_heade failed\n");
		return -1;
	}   

	if (h->tid != PAT_TID)
	{   
	    fprintf(stderr,"pat h->tid:%d\n");
		return -1; 
	}
	if(section[4]!= 0x01)
	{
	    section[4]= 0x01;	
        ts->tsid = 0x01;
    }else
        ts->tsid = h->id;
   
    for(;;) {
       		sid = get16(&p, p_end);
        	if (sid < 0)
            		break;
        	pmt_pid = get16(&p, p_end);
        	if (pmt_pid < 0)
            		break;
        	pmt_pid &= 0x1fff;
        	
        	if(ts->cur_pid <=0)
                ts->cur_pid = pmt_pid;
            else
                if(ts->cur_pid == pmt_pid)
                    break;
            
		   //fprintf(stdout,"LOG:program_No=0x%x pid=0x%x\n",sid,pmt_pid);

        	if (sid == 0x0000) {
            	uint8_t *tmp = p-1;
            	if(tmp[0]!= 0x10)
                {     	
            	    tmp[0]=0x10;
            	}
            	nit_flag =1;
        	} else {  
        	    if(ts->prg_num >=0)
        	    {
        	        uint8_t *tmp = p-4;
        	        put16(&tmp,ts->prg_num);    
        	    }    	    
        	    
        	    ts->service_id = sid;       	     
	    		add_pat_entry(ts,pmt_pid,sid);  	    
        	}
    }
    
    
    //orig-stream has no nit,we fill it
    if(nit_flag == 0 && copy_pat_flag ==1)
    {
        put16(&p,0x0000);
        put16(&p,0x0010);
        p += 4;//4bit-crc;
        modify_desc_len(sec_len_ptr,p);
        len += 4;
    }
    
    if(ts->pat_copy == 0 && copy_pat_flag ==1)
    {
    
        memcpy(ts->pat_pkt + ts->pat_pkt_len,section,len -4);
        int new_len = ts->pat_pkt_len +len;
        
        uint32_t crc = av_bswap32(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, ts->pat_pkt + 5, new_len -5-4));
        ts->pat_pkt[new_len-4] = (crc >> 24) & 0xff;
        ts->pat_pkt[new_len-3 ]= (crc >> 16) & 0xff;
        ts->pat_pkt[new_len-2 ]= (crc >> 8) & 0xff;
        ts->pat_pkt[new_len-1]= (crc) & 0xff;
        ts->pat_pkt_len = new_len;
        memset(ts->pat_pkt + ts->pat_pkt_len,0xff,TS_PACKET_SIZE-ts->pat_pkt_len);
        
        ts->pat_pkt_len = TS_PACKET_SIZE;
        ts->pat_copy =1;
   }
     
    
    if(ts->pcr_pid <=0)
    {
        init_sec(&(ts->bat.sec_header),BAT_TID,0x6010,0,0,0);
        init_sec(&(ts->nit.sec_header),NIT_TID,ts->tsid,0,0,0);
        init_sec(&(ts->sdt.sec_header),SDT_TID,ts->tsid,0,0,0);
       
    }
    
   	return 0;
}

 int demux_patpmt(MpegTSContext *ts,unsigned char * start,int len,int pmt_flag,unsigned char *packet,int copy_pat_flag)
{
    int cc = (packet[3] & 0xf);
    if(pmt_flag) {
		if(pmt_cb(ts,start,len,1,packet)>=0){	
			   if(ts->pmt_copy ==0)
			   {
			        memcpy(ts->pmt_pkt,packet,TS_PACKET_SIZE);		            
		            ts->pmt_copy =1;
		            ts->pmt_counter = cc+1;
			   }
		}else
		{
		    fprintf(stderr,"ERROR: this pmt demux failed\n");
		    return -1;
		}
								
	}else {
		if(pat_cb(ts,start,len,copy_pat_flag)>=0){
		    if(ts->pat_copy ==0)
			{
			    memcpy(ts->pat_pkt,packet,TS_PACKET_SIZE);		            
		        ts->pat_copy =1;
		        ts->pat_counter = cc+1;
			}
		    
		}else
		{
		    fprintf(stderr,"ERROR: this pat demux failed\n");
		    return -1;
		}
	}		
}


static inline int64_t ff_parse_pes_pts(const uint8_t *buf) {
    return (int64_t)(*buf & 0x0e) << 29 |
            (AV_RB16(buf+1) >> 1) << 15 |
             AV_RB16(buf+3) >> 1;
}

int64_t GetMPEG2PCR(uint8_t *packetData,  uint32_t remainPacketLength)
{
    
        int64_t PCR = -1;
        int64_t PCR_base = 0;
        int64_t PCR_ext = 0;

        if(remainPacketLength < MIN_PACK_LEN_FOR_PCR || !(*(packetData+3)& ADAPTATION_FLAG))
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

int64_t pes_cb(MpegTSContext *ts,unsigned char * buf,int buf_size,int64_t *cur_pts)
{
    unsigned char *p=buf,*r=NULL;
    int len,code,flags=0;
    int64_t dts=-1,pts=-1;
    int write_pcr =0;
		
	//contain the pes header,else skip
	if(p[0]==0x00 &&p[1]==0x00 &&p[2]==0x01)
	{
		code = p[3] | 0x100;                
		
        //due with pes header data
		if (code != 0x1bc && code != 0x1bf && /* program_stream_map, private_stream_2 */
			code != 0x1f0 && code != 0x1f1 && /* ECM, EMM */
			code != 0x1ff && code != 0x1f2 && /* program_stream_directory, DSMCC_stream */
			code != 0x1f8) 
	    {  

			
			flags = p[7];
			r = p + 9;
			pts = AV_NOPTS_VALUE;
			dts = AV_NOPTS_VALUE;
			
			if ((flags & 0xc0) == 0x80) {
				dts = pts = ff_parse_pes_pts(r);
				r += 5;
			} else if ((flags & 0xc0) == 0xc0) {
				pts = ff_parse_pes_pts(r);
				r += 5;
				dts = ff_parse_pes_pts(r);
				r += 5;
			}
       }
	}
   	if(cur_pts != NULL && dts >0)
   	{
   	    (*cur_pts) = dts;
   	}
     return pts;
}



 int handle_packet(MpegTSContext *ts, unsigned char *packet,int *cur_pid,int64_t *pcr_val,int *table_id,int*bouquet_id,unsigned int callid,int64_t *pts,bool need_get_pts)
{	
	int len, pid=-1, cc, expected_cc, cc_ok, afc, is_start, is_discontinuity,
        has_adaptation, has_payload;
	unsigned char *p, *p_end,*start;
	int64_t pos;
	double timestamp;
	int pmt_flag=0;
	int i=0,j=0;
	int ret=0;
	int ada_len;
	int v_flag =-1;
	int random_access_flag=0;  
	 int64_t diff =0;  
	 
	start = packet;
	   
	   
	if(ts->tdt.period <=0)
	{
	    ts->tdt.period = (ts->mux_rate)*2/(TS_PACKET_SIZE * 8);
	}  
	if(ts->pcr_pkts_period <=0)
	{
	    ts->pcr_pkts_period = (ts->mux_rate * ts->pcr_retransmit_time)/(TS_PACKET_SIZE * 8 * 1000);
	    fprintf(stdout,"ts->pcr_pkts_period:%d\n",ts->pcr_pkts_period);
	}    
	
	   
	if(ts->sdt_pkts_period <=0)
	{
	    ts->sdt_pkts_period = (ts->mux_rate )/(TS_PACKET_SIZE * 8 *2);//500ms
	}
	
	if(ts->nit_pkts_period <=0)
	{
	    ts->nit_pkts_period = (ts->mux_rate)/(TS_PACKET_SIZE * 8 );//1000ms
	}
	
	if(ts->bat_pkts_period <=0)
	{
	    ts->bat_pkts_period = (ts->mux_rate )/(TS_PACKET_SIZE * 8 );//1000ms
	}
	    
	pid = AV_RB16(packet + 1) & 0x1fff;
	
	
	
	
	if(pid<0 || (ts->nb_prg<=0 &&pid !=0)){		
		return 0;
    }
    if(cur_pid != NULL)
	    (*cur_pid) = pid;
	
	
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
	    random_access_flag = p[1]&0x40;
	    ada_len =p[0];
	}
	
	
	p_end = packet + TS_PACKET_SIZE;
	if (p >= p_end)
		return 0;  	
	
	if(is_session(ts,pid) ){		     
     
     if(ts->pat_pkt_len <=0 && ts->pat_copy ==0){
        memcpy(ts->pat_pkt,packet,5);
        ts->pat_pkt_len +=5;
      }
      
      
		for(i=0;i<ts->nb_prg;i++){
			if (pid==ts->prg[i].prg_id)
				pmt_flag=1;
				
				if(ts->prg_num >=0)
				{
				    int b = (ts->prg_num >> 8);
                    b |= 0x40;
                    packet[1] = b;
                    packet[2] =  ts->prg_num;     
				}
		}
		if ( pid==PAT_PID || pmt_flag) {
			if (is_start) {
				/* pointer field present */
				
				len = *p++;
				if (p + len > p_end)
					return 0;
				if (len ) {
					/* write remaining section bytes */
					if ( (ret =demux_patpmt(ts,p,len,pmt_flag,packet,1))<0 )
					    return 0;			
				}
				p += len;				
				if (p < p_end) {
				    //has more than one pmt or pat in one ts-packet
				    if(( ret= demux_patpmt(ts,p,p_end-p,pmt_flag,packet,1))<0)
				    return 0;				                         
				}
			} else { // no start
			    if(( ret= demux_patpmt(ts,p,p_end-p,pmt_flag,packet,1))<0)
				    return 0;					
			}
		
		}  
	}	
	else
	{
	    
	    if (has_adaptation && ada_len >0)
	     {	
	        int64_t PCR=GetMPEG2PCR(packet,ts->raw_packet_size);	
	        	           
	        int64_t cur_pcr= PCR/27/1000;
	        if(PCR >=0 && pcr_val != NULL)
	        {
	            (*pcr_val) = cur_pcr;
	        }
	       // fprintf(stdout,"PCR:%lu,cur_pcrMS:%d\n",PCR,ts->cur_pcrMS);
	       
	       
	        if(need_get_pts == true)
	        {
	            get_av_flag(ts,pid,&v_flag,NULL);
	    
	            //fprintf(stdout," cur_pcr:%d,is_start:%d,v_flag:%d,access:%d\n", cur_pcr,is_start,v_flag,random_access_flag);
	            if(is_start && v_flag ==1 && random_access_flag )
	            {	
	                ts->hasKeyframe =1;
	            }
	        }  
	        
	         
	        if(PCR >= 0 && ts->cur_pcrMS <=0 && ts->hasKeyframe){
	            ts->pcr_pkts_count =0;
	            ts->cur_pcrMS = cur_pcr;
	                        
                unsigned char packet[TS_PACKET_SIZE];
                //pcr = ts->mux_pos /2/188; 
                insert_pcr_only(ts,ts->pcr_pid,ts->counter,packet,cur_pcr);                  
                insert_one_pkt(ts,callid,packet,TS_PACKET_SIZE,cur_pcr);         
               
                modify_count(ts); 
                
                printf("insert new pcr:%d\n",ts->cur_pcrMS);
            }
	            
	        
	        if(ts->cur_pcrMS >=0 && ts->hasKeyframe)
	        {        
	            diff =  cur_pcr-ts->cur_pcrMS;	        
	        }
	        
	        //fprintf(stdout,"cur_PCR:%d,last_pcr:%d,cur_pcrMS:%d,mux_pos:%d,diff:%u\n",cur_pcr,ts->last_pcr,ts->cur_pcrMS,ts->mux_pos,diff);            
	       
	       if(PCR >= 0)
	            p[1]= p[1]&0xef;
	       
	      
	        if(cur_pcr>0 && diff >0)
	        {
	            ts->need_pkts_num = (ts->mux_rate/1000 /8*diff)/TS_PACKET_SIZE;
	            ts->need_pkts_num -= ts->pcr_pkts_count;	           	           
	            //fprintf(stdout,"diff:%d,need_Num:%d,cur_pcr:%d\n",diff,ts->need_pkts_num,cur_pcr);
	            if(ts->need_pkts_num <0 )
	            {
	                ts->need_pkts_num = 0;
	            }
	            
	        }else  if(diff <0 && PCR >0)
	        {
	            ts->need_pkts_num = diff/ts->delay -1;
	            ts->cur_pcrMS = cur_pcr;
	           // fprintf(stdout,"diff <0,ts->cur_pcrMS:%d\n",ts->cur_pcrMS);
	        }
	        
	        if(cur_pcr >=0 &&  ts->hasKeyframe)
	            ts->last_pcr = cur_pcr;        
	            
	        p += p[0] + 1;
	    }
	    
	    if(need_get_pts == true)
	    {
	        get_av_flag(ts,pid,&v_flag,NULL);
	    
	        //fprintf(stdout,"ts->last_pcr:%d,is_start:%d,v_flag:%d,access:%d\n",ts->last_pcr,is_start,v_flag,random_access_flag);
	        if(is_start && v_flag ==1  )
	        {	
	            if(random_access_flag)
	                ts->hasKeyframe =1;
         
		        if(pts!= NULL)	
		        {	    
		            return   pes_cb(ts, p, p_end - p,pts); 
		        } 
		    }
		}
	   
	}
  
    return cc;
}

void init_sec(SectionHeader *sec,int tid,int tsid,int version,int sec_num,int last_sec_num)
{
     sec->tid = tid;
     sec->id = tsid;
     sec->version = version;
     sec->sec_num = sec_num;
     sec->last_sec_num = last_sec_num;
}

int init_tscontext(MpegTSContext *ts,int64_t MUX_RATE,int delay_time)
{	
		
	memset(ts,0,sizeof(MpegTSContext));
	ts->mux_rate = MUX_RATE;
	ts->pcr_retransmit_time= delay_time;
	ts->delay = delay_time;
	ts->raw_packet_size = TS_PACKET_SIZE;
	ts->nit.pid = 0x10;
	ts->nit.cc =15;
	ts->bat.pid = 0x11;
	ts->bat.cc =15;
	ts->tdt.count =15;
	ts->tdt.hour = 17;
	ts->tdt.period =0;
	ts->tdt.sec =0;
	ts->tdt.min =10;
	ts->pre_pcr = -1;	
	ts->nb_prg = 0;
	ts->onid = 0x01;
	ts->cur_pcrMS = -1;
	ts->last_pcr = -1;
	ts->first_pcr =-1;
	ts->prg_num = -1;
	ts->isIpqam = false;
	ts->hasKeyframe = false;
}

void insert_null_packet(MpegTSContext *ts,uint8_t *buf)
{
    uint8_t *q;    
    q = buf;
    *q++ = 0x47;
    *q++ = 0x00 | 0x1f;
    *q++ = 0xff;
    *q++ = 0x00 | 0x10;
    memset(q, 0x0FF, TS_PACKET_SIZE - (q - buf));  
    /*
    if(ts->fd >0)
    {
        file_write(ts->fd,buf,TS_PACKET_SIZE);
    } */ 
}


static int write_pcr_bits(uint8_t *buf, int64_t pcr)
{
    int64_t pcr_low = pcr % 300, pcr_high = pcr / 300;

    *buf++ = pcr_high >> 25;
    *buf++ = pcr_high >> 17;
    *buf++ = pcr_high >> 9;
    *buf++ = pcr_high >> 1;
    *buf++ = pcr_high << 7 | pcr_low >> 8 | 0x7e;
    *buf++ = pcr_low;

    return 6;
}

void insert_pcr_only(MpegTSContext *ts,int64_t pid,int counter,uint8_t *buf,int64_t inpcr)
{
    uint8_t *q;    
    q = buf;
    *q++ = 0x47;
    *q++ = pid >> 8;
    *q++ = pid;
    *q++ = 0x20 | ts->counter;   /* Adaptation only */
    //ts->counter += 1;
    /* Continuity Count field does not increment (see 13818-1 section 2.4.3.3) */
    *q++ = TS_PACKET_SIZE - 5; /* Adaptation Field Length */
    *q++ = 0x10;               /* Adaptation flags: PCR present */

    /* PCR coded into 6 bytes */
    if(inpcr>=0)
        ts->cur_pcrMS = inpcr;
    else
    {
        int64_t pcr =  (ts->cur_pcrMS + ts->delay);
        ts->cur_pcrMS = pcr;
    }
    
    //fprintf(stdout,"insert_pcr,cur_pcrMS:%d\n",ts->cur_pcrMS);
    q += write_pcr_bits(q,ts->cur_pcrMS*27*1000);

    /* stuffing bytes */
    memset(q, 0xFF, TS_PACKET_SIZE - (q - buf));
    /*
    if(ts->fd >0)
    {
        file_write(ts->fd,buf,TS_PACKET_SIZE);
    } */                
		
}

#if DEBUG
static int write_pkts(MpegTSContext *ts,unsigned char * packet,int len)
{
    if(ts->fd >0)
	{
	     file_write(ts->fd,packet,len);
	}
	
}



 int insert_one_pkt(MpegTSContext *ts,unsigned int callid,unsigned char *buf,int buf_size,int64_t pcr)
{
    
    //if(ts->buf_len >= TS_TCP_PACKET_SIZE || pcr >=0 || (ts->cur_pcrMS != ts->sess_pkt_timestamp)) 
    int finish = 0;
    //while(true)
    { 
        if(ts->buf_len >= TS_TCP_PACKET_SIZE || pcr >=0 ) 
        {
            //fprintf(stdout,"cur_pkt_timestamp:%u,ts->buf_len:%d\n",ts->sess_pkt_timestamp,ts->buf_len);
            write_pkts(ts,ts->buf,ts->buf_len);        
            ts->buf_len =0; 
            if(pcr >=0 && finish ==1)
            {
                //break;
            }           
             
         }
    
        ts->sess_pkt_timestamp = ts->cur_pcrMS;
        memcpy(ts->buf+ts->buf_len,buf,buf_size);
        ts->buf_len += buf_size;
   
        finish =1;
       
    }
    return 0;
}
#endif
void modify_count(MpegTSContext *ts)
{
   if(ts->cur_pcrMS >=0)
   {
        ts->pcr_pkts_count ++ ; 
        ts->tdt.pkt_count ++;    
   }
   
   ts->bat_pkts_count ++;
   ts->nit_pkts_count ++;
   ts->sdt_pkts_count  ++;   
}

int insert_pkts(MpegTSContext *ts,int64_t pcr,unsigned int callid)
{
         
    unsigned char buf[TS_PACKET_SIZE ]={'\0'};
    unsigned char * p = buf;
    int write_pcr =0;
   
    int force =-1;
    int need_insert =0;
    
    if(ts->need_pkts_num >0)
        need_insert =1;
      
    if(pcr>=0 && ts->pre_pcr <0)
	{	
	    ts->pre_pcr = pcr;	
	} 	  
    
    while(ts->need_pkts_num <0)
    {       
            fprintf(stdout,"ts->need_num:%d <0,ts->cur_pcrMS:%u\n",ts->need_pkts_num,ts->cur_pcrMS);
            delete_pcr_pkt(callid,ts);
            ts->need_pkts_num ++;
           //fprintf(stdout,"after,dele_pcr,ts->cur_pcrMS:%u\n",ts->cur_pcrMS);        
    }   
   
   // fprintf(stdout,"ts->need_pkts_num:%d,ts->pcr_pkts_count:%d,ts->nb_services:%d,ts->sdt_pkts_count:%d,ts->nit_pkts_count:%d,ts->bat_pkts_count:%d\n",ts->need_pkts_num,ts->pcr_pkts_count,ts->nb_services,ts->sdt_pkts_count,ts->nit_pkts_count,ts->bat_pkts_count);
    while(ts->need_pkts_num>0  || (ts->pcr_pkts_count >= (ts->pcr_pkts_period)  && ts->pcr_pkts_period >0 ) || (ts->nb_services >0 && ((ts->sdt_pkts_count >=ts->sdt_pkts_period && ts->sdt_pkts_period >0) ||  (ts->nit_pkts_count >= ts->nit_pkts_period  && ts->nit_pkts_period >0 )|| (ts->bat_pkts_count >= ts->bat_pkts_period && ts->bat_pkts_period>0) ) )   )
    {        
        //fprintf(stdout,"Entry while\n");
        write_pcr =0; 
        force =-1;
        memset(buf,0,sizeof(buf));
                
        
        if (ts->pcr_pkts_count >= ts->pcr_pkts_period ){
            if(ts->cur_pcrMS + ts->delay >= pcr && pcr>0)
            {
               break;
            }
            ts->pcr_pkts_count = 0;
            write_pcr = 1;            
        }

        if (ts->mux_rate > 1) 
        {       
            if (write_pcr)
            {
                force =1;
                insert_pcr_only(ts,ts->pcr_pid,ts->counter,p,-1); 
                //printf("insert_pcr_only,ts->cur_pcrMS:%ld\n",ts->cur_pcrMS);                             	
	        }
            else{
               
                if(ts->sdt_pkts_count >=ts->sdt_pkts_period && ts->nb_services >0 && ts->sdt_pkts_period>0)
                {
                    mpegts_write_sdt(ts,callid,NULL);
                    ts->sdt_pkts_count =0;
                    modify_count(ts);  
                    ts->need_pkts_num >0? ts->need_pkts_num --:ts->need_pkts_num =0;
                    //printf("after sdt,ts->need_pkts_num:%d\n",ts->need_pkts_num);
                    goto final;
                }  
                
                
                if(ts->nit_pkts_count >= ts->nit_pkts_period && ts->nb_services >0 && ts->nit_pkts_period>0)
                {
                    mpegts_write_nit(ts,callid,NULL);
                    ts->nit_pkts_count =0;
                    modify_count(ts); 
                    ts->need_pkts_num >0? ts->need_pkts_num --:ts->need_pkts_num =0;
                    //printf("after nit,ts->need_pkts_num:%d\n",ts->need_pkts_num);
                    goto final;
                }
                
                if(ts->bat_pkts_count >= ts->bat_pkts_period && ts->nb_services >0 && ts->bat_pkts_period>0)
                {
                    mpegts_write_bat(ts,callid,NULL);
                    ts->bat_pkts_count =0;
                    modify_count(ts); 
                    ts->need_pkts_num >0? ts->need_pkts_num --:ts->need_pkts_num =0;
                    //printf("after bat,ts->need_pkts_num:%d\n",ts->need_pkts_num);
                    goto final;
                 }
                   
                 if( (pcr - ts->pre_pcr) >= 2 * 27000000)
	             {
	                ts->pre_pcr = pcr;
	                mpegts_write_tdt(ts,callid,NULL);
	                modify_count(ts);
	                ts->need_pkts_num >0? ts->need_pkts_num --:ts->need_pkts_num =0;
	                //printf("after tdt,ts->need_pkts_num:%d\n",ts->need_pkts_num);
	                goto final;
	             }      
            
                 insert_null_packet(ts,p);
            }
            modify_count(ts);
            
            ts->mux_pos += TS_PACKET_SIZE;  
            ts->need_pkts_num --;        
            insert_one_pkt(ts,callid,p,TS_PACKET_SIZE,force);   
                       
            if(ts->need_pkts_num <0)
                ts->need_pkts_num = 0;
          
          
final:              
            if(ts->need_pkts_num ==0 && need_insert == 1)
            {                
                insert_pcr_only(ts,ts->pcr_pid,ts->counter,p,ts->last_pcr);
                //printf("insert_pcr_only*1,ts->cur_pcrMS:%ld\n",ts->cur_pcrMS);  
                ts->pcr_pkts_count =0;
                modify_count(ts);
                force=1;
                insert_one_pkt(ts,callid,p,TS_PACKET_SIZE,force); 
            }                         
        } 
    }
   
    return 0;
}

void add_service (MpegTSContext *ts,unsigned int callid)
{
    if(ts->nb_services <=0)
    {
        ts->sdt.pid = SDT_PID;
        ts->sdt.cc = 0;
        const char *service_name =  DEFAULT_SERVICE_NAME;    
        const char *provider_name =  DEFAULT_PROVIDER_NAME;
    
        
        MpegTSService *service = mpegts_add_service(ts, ts->service_id, provider_name, service_name);
        
        
        if(service != NULL && callid >0)
        {
            service->type = 0x01;
            
            mpegts_write_nit(ts,callid,NULL);
            ts->nit_pkts_count =0;
            modify_count(ts);
             
            mpegts_write_bat(ts,callid,NULL);
            ts->bat_pkts_count =0;
            modify_count(ts);  
           
            
            mpegts_write_sdt(ts,callid,NULL);
            ts->sdt_pkts_count =0;           
            modify_count(ts);
        }        
    }
}

int modify_pmt(unsigned char *packet,int pkt_len)
{
    unsigned char * p =packet+4;
    int sec_len = packet[4];
    unsigned char *sec = p+sec_len+1;
    int old_len = sec[2];
    sec[2]=sec[2]-12;
    sec[11]=sec[11]-12;
    unsigned char *desc = sec+12;
    unsigned char *del_start = NULL;
    int del_len;
    unsigned char tmp[128];
    int tmp_len =0;
    while(true)
    {
        int desc_tag = get8(&desc,desc + pkt_len);        
        int desc_len = get8(&desc,desc+pkt_len);
        if(desc_tag == 0x0B)
        {
               tmp_len = old_len -(desc+10 - packet-4);
             memcpy(tmp,desc +10,tmp_len);
            
             memset(desc-2,0xff,188- (desc-2-packet));
             memcpy(desc-2,tmp,tmp_len);   
             
             int new_sec_len = old_len-4 -12+3;
             uint32_t crc = av_bswap32(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, sec, new_sec_len));
            sec[new_sec_len ] = (crc >> 24) & 0xff;
            sec[new_sec_len+1] = (crc >> 16) & 0xff;
            sec[new_sec_len+2] = (crc >> 8) & 0xff;
            sec[new_sec_len+3] = (crc) & 0xff;
             
             break;        
        }else
            desc += desc_len;
        
        
    }
    
}


void write_new_psi(MpegTSContext *ts,int pid, unsigned int callid,unsigned char *buf)
{
    if(ts->pat_copy == 1 && pid == PAT_PID)
    {
        if(buf != NULL)
        {
            memcpy(buf,ts->pat_pkt,TS_PACKET_SIZE);
        }else
            insert_one_pkt(ts,callid,ts->pat_pkt,TS_PACKET_SIZE,-1);        
        ts->pat_counter = (ts->pat_counter+1) & 0xf;    
        ts->pat_pkt[3] = (ts->pat_pkt[3] &0xf0) |ts->pat_counter;
            
    }
    
    if(ts->pmt_copy ==1 && pid != PAT_PID)
    {   if(buf != NULL)
        {
            memcpy(buf,ts->pmt_pkt,TS_PACKET_SIZE);
        }else
            insert_one_pkt(ts,callid,ts->pmt_pkt,TS_PACKET_SIZE,-1);        
        ts->pmt_counter = (ts->pmt_counter+1) & 0xf;    
        ts->pmt_pkt[3] = (ts->pmt_pkt[3] &0xf0) |ts->pmt_counter;        
    }
}

#if DEBUG
int main(int args,char* argv[])
{
    if(args < 3)
    {
        fprintf(stdout,"usage: ts-demux inputfile  outputfile\n");
        exit(-1);
    }
    char *file=argv[1];
    char *ofile = argv[2];
    int write_pcr;
    MpegTSContext ts;
    
 
    init_tscontext(&ts,3008000,40);
    
    
    
    int ret =0;
    int ifd = file_open(file,AVIO_FLAG_READ,&(ts.file_size));
     
    if(ifd <=0)
    {
        fprintf(stderr,"open input file failed!\n");
        exit(0);
    }
   
    int ofd = file_open(ofile,AVIO_FLAG_WRITE,NULL);    
    if(ofd >0)
    {
        ts.fd = ofd;
    }
    unsigned char buffer[3948]={'\0'};
    
    
   
   while(ts.last_pos < ts.file_size)
   {
        memset(buffer,0,sizeof(buffer));
        int  buf_size = file_read(ifd,buffer,sizeof(buffer));
        ts.last_pos += buf_size;
        
        unsigned char *p = buffer;
        while(p-buffer < buf_size)
        {
            unsigned char pkt[188];
            memcpy(pkt,p,188);
            int pid=-1;    
            int64_t pcr =-1;   
            int table_id = 0; 
            int bouquet_id =0; 
                       
            int cc=handle_packet(&ts, pkt,&pid,&pcr,&table_id,&bouquet_id,0); 
            int renew_bat =0;
            if(pid == PAT_PID)
            {
                add_service(&ts,0);
                write_new_psi(&ts,pid,0,NULL);
                renew_bat =1;
            }
           
            
                           
            ret=insert_pkts(&ts,-1,0);
            if(ret != 0)
                  return ret;
          
           
           if (renew_bat == 0 && (ts.pat_copy == 0 || (ts.pat_copy == 1&& pid !=0x00)))
           {               
                    ret=insert_one_pkt(&ts,0,pkt,TS_PACKET_SIZE,-1);
           }
           
           /*     
          if(pcr>0)
          {
            ts.pcr_pkts_count = 0;  
            ts.need_pkts_num =0;
           } 
         */
           if(ts.last_pcr>=0)
           {
                modify_count(&ts);
           }
           //ts.cur_pcrMS = FFMAX(ts.cur_pcrMS,ts.last_pcr);             
           ts.mux_pos += TS_PACKET_SIZE;                                
                               
            p+=188;
           
        }
        
   }  
   
    return 0;
}

#endif

