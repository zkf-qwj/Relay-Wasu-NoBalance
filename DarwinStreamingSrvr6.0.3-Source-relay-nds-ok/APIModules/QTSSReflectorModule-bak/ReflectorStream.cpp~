/*
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * Copyright (c) 1999-2008 Apple Inc.  All Rights Reserved.
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 *
 */
/*
    File:       ReflectorStream.cpp

    Contains:   Implementation of object defined in ReflectorStream.h. 
                    
    

*/

#include "ReflectorStream.h"
#include "QTSSModuleUtils.h"
#include "OSMemory.h"
#include "SocketUtils.h"
#include "atomic.h"
#include "RTCPPacket.h"
#include "ReflectorSession.h"

#include "QTSServerInterface.h"
extern "C" {
#include "memcache_wrap.h"

}

#include "QTSSRollingLog.h"
#include "QTSSARTSReflectorAccessLog.h"

#if DEBUG
#define REFLECTOR_STREAM_DEBUGGING 0
#else
#define REFLECTOR_STREAM_DEBUGGING 0
#endif


#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const UInt8*)(x))[0] << 8) |          \
      ((const UInt8*)(x))[1])
#endif

#ifndef MIN
#define	MIN(a,b) (((a)<(b))?(a):(b))
#endif

enum debug_level_t {
    NONE_ARTS_MODULE = 0,
    INFO_ARTS_MODULE,
    DEBUG_ARTS_MODULE
};


static ReflectorSocketPool  sSocketPool;

// ATTRIBUTES

static QTSS_AttributeID         sCantBindReflectorSocketErr = qtssIllegalAttrID;
static QTSS_AttributeID         sCantJoinMulticastGroupErr  = qtssIllegalAttrID;

// PREFS

static UInt32                   sDefaultOverBufferInSec             = 10; 
static UInt32                   sDefaultBucketDelayInMsec           = 73;
static Bool16                   sDefaultUsePacketReceiveTime        = false; 
static UInt32                   sDefaultMaxFuturePacketTimeSec      = 60;
static UInt32                   sDefaultFirstPacketOffsetMsec       = 500;

UInt32                          ReflectorStream::sBucketSize  = 16;
UInt32                          ReflectorStream::sOverBufferInMsec = 10000; // more or less what the client over buffer will be
UInt32                          ReflectorStream::sMaxFuturePacketMSec = 60000; // max packet future time
UInt32                          ReflectorStream::sMaxPacketAgeMSec = 10000;

UInt32                          ReflectorStream::sMaxFuturePacketSec = 60; // max packet future time
UInt32                          ReflectorStream::sOverBufferInSec = 10;
UInt32                          ReflectorStream::sBucketDelayInMsec = 73;
Bool16                          ReflectorStream::sUsePacketReceiveTime = false;
UInt32                          ReflectorStream::sFirstPacketOffsetMsec = 500;

void ReflectorStream::Register()
{
    // Add text messages attributes
    static char*        sCantBindReflectorSocket= "QTSSReflectorModuleCantBindReflectorSocket";
    static char*        sCantJoinMulticastGroup = "QTSSReflectorModuleCantJoinMulticastGroup";
    
    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sCantBindReflectorSocket, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sCantBindReflectorSocket, &sCantBindReflectorSocketErr);

    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sCantJoinMulticastGroup, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sCantJoinMulticastGroup, &sCantJoinMulticastGroupErr);
}

static Bool16  SetUpUDPSocketsPool(QTSServerPrefs* inPrefs)
{
    UInt32 theNumAllocatedPairs = 0;
    UInt32 theIndex =0;
    char portsStr[7*1024]={'\0'};
    UInt32 theNumPortsTotal =  inPrefs->GetNumUDPPorts();
    UInt32 theMinPort  =  inPrefs->GetMinUDPPort(); 
    UInt32 theAddrTotal = SocketUtils::GetNumIPAddrs();
    //qtss_printf("theNumPorts:%d,theMinPort:%d\n",theNumPortsTotal,theMinPort);
    
    UInt16 kLowestUDPPort = 6970; //UInt16
    UInt16 kHighestUDPPort = 65535; //UInt16
    QTSS_Error  theErr = QTSS_NoErr;
    
    char *theBindIPAddr = NULL;
    theErr = inPrefs->GetValueAsString(qtssPrefsBindUDPPortsIPAddr, 0, &theBindIPAddr);
    
   
    //UInt32 
    for (UInt32 theNumPairs = 0; theNumPairs < theAddrTotal; theNumPairs++)
    {
    
        StrPtrLen* AddrStrPtr = SocketUtils::GetIPAddrStr(theNumPairs);
        
        
        if( AddrStrPtr && (  ::strcmp(AddrStrPtr->Ptr, "127.0.0.1") != 0)  &&  theNumPortsTotal >0 && theMinPort >0  && theMinPort > kLowestUDPPort && theMinPort < kHighestUDPPort)
        {
            
            theIndex =  theNumPairs;
            
            if(theBindIPAddr != NULL &&  ::strcmp(AddrStrPtr->Ptr,theBindIPAddr) !=0 )
            {
                continue;
            }
            
            for(UInt32 theNumPorts =0; theNumPorts < theNumPortsTotal; theNumPorts ++)
            {
                UInt32 dest_port = theMinPort + 2* theNumPorts;
                
                if(dest_port >= kHighestUDPPort)
                {
                    qtss_printf("udp port %d reach  max port %d",dest_port,kHighestUDPPort);
                    break;
                }
                
                UDPSocketPair* thePair = sSocketPool.CreateUDPSocketPair(SocketUtils::GetIPAddr(theNumPairs),dest_port);
                
                 if (thePair != NULL)
                 {
                         qtss_printf("bind ports:%d ok,addr:%d\n",theMinPort + 2*theNumPorts, SocketUtils::GetIPAddr(theNumPairs));
                         sprintf(portsStr+ strlen(portsStr),"%d,",theMinPort + 2*theNumPorts);
                         theNumAllocatedPairs++;                
                 }//else
                    //qtss_printf("bind ports:%d bad,addr:%d\n",theMinPort + 2*theNumPorts, SocketUtils::GetIPAddr(theNumPairs));
            }
        }
    }
    
    portsStr[strlen(portsStr) -1] ='\0';
    
    
    char* theIPAddrStr = NULL;
    theErr = inPrefs->GetValueAsString(qtssPrefsDataBaseUrl, 0, &theIPAddrStr);
    
    if (theErr != QTSS_NoErr)
    {
        qtss_printf("database_url is null\n");
       
    }else
    {             
        if(strlen(portsStr) >0)
        {
            memcached_st * memc = NULL;  
    
            memc = create_conn(theIPAddrStr);
            if(memc == NULL)
            {
                qtss_printf("connect %s failed\n",theIPAddrStr);
                return -1;
            }
            char key[1024] ={'\0'};
        
           // StrPtrLen* AddrStrPtr = SocketUtils::GetIPAddrStr(theIndex);
            //char *theBindIPAddr = NULL;
            
            
           /// theErr = inPrefs->GetValueAsString(qtssPrefsBindUDPPortsIPAddr, 0, &theBindIPAddr);
            if(theBindIPAddr )
            {
                sprintf(key,"%s_udp_ports_range",theBindIPAddr);
                mem_set(memc,key,portsStr,86400);
               
            }else
                qtss_printf("no given ip addr for udp pool\n");
            
                
            release_conn(memc);
        }
    }
    
    
    delete [] theIPAddrStr;        
    delete [] theBindIPAddr;
    
    qtss_printf("udp pool size:%d\n",sSocketPool.GetSocketQueue()->GetLength());
    
    
    //only return an error if we couldn't allocate ANY pairs of sockets
    if (theNumAllocatedPairs == 0)
    {
        //qtss_printf("[ReflectorStream::Initialize] could not bind any ports\n");          
           return false;
    }
    return true;
}

void ReflectorStream::Initialize(QTSS_ModulePrefsObject inPrefs)
{

    QTSSModuleUtils::GetAttribute(inPrefs, "reflector_bucket_offset_delay_msec", qtssAttrDataTypeUInt32,
                              &ReflectorStream::sBucketDelayInMsec, &sDefaultBucketDelayInMsec, sizeof(sBucketDelayInMsec));
                                 
    QTSSModuleUtils::GetAttribute(inPrefs, "reflector_buffer_size_msec", qtssAttrDataTypeUInt32,
                              &ReflectorStream::sOverBufferInMsec, &sDefaultOverBufferInSec,  sizeof(sDefaultOverBufferInSec));
                              
    QTSSModuleUtils::GetAttribute(inPrefs, "reflector_use_in_packet_receive_time", qtssAttrDataTypeBool16,
                              &ReflectorStream::sUsePacketReceiveTime, &sDefaultUsePacketReceiveTime, sizeof(sDefaultUsePacketReceiveTime));

    QTSSModuleUtils::GetAttribute(inPrefs, "reflector_in_packet_max_receive_sec", qtssAttrDataTypeUInt32,
                              &ReflectorStream::sMaxFuturePacketSec, &sDefaultMaxFuturePacketTimeSec, sizeof(sDefaultMaxFuturePacketTimeSec));

    QTSSModuleUtils::GetAttribute(inPrefs, "reflector_rtp_info_offset_msec", qtssAttrDataTypeUInt32,
                              &ReflectorStream::sFirstPacketOffsetMsec, &sDefaultFirstPacketOffsetMsec, sizeof(sDefaultFirstPacketOffsetMsec));

    //ReflectorStream::sOverBufferInMsec = sOverBufferInSec * 1000;
    ReflectorStream::sMaxFuturePacketMSec = sMaxFuturePacketSec * 1000;
    ReflectorStream::sMaxPacketAgeMSec = sOverBufferInMsec; //allow a little time before deleting.
    qtss_printf("sMaxPacketAgeMSec:%d\n",sMaxPacketAgeMSec);
    
    
    SetUpUDPSocketsPool(QTSServerInterface::GetServer()->GetPrefs());
    
}

void ReflectorStream::GenerateSourceID(SourceInfo::StreamInfo* inInfo, char* ioBuffer)
{
    
    ::memcpy(ioBuffer, &inInfo->fSrcIPAddr, sizeof(inInfo->fSrcIPAddr));
    ::memcpy(&ioBuffer[sizeof(inInfo->fSrcIPAddr)], &inInfo->fPort, sizeof(inInfo->fPort));
}


