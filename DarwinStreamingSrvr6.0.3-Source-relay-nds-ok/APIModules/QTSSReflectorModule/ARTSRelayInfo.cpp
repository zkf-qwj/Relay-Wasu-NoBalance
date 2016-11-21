#include "StringParser.h"
#include "SDPSourceInfo.h"
#include "OSMemory.h"
#include "StringFormatter.h"
#include "SocketUtils.h"
#include "StringTranslator.h"
#include "SDPUtils.h"
#include "OSArrayObjectDeleter.h"
#include "ARTSRelayInfo.h"
#include "ClientSocket.h"
#include "RTSPClient.h"



ARTSRelayInfo::~ARTSRelayInfo()
{
    
    qtss_printf("<<~ARTSRelayInfo");
   
   if(fBackUpTask != NULL)
        fBackUpTask = NULL; 
   
    if (fARTSRelaySessionCreatorTask != NULL)
            fARTSRelaySessionCreatorTask->fInfo = NULL;
        
    if (fDescribeComplete)
    {
        Assert(fClientSocket != NULL);
        Assert(fClient != NULL);
        // the task will delete these objects when it is done with the teardown
        TeardownTask* task = new TeardownTask(fClientSocket, fClient);
        task->Signal(Task::kStartEvent);
    }
    else
    {
        if (fClientSocket != NULL) delete fClientSocket;
        if (fClient != NULL) delete fClient;
    }       
       
        
    if (fLocalSDP.Ptr != NULL) delete fLocalSDP.Ptr;
    fLocalSDP.Len = 0;
    
    if (fSourceURL != NULL) delete fSourceURL;
   
}

void ARTSRelayInfo::InitClient(UInt32 inSocketType)
{
    fClientSocket = NEW TCPClientSocket(inSocketType);
    fClient = NEW RTSPClient(fClientSocket, false, NULL);
}



void ARTSRelayInfo::SetClientInfo(UInt32 inAddr, UInt16 inPort, char* inURL, UInt32 inLocalAddr)
{
    if (fClientSocket != NULL)
        fClientSocket->Set(inAddr, inPort);
     
    qtss_printf("inAddr:%d,inPort:%d\n",inAddr,inPort);
    
    StrPtrLen inURLPtrLen(inURL);
    
    if (fClient != NULL)
        fClient->Set(inURLPtrLen);

    if (inLocalAddr != 0)
        fClientSocket->GetSocket()->Bind(inLocalAddr, 0);
}

void ARTSRelayInfo::freshStreamInfo()
{
    UInt32  streamNum = fReflectorInfo ->GetNumStreams();
    for (UInt32 x = 0; x < fNumStreams; x++)
    {
          if(fReflectorInfo->GetStreamInfo(x)->fPort)
          {
            fStreamArray[x].fPort =  fReflectorInfo->GetStreamInfo(x)->fPort;
            fStreamArray[x].fTimeToLive = fReflectorInfo->GetStreamInfo(x)->fTimeToLive;
          }         
    }      
    
}


//host and url are char-array
void ARTSRelayInfo::Configure(char *host,UInt32 port,char *url,UInt32 LocalAddr,UInt32 sendInterval)
{
    StrPtrLen theURL;
    
    if(url != NULL && strlen(url)>0)
    {
        theURL.Set(url);
        fSourceURL = theURL.GetAsCString();
    }
    
    if(host && strlen(host))
    {    
        fHostAddr = SocketUtils::ConvertStringToAddr(host);
    }
    
    if(port >0)
    {
        fHostPort = port;
    }
    fLocalAddr = LocalAddr;
    
    if(sendInterval >0)
    this->SetOptionInterval(sendInterval *1000 );
    
    
    InitClient(Socket::kNonBlockingSocketType);
    SetClientInfo(fHostAddr, fHostPort, fSourceURL, fLocalAddr);
    if (fUserName != NULL)
        fClient->SetName(fUserName);
    if (fPassword != NULL)
        fClient->SetPassword(fPassword);
    
}

