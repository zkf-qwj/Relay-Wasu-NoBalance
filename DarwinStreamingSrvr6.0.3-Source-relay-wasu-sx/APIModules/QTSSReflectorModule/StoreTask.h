#ifndef _STORE_TASK_H
#define _STORE_TASK_H
#include "QTSServerInterface.h"
#include <stdlib.h>
#include <stdio.h>
#define IntervalMsec  20*1000
class StoreTask :public Task
{
    public:
    
       
        // this Task for getting packets from adapter
        StoreTask();
         
        char vid[256];         
        char dataBaseHost[256];             
        UInt64 exp_time;  
        void configure(char *pvid,char *host,UInt64 expTime,UInt32 callid);
        virtual ~StoreTask();         
        bool  stop;
        UInt32 callid;
        virtual SInt64 Run();
       
};





#endif