ReflectorStream::ReflectorStream(SourceInfo::StreamInfo* inInfo)
:   fPacketCount(0),
    fSockets(NULL),
    fRTPSender(NULL, qtssWriteFlagsIsRTP),
    fRTCPSender(NULL, qtssWriteFlagsIsRTCP),
    fOutputArray(NULL),
    fNumBuckets(kMinNumBuckets),
    fNumElements(0),
    fBucketMutex(),
    
    fDestRTCPAddr(0),
    fDestRTCPPort(0),
    
    fCurrentBitRate(0),
    fLastBitRateSample(OS::Milliseconds()), // don't calculate our first bit rate until kBitRateAvgIntervalInMilSecs has passed!
    fBytesSentInThisInterval(0),
    
    fRTPChannel(-1),
    fRTCPChannel(-1),
    fHasFirstRTCPPacket(false),
    fHasFirstRTPPacket(false),
    fEnableBuffer(false),
    fEyeCount(0),
    fFirst_RTCP_RTP_Time(0),
    fFirst_RTCP_Arrival_Time(0)   
     
{

    fRTPSender.fStream = this;
    fRTCPSender.fStream = this;

    fStreamInfo.Copy(*inInfo);
    
    // ALLOCATE BUCKET ARRAY
    this->AllocateBucketArray(fNumBuckets);

    // WRITE RTCP PACKET
    
    //write as much of the RTCP RR as is possible right now (most of it never changes)
    UInt32 theSsrc = (UInt32)::rand();
    char theTempCName[RTCPSRPacket::kMaxCNameLen];
    UInt32 cNameLen = RTCPSRPacket::GetACName(theTempCName);
    
    //write the RR (just header + ssrc)
    UInt32* theRRWriter = (UInt32*)&fReceiverReportBuffer[0];
    *theRRWriter = htonl(0x80c90001);
    theRRWriter++;
    *theRRWriter = htonl(theSsrc);
    theRRWriter++;

    //SDES length is the length of the CName, plus 2 32bit words, minus 1
    *theRRWriter = htonl(0x81ca0000 + (cNameLen >> 2) + 1);
    theRRWriter++;
    *theRRWriter = htonl(theSsrc);
    theRRWriter++;
    ::memcpy(theRRWriter, theTempCName, cNameLen);
    theRRWriter += cNameLen >> 2;
    
    //APP packet format, QTSS specific stuff
    *theRRWriter = htonl(0x80cc0008);
    theRRWriter++;
    *theRRWriter = htonl(theSsrc);
    theRRWriter++;
    *theRRWriter = htonl(FOUR_CHARS_TO_INT('Q','T','S','S'));
    theRRWriter++;
    *theRRWriter = htonl(0);
    theRRWriter++;
    *theRRWriter = htonl(0x00000004);
    theRRWriter++;
    *theRRWriter = htonl(0x6579000c);
    theRRWriter++;
    
    fEyeLocation = theRRWriter;
    fReceiverReportSize = kReceiverReportSize + kAppSize + cNameLen;
    
    // If the source is a multicast, we should send our receiver reports
    // to the multicast address
    if (SocketUtils::IsMulticastIPAddr(fStreamInfo.fDestIPAddr))
    {
        fDestRTCPAddr = fStreamInfo.fDestIPAddr;
        fDestRTCPPort = fStreamInfo.fPort + 1;
    }
}


ReflectorStream::~ReflectorStream()
{
    Assert(fNumElements == 0);

    if (fSockets != NULL)
    {
        //first things first, let's take this stream off the socket's queue
        //of streams. This will basically ensure that no reflecting activity
        //can happen on this stream.
        ((ReflectorSocket*)fSockets->GetSocketA())->RemoveSender(&fRTPSender);
        ((ReflectorSocket*)fSockets->GetSocketB())->RemoveSender(&fRTCPSender);
        
        //leave the multicast group. Because this socket is shared amongst several
        //potential multicasts, we don't want to remain a member of a stale multicast
        if (SocketUtils::IsMulticastIPAddr(fStreamInfo.fDestIPAddr))
        {
            fSockets->GetSocketA()->LeaveMulticast(fStreamInfo.fDestIPAddr);
            fSockets->GetSocketB()->LeaveMulticast(fStreamInfo.fDestIPAddr);
        }
        //now release the socket pair
        sSocketPool.ReleaseUDPSocketPair(fSockets);
    }
        
   
   if(fTCPDataSocket != NULL)
    fTCPDataSocket = NULL;
    
   if(fRTSPClient != NULL)
    fRTSPClient = NULL;
        //qtss_printf("Deleting stream %x\n", this);

    //delete every client Bucket
    for (UInt32 y = 0; y < fNumBuckets; y++)
        delete [] fOutputArray[y];
    delete [] fOutputArray;
    
   qtss_printf("~ReflectorStream\n");
}

void ReflectorStream::AllocateBucketArray(UInt32 inNumBuckets)
{
    Bucket* oldArray = fOutputArray;
    //allocate the 2-dimensional array
    fOutputArray = NEW Bucket[inNumBuckets];
    for (UInt32 x = 0; x < inNumBuckets; x++)
    {
        fOutputArray[x] = NEW ReflectorOutput*[sBucketSize];
        ::memset(fOutputArray[x], 0, sizeof(ReflectorOutput*) * sBucketSize);
    }
    
    //copy over the old information if there was an old array
    if (oldArray != NULL)
    {
        Assert(inNumBuckets > fNumBuckets);
        for (UInt32 y = 0; y < fNumBuckets; y++)
        {
            ::memcpy(fOutputArray[y],oldArray[y], sBucketSize * sizeof(ReflectorOutput*));
            delete [] oldArray[y];
        }
        delete [] oldArray;
    }
    fNumBuckets = inNumBuckets;
}


SInt32 ReflectorStream::AddOutput(ReflectorOutput* inOutput, SInt32 putInThisBucket)
{
    OSMutexLocker locker(&fBucketMutex);
    
#if DEBUG
    // We should never be adding an output twice to a stream
    for (UInt32 dOne = 0; dOne < fNumBuckets; dOne++)
        for (UInt32 dTwo = 0; dTwo < sBucketSize; dTwo++)
            Assert(fOutputArray[dOne][dTwo] != inOutput);
#endif

    // If caller didn't specify a bucket, find a bucket
    if (putInThisBucket < 0)
        putInThisBucket = this->FindBucket();
        
    Assert(putInThisBucket >= 0);
    
    if (fNumBuckets <= (UInt32)putInThisBucket)
        this->AllocateBucketArray(putInThisBucket * 2);

    for(UInt32 y = 0; y < sBucketSize; y++)
    {
        if (fOutputArray[putInThisBucket][y] == NULL)
        {
            fOutputArray[putInThisBucket][y] = inOutput;
//#if REFLECTOR_STREAM_DEBUGGING 
            //qtss_printf("Adding new output (0x%lx) to bucket %"_S32BITARG_", index %"_S32BITARG_",\nnum buckets %li bucketSize: %li \n",inOutput, putInThisBucket, y, (SInt32)fNumBuckets, (SInt32)sBucketSize);
//#endif
            fNumElements++;
            return putInThisBucket;
        }
    }
    // There was no empty spot in the specified bucket. Return an error
    return -1;      
}

SInt32 ReflectorStream::FindBucket()
{
    // If we need more buckets, allocate them.
    if (fNumElements == (sBucketSize * fNumBuckets))
        this->AllocateBucketArray(fNumBuckets * 2);
    
    //find the first open spot in the array
    for (SInt32 putInThisBucket = 0; (UInt32)putInThisBucket < fNumBuckets; putInThisBucket++)
    {
        for(UInt32 y = 0; y < sBucketSize; y++)
            if (fOutputArray[putInThisBucket][y] == NULL)
                return putInThisBucket;
    }
    Assert(0);
    return 0;
}

void  ReflectorStream::RemoveOutput(ReflectorOutput* inOutput)
{
    OSMutexLocker locker(&fBucketMutex);
    Assert(fNumElements > 0);
    
    //look at all the indexes in the array
    for (UInt32 x = 0; x < fNumBuckets; x++)
    {
        for (UInt32 y = 0; y < sBucketSize; y++)
        {
            //The array may have blank spaces!
            if (fOutputArray[x][y] == inOutput)
            {
                fOutputArray[x][y] = NULL;//just clear out the pointer
                
#if REFLECTOR_STREAM_DEBUGGING  
                qtss_printf("Removing output %x from bucket %"_S32BITARG_", index %"_S32BITARG_"\n",inOutput,x,y);
#endif
                fNumElements--;
                return;             
            }
        }
    }
    Assert(0);
}

void  ReflectorStream::TearDownAllOutputs()
{

    OSMutexLocker locker(&fBucketMutex);
    
    //look at all the indexes in the array
    for (UInt32 x = 0; x < fNumBuckets; x++)
    {
        for (UInt32 y = 0; y < sBucketSize; y++)
        {   ReflectorOutput* theOutputPtr= fOutputArray[x][y];
            //The array may have blank spaces!
            if (theOutputPtr != NULL)
            {   theOutputPtr->TearDown();
#if REFLECTOR_STREAM_DEBUGGING  
                qtss_printf("TearDownAllOutputs Removing output from bucket %"_S32BITARG_", index %"_S32BITARG_"\n",x,y);
#endif
            }
        }
    }
}


