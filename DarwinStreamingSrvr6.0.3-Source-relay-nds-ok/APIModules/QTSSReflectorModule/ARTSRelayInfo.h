#ifndef _ARTS_RELAY_INFO_H
#define _ARTS_RELAY_INFO_H

#include "QTSS.h"
#include "StrPtrLen.h"
#include "RCFSourceInfo.h"
#include "RTSPClient.h"
#include "ClientSocket.h"
#include "IdleTask.h"

class ARTSRelayInfo : public RCFSourceInfo
{
    public:
    
        // Specify whether the client should be blocking or non-blocking
        ARTSRelayInfo(SourceInfo* inInfo) : fSourceURL(NULL),
                                            fHostAddr(0),
                                            fHostPort(0),
                                            fLocalAddr(0),
                                            fUserName(NULL),
                                            fPassword(NULL),                                           
                                            fClientSocket(NULL),
                                            fClient(NULL),
                                            fNumSetupsComplete(0),
                                            fDescribeComplete(false),
                                            fARTSRelaySessionCreatorTask(NULL),
                                            fBackUpTask(NULL)                                                                                    
                                            {fReflectorInfo = inInfo; }
        
       
        
        virtual ~ARTSRelayInfo();
        
        // Call this before calling ParsePrefs / Describe
        void InitClient(UInt32 inSocketType);
        
        void SetClientInfo(UInt32 inAddr, UInt16 inPort, char* inURL, UInt32 inLocalAddr = 0);
        
       
        // Connects, sends a DESCRIBE, and parses the incoming SDP data. After this
        // function completes sucessfully, GetLocalSDP returns the data, and the
        // SourceInfo & DestInfo arrays will be set up. Also sends SETUPs for all the
        // tracks, and finishes by issuing a PLAY.
        //
        // These functions return QTSS_NoErr if the transaction has completed
        // successfully. Otherwise, they return:
        //
        // EAGAIN: the transaction is still in progress, the call should be reissued
        // QTSS_RequestFailed: the remote host responded with an error.
        // Any other error means that the remote host was unavailable or refused the connection
        QTSS_Error  Describe();
        QTSS_Error  SetupAndPlay();
        
        // This function works the same way as the above ones, and should be
        // called before destroying the object to let the remote host know that
        // we are going away.
        QTSS_Error  Teardown();

        // This function uses the Parsed SDP file, and strips out all the network information,
        // producing an SDP file that appears to be local.
        //virtual char*   GetLocalSDP(UInt32* newSDPLen);
        void Configure(char *host,UInt32 port,char *url,UInt32 LocalAddr,UInt32 sendInterval);
        virtual StrPtrLen*  GetSourceID() { return fClient->GetURL(); }
        
        // This object looks for this keyword in the FilePrefsSource, where it
        // expects the IP address, port, and URL.
        
        
        RTSPClient* GetRTSPClient()     { return fClient; }
        TCPClientSocket* GetClientSocket()  { return fClientSocket; }
        
        Bool16      IsDescribeComplete(){ return fDescribeComplete; }
               
        char* GetSourceURL() { return fSourceURL; }        
       
        
        UInt32 GetHostAddr() { return fHostAddr; }
        UInt32 GetHostPort() { return fHostPort; }
        
        char* GetUsername() { return fUserName; }
        char* GetPassword() { return fPassword; }       
        
        void SetSourceParameters(UInt32 inHostAddr, UInt16 inHostPort, StrPtrLen& inURL); 
        
        void StartARTSRelayTask();
        
        SInt64 RunCreateSession();
        
        void freshStreamInfo();
        
        UInt32 GetOptionInterval() {return fOptionInterval;}
        void   SetOptionInterval(UInt32 sec_interval) { fOptionInterval = sec_interval ;}
  
        UInt32   GetSessionCreationState() {return fSessionCreationState;}
        
        void  SetBackUpTask(IdleTask * task)  {fBackUpTask = task;}
        void* GetBackUpTask () {return fBackUpTask;}
        
    private:
        class ARTSRelaySessionCreator : public Task
        {
            public:
                ARTSRelaySessionCreator(ARTSRelayInfo* inInfo) : fInfo(inInfo) {this->SetTaskName("ARTSRelayInfo::ARTSRelaySessionCreator");}
                ~ARTSRelaySessionCreator(){qtss_printf(" ~ARTSRelaySessionCreator");};
                virtual SInt64 Run();

                ARTSRelayInfo* fInfo;
        };
        
        class TeardownTask : public Task
        {
            public:
                TeardownTask(TCPClientSocket* clientSocket, RTSPClient* client);
                virtual ~TeardownTask();
                
                virtual SInt64 Run();
                
            private:
                TCPClientSocket*    fClientSocket;
                RTSPClient*         fClient;
        };
        
        char*                   fSourceURL;
        UInt32                  fHostAddr;
        UInt16                  fHostPort;
        UInt32                  fLocalAddr;
        char*                   fUserName;
        char*                   fPassword;       
        TCPClientSocket*        fClientSocket;
        RTSPClient*             fClient;
        UInt32                  fNumSetupsComplete;
        Bool16                  fDescribeComplete;
        StrPtrLen               fLocalSDP;
        SourceInfo              *fReflectorInfo;
        UInt32                  fOptionInterval;
     
        ARTSRelaySessionCreator*    fARTSRelaySessionCreatorTask;
        IdleTask                *fBackUpTask;
        
        enum    // relay session creation states
        {
            kSendingOptions     = 0,
            kSendingDescribe    = 1,
            kSendingSetup       = 2,
            kSendingPlay        = 3,
            kDone               = 4
        };
        UInt32          fSessionCreationState;
         
     
};

#endif
