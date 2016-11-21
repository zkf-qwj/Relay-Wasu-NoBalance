#include <stdio.h>
#include <stdlib.h>

#include "arts_ph_common.h"
#define MAX_LEN 1316

int main(int argc , char *argv[])
{
    if(argc <3)
    {
        printf("Usage: %s host port\n",argv[0]);
        
        exit(-1);
    }
    
    int server_port = atoi(argv[2]);
    char *host = argv[1];
    int fd =-1;
    int read_size=0;
    char buffer[MAX_LEN];
    struct sctp_initmsg initmsg;
    struct sockaddr_in servaddr;
    if ( -1 == ( fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP))
    {
        printf("create socket failed!\n");
        exit(-1); 
    }
    
    bzero( (void *)&servaddr, sizeof(servaddr) );
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(server_port);
    servaddr.sin_addr.s_addr = inet_addr( host );
    
    memset( &initmsg, 0, sizeof(initmsg) );  
    initmsg.sinit_num_ostreams = 5;  
    initmsg.sinit_max_instreams = 5;  
    initmsg.sinit_max_attempts = 4;  
    if ( setsockopt( fd, IPPROTO_SCTP, SCTP_INITMSG,  
                     &initmsg, sizeof(initmsg) <0)
    {
        printf("setsocket failed\n");
        exit(-1);
    }  
    if ( connect( fd, (struct sockaddr *)&servaddr, sizeof(servaddr) <0)
    {
        printf("connect %s:%d failed!\n",host,server_port);
        exit(-1);
        
    }   
    
    int file_fd = open(argv[3],O_RDWR);
    if(file_fd <=0)
    {
        printf("open %s failed!\n", argv[3]);
        exit(-1);
    }
    while(true)
    {
        if(( read_size = read(fd,buffer, MAX_LEN) <0))
        {
            printf("read failed!\n");
            exit(-1);
        }
        
        if(read_size == 0)
        {
            printf("read finished!\n");
            exit(0);
        }
        sctp_sendmsg( fd, (void *)buffer, (size_t)read_size,  
                         NULL, 0, 0, 0, 256, 0, 0 );
        usleep(10000);
    }
    
    
    return 0;
}