QTSS_Error ReflectorStream::BindSockets(QTSS_StandardRTSP_Params* inParams, UInt32 inReflectorSessionFlags, Bool16 filterState, UInt32 timeout,QTSS_RTPTransportType InTransportType)
{
    // If the incoming data is RTSP interleaved, we don't need to do anything here
    if (inReflectorSessionFlags & ReflectorSession::kIsPushSession)
        fStreamInfo.fSetupToReceive = true;
            
    QTSS_RTSPRequestObject inRequest = NULL;
    if (inParams != NULL)
        inRequest = inParams->inRTSPRequest;
    
            // Set the transport Type a Broadcaster
    QTSS_RTPTransportType transportType = InTransportType;
    
    
   /*
    if (inParams != NULL)
    {   
        UInt32 theLen = sizeof(transportType);
        (void) QTSS_GetValue(inParams->inRTSPRequest, qtssRTSPReqTransportType, 0, (void*)&transportType, &theLen);
    }*/
   
     // get a pair of sockets. The socket must be bound on INADDR_ANY because we don't know
    // which interface has access to this broadcast. If there is a source IP address
    // specified by the source info, we can use that to demultiplex separate broadcasts on
    // the same port. If the src IP addr is 0, we cannot do this and must dedicate 1 port per
    // broadcast
    
    // changing INADDR_ANY to fStreamInfo.fDestIPAddr to deal with NATs (need to track this change though)
    // change submitted by denis@berlin.ccc.de
    Bool16 isMulticastDest = (SocketUtils::IsMulticastIPAddr(fStreamInfo.fDestIPAddr));
    qtss_printf("[BindSockets]::DesADDr:%x,port:%d,srcAddr:%x\n",fStreamInfo.fDestIPAddr,fStreamInfo.fPort,fStreamInfo.fSrcIPAddr);
    
    if (isMulticastDest) {	
	
        fSockets = sSocketPool.GetUDPSocketPair(INADDR_ANY, fStreamInfo.fPort, fStreamInfo.fSrcIPAddr, 0);
    } else{
        fSockets = sSocketPool.GetUDPSocketPair(fStreamInfo.fDestIPAddr, fStreamInfo.fPort, fStreamInfo.fSrcIPAddr, 0);
            //fSockets = QTSServerInterface :: GetServer()->GetSocketPool()->GetUDPSocketPair(fStreamInfo.fDestIPAddr, fStreamInfo.fPort, fStreamInfo.fSrcIPAddr, 0);
    }   
    
    if ((fSockets == NULL) && fStreamInfo.fSetupToReceive)
    {
        fStreamInfo.fPort = 0;
        if (isMulticastDest) {
                fSockets = sSocketPool.GetUDPSocketPair(INADDR_ANY, fStreamInfo.fPort, fStreamInfo.fSrcIPAddr, 0);
        } else {
                fSockets = sSocketPool.GetUDPSocketPair(fStreamInfo.fDestIPAddr, fStreamInfo.fPort, fStreamInfo.fSrcIPAddr, 0);
        }
        qtss_printf("fPort:%d\n",fStreamInfo.fPort);       
    }
    
    if (fSockets == NULL)
    {
            qtss_printf("fSockets is  null RelectorStream.cpp:408");
            return QTSSModuleUtils::SendErrorResponse(inRequest, qtssServerInternal,
                                                    sCantBindReflectorSocketErr);
    }

        // If we know the source IP address of this broadcast, we can demux incoming traffic
        // on the same port by that source IP address. If we don't know the source IP addr,
        // it is impossible for us to demux, and therefore we shouldn't allow multiple
        // broadcasts on the same port.
        if (((ReflectorSocket*)fSockets->GetSocketA())->HasSender() && (fStreamInfo.fSrcIPAddr == 0))
            return QTSSModuleUtils::SendErrorResponse(inRequest, qtssServerInternal,
                                                    sCantBindReflectorSocketErr);
    
        //also put this stream onto the socket's queue of streams
        ((ReflectorSocket*)fSockets->GetSocketA())->AddSender(&fRTPSender);
        ((ReflectorSocket*)fSockets->GetSocketB())->AddSender(&fRTCPSender);
    
        qtss_printf("****[BindSockets]::AddSender,fRTPSender:%x \n",&fRTPSender);

        // A broadcaster is setting up a UDP session so let the sockets update the session
        if (fStreamInfo.fSetupToReceive &&  qtssRTPTransportTypeUDP == transportType && inParams != NULL)
        {   ((ReflectorSocket*)fSockets->GetSocketA())->AddBroadcasterSession(inParams->inClientSession);
            ((ReflectorSocket*)fSockets->GetSocketB())->AddBroadcasterSession(inParams->inClientSession);
        }
    
        ((ReflectorSocket*)fSockets->GetSocketA())->SetSSRCFilter(filterState, timeout);
        ((ReflectorSocket*)fSockets->GetSocketB())->SetSSRCFilter(filterState, timeout);

#if 1 
	    // Always set the Rcv buf size for the sockets. This is important because the
	    // server is going to be getting many packets on these sockets.
	    fSockets->GetSocketA()->SetSocketRcvBufSize(512 * 1024);
	    fSockets->GetSocketB()->SetSocketRcvBufSize(512 * 1024);
#endif
    
        //If the broadcaster is sending RTP directly to us, we don't
        //need to join a multicast group because we're not using multicast
        if (isMulticastDest)
        {
            QTSS_Error err = fSockets->GetSocketA()->JoinMulticast(fStreamInfo.fDestIPAddr);
            if (err == QTSS_NoErr)
                err = fSockets->GetSocketB()->JoinMulticast(fStreamInfo.fDestIPAddr);
            // If we get an error when setting the TTL, this isn't too important (TTL on
            // these sockets is only useful for RTCP RRs.
            if (err == QTSS_NoErr)
                (void)fSockets->GetSocketA()->SetTtl(fStreamInfo.fTimeToLive);
            if (err == QTSS_NoErr)
                (void)fSockets->GetSocketB()->SetTtl(fStreamInfo.fTimeToLive);
            if (err != QTSS_NoErr)
                return QTSSModuleUtils::SendErrorResponse(inRequest, qtssServerInternal,
                                                        sCantJoinMulticastGroupErr);
        }
    
        // If the port is 0, update the port to be the actual port value
        fStreamInfo.fPort = fSockets->GetSocketA()->GetLocalPort();


    if(transportType == qtssRTPTransportTypeUDP )
    {
        //finally, register these sockets for events
        fSockets->GetSocketA()->RequestEvent(EV_RE);
        fSockets->GetSocketB()->RequestEvent(EV_RE);
        ((ReflectorSocket*)fSockets->GetSocketA())->SetTransportType(qtssRTPTransportTypeUDP);
        qtss_printf("[BindSockets]Socket regist read event,port:%d\n",fStreamInfo.fPort);   
    }  
    else if (transportType == qtssRTPTransportTypeTCP)
    {
        if(this->GetTCPDataSocket())
        {
            this->GetTCPDataSocket()->SetSocketRcvBufSize(512*1024);
        }
        
        ((ReflectorSocket*)fSockets->GetSocketA())->SetTransportType(qtssRTPTransportTypeTCP);
        ((ReflectorSocket*)fSockets->GetSocketA())->fTCPDataSocket = this->GetTCPDataSocket();
        ((ReflectorSocket*)fSockets->GetSocketA())->fRTSPClient = this->fRTSPClient;
    }  
        // Copy the source ID and setup the ref    
   
    
    
    StrPtrLen theSourceID(fSourceIDBuf, kStreamIDSize);
    ReflectorStream::GenerateSourceID(&fStreamInfo, fSourceIDBuf);
    fRef.Set(theSourceID, this);
    return QTSS_NoErr;
}

void ReflectorStream::SendReceiverReport()
{
    // Check to see if our destination RTCP addr & port are setup. They may
    // not be if the source is unicast and we haven't gotten any incoming packets yet
    if (fDestRTCPAddr == 0)
        return;
    
    UInt32 theEyeCount = this->GetEyeCount();    
    UInt32* theEyeWriter = fEyeLocation;
    *theEyeWriter = htonl(theEyeCount) & 0x7fffffff;//no idea why we do this!
    theEyeWriter++;
    *theEyeWriter = htonl(theEyeCount) & 0x7fffffff;
    theEyeWriter++;
    *theEyeWriter = htonl(0) & 0x7fffffff;
    
    //send the packet to the multicast RTCP addr & port for this stream
    (void)fSockets->GetSocketB()->SendTo(fDestRTCPAddr, fDestRTCPPort, fReceiverReportBuffer, fReceiverReportSize);
}

void ReflectorStream::PushPacket(char *packet, UInt32 packetLen, Bool16 isRTCP)
{

    if (packetLen > 0)
    {   
        ReflectorPacket* thePacket = NULL;
        if (isRTCP)
        {   //qtss_printf("ReflectorStream::PushPacket RTCP packetlen = %"_U32BITARG_"\n",packetLen);
            thePacket = ((ReflectorSocket*)fSockets->GetSocketB())->GetPacket();
            if (thePacket == NULL)
            {   //qtss_printf("ReflectorStream::PushPacket RTCP GetPacket() is NULL\n");
                return;
            }
            
            OSMutexLocker locker( ((ReflectorSocket*)(fSockets->GetSocketB()) )->GetDemuxer()->GetMutex());
            thePacket->SetPacketData(packet, packetLen);
            ((ReflectorSocket*)fSockets->GetSocketB())->ProcessPacket(OS::Milliseconds(),thePacket,0,0);
            ((ReflectorSocket*)fSockets->GetSocketB())->Signal(Task::kIdleEvent);
        }
        else
        {   //qtss_printf("ReflectorStream::PushPacket RTP packetlen = %"_U32BITARG_"\n",packetLen);
            thePacket =  ((ReflectorSocket*)fSockets->GetSocketA())->GetPacket();
            if (thePacket == NULL)
            {   //qtss_printf("ReflectorStream::PushPacket GetPacket() is NULL\n");
                return;
            }
    
            OSMutexLocker locker(((ReflectorSocket*)(fSockets->GetSocketA()))->GetDemuxer()->GetMutex());
            thePacket->SetPacketData(packet, packetLen);
             ((ReflectorSocket*)fSockets->GetSocketA())->ProcessPacket(OS::Milliseconds(),thePacket,0,0);
             ((ReflectorSocket*)fSockets->GetSocketA())->Signal(Task::kIdleEvent);
        }
    }
}




ReflectorSender::ReflectorSender(ReflectorStream* inStream, UInt32 inWriteFlag)
:   fStream(inStream),
    fWriteFlag(inWriteFlag),
    fFirstNewPacketInQueue(NULL), 
    fFirstPacketInQueueForNewOutput(NULL),
    fHasNewPackets(false),
    fNextTimeToRun(0),
    fLastRRTime(0),
    fSocketQueueElem()
{   
    fSocketQueueElem.SetEnclosingObject(this); 
}

ReflectorSender::~ReflectorSender()
{
    //dequeue and delete every buffer
    qtss_printf("~ReflectorSender\n");
    while (fPacketQueue.GetLength() > 0)
    {
        ReflectorPacket* packet = (ReflectorPacket*)fPacketQueue.DeQueue()->GetEnclosingObject();
        //qtss_printf("will delete pkt:%x\n",packet);
        delete packet;
    }
}


Bool16 ReflectorSender::ShouldReflectNow(const SInt64& inCurrentTime, SInt64* ioWakeupTime)
{
    Assert(ioWakeupTime != NULL);
    //check to make sure there actually is work to do for this stream.
    //qtss_printf("fHasNewPackets:%d\n",fHasNewPackets);
    if ((!fHasNewPackets) && ((fNextTimeToRun == 0) || (inCurrentTime < fNextTimeToRun)))
    {
        //We don't need to do work right now, but
        //this stream must still communicate when it needs to be woken up next
        SInt64 theWakeupTime = fNextTimeToRun + inCurrentTime;
        //qtss_printf("ReflectorSender::ShouldReflectNow theWakeupTime=%qd fNextTimeToRun=%qd  ioWakepTime=%qd\n", theWakeupTime, fNextTimeToRun,*ioWakeupTime);
        if ((fNextTimeToRun > 0) && (theWakeupTime < *ioWakeupTime))
            *ioWakeupTime = theWakeupTime;
        return false;
    }
    return true;    
}

