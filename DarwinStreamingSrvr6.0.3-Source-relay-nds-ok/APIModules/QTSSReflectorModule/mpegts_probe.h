#ifndef _MPEGTS_PROBE_H
#define _MPEGTS_PROBE_H

#include <inttypes.h>
#include <stdio.h>  
#include <stdlib.h>  
#include <unistd.h>

#define MIN((a),(b)) ((a)<=(b) ? (a):(b))
#define MAX((a),(b)) ((a)>=(b) ? (a):(b))

#define TS_FEC_PACKET_SIZE 204
#define TS_DVHS_PACKET_SIZE 192
#define TS_PACKET_SIZE 188
#define TS_MAX_PACKET_SIZE 204

#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])
#endif


static int analyze(const char *buf, int size, int packet_size, int *index)
{
    int stat[TS_MAX_PACKET_SIZE];
    int stat_all = 0;
    int i;
    int best_score = 0;

    memset(stat, 0, packet_size * sizeof(*stat));

    for (i = 0; i < size - 3; i++) {
        if (buf[i] == 0x47 && !(buf[i + 1] & 0x80) && buf[i + 3] != 0x47) {
            int x = i % packet_size;
            stat[x]++;
            stat_all++;
            if (stat[x] > best_score) {
                best_score = stat[x];
                if (index)
                    *index = x;
            }
        }
    }

    return best_score - MAX(stat_all - 10*best_score, 0)/10;
}


inline int mpegts_probe( char *buf,int buf_size)
{
    const int size = buf_size;
    int maxscore = 0;
    int sumscore = 0;
    int i;
    int check_count = size / TS_FEC_PACKET_SIZE;
#define CHECK_COUNT 10
#define CHECK_BLOCK 100

    if (check_count < CHECK_COUNT)
        return -1;

    for (i = 0; i<check_count; i+=CHECK_BLOCK) {
        int left = MIN(check_count - i, CHECK_BLOCK);
        int score      = analyze(buf + TS_PACKET_SIZE     *i, TS_PACKET_SIZE     *left, TS_PACKET_SIZE     , NULL);
        int dvhs_score = analyze(buf + TS_DVHS_PACKET_SIZE*i, TS_DVHS_PACKET_SIZE*left, TS_DVHS_PACKET_SIZE, NULL);
        int fec_score  = analyze(buf + TS_FEC_PACKET_SIZE *i, TS_FEC_PACKET_SIZE *left, TS_FEC_PACKET_SIZE , NULL);
        score = FFMAX3(score, dvhs_score, fec_score);
        sumscore += score;
        maxscore = FFMAX(maxscore, score);
    }

    sumscore = sumscore * CHECK_COUNT / check_count;
    maxscore = maxscore * CHECK_COUNT / CHECK_BLOCK;
   

    if      (sumscore > 6) return AVPROBE_SCORE_MAX   + sumscore - CHECK_COUNT;
    else if (maxscore > 6) return AVPROBE_SCORE_MAX/2 + sumscore - CHECK_COUNT;
    else
        return -1;
}
#endif