void ARTSRelayInfo::StartARTSRelayTask()
{
    qtss_printf("**ARTSRelayInfo::StartARTSRelayTask,time:%"_64BITARG_"d\n",QTSS_Milliseconds());
         
    fSessionCreationState = kSendingDescribe;
    fARTSRelaySessionCreatorTask = NEW ARTSRelaySessionCreator(this);
    fARTSRelaySessionCreatorTask->Signal(Task::kStartEvent);
}


SInt64 ARTSRelayInfo::ARTSRelaySessionCreator::Run()
{    
    SInt64 result = -1;
    qtss_printf("ARTSRelaySessionCreator::Run()\n");
    
    if (fInfo != NULL)
    {
        qtss_printf("ARTSRelayInfo::RunCreateSession,time:%"_64BITARG_"d\n",QTSS_Milliseconds());
        result = fInfo->RunCreateSession(); 
    }        
    qtss_printf("result:%d\n",result);
    return result;
}

SInt64 ARTSRelayInfo::RunCreateSession()
{
    OS_Error osErr = OS_NoErr;
    SInt64 result = 100;
    
    
   if (fSessionCreationState == kSendingOptions)
    {
        osErr = fClient->SendOptions();
        qtss_printf("send option \n");
        if (osErr == OS_NoErr)
        {
            if (fClient->GetStatus() == 200)
            {
                
                return GetOptionInterval();
            
            }else
                osErr = ENOTCONN;   
        
        }
        result = -1;
    }
    
   
    if (fSessionCreationState == kSendingDescribe)
    {
        if (!fDescribeComplete)
        {
            qtss_printf("SendDescribe \n");
            osErr = fClient->SendDescribe();
        
            if (osErr == OS_NoErr)
            {
                if (fClient->GetStatus() == 200)
                {
                    // we've gotten the describe response, so process it.
                    SDPSourceInfo theSourceInfo(fClient->GetContentBody(), fClient->GetContentLength());
        
                    // Copy the Source Info into our local SourceInfo.
                    fNumStreams = theSourceInfo.GetNumStreams();
                    fStreamArray = NEW StreamInfo[fNumStreams];
        
                    for (UInt32 x = 0; x < fNumStreams; x++)
                    {
                        // Copy fPayloadType, fPayloadName, fTrackID, fBufferDelay
                        fStreamArray[x].Copy(*theSourceInfo.GetStreamInfo(x));

                        // Copy all stream info data. Also set fSrcIPAddr to be the host addr
                        fStreamArray[x].fSrcIPAddr = fClientSocket->GetHostAddr();
                        fStreamArray[x].fDestIPAddr = fClientSocket->GetLocalAddr();
                        fStreamArray[x].fPort = 0;
                        fStreamArray[x].fTimeToLive = 0;
                    }
                }
                else
                    osErr = ENOTCONN;                 
            }
        }
        
        //describe is complete 
        if(osErr == OS_NoErr)
        {
            //copy out the SDP information
            fLocalSDP.Ptr = NEW char[fClient->GetContentLength() + 1];
            
            // Look for an "a=range" line in the SDP. If there is one, remove it.
            static StrPtrLen sRangeStr("a=range:");
            StrPtrLen theSDPPtr(fClient->GetContentBody(), fClient->GetContentLength());
            StringParser theSDPParser(&theSDPPtr);
            
            do
            {
                // Loop until we reach the end of the SDP or hit a a=range line.
                StrPtrLen theSDPLine(theSDPParser.GetCurrentPosition(), theSDPParser.GetDataRemaining());
                if ((theSDPLine.Len > sRangeStr.Len) && (theSDPLine.NumEqualIgnoreCase(sRangeStr.Ptr, sRangeStr.Len)))
                    break;
            } while (theSDPParser.GetThruEOL(NULL));
            
            // Copy what we have so far
            ::memcpy(fLocalSDP.Ptr, fClient->GetContentBody(), theSDPParser.GetDataParsedLen());
            fLocalSDP.Len = theSDPParser.GetDataParsedLen();
        
            // Skip over the range (if it exists)
            (void)theSDPParser.GetThruEOL(NULL);
    
            // Copy the rest of the SDP
            ::memcpy(fLocalSDP.Ptr + fLocalSDP.Len, theSDPParser.GetCurrentPosition(), theSDPParser.GetDataRemaining());
            fLocalSDP.Len += theSDPParser.GetDataRemaining();

#define _WRITE_SDP_ 0

#if _WRITE_SDP_
            FILE* outputFile = ::fopen("rtspclient.sdp", "w");
            if (outputFile != NULL)
            {
                fLocalSDP.Ptr[fLocalSDP.Len] = '\0';
                qtss_fprintf(outputFile, "%s", fLocalSDP.Ptr);
                ::fclose(outputFile);
                qtss_printf("Wrote sdp to rtspclient.sdp\n");
            }
            else
                qtss_printf("Failed to write sdp\n");
#endif
            fDescribeComplete = true;
           
           //use in udp module
           // this->freshStreamInfo();
           
                
            fSessionCreationState = kSendingSetup;
        }        
     }
        
    while ((fSessionCreationState == kSendingSetup) && (osErr == OS_NoErr))
    {
       qtss_printf("SendUDPSetup\n");
      // osErr = fClient->SendUDPSetup(fStreamArray[fNumSetupsComplete].fTrackID, fStreamArray[fNumSetupsComplete].fPort);
       osErr = fClient->SendTCPSetup(fStreamArray[fNumSetupsComplete].fTrackID, 0,1);
        if(osErr == OS_NoErr)
        {
            if(fClient->GetStatus() == 200)
            {
                fNumSetupsComplete++;
                if (fNumSetupsComplete == fNumStreams)
                    fSessionCreationState = kSendingPlay;
            }
            else
                osErr = ENOTCONN;
        }
    }
    
    if (fSessionCreationState == kSendingPlay)
    {
        qtss_printf("SendPlay\n");
        osErr = fClient->SendPlay(0);
        if (osErr == OS_NoErr)
        {
            if (fClient->GetStatus() == 200)
                fSessionCreationState = kDone;
            else
                osErr = ENOTCONN;
        }
    }
    
    if (fSessionCreationState == kDone)
    { 
       //fSessionCreationState = kSendingOptions;
       //fClientSocket->GetSocket()->SetTask(NULL); //detach the task from the socket
       result = -1;    // let the task die
       fARTSRelaySessionCreatorTask = NULL;
       qtss_printf("fBackUpTask:%x\n",fBackUpTask);
       fClientSocket->GetTCPSocket()->SetTask(fBackUpTask);       
       fClientSocket->GetTCPSocket()->RequestEvent(EV_RE); 
         
    }
    
    if ((osErr == EINPROGRESS) || (osErr == EAGAIN))
    {
        // Request an async event
        fClientSocket->GetSocket()->SetTask(fARTSRelaySessionCreatorTask);
        fClientSocket->GetSocket()->RequestEvent(fClientSocket->GetEventMask() );
    }
    else if (osErr != OS_NoErr)
    {   
        // We encountered some fatal error with the socket. Record this as a connection failure
               
        fClientSocket->GetSocket()->SetTask(NULL); //detach the task from the socket
        result = -1;    // let the task die
        fARTSRelaySessionCreatorTask = NULL;
    }
    
    return result;
}


ARTSRelayInfo::TeardownTask::TeardownTask(TCPClientSocket* clientSocket, RTSPClient* client)
{
    this->SetTaskName("ARTSRelayInfo::TeardownTask");
    fClientSocket = clientSocket;
    fClient = client;
}

ARTSRelayInfo::TeardownTask::~TeardownTask()
{
    delete fClientSocket;
    delete fClient;
}


SInt64 ARTSRelayInfo::TeardownTask::Run()
{
    OS_Error err = fClient->SendTeardown();

    if ((err == EINPROGRESS) || (err == EAGAIN))
    {
        // Request an async event
        fClientSocket->GetSocket()->SetTask(this);
        fClientSocket->GetSocket()->RequestEvent(fClientSocket->GetEventMask() );
        return 250;
    }
    fClientSocket->GetSocket()->SetTask(NULL); //detach the task from the socket
    return -1;  // we're out of here, this will cause the destructor to be called
}