UInt32 ReflectorSender::GetOldestPacketRTPTime(Bool16 *foundPtr)             
{
    if (foundPtr != NULL) 
        *foundPtr = false;
    OSMutexLocker locker(&fStream->fBucketMutex);
    OSQueueElem* packetElem = this->GetClientBufferStartPacket();
    if (packetElem == NULL)
        return 0;
        
    ReflectorPacket* thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
    if (thePacket == NULL)
        return 0;
        
    if (foundPtr != NULL) 
        *foundPtr = true;
        
    return thePacket->GetPacketRTPTime();
}

UInt16 ReflectorSender::GetFirstPacketRTPSeqNum(Bool16 *foundPtr)             
{
    if (foundPtr != NULL) 
        *foundPtr = false;
        
    UInt16 resultSeqNum = 0;
    OSMutexLocker locker(&fStream->fBucketMutex);
    OSQueueElem* packetElem = this->GetClientBufferStartPacket();
            
    if (packetElem == NULL)
        return 0;
        
    ReflectorPacket* thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
    if (thePacket == NULL)
        return 0;
   
    if (foundPtr != NULL) 
        *foundPtr = true;
    
    resultSeqNum = thePacket->GetPacketRTPSeqNum();
        
   return resultSeqNum;
}

OSQueueElem*    ReflectorSender::GetClientBufferNextPacketTime(UInt32 inRTPTime)
{
        
    OSQueueIter qIter(&fPacketQueue);// start at oldest packet in q
    OSQueueElem* requestedPacket = NULL;
    OSQueueElem* elem =  NULL;
    
    while ( !qIter.IsDone() ) // start at oldest packet in q
    {
        elem = qIter.GetCurrent();
        
        if (requestedPacket == NULL)
            requestedPacket = elem;
        
        if (requestedPacket == NULL)
            break;
            
        ReflectorPacket* thePacket = (ReflectorPacket*)elem->GetEnclosingObject();      
        Assert( thePacket );
                 
        if (thePacket->GetPacketRTPTime() > inRTPTime)
        {
            requestedPacket = elem; // return the first packet we have that has a later time
            break; // found the packet we need: done processing
        }
        qIter.Next();
        
        
    }

    return requestedPacket;
}

Bool16 ReflectorSender::GetFirstRTPTimePacket(UInt16* outSeqNumPtr, UInt32* outRTPTimePtr, SInt64* outArrivalTimePtr) 
{
    OSMutexLocker locker(&fStream->fBucketMutex);
    OSQueueElem* packetElem = this->GetClientBufferStartPacketOffset(ReflectorStream::sFirstPacketOffsetMsec);
            
    if (packetElem == NULL)
        return false;
        
    ReflectorPacket* thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
    if (thePacket == NULL)
        return false;
    
    packetElem = GetClientBufferNextPacketTime(thePacket->GetPacketRTPTime());
    if (packetElem == NULL)
        return false;

    thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
    if (thePacket == NULL)
        return false;
    
    if (outSeqNumPtr)
        *outSeqNumPtr = thePacket->GetPacketRTPSeqNum();
        
    if (outRTPTimePtr)
        *outRTPTimePtr = thePacket->GetPacketRTPTime();

    if (outArrivalTimePtr)
        *outArrivalTimePtr = thePacket->fTimeArrived;
        
   return true;
}

Bool16 ReflectorSender::GetFirstPacketInfo(UInt16* outSeqNumPtr, UInt32* outRTPTimePtr, SInt64* outArrivalTimePtr) 
{
    OSMutexLocker locker(&fStream->fBucketMutex);
    OSQueueElem* packetElem = this->GetClientBufferStartPacketOffset(ReflectorStream::sFirstPacketOffsetMsec);
//    OSQueueElem* packetElem = this->GetClientBufferStartPacket();
            
    if (packetElem == NULL)
        return false;
        
    ReflectorPacket* thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
    if (thePacket == NULL)
        return false;
       
    if (outSeqNumPtr)
        *outSeqNumPtr = thePacket->GetPacketRTPSeqNum();
        
    if (outRTPTimePtr)
        *outRTPTimePtr = thePacket->GetPacketRTPTime();

    if (outArrivalTimePtr)
        *outArrivalTimePtr = thePacket->fTimeArrived;
        
    thePacket->fNeededByOutput = true; 

   return true;
}


#if REFLECTOR_STREAM_DEBUGGING
static UInt16 DGetPacketSeqNumber(StrPtrLen* inPacket)
{
    if (inPacket->Len < 4)
        return 0;
    
    //The RTP seq number is the second short of the packet
    UInt16* seqNumPtr = (UInt16*)inPacket->Ptr;
    return ntohs(seqNumPtr[1]);
}



#endif


void ReflectorSender::ReflectRelayPackets(SInt64* ioWakeupTime, OSQueue* inFreeQueue)
{   
    //Most of this code is useless i.e. buckets and bookmarks. This code will get cleaned up eventually

	//printf("ReflectorSender::ReflectPackets %qd %qd\n",*ioWakeupTime,fNextTimeToRun);
	UInt32 callid = 0;
#if DEBUG
	Assert(ioWakeupTime != NULL);
#endif
	#if REFLECTOR_STREAM_DEBUGGING > 2
	Bool16	printQueueLenOnExit = false;
	#endif	

	SInt64 currentTime = OS::Milliseconds();

	//make sure to reset these state variables
	fHasNewPackets = false;	
	fNextTimeToRun = 1000;	// init to 1 secs
	
	//determine if we need to send a receiver report to the multicast source
	if ((fWriteFlag == qtssWriteFlagsIsRTCP) && (currentTime > (fLastRRTime + kRRInterval)))
	{
		fLastRRTime = currentTime;
		fStream->SendReceiverReport();
		#if REFLECTOR_STREAM_DEBUGGING > 2
		printQueueLenOnExit = true;
		//printf( "fPacketQueue len %li\n", (SInt32)fPacketQueue.GetLength() );
		#endif	
	}
	
	//the rest of this function must be atomic wrt the ReflectorSession, because
	//it involves iterating through the RTPSession array, which isn't thread safe
	OSMutexLocker locker(&fStream->fBucketMutex);
	
	// Check to see if we should update the session's bitrate average
	if ((fStream->fLastBitRateSample + ReflectorStream::kBitRateAvgIntervalInMilSecs) < currentTime)
	{
		unsigned int intervalBytes = fStream->fBytesSentInThisInterval;
		(void)atomic_sub(&fStream->fBytesSentInThisInterval, intervalBytes);
		
		// Multiply by 1000 to convert from milliseconds to seconds, and by 8 to convert from bytes to bits
		Float32 bps = (Float32)(intervalBytes * 8) / (Float32)(currentTime - fStream->fLastBitRateSample);
		bps *= 1000;
		fStream->fCurrentBitRate = (UInt32)bps;
		
		// Don't check again for awhile!
		fStream->fLastBitRateSample = currentTime;
	}

	for (UInt32 bucketIndex = 0; bucketIndex < fStream->fNumBuckets; bucketIndex++)
	{	
		for (UInt32 bucketMemberIndex = 0; bucketMemberIndex < fStream->sBucketSize; bucketMemberIndex++)
		{	 
			ReflectorOutput* theOutput = fStream->fOutputArray[bucketIndex][bucketMemberIndex];
		
			
			if (theOutput != NULL)
			{	
				SInt32			availBookmarksPosition = -1;	// -1 == invalid position
				OSQueueElem*	packetElem = NULL;				
				UInt32			curBookmark = 0;
				
				Assert( curBookmark < theOutput->fNumBookmarks );		
				
				// see if we've bookmarked a held packet for this Sender in this Output
				while ( curBookmark < theOutput->fNumBookmarks )
				{					
					OSQueueElem* 	bookmarkedElem = theOutput->fBookmarkedPacketsElemsArray[curBookmark];
					
					if ( bookmarkedElem )	// there may be holes in this array
					{							
						if ( bookmarkedElem->IsMember( fPacketQueue ) )	
						{	
							// this packet was previously bookmarked for this specific queue
							// remove if from the bookmark list and use it
							// to jump ahead into the Sender's over all packet queue						
							theOutput->fBookmarkedPacketsElemsArray[curBookmark] = NULL;
							availBookmarksPosition = curBookmark;
							packetElem = bookmarkedElem;
							break;
						}
						
					}
					else
					{
						availBookmarksPosition = curBookmark;
					}
					
					curBookmark++;
						
				}
				
				Assert( availBookmarksPosition != -1 );		
				
				#if REFLECTOR_STREAM_DEBUGGING > 1
				if ( packetElem )	// show 'em what we got johnny
				{	ReflectorPacket* 	thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
					printf("Bookmarked packet time: %li, packetSeq %i\n", (SInt32)thePacket->fTimeArrived, DGetPacketSeqNumber( &thePacket->fPacketPtr ) );			
				}
				#endif
				
				// the output did not have a bookmarked packet if it's own
				// so show it the first new packet we have in this sender.
				// ( since TCP flow control may delay the sending of packets, this may not
				// be the same as the first packet in the queue
				if ( packetElem  == NULL )
				{	
					packetElem = fFirstNewPacketInQueue;
						
					#if REFLECTOR_STREAM_DEBUGGING > 1
					if ( packetElem )	// show 'em what we got johnny
					{	
						ReflectorPacket* 	thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
						printf("1st NEW packet from Sender sess 0x%lx time: %li, packetSeq %i\n", (SInt32)theOutput, (SInt32)thePacket->fTimeArrived, DGetPacketSeqNumber( &thePacket->fPacketPtr ) );			
					}
					else
						printf("no new packets\n" );
					#endif
				}
				
				OSQueueIter qIter(&fPacketQueue, packetElem);  // starts from beginning if packetElem == NULL, else from packetElem
				
				Bool16			dodBookmarkPacket = false;
				
				while ( !qIter.IsDone() )
				{					
					packetElem = qIter.GetCurrent();
					
					ReflectorPacket* 	thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
					QTSS_Error			err = QTSS_NoErr;
					
					#if REFLECTOR_STREAM_DEBUGGING > 2
					printf("packet time: %li, packetSeq %i\n", (SInt32)thePacket->fTimeArrived, DGetPacketSeqNumber( &thePacket->fPacketPtr ) );			
					#endif
					
					// once we see a packet we cant' send, we need to stop trying
					// during this pass mark remaining as still needed
					if ( !dodBookmarkPacket )
					{
						SInt64  packetLateness =  currentTime - thePacket->fTimeArrived - (ReflectorStream::sBucketDelayInMsec * (SInt64)bucketIndex);
					    // packetLateness measures how late this packet it after being corrected for the bucket delay
						
						#if REFLECTOR_STREAM_DEBUGGING > 2
						printf("packetLateness %li, seq# %li\n", (SInt32)packetLateness, (SInt32) DGetPacketSeqNumber( &thePacket->fPacketPtr ) );			
						#endif
						
						SInt64 timeToSendPacket = -1;
						err = theOutput->WritePacket(&thePacket->fPacketPtr, fStream, fWriteFlag, packetLateness, &timeToSendPacket, NULL, NULL, false,callid);
					
						if ( err == QTSS_WouldBlock )
						{	
							#if REFLECTOR_STREAM_DEBUGGING > 2
							printf("EAGAIN bookmark: %li, packetSeq %i\n", (SInt32)packetLateness, DGetPacketSeqNumber( &thePacket->fPacketPtr ) );			
							#endif
							// tag it and bookmark it
							thePacket->fNeededByOutput = true;
							
							Assert( availBookmarksPosition != -1 );
							if ( availBookmarksPosition != -1 )
								theOutput->fBookmarkedPacketsElemsArray[availBookmarksPosition] =  packetElem;

							dodBookmarkPacket = true;
							
							// call us again in # ms to retry on an EAGAIN
							if ((timeToSendPacket > 0) && (fNextTimeToRun > timeToSendPacket ))
								fNextTimeToRun = timeToSendPacket;
							if ( timeToSendPacket == -1 )
								this->SetNextTimeToRun(5); // keep in synch with delay on would block for on-demand lower is better for high-bit rate movies.
						
						}
					}
					else
					{	
						if ( thePacket->fNeededByOutput )	// optimization: if the packet is already marked, another Output has been through this already
							break;
						thePacket->fNeededByOutput = true;
					}
					
					qIter.Next();
				} 
				
			}
		}
	}
	
	// reset our first new packet bookmark
	fFirstNewPacketInQueue = NULL;

	// iterate one more through the senders queue to clear out
	// the unneeded packets
	OSQueueIter removeIter(&fPacketQueue);
	while ( !removeIter.IsDone() )
	{
		OSQueueElem* elem = removeIter.GetCurrent();
		Assert( elem );

		//at this point, move onto the next queue element, because we may be altering
		//the queue itself in the code below
		removeIter.Next();

		ReflectorPacket* thePacket = (ReflectorPacket*)elem->GetEnclosingObject();		
		Assert( thePacket );
		
		if ( thePacket->fNeededByOutput == false )
		{	
			thePacket->fNeededByOutput = true;
			fPacketQueue.Remove( elem );
			inFreeQueue->EnQueue( elem );
			
		}
		else	// reset for next call to ReflectPackets
		{
			thePacket->fNeededByOutput = false;
		}
	}
	
	//Don't forget that the caller also wants to know when we next want to run
	if (*ioWakeupTime == 0)
		*ioWakeupTime = fNextTimeToRun;
	else if ((fNextTimeToRun > 0) && (*ioWakeupTime > fNextTimeToRun))
		*ioWakeupTime = fNextTimeToRun;
	// exit with fNextTimeToRun in real time, not relative time.
	fNextTimeToRun += currentTime;
	
	#if REFLECTOR_STREAM_DEBUGGING > 2
	if ( printQueueLenOnExit )
		printf( "EXIT fPacketQueue len %li\n", (SInt32)fPacketQueue.GetLength() );
	#endif
}

/***********************************************************************************************
/   ReflectorSender::ReflectPackets
/   
/   There are n ReflectorSender's for n output streams per presentation.
/   
/   Each sender is associated with an array of ReflectorOutput's.  Each
/   output represents a client connection.  Each output has # RTPStream's. 
/   
/   When we write a packet to the ReflectorOutput he matches it's payload
/   to one of his streams and sends it there.
/   
/   To smooth the bandwitdth (server, not user) requirements of the reflected streams, the Sender
/   groups the ReflectorOutput's into buckets.  The input streams are reflected to
/   each bucket progressively later in time.  So rather than send a single packet
/   to say 1000 clients all at once, we send it to just the first 16, then then next 16 
/   100 ms later and so on.
/
/
/   intputs     ioWakeupTime - relative time to call us again in MSec
/               inFreeQueue - queue of free packets.
*/

void ReflectorSender::ReflectPackets(SInt64* ioWakeupTime, OSQueue* inFreeQueue)
{
    //qtss_printf("Entry ReflectPackets\n");
    if (!fStream->BufferEnabled()) // Call old routine for relays; they don't want buffering.
    {
        this->ReflectRelayPackets(ioWakeupTime,inFreeQueue);
        return;
    }
   
   QTSS_Error sendErr=0;
    
    SInt64 currentTime = OS::Milliseconds();

    //make sure to reset these state variables
    fHasNewPackets = false; 

	//fNextTimeToRun = 1000;	// init to 1 secs
         
   // if (fWriteFlag == qtssWriteFlagsIsRTCP)
    //    fNextTimeToRun = 1000;
    fNextTimeToRun =0;
    
   
    //determine if we need to send a receiver report to the multicast source
    if ((fWriteFlag == qtssWriteFlagsIsRTCP) && (currentTime > (fLastRRTime + kRRInterval)))
    {
        fLastRRTime = currentTime;
        fStream->SendReceiverReport();
    }
    
    //the rest of this function must be atomic wrt the ReflectorSession, because
    //it involves iterating through the RTPSession array, which isn't thread safe
    OSMutexLocker locker(&fStream->fBucketMutex);
    
    // Check to see if we should update the session's bitrate average
    fStream->UpdateBitRate(currentTime);

    // where to start new clients in the q
    fFirstPacketInQueueForNewOutput = this->GetClientBufferStartPacketOffset(0); 
    //qtss_printf("fFirstPacketInQueueForNewOutput:%x\n",fFirstPacketInQueueForNewOutput);

#if (0) //test code 
    if (NULL != fFirstPacketInQueueForNewOutput)
        printf("ReflectorSender::ReflectPackets SET first packet fFirstPacketInQueueForNewOutput %d \n",  DGetPacketSeqNumber( &( (ReflectorPacket*) ( fFirstPacketInQueueForNewOutput->GetEnclosingObject()))->fPacketPtr ));
    
    ReflectorPacket* thePacket = NULL;
    if (fFirstPacketInQueueForNewOutput != NULL)
        thePacket = (ReflectorPacket*) fFirstPacketInQueueForNewOutput->GetEnclosingObject();
    if (thePacket == NULL)
    {    printf("fFirstPacketInQueueForNewOutput is NULL \n");
      
    }

#endif

	Bool16 firstPacket =false;

    //qtss_printf("fNumBuckets:%d,sBucketSize:%d\n",fStream->fNumBuckets,fStream->sBucketSize);
    for (UInt32 bucketIndex = 0; bucketIndex < fStream->fNumBuckets; bucketIndex++)
    {   
        for (UInt32 bucketMemberIndex = 0; bucketMemberIndex < fStream->sBucketSize; bucketMemberIndex++)
        {    
            ReflectorOutput* theOutput = fStream->fOutputArray[bucketIndex][bucketMemberIndex];
            //if(theOutput == NULL)
           // {
               //RLogRequest(DEBUG_ARTS_MODULE,0,"output null,bucketMemberIndex:%d",bucketMemberIndex);
            //}
            
            if (theOutput != NULL)
            {  
                //RLogRequest(DEBUG_ARTS_MODULE,theOutput->callid,"will send packets,bucketMemberIndex:%d",bucketMemberIndex);  
                if ( false == theOutput->IsPlaying() ) 
                {
                   // RLogRequest(DEBUG_ARTS_MODULE,theOutput->callid,"noPlaying,index:%d",bucketMemberIndex); 
                    continue;
                }
                    
                OSQueueElem*    packetElem = theOutput->GetBookMarkedPacket(&fPacketQueue); 
                
                //qtss_printf("1 packetElem:%x\n",packetElem);
                
                if ( packetElem  == NULL ) // should only be a new output
                {                  
                    packetElem = fFirstPacketInQueueForNewOutput; // everybody starts at the oldest packet in the buffer delay or uses a bookmark
                    firstPacket = true;
                    theOutput->fNewOutput = false;    
                    //if (packetElem) printf("ReflectorSender::ReflectPackets Sending first packet in Queue packetElem=fFirstPacketInQueueForNewOutput %d \n",  ( (ReflectorPacket*) (packetElem->GetEnclosingObject() )  )->GetPacketRTPSeqNum());

                 }
                
                SInt64  bucketDelay = ReflectorStream::sBucketDelayInMsec * (SInt64)bucketIndex;
               //RLogRequest(DEBUG_ARTS_MODULE,theOutput->callid,"will SendPacketsToOutput.packetElem:%x",packetElem);
                packetElem = this->SendPacketsToOutput(theOutput, packetElem,currentTime, bucketDelay, firstPacket,&sendErr);
                if (packetElem && (bucketIndex!= (fStream->fNumBuckets-1)) &&  (bucketMemberIndex!= (fStream->sBucketSize-1)))
                {
                    ReflectorPacket*    thePacket = (ReflectorPacket*)packetElem->GetEnclosingObject();
                    thePacket->fNeededByOutput = true; // flag to prevent removal in RemoveOldPackets
                    (void) theOutput->SetBookMarkPacket(packetElem); // store a reference to the packet
                    RLogRequest(DEBUG_ARTS_MODULE,theOutput->callid,"SetBookMarkPacket");
                }
                
                
                if(!packetElem)
                {
                    RLogRequest(DEBUG_ARTS_MODULE,theOutput->callid,"packetElem is null,sendErr:%d",sendErr);
                     if(sendErr == QTSS_NotConnected)
                     {
                        fStream->RemoveOutput(theOutput);
                        RLogRequest(INFO_ARTS_MODULE,theOutput->callid,"RemoveOutput,will close this session");
                     }
                }
            } 
        }
    }

    this->RemoveOldPackets(inFreeQueue);
    fFirstNewPacketInQueue = NULL;

    //Don't forget that the caller also wants to know when we next want to run
    if (*ioWakeupTime == 0)
        *ioWakeupTime = fNextTimeToRun;
    else if ((fNextTimeToRun > 0) && (*ioWakeupTime > fNextTimeToRun))
        *ioWakeupTime = fNextTimeToRun;
    RLogRequest(DEBUG_ARTS_MODULE,0,"fNextTimeToRun:%d",fNextTimeToRun);
    // exit with fNextTimeToRun in real time, not relative time.
    fNextTimeToRun += currentTime;
     
    //qtss_printf("SetNextTimeToRun fNextTimeToRun=%qd + currentTime=%qd\n", fNextTimeToRun, currentTime);
   // qtss_printf("ReflectorSender::ReflectPackets *ioWakeupTime = %qd\n", *ioWakeupTime);

}

OSQueueElem*    ReflectorSender::SendPacketsToOutput(ReflectorOutput* theOutput, OSQueueElem* currentPacket, SInt64 currentTime,  SInt64  bucketDelay, Bool16 firstPacket,QTSS_Error*sendErr)
{
    OSQueueElem* lastPacket = currentPacket;
    OSQueueIter qIter(&fPacketQueue, currentPacket);  // starts from beginning if currentPacket == NULL, else from currentPacket                
    
    UInt32 count = 0;
    QTSS_Error err = QTSS_NoErr;
    RLogRequest(DEBUG_ARTS_MODULE,theOutput->callid,"SendPacketsToOutput Entry,isDone:%d",qIter.IsDone());
    while ( !qIter.IsDone() )
    {                   
        currentPacket = qIter.GetCurrent();
        lastPacket = currentPacket;
        
        ReflectorPacket*    thePacket = (ReflectorPacket*)currentPacket->GetEnclosingObject();
        SInt64  packetLateness =  bucketDelay;
        SInt64 timeToSendPacket = -1;
              
                 
        RLogRequest(DEBUG_ARTS_MODULE,theOutput->callid,"packet len %qd,err:%d\n", thePacket->fPacketPtr.Len,err);                                   
        err = theOutput->WritePacket(&thePacket->fPacketPtr, fStream, fWriteFlag, packetLateness, &timeToSendPacket,&thePacket->fStreamCountID,&thePacket->fTimeArrived, firstPacket,theOutput->callid );                

        
        if (err == QTSS_WouldBlock)
        { // call us again in # ms to retry on an EAGAIN
            
            if ((timeToSendPacket > 0) && ( (fNextTimeToRun + currentTime) > timeToSendPacket )) // blocked but we are scheduled to wake up later
                fNextTimeToRun = timeToSendPacket - currentTime;
            
            if (theOutput->fLastIntervalMilliSec < 5 )
                theOutput->fLastIntervalMilliSec = 5;

            if ( timeToSendPacket < 0 ) // blocked and we are behind
            {    //qtss_printf("fNextTimeToRun = theOutput->fLastIntervalMilliSec=%qd;\n", theOutput->fLastIntervalMilliSec); // Use the last packet interval 
                 this->SetNextTimeToRun(theOutput->fLastIntervalMilliSec);
            }
               
            if (fNextTimeToRun > 100) //don't wait that long
            {    //qtss_printf("fNextTimeToRun = %qd now 100;\n", fNextTimeToRun);
                 this->SetNextTimeToRun(100);
            }

            if (fNextTimeToRun < 5) //wait longer
            {    //qtss_printf("fNextTimeToRun = 5;\n");
                 this->SetNextTimeToRun(5);
            }

            if (theOutput->fLastIntervalMilliSec >= 100) // allow up to 1 second max -- allow some time for the socket to clear and don't go into a tight loop if the client is gone.
                theOutput->fLastIntervalMilliSec = 100;
            else
                theOutput->fLastIntervalMilliSec *= 2; // scale upwards over time

          //  qtss_printf ( "Blocked ReflectorSender::SendPacketsToOutput timeToSendPacket=%qd fLastIntervalMilliSec=%qd fNextTimeToRun=%qd \n", timeToSendPacket, theOutput->fLastIntervalMilliSec, fNextTimeToRun);
           
           break;
        }else if(err!= QTSS_NoErr)
		{
		   (* sendErr) = err;
		   RLogRequest(DEBUG_ARTS_MODULE,theOutput->callid,"send err:%d",err);
		   return NULL;
		}

        count++;
        qIter.Next();
    
    }

    return lastPacket;
}

OSQueueElem*    ReflectorSender::GetClientBufferStartPacketOffset(SInt64 offsetMsec)
{
        
    OSQueueIter qIter(&fPacketQueue);// start at oldest packet in q
    SInt64 theCurrentTime = OS::Milliseconds();
    SInt64 packetDelay = 0;
    OSQueueElem* oldestPacketInClientBufferTime = NULL;
    
    
    while ( !qIter.IsDone() ) // start at oldest packet in q
    {
        OSQueueElem* elem = qIter.GetCurrent();
        Assert( elem );
        qIter.Next();
       
        ReflectorPacket* thePacket = (ReflectorPacket*)elem->GetEnclosingObject();      
        Assert( thePacket );
             
        packetDelay = theCurrentTime - thePacket->fTimeArrived;
        if (offsetMsec > ReflectorStream::sOverBufferInMsec)
            offsetMsec = ReflectorStream::sOverBufferInMsec;
       
         
        if ( packetDelay <= (ReflectorStream::sOverBufferInMsec - offsetMsec) ) 
        {   
             //qtss_printf("packetDelay:%d,offsetMsec:%d\n",packetDelay,offsetMsec);  
            oldestPacketInClientBufferTime = &thePacket->fQueueElem;
            break; // found the packet we need: done processing
        }
        
    }
    
    return oldestPacketInClientBufferTime;
}

void    ReflectorSender::RemoveOldPackets(OSQueue* inFreeQueue)
{
        
// Iterate through the senders queue to clear out packets
// Start at the oldest packet and walk forward to the newest packet
// 
    OSQueueIter removeIter(&fPacketQueue);
    SInt64 theCurrentTime = OS::Milliseconds();
    SInt64 packetDelay = 0;
    SInt64 currentMaxPacketDelay = ReflectorStream::sMaxPacketAgeMSec;
    

    while ( !removeIter.IsDone() )
    {
        OSQueueElem* elem = removeIter.GetCurrent();
        Assert( elem );
    
        //at this point, move onto the next queue element, because we may be altering
        //the queue itself in the code below
        removeIter.Next();
    
        ReflectorPacket* thePacket = (ReflectorPacket*)elem->GetEnclosingObject();      
        Assert( thePacket );
       // qtss_printf("ReflectorSender::RemoveOldPackets Packet %x in queue is %qd milliseconds old\n",thePacket,theCurrentTime - thePacket->fTimeArrived);
        //printf("ReflectorSender::RemoveOldPackets Packet %d in queue is %qd milliseconds old\n", DGetPacketSeqNumber( &thePacket->fPacketPtr ) ,theCurrentTime - thePacket->fTimeArrived);
    
            
        packetDelay = theCurrentTime - thePacket->fTimeArrived;
                
        // walk q and remove packets that are too old
        if ( !thePacket->fNeededByOutput && packetDelay > currentMaxPacketDelay) // delete based on late tolerance and whether a client is blocked on the packet
        {   // not needed and older than our required buffer
            thePacket->Reset();
            fPacketQueue.Remove( elem );
            inFreeQueue->EnQueue( elem );
        }
        else   
        {   // we want to keep all of these but we should reset the ones that should be aged out unless marked
            // as need the next time through reflect packets.
            
            thePacket->fNeededByOutput = false; //mark not needed.. will be set next time through reflect packets
            if (packetDelay <= currentMaxPacketDelay)  // this packet is going to be kept around as well as the ones that follow.
                break;
        }   
    }
    

}

void ReflectorSocketPool::SetUDPSocketOptions(UDPSocketPair* inPair)
{
    // Fix add ReuseAddr for compatibility with MPEG4IP broadcaster which likes to use the same
    //sockets.  
    
    //Make sure this works with PlaylistBroadcaster 
    //inPair->GetSocketA()->ReuseAddr();
    //inPair->GetSocketA()->ReuseAddr();

}


UDPSocketPair* ReflectorSocketPool::ConstructUDPSocketPair()
{
    return NEW UDPSocketPair
        (NEW ReflectorSocket(), NEW ReflectorSocket());
}

void ReflectorSocketPool::DestructUDPSocket(ReflectorSocket* socket)
{
    if (socket) // allocated
    {
        if (socket->GetLocalPort() > 0) // bound and active
        {   //The socket's run function may be executing RIGHT NOW! So we can't
            //just delete the thing, we need to send the sockets kill events.
            //qtss_printf("ReflectorSocketPool::DestructUDPSocketPair Signal kKillEvent socket=%p\n",socket);
            socket->Signal(Task::kKillEvent);
        }
        else // not bound ok to delete
        {   //qtss_printf("ReflectorSocketPool::DestructUDPSocketPair delete socket=%p\n",socket);
            delete socket;
        }  
    }
}

void ReflectorSocketPool::DestructUDPSocketPair(UDPSocketPair *inPair)
{
    //qtss_printf("ReflectorSocketPool::DestructUDPSocketPair inPair=%p socketA=%p\n", inPair,(ReflectorSocket*)inPair->GetSocketA());
    this->DestructUDPSocket((ReflectorSocket*)inPair->GetSocketA());
    
    //qtss_printf("ReflectorSocketPool::DestructUDPSocketPair inPair=%p socketB=%p\n", inPair,(ReflectorSocket*)inPair->GetSocketB());
    this->DestructUDPSocket((ReflectorSocket*)inPair->GetSocketB());
    
    delete inPair;
}

ReflectorSocket::ReflectorSocket()
:   IdleTask(), 
    UDPSocket(NULL, Socket::kNonBlockingSocketType | UDPSocket::kWantsDemuxer), 
    fBroadcasterClientSession(NULL), 
    fLastBroadcasterTimeOutRefresh(0), 
    fSleepTime(0),
    fValidSSRC(0),
    fLastValidSSRCTime(0),
    fFilterSSRCs(true),
    fTimeoutSecs(30),
    fHasReceiveTime(false),
    fFirstReceiveTime(0),
    fFirstArrivalTime(0),
    fCurrentSSRC(0)     
{
    //construct all the preallocated packets
    this->SetTaskName("ReflectorSocket");
    this->SetTask(this);
    fTCPDataSocket = NULL;
    fRTSPClient = NULL;
    fReceiveBufPtr.Set(fReceiveBuf,0) ;
    for (UInt32 numPackets = 0; numPackets < kNumPreallocatedPackets; numPackets++)
    {
        //If the local port # of this socket is odd, then all the packets
        //used for this socket are rtcp packets.
        ReflectorPacket* packet = NEW ReflectorPacket();
        fFreeQueue.EnQueue(&packet->fQueueElem);//put this packet onto the free queue
    }
    fLastTCPPacketDemux= NULL;
    fTempBufDataLen=0; 
    fTransportType=0;
}

ReflectorSocket::~ReflectorSocket()
{
   //qtss_printf("~ReflectorSocket\n");
   if(fTCPDataSocket != NULL)
        fTCPDataSocket = NULL;
    
   if(fRTSPClient != NULL)
    fRTSPClient = NULL;
   if(fLastTCPPacketDemux != NULL)
   {
      delete fLastTCPPacketDemux;
      fLastTCPPacketDemux = NULL;
   }
    while (fFreeQueue.GetLength() > 0)
    
    {
        ReflectorPacket* packet = (ReflectorPacket*)fFreeQueue.DeQueue()->GetEnclosingObject();
        //qtss_printf("will delete packet:%x\n",packet);
        delete packet;
    }
    
    //qtss_printf("~ReflectorSocket finished\n");
}

void    ReflectorSocket::AddSender(ReflectorSender* inSender)
{
    qtss_printf("add sender:%x,ReflectorSocket:%x\n",inSender,this);
    OSMutexLocker locker(this->GetDemuxer()->GetMutex());
    QTSS_Error err = this->GetDemuxer()->RegisterTask(inSender->fStream->fStreamInfo.fSrcIPAddr, 0, inSender);
    Assert(err == QTSS_NoErr);
    fSenderQueue.EnQueue(&inSender->fSocketQueueElem);
}

void    ReflectorSocket::RemoveSender(ReflectorSender* inSender)
{
    qtss_printf("remove sender:%x\n",inSender);
    OSMutexLocker locker(this->GetDemuxer()->GetMutex());
    fSenderQueue.Remove(&inSender->fSocketQueueElem);
    QTSS_Error err = this->GetDemuxer()->UnregisterTask(inSender->fStream->fStreamInfo.fSrcIPAddr, 0, inSender);
    Assert(err == QTSS_NoErr);
}

SInt64 ReflectorSocket::Run()
{
    //We want to make sure we can't get idle events WHILE we are inside
    //this function. That will cause us to run the queues unnecessarily
    //and just get all confused.
    //qtss_printf("ReflectorSocket:Run()\n");
    this->CancelTimeout();
    
    Task::EventFlags theEvents = this->GetEvents();
    //if we have been told to delete ourselves, do so.
    if (theEvents & Task::kKillEvent)
        return -1;

    OSMutexLocker locker(this->GetDemuxer()->GetMutex());
    SInt64 theMilliseconds = OS::Milliseconds();
    
    //Only check for data on the socket if we've actually been notified to that effect
    if (theEvents & Task::kReadEvent)
    {
	    //qtss_printf("start getIncomingData,this:%x\n",this);
        this->GetIncomingData(theMilliseconds);
    }
#if DEBUG
    //make sure that we haven't gotten here prematurely! This wouldn't mess
    //anything up, but it would waste CPU.
    if (theEvents & Task::kIdleEvent)
    {
        SInt32 temp = (SInt32)(fSleepTime - theMilliseconds);
        char tempBuf[20];
        qtss_sprintf(tempBuf,"%"_S32BITARG_"",temp);
        WarnV(fSleepTime <= theMilliseconds, tempBuf);
    }
#endif
    
    fSleepTime = 0;
    //Now that we've gotten all available packets, have the streams reflect
    //RLogRequest(DEBUG_ARTS_MODULE,0,"before ReflectPackets,port:%d\n",this->GetLocalPort());
    for (OSQueueIter iter2(&fSenderQueue); !iter2.IsDone(); iter2.Next())
    {             
        ReflectorSender* theSender2 = (ReflectorSender*)iter2.GetCurrent()->GetEnclosingObject();    
          
        if (theSender2 != NULL )
        {
            if(theSender2->ShouldReflectNow(theMilliseconds, &fSleepTime))
            {
                theSender2->ReflectPackets(&fSleepTime, &fFreeQueue);
                //qtss_printf("ReflectPackets,cur sender:%x\n",theSender2);
            }else
                RLogRequest(DEBUG_ARTS_MODULE,0,"!ShouldReflectNow\n");
         }
    }
    
    //qtss_printf("after ReflectPackets\n");
    
#if DEBUG
    theMilliseconds = OS::Milliseconds();
#endif
    
    //For smoothing purposes, the streams can mark when they want to wakeup.
    if (fSleepTime > 0)
        this->SetIdleTimer(fSleepTime);
#if DEBUG
    //The debugging check above expects real time.
    fSleepTime += theMilliseconds;
#endif
        
    return 0;
}


void ReflectorSocket::FilterInvalidSSRCs(ReflectorPacket* thePacket,Bool16 isRTCP)
{   // assume the first SSRC we see is valid and all others are to be ignored.
    if ( thePacket->fPacketPtr.Len > 0) do 
    {
        SInt64 currentTime = OS::Milliseconds() / 1000;
        if (0 == fValidSSRC)
        {   fValidSSRC = thePacket->GetSSRC(isRTCP); // SSRC of 0 is allowed
            fLastValidSSRCTime = currentTime;
            //qtss_printf("socket=%"_U32BITARG_" FIRST PACKET fValidSSRC=%"_U32BITARG_" \n", (UInt32) this,fValidSSRC);
            break;
        }
    
        UInt32 packetSSRC = thePacket->GetSSRC(isRTCP);
        if (packetSSRC != 0)
        {   
            if (packetSSRC == fValidSSRC)
            {   fLastValidSSRCTime = currentTime;
                //qtss_printf("socket=%"_U32BITARG_" good packet\n", (UInt32) this );
                break;
            }
            
            //qtss_printf("socket=%"_U32BITARG_" bad packet packetSSRC= %"_U32BITARG_" fValidSSRC=%"_U32BITARG_" \n", (UInt32) this,packetSSRC,fValidSSRC);
            thePacket->fPacketPtr.Len = 0; // ignore this packet wrong SSRC
        }
        
        // this executes whenever an invalid SSRC is found -- maybe the original stream ended and a new one is now active
        if ( (fLastValidSSRCTime + fTimeoutSecs) < currentTime) // fValidSSRC timed out --no packets with this SSRC seen for awhile
        {   fValidSSRC = 0; // reset the valid SSRC with the next packet's SSRC
            //qtss_printf("RESET fValidSSRC\n");
        }

    }while (false);
}


Bool16 ReflectorSocket::ProcessPacket(const SInt64& inMilliseconds,ReflectorPacket* thePacket,UInt32 theRemoteAddr,UInt16 theRemotePort)
{   
    Bool16 done = false; // stop when result is true
    //qtss_printf("ProcessPacket,packet_len:%d\n",thePacket->fPacketPtr.Len);
    if (thePacket != NULL) do
    {
        if (GetLocalPort() & 1)
            thePacket->fIsRTCP = true;
        else
            thePacket->fIsRTCP = false;
       
        if (fBroadcasterClientSession != NULL) // alway refresh timeout even if we are filtering.
        {   if ( (inMilliseconds - fLastBroadcasterTimeOutRefresh) > kRefreshBroadcastSessionIntervalMilliSecs)
            {   QTSS_RefreshTimeOut(fBroadcasterClientSession);
                fLastBroadcasterTimeOutRefresh = inMilliseconds;
            }
        }

   
        if (thePacket->fPacketPtr.Len == 0)
        {
            //put the packet back on the free queue, because we didn't actually
            //get any data here.
            fFreeQueue.EnQueue(&thePacket->fQueueElem);
            
            if(fTCPDataSocket!=NULL)
            {         
                fTCPDataSocket->RequestEvent(EV_RE);
            }else
                this->RequestEvent(EV_RE);
            done = true;
             //qtss_printf("ReflectorSocket::ProcessPacket no more packets on this socket!\n");
            break;//no more packets on this socket!
        }
        
        if (thePacket->IsRTCP())
        {
            //if this is a new RTCP packet, check to see if it is a sender report.
            //We should only reflect sender reports. Because RTCP packets can't have both
            //an SR & an RR, and because the SR & the RR must be the first packet in a
            //compound RTCP packet, all we have to do to determine this is look at the
            //packet type of the first packet in the compound packet.
            //qtss_printf("thePacket->IsRTCP()\n");
            RTCPPacket theRTCPPacket;
            if ((!theRTCPPacket.ParsePacket((UInt8*)thePacket->fPacketPtr.Ptr, thePacket->fPacketPtr.Len)) ||
                (theRTCPPacket.GetPacketType() != RTCPSRPacket::kSRPacketType))
            {
                //pretend as if we never got this packet
                fFreeQueue.EnQueue(&thePacket->fQueueElem);
                done = true;
                break;
            }
        }
        
        // Only reflect one SSRC stream at a time.
        // Pass the packet and whether it is an RTCP or RTP packet based on the port number.
        
        
        //if (fFilterSSRCs)
        //    this->FilterInvalidSSRCs(thePacket,GetLocalPort() & 1);// thePacket->fPacketPtr.Len is set to 0 for invalid SSRCs.
                 
        // Find the appropriate ReflectorSender for this packet.
        ReflectorSender* theSender = (ReflectorSender*)this->GetDemuxer()->GetTask(theRemoteAddr, 0);
        // If there is a generic sender for this socket, use it.
        if (theSender == NULL)
            theSender = (ReflectorSender*)this->GetDemuxer()->GetTask(0, 0);
        
        if (theSender == NULL)
        {   
            UInt16* theSeqNumberP = (UInt16*)thePacket->fPacketPtr.Ptr;
            //qtss_printf("ReflectorSocket::ProcessPacket no sender found for packet! sequence number=%d\n",ntohs(theSeqNumberP[1]));
            fFreeQueue.EnQueue(&thePacket->fQueueElem); // don't process the packet
            done = true;
            break;
        }
            
        Assert(theSender != NULL); // at this point we have a sender
            
        const UInt32 maxQSize = 10;
               
         // Check to see if we need to set the remote RTCP address
        // for this stream. This will be necessary if the source is unicast.
#ifdef NAT_WORKAROUND
        if ((theRemoteAddr != 0) && ((theSender->fStream->fDestRTCPAddr == 0) || (thePacket->IsRTCP()))) // Submitted fix from  denis@berlin.ccc.de
        {
            Assert(!SocketUtils::IsMulticastIPAddr(theSender->fStream->fStreamInfo.fDestIPAddr));
            Assert(theRemotePort != 0);
            theSender->fStream->fDestRTCPAddr = theRemoteAddr;
            theSender->fStream->fDestRTCPPort = theRemotePort;
   
            // RTCPs are always on odd ports, so check to see if this port is an
            // RTP port, and if so, just add 1.
            if (!(thePacket->IsRTCP()) && !(theRemotePort & 1))
                theSender->fStream->fDestRTCPPort++;
        }
#else
        if ((theRemoteAddr != 0) && (theSender->fStream->fDestRTCPAddr == 0))
        {
            // If the source is multicast, this shouldn't be necessary
            Assert(!SocketUtils::IsMulticastIPAddr(theSender->fStream->fStreamInfo.fDestIPAddr));
            Assert(theRemotePort != 0);
            theSender->fStream->fDestRTCPAddr = theRemoteAddr;
            theSender->fStream->fDestRTCPPort = theRemotePort;

            // RTCPs are always on odd ports, so check to see if this port is an
            // RTP port, and if so, just add 1.
            if (!(theRemotePort & 1))
                theSender->fStream->fDestRTCPPort++;
        }
#endif //NAT_WORKAROUND
        thePacket->fStreamCountID = ++(theSender->fStream->fPacketCount);
        thePacket->fBucketsSeenThisPacket = 0;
        thePacket->fTimeArrived = inMilliseconds;
        theSender->fPacketQueue.EnQueue(&thePacket->fQueueElem); 
           
        //qtss_printf("Enqueue packet:%x,packet_len:%d",thePacket,thePacket->fPacketPtr.Len);        
        if ( theSender->fFirstNewPacketInQueue == NULL )
            theSender->fFirstNewPacketInQueue = &thePacket->fQueueElem;                 
        theSender->fHasNewPackets = true;
           //qtss_printf("theSender->fHasNewPackets:%d\n",theSender->fHasNewPackets);
        
        if (!(thePacket->IsRTCP()))
        {
            // don't check for duplicate packets, they may be needed to keep in sync.
            // Because this is an RTP packet make sure to atomic add this because
            // multiple sockets can be adding to this variable simultaneously
            (void)atomic_add(&theSender->fStream->fBytesSentInThisInterval, thePacket->fPacketPtr.Len);
           // printf("ReflectorSocket::ProcessPacket received RTP id=%qu\n", thePacket->fStreamCountID); 
            theSender->fStream->SetHasFirstRTP(true);
         }
         else 
         {
            //printf("ReflectorSocket::ProcessPacket received RTCP id=%qu\n", thePacket->fStreamCountID); 
            theSender->fStream->SetHasFirstRTCP(true);
            theSender->fStream->SetFirst_RTCP_RTP_Time(thePacket->GetPacketRTPTime());
            theSender->fStream->SetFirst_RTCP_Arrival_Time(thePacket->fTimeArrived);
         }

         
        if (ReflectorStream::sUsePacketReceiveTime && thePacket->fPacketPtr.Len > 12)
        {
            UInt32 offset = thePacket->fPacketPtr.Len;
            char* theTag = ((char*) thePacket->fPacketPtr.Ptr + offset) - 12;
            UInt64* theValue =  (UInt64*) ((char*)  ( (char*)  thePacket->fPacketPtr.Ptr +  offset) - 8);
                            
            if (0 == ::strncmp(theTag,"aktt",4))
            {
                UInt64 theReceiveTime = OS::NetworkToHostSInt64(*theValue);
                UInt32 theSSRC = thePacket->GetSSRC(theRemotePort & 1); // use to check if broadcast has restarted so we can reset
                
                if ( !this->fHasReceiveTime || (this->fCurrentSSRC != theSSRC) )
                {
                    this->fCurrentSSRC = theSSRC;
                    this->fFirstArrivalTime = thePacket->fTimeArrived;
                    this->fFirstReceiveTime = theReceiveTime;
                    this->fHasReceiveTime = true;
                }
                
                   
                SInt64 packetOffsetFromStart = theReceiveTime - this->fFirstReceiveTime; // packets arrive at time 0 and fill forward into the future
                thePacket->fTimeArrived = this->fFirstArrivalTime + packetOffsetFromStart; // offset starts negative by over buffer amount
                thePacket->fPacketPtr.Len -= 12;
                
                SInt64 arrivalTimeOffset = thePacket->fTimeArrived - inMilliseconds;
                if ( arrivalTimeOffset > ReflectorStream::sMaxFuturePacketMSec ) // way out in the future.
                    thePacket->fTimeArrived = inMilliseconds + ReflectorStream::sMaxFuturePacketMSec; //keep it but only for sMaxFuturePacketMSec =  (sMaxPacketAgeMSec <-- current --> sMaxFuturePacketMSec)
                
                // if it was in the past we leave it alone because it will be deleted after processing.
                
                
               // printf("ReflectorSocket::ProcessPacket packetOffsetFromStart=%f,packet_len:%d\n", (Float32) packetOffsetFromStart / 1000,thePacket->fPacketPtr.Len);
            }
        
        }
         
        //printf("ReflectorSocket::GetIncomingData has packet from time=%qd src addr=%"_U32BITARG_" src port=%u packetlen=%"_U32BITARG_"\n",inMilliseconds, theRemoteAddr,theRemotePort,thePacket->fPacketPtr.Len);
        if (0) //turn on / off buffer size checking --  pref can go here if we find we need to adjust this
       
        if (theSender->fPacketQueue.GetLength() > maxQSize && fTCPDataSocket ) //don't grow memory too big
        { 
            qtss_printf("buffer queue is full");
            fFreeQueue.EnQueue(&thePacket->fQueueElem);
            fTCPDataSocket->RequestEvent(EV_RE);
            //char outMessage[256];
            //sprintf(outMessage,"Packet Queue for port=%d qsize = %"_S32BITARG_" hit max qSize=%"_U32BITARG_"", theRemotePort,theSender->fPacketQueue.GetLength(), maxQSize);
            //WarnV(false, outMessage); 
           done = true;
           break;
        }



    } while(false);
    
    return done;
}

void ReflectorSocket::GetMediaData(ReflectorPacket* thePacket)
{
    UInt32 idx = 0;
    UInt32 dataLen =0;
    UInt32 real_dataLen =0;
    UInt32 left_len =0;
    UInt32 fPacketPtr_Ptr =0;
    
    
    
    if(fLastTCPPacketDemux == NULL)
    {
        fLastTCPPacketDemux = new TCPInterleaveDemux();
    }
    
    if(fTempBufDataLen >0)
    {        
        memcpy(thePacket->fPacketPtr.Ptr,fTempBuf,fTempBufDataLen);
        thePacket->fPacketPtr.Len  +=  fTempBufDataLen;              
     }
    
    
    if(fLastTCPPacketDemux->GetPacketDataNeedLen() >0)
    {
        left_len = fLastTCPPacketDemux->GetPacketDataNeedLen();
        
        memcpy(thePacket->fPacketPtr.Ptr + thePacket->fPacketPtr.Len,fReceiveBufPtr.Ptr,left_len);
        thePacket->fPacketPtr.Len  +=  left_len;   
        idx += left_len;
    }
    
    
    while(idx < fReceiveBufPtr.Len)
    {   
            
        if(fReceiveBufPtr.Ptr[idx] == '$' && fReceiveBufPtr.Ptr[idx +4] == 0x47)
        {
            dataLen = AV_RB16(fReceiveBufPtr.Ptr+idx+2);
            //qtss_printf("dataLen:%d\n",dataLen);
            
            idx += 4;
                
            real_dataLen = MIN(dataLen, fReceiveBufPtr.Len-idx);
            real_dataLen = real_dataLen - real_dataLen % 188;
    
            memcpy(thePacket->fPacketPtr.Ptr + thePacket->fPacketPtr.Len,fReceiveBufPtr.Ptr+idx,real_dataLen);
            thePacket->fPacketPtr.Len += real_dataLen;
        
            fTempBufDataLen = real_dataLen % 188;
        
            fLastTCPPacketDemux->SetPacketDataNeedLen( dataLen - real_dataLen);
        
            if(fTempBufDataLen >0)
            {
                ::memcpy(fTempBuf,fReceiveBufPtr.Ptr+(kMaxReceivePacketSize - fTempBufDataLen),fTempBufDataLen);
            } 
                
            idx += real_dataLen;     
        }else
            
            idx ++;            
    }
    
    
    fReceiveBufPtr.Len =0;
}


void ReflectorSocket::GetIncomingData(const SInt64& inMilliseconds)
{
    OSMutexLocker locker(this->GetDemuxer()->GetMutex());
    UInt32 theRemoteAddr = 0;
    UInt16 theRemotePort = 0;
    UInt32 theRecvLen    = 0;
    //get all the outstanding packets for this socket
    
    UInt32 sPacketDataInHeaderBufferLen =0;
    UInt32 dataLen =0;
    char*  theHeaderBuf = NULL;
    
    
    if(fRTSPClient )
    {
        sPacketDataInHeaderBufferLen = fRTSPClient->GetPacketDataInHeaderBufferLen();
        theHeaderBuf =fRTSPClient->GetPacketDataInHeaderBuffer();
    }
    
   
    
    while (true)
    {
        //get a packet off the free queue.
        theRecvLen =0;
        ReflectorPacket* thePacket = this->GetPacket();

        thePacket->fPacketPtr.Len = 0;
        
        
        
        if(sPacketDataInHeaderBufferLen>0)
        {
            dataLen = MIN(sPacketDataInHeaderBufferLen,kMaxReceivePacketSize); 
        
            ::memcpy(fReceiveBufPtr.Ptr, theHeaderBuf,dataLen);
            fReceiveBufPtr.Len += dataLen;
            //qtss_printf("sPacketDataInHeaderBufferLen:%d,fReceiveBufPtr.Len:%d\n",sPacketDataInHeaderBufferLen,fReceiveBufPtr.Len);
            sPacketDataInHeaderBufferLen -= dataLen;
            
                        
            theHeaderBuf += dataLen;
            if(dataLen >= kMaxReceivePacketSize)
            {
                goto handle_tcp;
            }
        }
        
        if(sPacketDataInHeaderBufferLen ==0)
        {
                fRTSPClient = NULL;
        }
        
        
        if(  this->GetTransportType() == qtssRTPTransportTypeTCP && this->fTCPDataSocket)
        {
           //this->fTCPDataSocket->Read(thePacket->fPacketPtr.Ptr, kMaxReceivePacketSize,&thePacket->fPacketPtr.Len);
           (void) this->fTCPDataSocket->Read(fReceiveBufPtr.Ptr + fReceiveBufPtr.Len, kMaxReceivePacketSize-fReceiveBufPtr.Len, &theRecvLen);
           fReceiveBufPtr.Len +=  theRecvLen; 
           
           //qtss_printf("rece_packet_len:%d\n",theRecvLen);                  
        }
        
              
        if(this->GetTransportType() == qtssRTPTransportTypeUDP)
        {
            (void)this->RecvFrom(&theRemoteAddr, &theRemotePort, thePacket->fPacketPtr.Ptr,
                            3948, &thePacket->fPacketPtr.Len);
            RLogRequest(DEBUG_ARTS_MODULE,0,"RecvFrom len:%d,fd:%d,port:%d",thePacket->fPacketPtr.Len,this->GetSocketFD(),this->GetLocalPort());
           
        }

handle_tcp:  

       if(  this->GetTransportType() == qtssRTPTransportTypeTCP)
       {
            this->GetMediaData(thePacket);  
       }   
        
              
        if (this->ProcessPacket(inMilliseconds,thePacket,theRemoteAddr, theRemotePort))
            break;
                  
    }
    
}




ReflectorPacket* ReflectorSocket::GetPacket()
{
    OSMutexLocker locker(this->GetDemuxer()->GetMutex());
    if (fFreeQueue.GetLength() == 0)
        //if the port number of this socket is odd, this packet is an RTCP packet.
        return NEW ReflectorPacket();
    else
        return (ReflectorPacket*)fFreeQueue.DeQueue()->GetEnclosingObject();
}
