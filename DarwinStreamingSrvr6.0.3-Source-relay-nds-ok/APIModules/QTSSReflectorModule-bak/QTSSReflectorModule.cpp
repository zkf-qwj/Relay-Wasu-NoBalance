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
    File:       QTSSReflectorModule.cpp

    Contains:   Implementation of QTSSReflectorModule class. 
                    
    
    
*/
#include "QTSSReflectorModule.h"
#include "QTSSModuleUtils.h"
#include "ReflectorSession.h"
#include "OSArrayObjectDeleter.h"
#include "QTSS_Private.h"
#include "QTSSMemoryDeleter.h"
#include "OSMemory.h"
#include "OSRef.h"
#include "IdleTask.h"
#include "Task.h"
#include "OS.h"
#include "Socket.h"
#include "SocketUtils.h"
#include "FilePrefsSource.h"
#include "ResizeableStringFormatter.h"
#include "StringParser.h"
#include "QTAccessFile.h"
#include "QTSSModuleUtils.h"
#include "QTSS3GPPModuleUtils.h"
#include "RTSPRequestInterface.h"
#include "RTPSession.h"
#include "RTSPSession.h"
//ReflectorOutput objects
#include "RTPSessionOutput.h"
//SourceInfo objects
#include "SDPSourceInfo.h"

#include "SDPUtils.h"
#include "QTSSARTSReflectorAccessLog.h"
#include "StoreTask.h"
#include "ht_co_socket.h"
#ifndef __Win32__
    #include <unistd.h>
#endif

extern "C" {
#include "memcache_wrap.h"
}

#if DEBUG
#define REFLECTOR_MODULE_DEBUGGING 0
#else
#define REFLECTOR_MODULE_DEBUGGING 0
#endif

#define MAX_HANDLE_DIR 64
// ATTRIBUTES
static QTSS_AttributeID         sOutputAttr                 =   qtssIllegalAttrID;
static QTSS_AttributeID         sSessionAttr                =   qtssIllegalAttrID;
static QTSS_AttributeID         sStreamCookieAttr           =   qtssIllegalAttrID;
static QTSS_AttributeID         sRequestBodyAttr            =   qtssIllegalAttrID;
static QTSS_AttributeID         sBufferOffsetAttr           =   qtssIllegalAttrID;
static QTSS_AttributeID         sExpectedDigitFilenameErr   =   qtssIllegalAttrID;
static QTSS_AttributeID         sReflectorBadTrackIDErr     =   qtssIllegalAttrID;
static QTSS_AttributeID         sDuplicateBroadcastStreamErr=   qtssIllegalAttrID;

static QTSS_AttributeID         sUdpPortsPoolAttr           =  qtssIllegalAttrID;

static QTSS_AttributeID         sClientBroadcastSessionAttr =   qtssIllegalAttrID;
static QTSS_AttributeID         sRTSPBroadcastSessionAttr   =   qtssIllegalAttrID;
static QTSS_AttributeID         sAnnounceRequiresSDPinNameErr  = qtssIllegalAttrID;
static QTSS_AttributeID         sAnnounceDisabledNameErr    = qtssIllegalAttrID;
static QTSS_AttributeID         sSDPcontainsInvalidMinimumPortErr  = qtssIllegalAttrID;
static QTSS_AttributeID         sSDPcontainsInvalidMaximumPortErr  = qtssIllegalAttrID;
static QTSS_AttributeID         sStaticPortsConflictErr  = qtssIllegalAttrID;
static QTSS_AttributeID         sInvalidPortRangeErr  = qtssIllegalAttrID;

static QTSS_AttributeID         sKillClientsEnabledAttr  = qtssIllegalAttrID;
static QTSS_AttributeID         sRTPInfoWaitTimeAttr  =   qtssIllegalAttrID;
static QTSS_AttributeID         sARTSClientSessionAttr = qtssIllegalAttrID;
static QTSS_AttributeID         sARTSSTBModuleAttr = qtssIllegalAttrID;

// STATIC DATA

// ref to the prefs dictionary object
static OSRefTable*      sSessionMap = NULL;
static const StrPtrLen  kCacheControlHeader("no-cache");
static QTSS_PrefsObject sServerPrefs = NULL;
static UInt32   ClientSessID;
static UInt32   RefSessID;
static UInt32    sARTSNumHandleDir;
//static QTSS_ServerObject sServer = NULL;
QTSS_ModulePrefsObject       sPrefs = NULL;

//
// Prefs
static Bool16   sAllowNonSDPURLs = true;
static Bool16   sDefaultAllowNonSDPURLs = true;

static Bool16   sRTPInfoDisabled = false;
static Bool16   sDefaultRTPInfoDisabled = false;

static Bool16   sAnnounceEnabled = true;
static Bool16   sDefaultAnnounceEnabled = true;
static Bool16   sBroadcastPushEnabled = true;
static Bool16   sDefaultBroadcastPushEnabled = true;
static Bool16   sAllowDuplicateBroadcasts = false;
static Bool16   sDefaultAllowDuplicateBroadcasts = false;

static UInt32   sMaxBroadcastAnnounceDuration = 0;
static UInt32   sDefaultMaxBroadcastAnnounceDuration = 0;
static UInt16   sMinimumStaticSDPPort = 0;
static UInt16   sDefaultMinimumStaticSDPPort = 20000;
static UInt16   sMaximumStaticSDPPort = 0;
static UInt16   sDefaultMaximumStaticSDPPort = 65535;

static Bool16   sTearDownClientsOnDisconnect = false;
static Bool16   sDefaultTearDownClientsOnDisconnect = false;

static Bool16   sOneSSRCPerStream = true;
static Bool16   sDefaultOneSSRCPerStream = true;
static Bool16   sRelayModuleClient = false;
static Bool16   sDefaultRelayMode = false;
                
static UInt32   sTimeoutSSRCSecs = 30;
static UInt32   sDefaultTimeoutSSRCSecs = 30;

static UInt32   sBroadcasterSessionTimeoutSecs = 20;
static UInt32   sDefaultBroadcasterSessionTimeoutSecs = 20;
static UInt32   sBroadcasterSessionTimeoutMilliSecs = sBroadcasterSessionTimeoutSecs * 1000;
                                
static UInt16 sLastMax = 0;
static UInt16 sLastMin = 0;

static Bool16   sEnforceStaticSDPPortRange = false;
static Bool16   sDefaultEnforceStaticSDPPortRange = false;
static Bool16   sSupportOtherChannel = false;
static Bool16   sDefaultSupportOtherChannel =false;

static UInt32   sMaxAnnouncedSDPLengthInKbytes = 4;
static UInt32   sClientSendInterval = 30;
//static UInt32   sDefaultMaxAnnouncedSDPLengthInKbytes = 4;

static QTSS_AttributeID sIPAllowListID = qtssIllegalAttrID;
static char*            sIPAllowList = NULL;
static char*            sLocalLoopBackAddress = "127.0.0.*";

static char *   sBindLocalHost =NULL;

static Bool16   sAuthenticateLocalBroadcast = false;
static Bool16   sDefaultAuthenticateLocalBroadcast = false;

static Bool16	sDisableOverbuffering = false;
static Bool16	sDefaultDisableOverbuffering = false;
static Bool16	sFalse = false;

static Bool16   sReflectBroadcasts = true;
static Bool16   sDefaultReflectBroadcasts = true;

static Bool16   sAnnouncedKill = true;
static Bool16   sDefaultAnnouncedKill = true;


static Bool16   sPlayResponseRangeHeader = true;
static Bool16   sDefaultPlayResponseRangeHeader = true;

static Bool16   sPlayerCompatibility = true;
static Bool16   sDefaultPlayerCompatibility = true;

static UInt32   sAdjustMediaBandwidthPercent = 100;
static UInt32   sAdjustMediaBandwidthPercentDefault = 100;

static Bool16   sForceRTPInfoSeqAndTime = false;
static Bool16   sDefaultForceRTPInfoSeqAndTime = false;

static char*	sRedirectBroadcastsKeyword = NULL;
static char*    sDefaultRedirectBroadcastsKeyword = "";
static char*    sBroadcastsRedirectDir = NULL;
static char*    sDefaultBroadcastsRedirectDir = ""; // match none
static char*    sDefaultBroadcastsDir = ""; // match all
static char*	sDefaultsBroadcasterGroup = "broadcaster";

static char*  sARTSHandleDir[MAX_HANDLE_DIR] = {0,};


int ARTS_MODULE_DEBUG_LEVEL =1;

static char *   sDataBaseHost=NULL;

static StrPtrLen sBroadcasterGroup;

static QTSS_AttributeID sBroadcastDirListID = qtssIllegalAttrID;
                                
static SInt32   sWaitTimeLoopCount = 10;  

// Important strings
static StrPtrLen    sSDPKillSuffix(".kill");
static StrPtrLen    sSDPSuffix(".sdp");
static StrPtrLen    sMOVSuffix(".mov");
static StrPtrLen    sSDPTooLongMessage("Announced SDP is too long");
static StrPtrLen    sSDPNotValidMessage("Announced SDP is not a valid SDP");
static StrPtrLen    sKILLNotValidMessage("Announced .kill is not a valid SDP");
static StrPtrLen    sSDPTimeNotValidMessage("SDP time is not valid or movie not available at this time.");
static StrPtrLen    sBroadcastNotAllowed("Broadcast is not allowed.");
static StrPtrLen    sBroadcastNotActive("Broadcast is not active.");
static StrPtrLen    sTheNowRangeHeader("npt=now-");

const int kBuffLen = 512;

// FUNCTION PROTOTYPES

static QTSS_Error QTSSReflectorModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams);
static QTSS_Error Register(QTSS_Register_Params* inParams);
static QTSS_Error Initialize(QTSS_Initialize_Params* inParams);
static QTSS_Error Shutdown();
static QTSS_Error ProcessRTSPRequest(QTSS_StandardRTSP_Params* inParams);
static QTSS_Error DoAnnounce(QTSS_StandardRTSP_Params* inParams);
static QTSS_Error DoDescribe(QTSS_StandardRTSP_Params* inParams);
ReflectorSession* FindOrCreateSession(StrPtrLen* inPath, QTSS_StandardRTSP_Params* inParams, StrPtrLen* inData = NULL,Bool16 isPush=false, Bool16 *foundSessionPtr = NULL);
static QTSS_Error DoSetup(QTSS_StandardRTSP_Params* inParams);
static QTSS_Error DoPlay(QTSS_StandardRTSP_Params* inParams, ReflectorSession* inSession);
static QTSS_Error DestroySession(QTSS_ClientSessionClosing_Params* inParams);
static void RemoveOutput(ReflectorOutput* inOutput, ReflectorSession* inSession, Bool16 killClients);
static ReflectorSession* DoSessionSetup(QTSS_StandardRTSP_Params* inParams, QTSS_AttributeID inPathType,Bool16 isPush=false,Bool16 *foundSessionPtr= NULL, char** resultFilePath = NULL);
static QTSS_Error RereadPrefs();
static QTSS_Error ProcessRTPData(QTSS_IncomingData_Params* inParams);
static QTSS_Error ReflectorAuthorizeRTSPRequest(QTSS_StandardRTSP_Params* inParams);
static Bool16 InfoPortsOK(QTSS_StandardRTSP_Params* inParams, SDPSourceInfo* theInfo, StrPtrLen* inPath);
void KillCommandPathInList();
Bool16 KillSession(StrPtrLen *sdpPath, Bool16 killClients);
QTSS_Error IntervalRole();
static Bool16 AcceptSession(QTSS_StandardRTSP_Params* inParams);
static QTSS_Error RedirectBroadcast(QTSS_StandardRTSP_Params* inParams);
static Bool16 AllowBroadcast(QTSS_RTSPRequestObject inRTSPRequest);
static Bool16 InBroadcastDirList(QTSS_RTSPRequestObject inRTSPRequest);
static Bool16 IsAbsolutePath(StrPtrLen *inPathPtr);
static QTSS_Error DoValidate(QTSS_StandardRTSP_Params* inParams);
static   int  CheckProfile(QTSS_StandardRTSP_Params* inParams);

enum debug_level_t {
    NONE_ARTS_MODULE = 0,
    INFO_ARTS_MODULE,
    DEBUG_ARTS_MODULE
};

enum transport_type{
    TCP_MODULE =0,
    UDP_MODULE,
    WUXI_MODULE,
    SC_MOTO_MODULE 
};


static debug_level_t  sARTSLogLevel = INFO_ARTS_MODULE;
static debug_level_t  sDefaultARTSLogLevel =INFO_ARTS_MODULE;


static transport_type   sARTSTransportType = TCP_MODULE;
static transport_type   sDefaultARTSTransportType =  TCP_MODULE;

inline void KeepSession(QTSS_RTSPRequestObject theRequest,Bool16 keep)
{
    (void)QTSS_SetValue(theRequest, qtssRTSPReqRespKeepAlive, 0, &keep, sizeof(keep));
}

    

// FUNCTION IMPLEMENTATIONS

QTSS_Error QTSSReflectorModule_Main(void* inPrivateArgs)
{
    return _stublibrary_main(inPrivateArgs, QTSSReflectorModuleDispatch);
}


QTSS_Error  QTSSReflectorModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams)
{
    switch (inRole)
    {
        case QTSS_Register_Role:
            return Register(&inParams->regParams);
        case QTSS_Initialize_Role:
            return Initialize(&inParams->initParams);
        case QTSS_RereadPrefs_Role:
            return RereadPrefs();
        case QTSS_RTSPRoute_Role:
            return RedirectBroadcast(&inParams->rtspRouteParams);
        case QTSS_RTSPPreProcessor_Role:
            return ProcessRTSPRequest(&inParams->rtspRequestParams);
        case QTSS_RTSPIncomingData_Role:
            return ProcessRTPData(&inParams->rtspIncomingDataParams);
        case QTSS_ClientSessionClosing_Role:
            return DestroySession(&inParams->clientSessionClosingParams);
        case QTSS_Shutdown_Role:
            return Shutdown();
        case QTSS_RTSPAuthorize_Role:
            return ReflectorAuthorizeRTSPRequest(&inParams->rtspRequestParams);
        case QTSS_Interval_Role:
            return IntervalRole();
   }
    return QTSS_NoErr;
}




QTSS_Error Register(QTSS_Register_Params* inParams)
{
    // Do role & attribute setup
    qtss_printf("QTSSReflector Register\n");
    
    QTSS_Error error=QTSS_NoErr;
    
    (void)QTSS_AddRole(QTSS_Initialize_Role);
    (void)QTSS_AddRole(QTSS_Shutdown_Role);
    (void)QTSS_AddRole(QTSS_RTSPPreProcessor_Role);
    (void)QTSS_AddRole(QTSS_ClientSessionClosing_Role);
    (void)QTSS_AddRole(QTSS_RTSPIncomingData_Role); // call me with interleaved RTP streams on the RTSP session
    (void)QTSS_AddRole(QTSS_RTSPAuthorize_Role);
    (void)QTSS_AddRole(QTSS_RereadPrefs_Role);
    (void)QTSS_AddRole(QTSS_RTSPRoute_Role);
    
    
    // Add text messages attributes
    static char*        sExpectedDigitFilenameName      = "QTSSReflectorModuleExpectedDigitFilename";
    static char*        sReflectorBadTrackIDErrName     = "QTSSReflectorModuleBadTrackID";
    static char*        sDuplicateBroadcastStreamName   = "QTSSReflectorModuleDuplicateBroadcastStream";
    static char*        sAnnounceRequiresSDPinName      = "QTSSReflectorModuleAnnounceRequiresSDPSuffix";
    static char*        sAnnounceDisabledName           = "QTSSReflectorModuleAnnounceDisabled";
    
    static char*        sUdpPrtsPoolName = "QTSSRelectorModuleUdpPortsPool";
    
     error = QTSS_AddStaticAttribute(qtssModuleObjectType, sUdpPrtsPoolName, NULL, qtssAttrDataTypeVoidPointer);
    (void)QTSS_IDForAttr(qtssModuleObjectType, sUdpPrtsPoolName, &sUdpPortsPoolAttr);
    
     qtss_printf("sUdpPortsPoolAttr:%d,err:%d\n",sUdpPortsPoolAttr,error);
    
    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sDuplicateBroadcastStreamName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sDuplicateBroadcastStreamName, &sDuplicateBroadcastStreamErr);

    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sAnnounceRequiresSDPinName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sAnnounceRequiresSDPinName, &sAnnounceRequiresSDPinNameErr);

    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sAnnounceDisabledName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sAnnounceDisabledName, &sAnnounceDisabledNameErr);


    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sExpectedDigitFilenameName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sExpectedDigitFilenameName, &sExpectedDigitFilenameErr);

    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sReflectorBadTrackIDErrName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sReflectorBadTrackIDErrName, &sReflectorBadTrackIDErr);
    
    static char* sSDPcontainsInvalidMinumumPortErrName  = "QTSSReflectorModuleSDPPortMinimumPort";
    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sSDPcontainsInvalidMinumumPortErrName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sSDPcontainsInvalidMinumumPortErrName, &sSDPcontainsInvalidMinimumPortErr);

    static char* sSDPcontainsInvalidMaximumPortErrName  = "QTSSReflectorModuleSDPPortMaximumPort";
    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sSDPcontainsInvalidMaximumPortErrName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sSDPcontainsInvalidMaximumPortErrName, &sSDPcontainsInvalidMaximumPortErr);
    
    static char* sStaticPortsConflictErrName    = "QTSSReflectorModuleStaticPortsConflict";
    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sStaticPortsConflictErrName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sStaticPortsConflictErrName, &sStaticPortsConflictErr);

    static char* sInvalidPortRangeErrName   = "QTSSReflectorModuleStaticPortPrefsBadRange";
    (void)QTSS_AddStaticAttribute(qtssTextMessagesObjectType, sInvalidPortRangeErrName, NULL, qtssAttrDataTypeCharArray);
    (void)QTSS_IDForAttr(qtssTextMessagesObjectType, sInvalidPortRangeErrName, &sInvalidPortRangeErr);
    
    
    // Add an RTP session attribute for tracking ReflectorSession objects
    static char*        sOutputName         = "QTSSReflectorModuleOutput";
    
    static char*        sSessionName        = "QTSSReflectorModuleSession";
    static char*        sStreamCookieName   = "QTSSReflectorModuleStreamCookie";
    static char*        sRequestBufferName  = "QTSSReflectorModuleRequestBuffer";
    static char*        sRequestBufferLenName= "QTSSReflectorModuleRequestBufferLen";
    static char*        sBroadcasterSessionName= "QTSSReflectorModuleBroadcasterSession";
    static char*        sKillClientsEnabledName= "QTSSReflectorModuleTearDownClients";

    static char*        sRTPInfoWaitTime         = "QTSSReflectorModuleRTPInfoWaitTime";
    
    static char*        sClientSTBModuleName        = "QTSSCientSTBModule";
       
    //static char *       sSessionCallidName  ="QTSSReflectorModuleCallid";
    (void)QTSS_AddStaticAttribute(qtssClientSessionObjectType, sRTPInfoWaitTime, NULL, qtssAttrDataTypeSInt32);
    (void)QTSS_IDForAttr(qtssClientSessionObjectType, sRTPInfoWaitTime, &sRTPInfoWaitTimeAttr);
    
  
    (void)QTSS_AddStaticAttribute(qtssClientSessionObjectType, sOutputName, NULL, qtssAttrDataTypeVoidPointer);
    (void)QTSS_IDForAttr(qtssClientSessionObjectType, sOutputName, &sOutputAttr);


    (void)QTSS_AddStaticAttribute(qtssRTPStreamObjectType, sStreamCookieName, NULL, qtssAttrDataTypeVoidPointer);
    (void)QTSS_IDForAttr(qtssRTPStreamObjectType, sStreamCookieName, &sStreamCookieAttr);

    (void)QTSS_AddStaticAttribute(qtssRTSPRequestObjectType, sRequestBufferName, NULL, qtssAttrDataTypeVoidPointer);
    (void)QTSS_IDForAttr(qtssRTSPRequestObjectType, sRequestBufferName, &sRequestBodyAttr);

    (void)QTSS_AddStaticAttribute(qtssRTSPRequestObjectType, sRequestBufferLenName, NULL, qtssAttrDataTypeUInt32);
    (void)QTSS_IDForAttr(qtssRTSPRequestObjectType, sRequestBufferLenName, &sBufferOffsetAttr);
    
    (void)QTSS_AddStaticAttribute(qtssClientSessionObjectType, sBroadcasterSessionName, NULL, qtssAttrDataTypeVoidPointer);
    (void)QTSS_IDForAttr(qtssClientSessionObjectType, sBroadcasterSessionName, &sClientBroadcastSessionAttr);
    
    error =QTSS_AddStaticAttribute(qtssClientSessionObjectType, sKillClientsEnabledName, NULL, qtssAttrDataTypeBool16);
    (void)QTSS_IDForAttr(qtssClientSessionObjectType, sKillClientsEnabledName, &sKillClientsEnabledAttr);
    qtss_printf("sKillClientsEnabledAttr:%d,err:%d\n",sKillClientsEnabledAttr,error);
 
     // keep the same attribute name for the RTSPSessionObject as used int he ClientSessionObject
    error=QTSS_AddStaticAttribute(qtssRTSPSessionObjectType, sBroadcasterSessionName, NULL, qtssAttrDataTypeVoidPointer);
    (void)QTSS_IDForAttr(qtssRTSPSessionObjectType, sBroadcasterSessionName, &sRTSPBroadcastSessionAttr);
    
    qtss_printf("sRTSPBroadcastSessionAttr:%d,err:%d\n",sRTSPBroadcastSessionAttr,error);
    
    
    error =QTSS_AddStaticAttribute(qtssClientSessionObjectType, sSessionName, NULL, qtssAttrDataTypeUInt32);
    (void)QTSS_IDForAttr(qtssClientSessionObjectType, sSessionName, &sARTSClientSessionAttr);
    qtss_printf("sARTSClientSessionAttr:%d,err:%d\n",sARTSClientSessionAttr,error);
    
    
    error =QTSS_AddStaticAttribute(qtssClientSessionObjectType, sClientSTBModuleName, NULL, qtssAttrDataTypeUInt32);
    (void)QTSS_IDForAttr(qtssClientSessionObjectType, sClientSTBModuleName, &sARTSSTBModuleAttr);
    qtss_printf("sARTSSTBModuleAttr:%d,err:%d\n",sARTSSTBModuleAttr,error);
    
    
    
    
    
   
    // Reflector session needs to setup some parameters too.
    ReflectorStream::Register();
    // RTPSessionOutput needs to do the same
    RTPSessionOutput::Register();

    // Tell the server our name!
    static char* sModuleName = "QTSSReflectorModule";
    ::strcpy(inParams->outModuleName, sModuleName);
    
    qtss_printf("QTSSReflector Register finished\n");

    return QTSS_NoErr;
}


QTSS_Error Initialize(QTSS_Initialize_Params* inParams)
{
    // Setup module utils
    QTSSModuleUtils::Initialize(inParams->inMessages, inParams->inServer, inParams->inErrorLogStream);
	QTSS3GPPModuleUtils::Initialize(inParams);
    QTAccessFile::Initialize();
    sSessionMap = NEW OSRefTable();
    sServerPrefs = inParams->inPrefs;
    sServer = inParams->inServer;
#if QTSS_REFLECTOR_EXTERNAL_MODULE
    // The reflector is dependent on a number of objects in the Common Utilities
    // library that get setup by the server if the reflector is internal to the
    // server.
    //
    // So, if the reflector is being built as a code fragment, it must initialize
    // those pieces itself
#if !MACOSXEVENTQUEUE
    ::select_startevents();//initialize the select() implementation of the event queue
#endif
    OS::Initialize();
    Socket::Initialize();
    SocketUtils::Initialize();

    const UInt32 kNumReflectorThreads = 8;
    TaskThreadPool::AddThreads(kNumReflectorThreads);
    IdleTask::Initialize();
    Socket::StartThread();
#endif
    
    sPrefs = QTSSModuleUtils::GetModulePrefsObject(inParams->inModule);

    // Call helper class initializers
    ReflectorStream::Initialize(sPrefs);
    ReflectorSession::Initialize();
      
    sARTSReflectorAccessLog = NEW QTSSARTSReflectorAccessLog();
    if (sARTSReflectorAccessLog != NULL && sLogEnabled)
        sARTSReflectorAccessLog->EnableLog();
     
     WriteStartupMessage();
     //qtss_printf("finished WriteStartupMessage\n"); 

    
    // Report to the server that this module handles DESCRIBE, SETUP, PLAY, PAUSE, and TEARDOWN
	static QTSS_RTSPMethod sSupportedMethods[] = { qtssDescribeMethod, qtssSetupMethod, qtssTeardownMethod, qtssPlayMethod, qtssPauseMethod, qtssAnnounceMethod, qtssRecordMethod };
	QTSSModuleUtils::SetupSupportedMethods(inParams->inServer, sSupportedMethods, 7);

    RereadPrefs();
    
     switch (sARTSLogLevel)
    {
        case 0:
            RLogRequest(INFO_ARTS_MODULE, 0,"loglevel:NON");
            break;
        case 1:
            RLogRequest(INFO_ARTS_MODULE, 0,"loglevel:INFO");
            break;
        case 2:
            RLogRequest(INFO_ARTS_MODULE, 0,"loglevel:DEBUG");
            break;
        default:
            break;
    }
    
    for (UInt32 theIndex = 0; theIndex < sARTSNumHandleDir; theIndex++)
    {
	    qtss_printf("Reflector Module:  HandleDir = %s\n", sARTSHandleDir[theIndex]);
		RLogRequest(INFO_ARTS_MODULE, 0, "HandleDir = %s", sARTSHandleDir[theIndex]);
    }
    
    
    ARTS_MODULE_DEBUG_LEVEL = sARTSLogLevel;
    
    init_co_socket_ht(); 
   return QTSS_NoErr;
}



char *GetTrimmedKeyWord(char *prefKeyWord)
{
    StrPtrLen redirKeyWordStr(prefKeyWord);
	StringParser theRequestPathParser(&redirKeyWordStr);
	
	// trim leading / from the keyword
	while(theRequestPathParser.Expect(kPathDelimiterChar)) {};

	StrPtrLen theKeyWordStr;
	theRequestPathParser.ConsumeUntil(&theKeyWordStr, kPathDelimiterChar); // stop when we see a / and don't include

   char *keyword = NEW char[theKeyWordStr.Len +1];
    ::memcpy(keyword, theKeyWordStr.Ptr, theKeyWordStr.Len);
    keyword[theKeyWordStr.Len] = 0;
   
   return keyword;
}

void SetMoviesRelativeDir()
{
    char* movieFolderString = NULL;
    (void) QTSS_GetValueAsString (sServerPrefs, qtssPrefsMovieFolder, 0, &movieFolderString);
    OSCharArrayDeleter deleter(movieFolderString);
    
    ResizeableStringFormatter redirectPath(NULL,0);
    redirectPath.Put(movieFolderString);
    if (redirectPath.GetBytesWritten() > 0 && kPathDelimiterChar != redirectPath.GetBufPtr()[redirectPath.GetBytesWritten() -1])
        redirectPath.PutChar(kPathDelimiterChar);
    redirectPath.Put(sBroadcastsRedirectDir);
    
    char *newMovieRelativeDir = NEW char[redirectPath.GetBytesWritten() +1];
    ::memcpy(newMovieRelativeDir, redirectPath.GetBufPtr(), redirectPath.GetBytesWritten());
    newMovieRelativeDir[redirectPath.GetBytesWritten()] = 0;
    
    delete [] sBroadcastsRedirectDir;
    sBroadcastsRedirectDir = newMovieRelativeDir;
        
}

QTSS_Error GetHandleDir()
{
    QTSS_Object theAttrInfo = NULL;
    QTSS_Error theErr = QTSS_GetAttrInfoByName(sPrefs, "handledir", &theAttrInfo);
    if (theErr != QTSS_NoErr)
	    return qtssIllegalAttrID;
   
    QTSS_AttrDataType theAttributeType = qtssAttrDataTypeUnknown;
    UInt32 theLen = sizeof(theAttributeType);
   
    QTSS_AttributeID theID = qtssIllegalAttrID; 
    theLen = sizeof(theID);
    theErr = QTSS_GetValue(theAttrInfo, qtssAttrID, 0 , &theID, &theLen);
    Assert(theErr == QTSS_NoErr);

    QTSS_GetNumValues(sPrefs, theID, &sARTSNumHandleDir);
    Assert(theErr == QTSS_NoErr);    

    if (sARTSNumHandleDir == 0)
        return NULL;

    for (UInt32 theIndex = 0; theIndex < sARTSNumHandleDir; theIndex++)
    {
        theErr = QTSS_NoErr;
        (void)QTSS_GetValueAsString((QTSS_Object)sPrefs, theID, theIndex, &sARTSHandleDir[theIndex]);
        Assert(theErr == QTSS_NoErr);   
    }
	return QTSS_NoErr;
}


QTSS_Error RereadPrefs()
{
    //
    // Use the standard GetPref routine to retrieve the correct values for our preferences
    qtss_printf("Entry QTSSReflecotrModule ReadPrefs\n");
    
    QTSSModuleUtils::GetAttribute(sPrefs, "log_level",  qtssAttrDataTypeUInt16,
                                &sARTSLogLevel, &sDefaultARTSLogLevel, sizeof(sARTSLogLevel));
    qtss_printf("log level:%d\n",sARTSLogLevel);
    
    QTSSModuleUtils::GetAttribute(sPrefs, "transport_mode",  qtssAttrDataTypeUInt16,
                                &sARTSTransportType, &sDefaultARTSTransportType, sizeof(sARTSTransportType));
                                
    qtss_printf("sARTSTransportType:%d\n",sARTSTransportType);
    
    QTSSModuleUtils::GetAttribute(sPrefs, "disable_rtp_play_info",  qtssAttrDataTypeBool16,
                                &sRTPInfoDisabled, &sDefaultRTPInfoDisabled, sizeof(sDefaultRTPInfoDisabled));

    QTSSModuleUtils::GetAttribute(sPrefs, "allow_non_sdp_urls",     qtssAttrDataTypeBool16,
                                &sAllowNonSDPURLs, &sDefaultAllowNonSDPURLs, sizeof(sDefaultAllowNonSDPURLs));
                                                                
    QTSSModuleUtils::GetAttribute(sPrefs, "enable_broadcast_announce",  qtssAttrDataTypeBool16,
                                &sAnnounceEnabled, &sDefaultAnnounceEnabled, sizeof(sDefaultAnnounceEnabled));
    QTSSModuleUtils::GetAttribute(sPrefs, "enable_broadcast_push",  qtssAttrDataTypeBool16,
                                &sBroadcastPushEnabled, &sDefaultBroadcastPushEnabled, sizeof(sDefaultBroadcastPushEnabled));
    QTSSModuleUtils::GetAttribute(sPrefs, "max_broadcast_announce_duration_secs",   qtssAttrDataTypeUInt32,
                                &sMaxBroadcastAnnounceDuration, &sDefaultMaxBroadcastAnnounceDuration, sizeof(sDefaultMaxBroadcastAnnounceDuration));
                                
                  
                                
    
    QTSSModuleUtils::GetAttribute(sPrefs, "client_send_interval",   qtssAttrDataTypeUInt32,
                                &sClientSendInterval, &sDefaultMaxBroadcastAnnounceDuration, sizeof(sDefaultMaxBroadcastAnnounceDuration));                         
                                
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "allow_duplicate_broadcasts",     qtssAttrDataTypeBool16,
                                &sAllowDuplicateBroadcasts, &sDefaultAllowDuplicateBroadcasts, sizeof(sDefaultAllowDuplicateBroadcasts));
                                
    QTSSModuleUtils::GetAttribute(sPrefs, "enforce_static_sdp_port_range",  qtssAttrDataTypeBool16,
                                &sEnforceStaticSDPPortRange, &sDefaultEnforceStaticSDPPortRange, sizeof(sDefaultEnforceStaticSDPPortRange));
    QTSSModuleUtils::GetAttribute(sPrefs, "minimum_static_sdp_port",    qtssAttrDataTypeUInt16,
                                &sMinimumStaticSDPPort, &sDefaultMinimumStaticSDPPort, sizeof(sDefaultMinimumStaticSDPPort));
    QTSSModuleUtils::GetAttribute(sPrefs, "maximum_static_sdp_port",    qtssAttrDataTypeUInt16,
                                &sMaximumStaticSDPPort, &sDefaultMaximumStaticSDPPort, sizeof(sDefaultMaximumStaticSDPPort));
    
    QTSSModuleUtils::GetAttribute(sPrefs, "kill_clients_when_broadcast_stops",  qtssAttrDataTypeBool16,
                                &sTearDownClientsOnDisconnect, &sDefaultTearDownClientsOnDisconnect, sizeof(sDefaultTearDownClientsOnDisconnect));
    QTSSModuleUtils::GetAttribute(sPrefs, "use_one_SSRC_per_stream",    qtssAttrDataTypeBool16,
                                &sOneSSRCPerStream, &sDefaultOneSSRCPerStream, sizeof(sDefaultOneSSRCPerStream));
    QTSSModuleUtils::GetAttribute(sPrefs, "timeout_stream_SSRC_secs",   qtssAttrDataTypeUInt32,
                                &sTimeoutSSRCSecs, &sDefaultTimeoutSSRCSecs, sizeof(sDefaultTimeoutSSRCSecs));

    QTSSModuleUtils::GetAttribute(sPrefs, "timeout_broadcaster_session_secs",   qtssAttrDataTypeUInt32,
                                &sBroadcasterSessionTimeoutSecs, &sDefaultBroadcasterSessionTimeoutSecs, sizeof(sDefaultTimeoutSSRCSecs));

    QTSSModuleUtils::GetAttribute(sPrefs, "authenticate_local_broadcast",   qtssAttrDataTypeBool16,
                                &sAuthenticateLocalBroadcast, &sDefaultAuthenticateLocalBroadcast, sizeof(sDefaultAuthenticateLocalBroadcast));
    
	QTSSModuleUtils::GetAttribute(sPrefs, "disable_overbuffering", 	qtssAttrDataTypeBool16,
								&sDisableOverbuffering, &sDefaultDisableOverbuffering, sizeof(sDefaultDisableOverbuffering));

    QTSSModuleUtils::GetAttribute(sPrefs, "allow_broadcasts",   qtssAttrDataTypeBool16,
                                &sReflectBroadcasts, &sDefaultReflectBroadcasts, sizeof(sDefaultReflectBroadcasts));
    
	QTSSModuleUtils::GetAttribute(sPrefs, "allow_announced_kill", 	qtssAttrDataTypeBool16,
								&sAnnouncedKill, &sDefaultAnnouncedKill, sizeof(sDefaultAnnouncedKill));
								
	QTSSModuleUtils::GetAttribute(sPrefs, "relay_mode_client", 	qtssAttrDataTypeBool16,
								&sRelayModuleClient, &sDefaultRelayMode, sizeof(sDefaultRelayMode));							
								 
							
    QTSSModuleUtils::GetAttribute(sPrefs, "support_other_channel", 	qtssAttrDataTypeBool16,
								&sSupportOtherChannel, &sDefaultSupportOtherChannel, sizeof(sSupportOtherChannel));		

	QTSSModuleUtils::GetAttribute(sPrefs, "enable_play_response_range_header", 	qtssAttrDataTypeBool16,
								&sPlayResponseRangeHeader, &sDefaultPlayResponseRangeHeader, sizeof(sDefaultPlayResponseRangeHeader));

    QTSSModuleUtils::GetAttribute(sPrefs, "enable_player_compatibility",   qtssAttrDataTypeBool16,
                                &sPlayerCompatibility, &sDefaultPlayerCompatibility, sizeof(sDefaultPlayerCompatibility));

    QTSSModuleUtils::GetAttribute(sPrefs, "compatibility_adjust_sdp_media_bandwidth_percent",   qtssAttrDataTypeUInt32,
                                &sAdjustMediaBandwidthPercent, &sAdjustMediaBandwidthPercentDefault, sizeof(sAdjustMediaBandwidthPercentDefault));
                                
    if (sAdjustMediaBandwidthPercent > 100)
        sAdjustMediaBandwidthPercent = 100;
        
    if (sAdjustMediaBandwidthPercent < 1)
        sAdjustMediaBandwidthPercent = 1;

    QTSSModuleUtils::GetAttribute(sPrefs, "force_rtp_info_sequence_and_time",   qtssAttrDataTypeBool16,
                                &sForceRTPInfoSeqAndTime, &sDefaultForceRTPInfoSeqAndTime, sizeof(sDefaultForceRTPInfoSeqAndTime));

    sBroadcasterGroup.Delete();
    sBroadcasterGroup.Set(QTSSModuleUtils::GetStringAttribute(sPrefs, "BroadcasterGroup", sDefaultsBroadcasterGroup)); 

    delete [] sRedirectBroadcastsKeyword;
    char* tempKeyWord = QTSSModuleUtils::GetStringAttribute(sPrefs, "redirect_broadcast_keyword", sDefaultRedirectBroadcastsKeyword); 
    sRedirectBroadcastsKeyword = GetTrimmedKeyWord(tempKeyWord);
    delete [] tempKeyWord;
    
    delete[] sBindLocalHost;
    sBindLocalHost = QTSSModuleUtils::GetStringAttribute(sPrefs, "bind_host", sLocalLoopBackAddress);
    
    
    delete [] sBroadcastsRedirectDir;
    sBroadcastsRedirectDir = QTSSModuleUtils::GetStringAttribute(sPrefs, "redirect_broadcasts_dir", sDefaultBroadcastsRedirectDir);                           
    if (sBroadcastsRedirectDir && sBroadcastsRedirectDir[0] != kPathDelimiterChar)
        SetMoviesRelativeDir();   

        
    delete [] QTSSModuleUtils::GetStringAttribute(sPrefs, "broadcast_dir_list", sDefaultBroadcastsDir); // initialize if there isn't one
    sBroadcastDirListID = QTSSModuleUtils::GetAttrID(sPrefs, "broadcast_dir_list");
 
    delete [] sIPAllowList;
    sIPAllowList = QTSSModuleUtils::GetStringAttribute(sPrefs, "ip_allow_list", sLocalLoopBackAddress);
    sIPAllowListID = QTSSModuleUtils::GetAttrID(sPrefs, "ip_allow_list");
    
    delete [] sDataBaseHost;
    sDataBaseHost = QTSSModuleUtils::GetStringAttribute(sPrefs, "couchbase_host", sLocalLoopBackAddress);


    sBroadcasterSessionTimeoutMilliSecs = sBroadcasterSessionTimeoutSecs * 1000;
    


    if (sEnforceStaticSDPPortRange)
    {   Bool16 reportErrors = false;
        if (sLastMax != sMaximumStaticSDPPort)
        {   sLastMax = sMaximumStaticSDPPort;
            reportErrors = true;
        }
        
        if (sLastMin != sMinimumStaticSDPPort)
        {   sLastMin = sMinimumStaticSDPPort;
            reportErrors = true;
        }

        if (reportErrors)
        {
            UInt16 minServerPort = 6970;
            UInt16 maxServerPort = 9999;
            char min[32];
            char max[32];
                    
            if  (   ( (sMinimumStaticSDPPort <= minServerPort) && (sMaximumStaticSDPPort >= minServerPort) )
                ||  ( (sMinimumStaticSDPPort >= minServerPort) && (sMinimumStaticSDPPort <= maxServerPort) )
                )
            {
                qtss_sprintf(min,"%u",minServerPort);
                qtss_sprintf(max,"%u",maxServerPort);    
                QTSSModuleUtils::LogError(  qtssWarningVerbosity, sStaticPortsConflictErr, 0, min, max);
            }
            
            if  (sMinimumStaticSDPPort > sMaximumStaticSDPPort) 
            { 
                qtss_sprintf(min,"%u",sMinimumStaticSDPPort);
                qtss_sprintf(max,"%u",sMaximumStaticSDPPort);    
                QTSSModuleUtils::LogError(  qtssWarningVerbosity, sInvalidPortRangeErr, 0, min, max);
            }
        }
    }    

    KillCommandPathInList();
    
    QTSS3GPPModuleUtils::ReadPrefs();
    GetHandleDir();
                        
    return QTSS_NoErr;
}

QTSS_Error Shutdown()
{
#if QTSS_REFLECTOR_EXTERNAL_MODULE
    TaskThreadPool::RemoveThreads();
#endif
    destory_co_socket_ht();
    WriteShutdownMessage();
    delete sARTSReflectorAccessLog;
    return QTSS_NoErr;
}

QTSS_Error IntervalRole() // not used
{   
    (void) QTSS_SetIntervalRoleTimer(0); // turn off
    
    return QTSS_NoErr;
}


QTSS_Error ProcessRTPData(QTSS_IncomingData_Params* inParams)
{
    if (!sBroadcastPushEnabled)
        return QTSS_NoErr;
        
    //qtss_printf("QTSSReflectorModule:ProcessRTPData inRTSPSession=%"_U32BITARG_" inClientSession=%"_U32BITARG_"\n",inParams->inRTSPSession, inParams->inClientSession);
    ReflectorSession* theSession = NULL;
    UInt32 theLen = sizeof(theSession);
    QTSS_Error theErr = QTSS_GetValue(inParams->inRTSPSession, sRTSPBroadcastSessionAttr, 0, &theSession, &theLen);
    //qtss_printf("QTSSReflectorModule.cpp:ProcessRTPData    sClientBroadcastSessionAttr=%"_U32BITARG_" theSession=%"_U32BITARG_" err=%"_S32BITARG_" \n",sClientBroadcastSessionAttr, theSession,theErr);
    if (theSession == NULL || theErr != QTSS_NoErr) 
        return QTSS_NoErr;
    
    // it is a broadcaster session
    //qtss_printf("QTSSReflectorModule.cpp:is broadcaster session\n");

    SourceInfo* theSoureInfo = theSession->GetSourceInfo(); 
    Assert(theSoureInfo != NULL);
    if (theSoureInfo == NULL)
        return QTSS_NoErr;
            

    UInt32  numStreams = theSession->GetNumStreams();
    //qtss_printf("QTSSReflectorModule.cpp:ProcessRTPData numStreams=%"_U32BITARG_"\n",numStreams);

{
/*
   Stream data such as RTP packets is encapsulated by an ASCII dollar
   sign (24 hexadecimal), followed by a one-byte channel identifier,
   followed by the length of the encapsulated binary data as a binary,
   two-byte integer in network byte order. The stream data follows
   immediately afterwards, without a CRLF, but including the upper-layer
   protocol headers. Each $ block contains exactly one upper-layer
   protocol data unit, e.g., one RTP packet.
*/
    char*   packetData= inParams->inPacketData;
    
    UInt8   packetChannel;
    packetChannel = (UInt8) packetData[1];

    UInt16  packetDataLen;
    memcpy(&packetDataLen,&packetData[2],2);
    packetDataLen = ntohs(packetDataLen);
    
    char*   rtpPacket = &packetData[4];
    
    //UInt32    packetLen = inParams->inPacketLen;
    //qtss_printf("QTSSReflectorModule.cpp:ProcessRTPData channel=%u theSoureInfo=%"_U32BITARG_" packetLen=%"_U32BITARG_" packetDatalen=%u\n",(UInt16) packetChannel,theSoureInfo,inParams->inPacketLen,packetDataLen);

    if (1)
    {
        UInt32 inIndex = packetChannel / 2; // one stream per every 2 channels rtcp channel handled below
        ReflectorStream* theStream = NULL;
        if (inIndex < numStreams) 
        {   theStream = theSession->GetStreamByIndex(inIndex);

            SourceInfo::StreamInfo* theStreamInfo =theStream->GetStreamInfo();  
            UInt16 serverReceivePort =theStreamInfo->fPort;         

            Bool16 isRTCP =false;
            if (theStream != NULL)
            {   if (packetChannel & 1)
                {   serverReceivePort ++;
                    isRTCP = true;
                }
                theStream->PushPacket(rtpPacket,packetDataLen, isRTCP);
                //qtss_printf("QTSSReflectorModule.cpp:ProcessRTPData Send RTSP packet channel=%u to UDP localServerAddr=%"_U32BITARG_" serverReceivePort=%"_U32BITARG_" packetDataLen=%u \n", (UInt16) packetChannel, localServerAddr, serverReceivePort,packetDataLen);
            }
        }
    }
    
}
    return theErr;
}

QTSS_Error ProcessRTSPRequest(QTSS_StandardRTSP_Params* inParams)
{
    OSMutexLocker locker (sSessionMap->GetMutex()); //operating on sOutputAttr
    QTSS_Error theErr =0;
    QTSS_RTSPMethod* theMethod = NULL;
    //qtss_printf("QTSSReflectorModule:ProcessRTSPRequest inClientSession=%"_U32BITARG_"\n", (UInt32) inParams->inClientSession);
    UInt32 theLen = 0,callid;
    if ((QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqMethod, 0,
            (void**)&theMethod, &theLen) != QTSS_NoErr) || (theLen != sizeof(QTSS_RTSPMethod)))
    {
        Assert(0);
        return QTSS_RequestFailed;
    }

    if (*theMethod == qtssAnnounceMethod)
        return DoAnnounce(inParams);
    if (*theMethod == qtssDescribeMethod)
        return DoDescribe(inParams);
    
     if(*theMethod == qtssValidateMethod)
        return DoValidate(inParams);
        
    if (*theMethod == qtssSetupMethod)
        return DoSetup(inParams);
        
    RTPSessionOutput** theOutput = NULL;
    theErr = QTSS_GetValuePtr(inParams->inClientSession, sOutputAttr, 0, (void**)&theOutput, &theLen);
    if ((theErr != QTSS_NoErr) || (theLen != sizeof(RTPSessionOutput*))) // a broadcaster push session
    {   if (*theMethod == qtssPlayMethod || *theMethod == qtssRecordMethod)
            return DoPlay(inParams, NULL);
        else
            return QTSS_RequestFailed;
    }
    
    switch (*theMethod)
    {
        case qtssPlayMethod:
            return DoPlay(inParams, (*theOutput)->GetReflectorSession());
        case qtssTeardownMethod:
            // Tell the server that this session should be killed, and send a TEARDOWN response
            theErr = QTSS_GetValue(inParams->inClientSession, sARTSClientSessionAttr, 0, (void *)&callid, &theLen);
            RLogRequest(INFO_ARTS_MODULE,callid,"TearDown");
            (void)QTSS_Teardown(inParams->inClientSession);
            (void)QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
            break;
        case qtssPauseMethod:
            (void)QTSS_Pause(inParams->inClientSession);
            (void)QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
            break;
        case qtssGetParameterMethod:
           (void) QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
	    break;
        default:
            break;
    }           
    return QTSS_NoErr;
}


QTSS_Error DoValidate(QTSS_StandardRTSP_Params* inParams)
{
    RLogRequest(DEBUG_ARTS_MODULE, 0,"Entry");
    if(CheckProfile(inParams) <0)
    {
        return QTSS_NoErr;
    } 
    
     if(inParams->inRTSPHeaders != NULL)
    {
        QTSSDictionary *headerDict = (QTSSDictionary* )inParams->inRTSPHeaders;
        StrPtrLen *theKeySessionID=headerDict->GetValue(qtssVaryHeader);
        RTSPSessionInterface * theRTSPSess =(RTSPSessionInterface *) inParams->inRTSPSession;
         
        RLogRequest(DEBUG_ARTS_MODULE, 0,"RTSPSession:%x",theRTSPSess);
        RTPSession * theRtpSession  = (RTPSession *) inParams->inClientSession;
        Assert(theRTSPSess !=NULL && theRtpSession !=NULL);
              
        StrPtrLen *realSession = headerDict->GetValue(qtssSessionHeader);
        if(realSession != NULL &&realSession->Len >0 )
        {
            theRTSPSess->NoCleanObjectHolderCount = true;
            RLogRequest(DEBUG_ARTS_MODULE, 0,"NoCleanObjectHolderCount set");
        }       
                      
            
                  
               
        if(theKeySessionID!= NULL && theKeySessionID->Len >0)
        {
                      
            co_socket_t *sockstruct = (co_socket_t *)malloc(sizeof(co_socket_t));
            memset(sockstruct,0,sizeof(co_socket_t));
           
            char *sessionCStr = theKeySessionID->GetAsCString();
            memcpy(sockstruct->sessionID,sessionCStr,strlen(sessionCStr));
            
            sockstruct->sock =theRTSPSess->GetOutputStream()->GetSocket();          
            sockstruct->used = false;            
            sockstruct->rtspSessObj= inParams->inRTSPSession; 
            sockstruct->rtpSessObj = inParams->inClientSession;     
            
            RLogRequest(DEBUG_ARTS_MODULE, 0,"validate header sessionID:%s",sessionCStr);
            delete sessionCStr;
            insert_co_socket_list(sockstruct);            
        }
    }
    
    
    QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
    RLogRequest(INFO_ARTS_MODULE, 0,"exit");
    
    return QTSS_NoErr;    
}



QTSS_Error get_host_port(char *pBackends,char **host_name )
{
    char *colon;
    char *hostname = NULL, *port = NULL;
    int portnum =0;
    if (NULL != (colon = strrchr(pBackends, ':')))
    {
        int len = colon - pBackends;
        hostname = new char[len+1];
        strncpy(hostname, pBackends, len );
        hostname[len] = 0;
        (*host_name) = hostname;
        port = new char[strlen(colon + 1) + 1];
        strcpy(port, colon + 1);

        portnum = atoi(port);
        free(port);
     }
    return portnum;

}

/*
QTSS_Error GetSrcHost(char *src_host,UInt32 * src_port,char *vid,UInt32 l_callid)
{
    if (!vid) return -1;
    memcached_st * memc = NULL;
    char key[1024]={'\0'};
    char addr[1024]={'\0'};
    char *host = NULL;
    int port = 0;
    
    memc = create_conn(sDataBaseHost);
    if(memc == NULL)
    {
        RLogRequest(INFO_ARTS_MODULE,l_callid,"connect %s failed",sDataBaseHost);
       return QTSS_RequestFailed;
    }
   
    sprintf(key,"%s_src_host",vid);
    mem_get(memc,key,addr); 
    port = get_host_port(addr,&host);
    RLogRequest(INFO_ARTS_MODULE,l_callid,"host:%s,port:%d",host,port);
    strcpy(src_host,host);
    (*src_port) = port;
    
    free(host);
    return QTSS_NoErr;
}
*/


//create video's sdp form database with vid
QTSS_Error  generate_sdp(char *vid,UInt32 l_callid,char *sdp_buf,Bool16 isCamera,char *src_host,UInt32 *src_port,char *local_host)
{
    if (!vid) return QTSS_RequestFailed;
    memcached_st * memc = NULL;
    
    char multi_addr[1024]={'\0'};
    memc = create_conn(sDataBaseHost);
    if(memc == NULL)
    {
        RLogRequest(INFO_ARTS_MODULE,l_callid,"connect %s failed",sDataBaseHost);
       return QTSS_RequestFailed;
    }
    char key[1024]={'\0'};
    if(!sRelayModuleClient)
    {
        sprintf(key,"channel%s_udp",vid);
    }else
    {
         sprintf(key,"%s_src_host",vid);
    }    
   
    mem_get(memc,key,multi_addr); 
    
    if(strlen(multi_addr)>0 && isCamera && !sRelayModuleClient)
    {
        memset(key,0,sizeof(key));
        sprintf(key,"channel%s_access",vid);
        mem_set(memc,key,"1",30); 
    }    
    
    char *multi_host = NULL;
    int port = get_host_port(multi_addr,&multi_host);
    RLogRequest(INFO_ARTS_MODULE,l_callid,"multi_host:%s,port:%d",multi_host,port);
    
    if(multi_host == NULL)
        return QTSS_RequestFailed;
    
    (*src_port)  = port;
    
    if(strlen(multi_host) >0)
        strcpy(src_host,multi_host);
        
     
    
    if(sRelayModuleClient)
    {
        port  =0;
        sprintf(sdp_buf,"c=IN IP4 %s\r\n",local_host);
    }else
         sprintf(sdp_buf,"c=IN IP4 %s\r\n",multi_host);
    
    if(sARTSTransportType == TCP_MODULE)
    {
        sprintf(sdp_buf+strlen(sdp_buf),"m=video %d MP2T TCP\r\n\r\n",port);
    }
    
    if(sARTSTransportType == UDP_MODULE)
    {
        sprintf(sdp_buf+strlen(sdp_buf),"m=video %d udp MP2T\r\n\r\n",port);
    }
    
    if(sARTSTransportType == WUXI_MODULE)
    {
        sprintf(sdp_buf+strlen(sdp_buf),"m=video %d MP2T mpgv\r\na=control:trackID=1\r\n\r\n",port);
    }
    
    
    release_conn(memc);
    free(multi_host);
    return QTSS_NoErr;
    
}

StoreTask::StoreTask() 
{
    memset(vid,0,sizeof(vid));
    memset(dataBaseHost,0,sizeof(dataBaseHost));
    this->SetTaskName("storeTask");
    stop = false;
}


//expTime is sec
void StoreTask::configure(char *pvid,char *host,UInt64 expTime,UInt32 l_callid)
{
    if(pvid)
    {
        strncpy(vid,pvid,sizeof(vid));
    }
    
    if(host)
    {
        strncpy(dataBaseHost,host,sizeof(dataBaseHost));       
    }
    
    if(expTime >0)
        exp_time = expTime;
    
    if(l_callid >0)
        callid = l_callid;
   
}


StoreTask::~StoreTask()
{
   
}


SInt64 StoreTask::Run()
{
    RLogRequest(DEBUG_ARTS_MODULE,callid,"Run Entry");
    EventFlags events = this->GetEvents();
    if ((events & Task::kTimeoutEvent) || (events & Task::kKillEvent))
    {
        RLogRequest(DEBUG_ARTS_MODULE,callid,"kKillEvent");
        return -1;
    }
    if(!stop  )
    {
        memcached_st * memc = NULL;
        memc = create_conn(dataBaseHost);
        if(memc == NULL)
        {
            RLogRequest(INFO_ARTS_MODULE,callid,"connect %s failed",sDataBaseHost);
            return -1;
        }
        char key[1024]={'\0'};
        sprintf(key,"channel%s_access",vid);
        int rc  = mem_set(memc,key,"1",exp_time); 
        if(rc == MEMCACHED_SUCCESS)
        {
            RLogRequest(INFO_ARTS_MODULE,callid,"set %s success",vid);
        }
        release_conn(memc);
        return IntervalMsec;
    }else
        return -1;    
}

ReflectorSession* DoSessionSetup(QTSS_StandardRTSP_Params* inParams, QTSS_AttributeID inPathType,Bool16 isPush, Bool16 *foundSessionPtr, char** resultFilePath)
{
    char* theFullPathStr = NULL;
    
     
    
    QTSS_Error theErr = QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqLocalPath, 0, &theFullPathStr);
    Assert(theErr == QTSS_NoErr);
    QTSSCharArrayDeleter theFullPathStrDeleter(theFullPathStr);
     
    
    UInt32 l_callid = 0, l_len = sizeof(UInt32);   
    QTSS_ClientSessionObject     theClientSession   = inParams->inClientSession; 
    theErr = QTSS_GetValue(theClientSession, sARTSClientSessionAttr, 0, (void *)&l_callid, &l_len);
      

    StrPtrLen theFullPath(theFullPathStr);
    RLogRequest(INFO_ARTS_MODULE,l_callid,"theFullPathStr:%s",theFullPathStr);
    if (theFullPath.Len > sMOVSuffix.Len )
    {   StrPtrLen endOfPath2(&theFullPath.Ptr[theFullPath.Len -  sMOVSuffix.Len], sMOVSuffix.Len);
        if (endOfPath2.Equal(sMOVSuffix)) // it is a .mov so it is not meant for us
        {   
            return NULL;
        }
    }


    if (sAllowNonSDPURLs && !isPush)
    {
        // Check and see if the full path to this file matches an existing ReflectorSession
        StrPtrLen thePathPtr;
        OSCharArrayDeleter sdpPath(QTSSModuleUtils::GetFullPath(    inParams->inRTSPRequest,
                                                                    inPathType,
                                                                    &thePathPtr.Len, &sSDPSuffix));
        
        thePathPtr.Ptr = sdpPath.GetObject();

        // If the actual file path has a .sdp in it, first look for the URL without the extra .sdp
        if (thePathPtr.Len > (sSDPSuffix.Len * 2))
        {
            // Check and see if there is a .sdp in the file path.
            // If there is, truncate off our extra ".sdp", cuz it isn't needed
            StrPtrLen endOfPath(&sdpPath.GetObject()[thePathPtr.Len - (sSDPSuffix.Len * 2)], sSDPSuffix.Len);
            if (endOfPath.Equal(sSDPSuffix))
            {
                sdpPath.GetObject()[thePathPtr.Len -sSDPSuffix.Len] = '\0';
                thePathPtr.Len -= sSDPSuffix.Len;
            }
        }
        
        if (resultFilePath != NULL)
            *resultFilePath = thePathPtr.GetAsCString();
        return FindOrCreateSession(&thePathPtr, inParams);
    }
    else
    {
        if (!sDefaultBroadcastPushEnabled)
            return NULL;
        //
        // We aren't supposed to auto-append a .sdp, so just get the URL path out of the server
        //StrPtrLen theFullPath;
        //QTSS_Error theErr = QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqLocalPath, 0, (void**)&theFullPath.Ptr, &theFullPath.Len);
        //Assert(theErr == QTSS_NoErr);
        
        if (theFullPath.Len > sSDPSuffix.Len)
        {
            //
            // Check to make sure this path has a .sdp at the end. If it does,
            // attempt to get a reflector session for this URL.
            StrPtrLen endOfPath2(&theFullPath.Ptr[theFullPath.Len - sSDPSuffix.Len], sSDPSuffix.Len);
            if (endOfPath2.Equal(sSDPSuffix))
            {   if (resultFilePath != NULL)
                    *resultFilePath = theFullPath.GetAsCString();
                return FindOrCreateSession(&theFullPath, inParams,NULL, isPush,foundSessionPtr);
            }
        }
        return NULL;
    }
	return NULL;
}

void DoAnnounceAddRequiredSDPLines(QTSS_StandardRTSP_Params* inParams, ResizeableStringFormatter *editedSDP, char* theSDPPtr)
{
    SDPContainer checkedSDPContainer;
    checkedSDPContainer.SetSDPBuffer( theSDPPtr );  
    if (!checkedSDPContainer.HasReqLines())
    {
        if (!checkedSDPContainer.HasLineType('v'))
        { // add v line
            editedSDP->Put("v=0\r\n");
        }
        
        if (!checkedSDPContainer.HasLineType('s'))
        { // add s line
            char* theSDPName = NULL; 
                        
            (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqFilePath, 0, &theSDPName);
            QTSSCharArrayDeleter thePathStrDeleter(theSDPName);
            if (theSDPName == NULL)
                editedSDP->Put("s=unknown\r\n");
            else
            {
                editedSDP->Put("s=");
                editedSDP->Put(theSDPName);
                editedSDP->PutEOL();
            }
        }
        
       if (!checkedSDPContainer.HasLineType('t'))
       { // add t line
            editedSDP->Put("t=0 0\r\n");
       }

       if (!checkedSDPContainer.HasLineType('o'))
       { // add o line
            editedSDP->Put("o=");
            char tempBuff[256] = ""; tempBuff[255] = 0;
            char *nameStr = tempBuff;
            UInt32 buffLen = sizeof(tempBuff) - 1;
            (void)QTSS_GetValue(inParams->inClientSession, qtssCliSesFirstUserAgent, 0, nameStr, &buffLen);
            for (UInt32 c = 0; c < buffLen; c++)
            {   
                if (StringParser::sEOLWhitespaceMask[ (UInt8) nameStr[c]])
                {   nameStr[c] = 0;             
                    break;
                }
            }

            buffLen = ::strlen(nameStr);
            if (buffLen == 0)
                editedSDP->Put("announced_broadcast");
            else
                editedSDP->Put(nameStr, buffLen);
                      
            editedSDP->Put(" ");
                            
            buffLen = sizeof(tempBuff) -1;
            (void)QTSS_GetValue(inParams->inClientSession, qtssCliSesRTSPSessionID, 0, &tempBuff, &buffLen);
            editedSDP->Put(tempBuff, buffLen );

            editedSDP->Put(" ");
            qtss_snprintf(tempBuff, sizeof(tempBuff) -1, "%"_64BITARG_"d", (SInt64) OS::UnixTime_Secs() + 2208988800LU);
            editedSDP->Put(tempBuff);

            editedSDP->Put(" IN IP4 ");
            (void)QTSS_GetValue(inParams->inClientSession, qtssCliRTSPSessRemoteAddrStr, 0, tempBuff, &buffLen);
            editedSDP->Put(tempBuff, buffLen);

            editedSDP->PutEOL();
        }
    }

    editedSDP->Put(theSDPPtr);


}


QTSS_Error DoAnnounce(QTSS_StandardRTSP_Params* inParams)
{
    if (!sAnnounceEnabled)
        return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssPreconditionFailed, sAnnounceDisabledNameErr);

    //
    // If this is SDP data, the reflector has the ability to write the data
    // to the file system location specified by the URL.
    
    //
    // This is a completely stateless action. No ReflectorSession gets created (obviously).
    
    //
    // Eventually, we should really require access control before we do this.
    //qtss_printf("QTSSReflectorModule:DoAnnounce\n");
    //
    // Get the full path to this file
    char* theFullPathStr = NULL;
    (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqLocalPath, 0, &theFullPathStr);
    QTSSCharArrayDeleter theFullPathStrDeleter(theFullPathStr);
    StrPtrLen theFullPath(theFullPathStr);
    
    // Check for a .kill at the end
    Bool16 pathOK = false;
    Bool16 killBroadcast = false;
    if (sAnnouncedKill && theFullPath.Len > sSDPKillSuffix.Len)
    {
        StrPtrLen endOfPath(theFullPath.Ptr + (theFullPath.Len - sSDPKillSuffix.Len), sSDPKillSuffix.Len);
        if (endOfPath.Equal(sSDPKillSuffix))
        {   
            pathOK = true;
            killBroadcast = true;
        }
     }


    // Check for a .sdp at the end
     if (!pathOK)
     {
        if (theFullPath.Len <= sSDPSuffix.Len)
        {
            StrPtrLen endOfPath(theFullPath.Ptr + (theFullPath.Len - sSDPSuffix.Len), sSDPSuffix.Len);
            if (!endOfPath.Equal(sSDPSuffix))
                return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssPreconditionFailed, sAnnounceRequiresSDPinNameErr);

        }       
     }

    
    //
    // Ok, this is an sdp file. Retreive the entire contents of the SDP.
    // This has to be done asynchronously (in case the SDP stuff is fragmented across
    // multiple packets. So, we have to have a simple state machine.

    //
    // We need to know the content length to manage memory
    UInt32 theLen = 0;
    UInt32* theContentLenP = NULL;
    QTSS_Error theErr = QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqContentLen, 0, (void**)&theContentLenP, &theLen);
    if ((theErr != QTSS_NoErr) || (theLen != sizeof(UInt32)))
    {
        //
        // RETURN ERROR RESPONSE: ANNOUNCE WITHOUT CONTENT LENGTH
        return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssClientBadRequest,0);
    }

    // Check if the content-length is more than the imposed maximum
	// if it is then return error response
    if ( (sMaxAnnouncedSDPLengthInKbytes != 0) && (*theContentLenP > (sMaxAnnouncedSDPLengthInKbytes * 1024)) )
        return QTSSModuleUtils::SendErrorResponseWithMessage( inParams->inRTSPRequest, qtssPreconditionFailed, &sSDPTooLongMessage );
    
    //
    // Check for the existence of 2 attributes in the request: a pointer to our buffer for
    // the request body, and the current offset in that buffer. If these attributes exist,
    // then we've already been here for this request. If they don't exist, add them.
    UInt32 theBufferOffset = 0;
    char* theRequestBody = NULL;

    theLen = sizeof(theRequestBody);
    theErr = QTSS_GetValue(inParams->inRTSPRequest, sRequestBodyAttr, 0, &theRequestBody, &theLen);

    //qtss_printf("QTSSReflectorModule:DoAnnounce theRequestBody =%s\n",theRequestBody);
    if (theErr != QTSS_NoErr)
    {
        //
        // First time we've been here for this request. Create a buffer for the content body and
        // shove it in the request.
        theRequestBody = NEW char[*theContentLenP + 1];
        memset(theRequestBody,0,*theContentLenP + 1);
        theLen = sizeof(theRequestBody);
        theErr = QTSS_SetValue(inParams->inRTSPRequest, sRequestBodyAttr, 0, &theRequestBody, theLen);// SetValue creates an internal copy.
        Assert(theErr == QTSS_NoErr);
        
        //
        // Also store the offset in the buffer
        theLen = sizeof(theBufferOffset);
        theErr = QTSS_SetValue(inParams->inRTSPRequest, sBufferOffsetAttr, 0, &theBufferOffset, theLen);
        Assert(theErr == QTSS_NoErr);
    }
    
    theLen = sizeof(theBufferOffset);
    theErr = QTSS_GetValue(inParams->inRTSPRequest, sBufferOffsetAttr, 0, &theBufferOffset, &theLen);

    //
    // We have our buffer and offset. Read the data.
    theErr = QTSS_Read(inParams->inRTSPRequest, theRequestBody + theBufferOffset, *theContentLenP - theBufferOffset, &theLen);
    Assert(theErr != QTSS_BadArgument);

    if (theErr == QTSS_RequestFailed)
    {
        OSCharArrayDeleter charArrayPathDeleter(theRequestBody);
        //
        // NEED TO RETURN RTSP ERROR RESPONSE
        return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssClientBadRequest,0);
    }
    
    if ((theErr == QTSS_WouldBlock) || (theLen < (*theContentLenP - theBufferOffset)))
    {
		//
        // Update our offset in the buffer
        theBufferOffset += theLen;
        (void)QTSS_SetValue(inParams->inRTSPRequest, sBufferOffsetAttr, 0, &theBufferOffset, sizeof(theBufferOffset));
        //qtss_printf("QTSSReflectorModule:DoAnnounce Request some more data \n");
        //
        // The entire content body hasn't arrived yet. Request a read event and wait for it.
        // Our DoAnnounce function will get called again when there is more data.
        theErr = QTSS_RequestEvent(inParams->inRTSPRequest, QTSS_ReadableEvent);
        Assert(theErr == QTSS_NoErr);
        return QTSS_NoErr;
    }

    Assert(theErr == QTSS_NoErr);
    

//
// If we've gotten here, we have the entire content body in our buffer.
//

    if (killBroadcast)
    {  
        theFullPath.Len -= sSDPKillSuffix.Len;
        if (KillSession(&theFullPath, killBroadcast))
            return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssServerInternal,0);
        else
            return QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest, qtssClientNotFound, &sKILLNotValidMessage);  
    }

// ------------  Clean up missing required SDP lines

    ResizeableStringFormatter editedSDP(NULL,0);
    DoAnnounceAddRequiredSDPLines(inParams, &editedSDP, theRequestBody);
    StrPtrLen editedSDPSPL(editedSDP.GetBufPtr(),editedSDP.GetBytesWritten());

// ------------ Check the headers

    SDPContainer checkedSDPContainer;
    checkedSDPContainer.SetSDPBuffer( &editedSDPSPL );  
    if (!checkedSDPContainer.IsSDPBufferValid())
    {  
        return QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest, qtssUnsupportedMediaType, &sSDPNotValidMessage);
    }

    SDPSourceInfo theSDPSourceInfo(editedSDPSPL.Ptr, editedSDPSPL.Len );
    OSCharArrayDeleter charArrayPathDeleter(theRequestBody);
                        
    if (!InfoPortsOK(inParams,&theSDPSourceInfo,&theFullPath)) // All validity checks like this check should be done before touching the file.
    {   return QTSS_NoErr; // InfoPortsOK is sending back the error.
    }

// ------------ reorder the sdp headers to make them proper.
		
    SDPLineSorter sortedSDP(&checkedSDPContainer );

// ------------ Write the SDP 

    char* sessionHeaders = sortedSDP.GetSessionHeaders()->GetAsCString();
    OSCharArrayDeleter sessionHeadersDeleter(sessionHeaders);

    char* mediaHeaders = sortedSDP.GetMediaHeaders()->GetAsCString();
    OSCharArrayDeleter mediaHeadersDeleter(mediaHeaders);

   // sortedSDP.GetSessionHeaders()->PrintStrEOL();
   // sortedSDP.GetMediaHeaders()->PrintStrEOL();

    // write the file !! need error reporting
    FILE* theSDPFile= ::fopen(theFullPath.Ptr, "wb");//open 
    if (theSDPFile != NULL)
    {  
        qtss_fprintf(theSDPFile, "%s", sessionHeaders);
        qtss_fprintf(theSDPFile, "%s", mediaHeaders);
        ::fflush(theSDPFile);
        ::fclose(theSDPFile);   
    }
    else
    {   return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssClientForbidden,0);
    }
    

    //qtss_printf("QTSSReflectorModule:DoAnnounce SendResponse OK=200\n");
    
    return QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, 0);
}

void DoDescribeAddRequiredSDPLines(QTSS_StandardRTSP_Params* inParams, ReflectorSession* theSession, QTSS_TimeVal modDate,  ResizeableStringFormatter *editedSDP, StrPtrLen* theSDPPtr)
{
    SDPContainer checkedSDPContainer;
    checkedSDPContainer.SetSDPBuffer( theSDPPtr );  
    if (!checkedSDPContainer.HasReqLines())
    {
        if (!checkedSDPContainer.HasLineType('v'))
        { // add v line
            editedSDP->Put("v=0\r\n");
        }
        
        if (!checkedSDPContainer.HasLineType('s'))
        { // add s line
           char* theSDPName = NULL;            
            (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqFilePath, 0, &theSDPName);
            QTSSCharArrayDeleter thePathStrDeleter(theSDPName);
            editedSDP->Put("s=");
            editedSDP->Put(theSDPName);
            editedSDP->PutEOL();
        }
        
       if (!checkedSDPContainer.HasLineType('t'))
       { // add t line
            editedSDP->Put("t=0 0\r\n");
       }

       if (!checkedSDPContainer.HasLineType('o'))
       { // add o line
            editedSDP->Put("o=broadcast_sdp ");
            char tempBuff[256]= "";               
            tempBuff[255] = 0;
            qtss_snprintf(tempBuff,sizeof(tempBuff) - 1, "%"_U32BITARG_"", *(UInt32 *) &theSession);
            editedSDP->Put(tempBuff);

            editedSDP->Put(" ");
            // modified date is in milliseconds.  Convert to NTP seconds as recommended by rfc 2327
            qtss_snprintf(tempBuff, sizeof(tempBuff) - 1, "%"_64BITARG_"d", (SInt64) (modDate/1000) + 2208988800LU);
            editedSDP->Put(tempBuff);

            editedSDP->Put(" IN IP4 ");
            UInt32 buffLen = sizeof(tempBuff) -1;
            (void)QTSS_GetValue(inParams->inClientSession, qtssCliSesHostName, 0, &tempBuff, &buffLen);
            editedSDP->Put(tempBuff, buffLen);

            editedSDP->PutEOL();
        }
    } 

    editedSDP->Put(*theSDPPtr);

}



QTSS_Error DoDescribe(QTSS_StandardRTSP_Params* inParams)
{

    RLogRequest(INFO_ARTS_MODULE, 0,"Entry DoDescribe");
   if(CheckProfile(inParams) <0)
    {        
        return QTSS_NoErr;
    }
    
    char *theFilepath = NULL;
    ReflectorSession* theSession = DoSessionSetup(inParams, qtssRTSPReqFilePath, false, NULL, &theFilepath );
    OSCharArrayDeleter tempFilePath(theFilepath);
    
    UInt32 l_callid = 0, l_len = sizeof(UInt32);  
    
    UInt32 stb_module =0; 
    QTSS_ClientSessionObject     theClientSession   = inParams->inClientSession; 
    QTSS_Error err = QTSS_GetValue(theClientSession, sARTSClientSessionAttr, 0, &l_callid, &l_len);
    if ( err )
    {
       OSMutexLocker locker(sSessionMap->GetMutex());
       ClientSessID +=16;        
       if(ClientSessID>=  0xffff0)
            ClientSessID =16;
       
       l_callid =(UInt32) ClientSessID | RefSessID <<24;  
       
       err = QTSS_SetValue(theClientSession, sARTSClientSessionAttr, 0, (void *)&l_callid, sizeof(UInt32));
      Assert(err == QTSS_NoErr);    
    }
    
    RLogRequest(INFO_ARTS_MODULE, l_callid,"Entry,clientSession:%x,reflector session:%x",theClientSession,theSession);
    if (theSession == NULL)
        return QTSS_RequestFailed;
        
    RTPSessionOutput** theOutput = NULL;
    UInt32 theLen = 0;
    QTSS_Error theErr = QTSS_GetValuePtr(inParams->inClientSession, sOutputAttr, 0, (void**)&theOutput, &theLen);

    // If there already  was an RTPSessionOutput attached to this Client Session,
    // destroy it. 
    if (theErr == QTSS_NoErr && theOutput != NULL)
    {   RemoveOutput(*theOutput, (*theOutput)->GetReflectorSession(), false);
        RTPSessionOutput* theOutput = NULL;
        (void)QTSS_SetValue(inParams->inClientSession, sOutputAttr, 0, &theOutput, sizeof(theOutput));
        
    }
    // send the DESCRIBE response
    
    //above function has signalled that this request belongs to us, so let's respond
    iovec theDescribeVec[3] = { {0 }};
    
    Assert(theSession->GetLocalSDP()->Ptr != NULL);
    

    StrPtrLen theFileData;
    QTSS_TimeVal outModDate = 0;
    QTSS_TimeVal inModDate = -1;
    (void)QTSSModuleUtils::ReadEntireFile(theFilepath, &theFileData, inModDate, &outModDate);
    OSCharArrayDeleter fileDataDeleter(theFileData.Ptr); 

// -------------- process SDP to remove connection info and add track IDs, port info, and default c= line

    StrPtrLen theSDPData;
    SDPSourceInfo tempSDPSourceInfo(theFileData.Ptr, theFileData.Len); // will make a copy and delete in destructor
    theSDPData.Ptr = tempSDPSourceInfo.GetLocalSDP(&theSDPData.Len); // returns a new buffer with processed sdp
    OSCharArrayDeleter sdpDeleter(theSDPData.Ptr); // delete the temp sdp source info buffer returned by GetLocalSDP
    
    if (theSDPData.Len <= 0) // can't find it on disk or it failed to parse just use the one in the session.
    {
        theSDPData.Ptr = theSession->GetLocalSDP()->Ptr; // this sdp isn't ours it must not be deleted
        theSDPData.Len = theSession->GetLocalSDP()->Len;
    }


    char       *theRequestAttributes = NULL;
    UInt32     attributeLen          = 0;
    theErr = QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqFullRequest, 0, (void **)&theRequestAttributes, &attributeLen); 
    if(attributeLen >0 && strstr(theRequestAttributes,"motorola"))
    {            
       stb_module = SC_MOTO_MODULE;
    }

  
// ------------  Clean up missing required SDP lines

    ResizeableStringFormatter editedSDP(NULL,0);
    UInt32 sessLen=0, mediaLen=0;
    
    if(stb_module == SC_MOTO_MODULE)
    {
        char tmp_buf[1024]={'\0'};   
    
    sprintf(tmp_buf,"<MediaDescription>\n\t<MediaContainer NumStreams=\"1\"/>\n\t<MediaStreams>\n\t\t<MediaStream streamID=\"0\" MineType=\"\" MaxBitRate=\"1852\" Height=\"0\" Width=\"0\" FramePerSeconf=\"0\" AspectRatio=\"0\" StartTime=\"0\" Duration=\"0\" ParentDuration=\"0\" TrickMode=\"0\" NumTrickSpeed=\"0\" TrickSpeeds=\"\" TypeSpecificData=\"\" VideoType=\"\" VideoPID=\"\" AudioType=\"\" AudioPID=\"\" PcrPID=\"\"/>\n\t</MediaStreams>\n</MediaDescription>\n\r\n");
    
     editedSDP.Put(tmp_buf);
     StrPtrLen editedSDPSPL(editedSDP.GetBufPtr(),editedSDP.GetBytesWritten());
     RLogRequest(INFO_ARTS_MODULE, l_callid,"sdp:%s\n",tmp_buf);
     theDescribeVec[1].iov_base = editedSDPSPL.Ptr;
     theDescribeVec[1].iov_len = editedSDPSPL.Len;
     mediaLen =  editedSDPSPL.Len;
     
     RTSPRequestInterface      *theRTSPRequest    = (RTSPRequestInterface*)inParams->inRTSPRequest;  
     theRTSPRequest ->SetContentType (qtssRTSPMHType);
     
     (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssCacheControlHeader,
                                kCacheControlHeader.Ptr, kCacheControlHeader.Len);
    QTSSModuleUtils::SendDescribeResponse(inParams->inRTSPRequest, inParams->inClientSession,
                                            &theDescribeVec[0], 3, sessLen + mediaLen );
        
    }else
    {    
        DoDescribeAddRequiredSDPLines(inParams, theSession, outModDate, &editedSDP, &theSDPData);
        StrPtrLen editedSDPSPL(editedSDP.GetBufPtr(),editedSDP.GetBytesWritten());

        // ------------ Check the headers
    
        SDPContainer checkedSDPContainer;
        checkedSDPContainer.SetSDPBuffer( &editedSDPSPL );  
        if (!checkedSDPContainer.IsSDPBufferValid())
        {  
            return QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest, qtssUnsupportedMediaType, &sSDPNotValidMessage);
        }
            
 
        // ------------ Write the SDP 
        SDPLineSorter sortedSDP(&checkedSDPContainer);
        sessLen = sortedSDP.GetSessionHeaders()->Len;
        mediaLen = sortedSDP.GetMediaHeaders()->Len;
        theDescribeVec[1].iov_base = sortedSDP.GetSessionHeaders()->Ptr;
        theDescribeVec[1].iov_len = sortedSDP.GetSessionHeaders()->Len;

        theDescribeVec[2].iov_base = sortedSDP.GetMediaHeaders()->Ptr;
        theDescribeVec[2].iov_len = sortedSDP.GetMediaHeaders()->Len;
        
        (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssCacheControlHeader,
                                kCacheControlHeader.Ptr, kCacheControlHeader.Len);
         QTSSModuleUtils::SendDescribeResponse(inParams->inRTSPRequest, inParams->inClientSession,
                                            &theDescribeVec[0], 3, sessLen + mediaLen );
        
   }
                                            
    RLogRequest(INFO_ARTS_MODULE, l_callid,"Exiting");
    return QTSS_NoErr;
}

Bool16 InfoPortsOK(QTSS_StandardRTSP_Params* inParams, SDPSourceInfo* theInfo, StrPtrLen* inPath)
{   // Check the ports based on the Pref whether to enforce a static SDP port range.

    Bool16 isOK = true;

    if (sEnforceStaticSDPPortRange)
    {   UInt16 theInfoPort = 0;
        for (UInt32 x = 0; x < theInfo->GetNumStreams(); x++)
        {   theInfoPort = theInfo->GetStreamInfo(x)->fPort;
            QTSS_AttributeID theErrorMessageID = qtssIllegalAttrID;
            if (theInfoPort != 0)
            {   if  (theInfoPort < sMinimumStaticSDPPort) 
                    theErrorMessageID = sSDPcontainsInvalidMinimumPortErr;
                else if (theInfoPort > sMaximumStaticSDPPort)
                    theErrorMessageID = sSDPcontainsInvalidMaximumPortErr;
            }
            
            if (theErrorMessageID != qtssIllegalAttrID)
            {   
                char thePort[32];
                qtss_sprintf(thePort,"%u",theInfoPort);

                char *thePath = inPath->GetAsCString();
                OSCharArrayDeleter charArrayPathDeleter(thePath);
                
                char *thePathPort = NEW char[inPath->Len + 32];
                OSCharArrayDeleter charArrayPathPortDeleter(thePathPort);
                
                qtss_sprintf(thePathPort,"%s:%s",thePath,thePort);
                (void) QTSSModuleUtils::LogError(qtssWarningVerbosity, theErrorMessageID, 0, thePathPort);
                
                StrPtrLen thePortStr(thePort);                  
                (void) QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssUnsupportedMediaType, theErrorMessageID,&thePortStr);

                return false;
            }
        }
    }
    
    return isOK;
}


void GetLocalHost( char *host)
{
    if( sBindLocalHost && strlen(sBindLocalHost))
    {
        strcpy(host,sBindLocalHost);
        return;
    }
    
    for (UInt32 theNumPairs = 0; theNumPairs < SocketUtils::GetNumIPAddrs(); theNumPairs++)
    {
    
        StrPtrLen* AddrStrPtr = SocketUtils::GetIPAddrStr(theNumPairs);
        if( AddrStrPtr && (  ::strcmp(AddrStrPtr->Ptr, "127.0.0.1") != 0))
        {
            memcpy(host,AddrStrPtr->Ptr,AddrStrPtr->Len);
            break;
        }
    }
    
    
}


ReflectorSession* FindOrCreateSession(StrPtrLen* inPath, QTSS_StandardRTSP_Params* inParams, StrPtrLen* inData, Bool16 isPush, Bool16 *foundSessionPtr)
{   
    // This function assumes that inPath is NULL terminated
    // Ok, look for a reflector session matching this full path as the ID
    
    char       *theRequestAttributes = NULL;
    char        src_host[128]={'\0'};
    char        local_host[128]={'\0'};
    
    char        *fileName=NULL; 
    char        vid[128]={'\0'}; 
    char        url[4096]={'\0'}; 
    UInt32      attributeLen          = 0;    
    UInt32      src_port =0;       
    UInt32      l_callid = 0, l_len = sizeof(UInt32);  
    UInt32      local_host_len =0;
    bool        isCamera = 0;
    
    
    OSMutexLocker locker(sSessionMap->GetMutex());
    OSRef* theSessionRef = sSessionMap->Resolve(inPath);   
     
    QTSS_ClientSessionObject     theClientSession   = inParams->inClientSession; 
    QTSS_RTSPSessionObject       theRTSPSession = inParams->inRTSPSession;
   
    
    QTSS_Error err = QTSS_GetValue(theClientSession, sARTSClientSessionAttr, 0, (void *)&l_callid, &l_len);
    
    
    GetLocalHost(local_host); 
  
    QTSS_Error theErr = QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqFileName, 0, &fileName);
    if(fileName == NULL)
    {
        RLogRequest(INFO_ARTS_MODULE,l_callid,"RTSPReqFileName is null");
        return NULL;
    }
    QTSSCharArrayDeleter fileNameStrDeleter(fileName);  
   
    RLogRequest(DEBUG_ARTS_MODULE,l_callid,"inPath:%s,local_host:%s,fileName:%s",inPath->Ptr,local_host,fileName);
    
    if(strstr(inPath->Ptr,"camera") != NULL)
    {
        isCamera =1;
    }
    
    ReflectorSession* theSession = NULL;
       
    if(theSessionRef!=NULL)
        RLogRequest(DEBUG_ARTS_MODULE,l_callid,"session_ref:%d,inPath:%s,theSessionRef:%x",theSessionRef->GetRefCount(),inPath->Ptr,theSessionRef);
    if (theSessionRef == NULL)
    {
        //If this URL doesn't already have a reflector session, we must make a new
        //one. The first step is to create an SDPSourceInfo object.
        
        StrPtrLen theFileData;
        StrPtrLen theFileDeleteData;        
        //
        // If no file data is provided by the caller, read the file data out of the file.
        // If file data is provided, use that as our SDP data
       
        
        char *tsSuffix = strstr(fileName,".ts");
        
        if(tsSuffix != NULL && theFileData.Len <=0 )
        {   
            memcpy(vid,fileName,tsSuffix-fileName);           
            RLogRequest(INFO_ARTS_MODULE,l_callid,"vid:%s",vid);
            char sdp_buf[1024]={'\0'};
            
            if( generate_sdp(vid,l_callid,sdp_buf,isCamera,src_host,&src_port,local_host)<0 || src_port <= 0)
            {
                return NULL;
            }else
            {
                RLogRequest(DEBUG_ARTS_MODULE,l_callid,"sdp:%s",sdp_buf);
                StrPtrLen sdpData(sdp_buf); 
                theFileData = sdpData;  
                
                FILE *fb = fopen(inPath->Ptr,"wb+");
                if(fb)
                {
                    fwrite(theFileData.Ptr,sizeof(char),theFileData.Len,fb);
                    fclose(fb);
                }else
                {
                    RLogRequest(INFO_ARTS_MODULE,l_callid,"open %s failed,err:%s",inPath->Ptr,strerror(errno));
                }
                           
            }
        }
        
        
         if( theFileData.Len <=0)
        {
            if (inData == NULL)
            {   (void)QTSSModuleUtils::ReadEntireFile(inPath->Ptr, &theFileDeleteData);
                theFileData = theFileDeleteData;
            }
            else
            {
                theFileData = *inData;          
            }
        }
        
       
        OSCharArrayDeleter fileDataDeleter(theFileDeleteData.Ptr); 
            
        if (theFileData.Len <= 0)
        {
	        RLogRequest(INFO_ARTS_MODULE,l_callid,"theFileData is null,path:%s",inPath->Ptr);
            return NULL;
        }    
        SDPSourceInfo* theInfo = NEW SDPSourceInfo(theFileData.Ptr, theFileData.Len); // will make a copy
            
        if (!theInfo->IsReflectable())
        {   delete theInfo;
	        RLogRequest(INFO_ARTS_MODULE,l_callid,"!theInfo->IsReflectable()");
            return NULL;
        }
        
        
        if (!InfoPortsOK(inParams, theInfo, inPath))
        {   delete theInfo;	   
            return NULL;
        }
       
        // Check if broadcast is allowed before doing anything else
        // At this point we know it is a definitely a reflector session
        // It is either incoming automatic broadcast setup or a client setup to view broadcast
        // In either case, verify whether the broadcast is allowed, and send forbidden response
        // back
        if (!AllowBroadcast(inParams->inRTSPRequest))
        {
	    
            (void) QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest, qtssClientForbidden, &sBroadcastNotAllowed);
            return NULL;
        }
    
        //
        // Setup a ReflectorSession and bind the sockets. If we are negotiating,
        // make sure to let the session know that this is a Push Session so
        // ports may be modified.
        UInt32 theSetupFlag = ReflectorSession::kMarkSetup;
        if (isPush)
            theSetupFlag |= ReflectorSession::kIsPushSession;
       
        theSession = NEW ReflectorSession(inPath);
		if (theSession == NULL)
		{
		      RLogRequest(INFO_ARTS_MODULE,l_callid,"new reflector failed");	
              return NULL;
		}
		RefSessID ++;
		theSession->SetHasBufferedStreams(true); // buffer the incoming streams for clients
        
        // SetupReflectorSession stores theInfo in theSession so DONT delete the Info if we fail here, leave it alone.
        // deleting the session will delete the info.
        
        
       if(theSession->fARTSRelayInfo == NULL   && sRelayModuleClient)
       {
            theSession->fARTSRelayInfo = new ARTSRelayInfo(theSession->GetSourceInfo());
            theErr = QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqURI, 0, (void **)&theRequestAttributes, &attributeLen);  
            sprintf(url,"rtsp://%s",src_host);   
            memcpy(url+strlen(url),theRequestAttributes,attributeLen);
            RLogRequest(INFO_ARTS_MODULE,l_callid,"src url:%s",url);
            theSession->fARTSRelayInfo->Configure(src_host,7052,url,0,sClientSendInterval);        
       }
       
       
       
        QTSS_Error theErr = theSession->SetupReflectorSession(theInfo, inParams, theSetupFlag,sOneSSRCPerStream, sTimeoutSSRCSecs,theSession->fARTSRelayInfo);
        if (theErr != QTSS_NoErr)
        {   
             RLogRequest(INFO_ARTS_MODULE,l_callid,"SetupReflectorSession failed\n");
            delete theSession;
            return NULL;
        }
       
       
        //put the session's ID into the session map.
        theErr = sSessionMap->Register(theSession->GetRef());
        Assert(theErr == QTSS_NoErr);

        //unless we do this, the refcount won't increment (and we'll delete the session prematurely
        
        /*
        if (!isPush)
        {   OSRef* debug = sSessionMap->Resolve(inPath);
            Assert(debug == theSession->GetRef());
        }*/
    }
    else
    {
        // Check if broadcast is allowed before doing anything else
        // At this point we know it is a definitely a reflector session
        // It is either incoming automatic broadcast setup or a client setup to view broadcast
        // In either case, verify whether the broadcast is allowed, and send forbidden response
        // back
                 
        
        if (!AllowBroadcast(inParams->inRTSPRequest))
        {
            (void) QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest, qtssClientForbidden, &sBroadcastNotAllowed);
            return NULL;
        }
        
        char *fullRequest = NULL;
        (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqFullRequest, 0, &fullRequest);
        OSCharArrayDeleter methodDeleter(fullRequest);
        if(fullRequest != NULL )
        {      
            
            if(strstr(fullRequest,"DESCRIBE")!= NULL)
            {
                 sSessionMap->Release(theSessionRef); 
            }     
        }
      
        if (foundSessionPtr)
            *foundSessionPtr = true;
            
        StrPtrLen theFileData;
        SDPSourceInfo* theInfo = NULL;
        
        if (inData == NULL)
            (void)QTSSModuleUtils::ReadEntireFile(inPath->Ptr, &theFileData);
        OSCharArrayDeleter charArrayDeleter(theFileData.Ptr);
            
        if (theFileData.Len <= 0)
            return NULL;
        
        theInfo = NEW SDPSourceInfo(theFileData.Ptr, theFileData.Len);
        if (theInfo == NULL) 
            return NULL;
      
        if (!InfoPortsOK(inParams, theInfo, inPath))
        {   
            delete theInfo;
            return NULL;
        }
        
        delete theInfo;
        
       
        theSession = (ReflectorSession*)theSessionRef->GetObject();   
              
        if (isPush && theSession)
        {
            UInt32 theSetupFlag = ReflectorSession::kMarkSetup | ReflectorSession::kIsPushSession;
            QTSS_Error theErr = theSession->SetupReflectorSession(NULL, inParams, theSetupFlag);
            if (theErr != QTSS_NoErr)
            {   return NULL;
            }
        }
    }
        
   if(theSession)     
   // RLogRequest(INFO_ARTS_MODULE,l_callid,"sRelayModuleClient:%d,isCamera:%d,vid:%s,fstartTask:%d",sRelayModuleClient,isCamera,vid,theSession->fStartStoreTask);   
    RLogRequest(INFO_ARTS_MODULE,l_callid,"reflectorSess_ref:%x,ref_count:%d",theSession->GetRef(),theSession->GetRef()->GetRefCount());
   if(theSession && !(theSession->fStartStoreTask) && strlen(vid) >0 && isCamera && !sRelayModuleClient)
   {
        StoreTask * curStoreTask = new StoreTask();
        curStoreTask->configure(vid,sDataBaseHost,30,l_callid);
        curStoreTask->Signal(Task::kStartEvent);
        theSession->fStartStoreTask = true;
        theSession->fStoreTask = curStoreTask;
        RLogRequest(INFO_ARTS_MODULE,l_callid,"start store Task");
   }
    
   
   if(theSession->fARTSRelayInfo != NULL && theSession->fStartSourceThread == FALSE )
   {
     theSession->fARTSRelayInfo->StartARTSRelayTask();
     theSession->fStartSourceThread = TRUE;
   }
   
    
       
    Assert(theSession != NULL);  
    return theSession;

    if (theSessionRef == NULL)
    {
        //If this URL doesn't already have a reflector session, we must make a new
        //one. The first step is to create an SDPSourceInfo object.
        
        StrPtrLen theFileData;
        StrPtrLen theFileDeleteData;
        
        //
        // If no file data is provided by the caller, read the file data out of the file.
        // If file data is provided, use that as our SDP data
        if (inData == NULL)
        {   (void)QTSSModuleUtils::ReadEntireFile(inPath->Ptr, &theFileDeleteData);
            theFileData = theFileDeleteData;
        }
        else
            theFileData = *inData;
        OSCharArrayDeleter fileDataDeleter(theFileDeleteData.Ptr); 
            
        if (theFileData.Len <= 0)
            return NULL;
            
        SDPSourceInfo* theInfo = NEW SDPSourceInfo(theFileData.Ptr, theFileData.Len); // will make a copy
            
        if (!theInfo->IsReflectable())
        {   delete theInfo;
            return NULL;
        }
        if ( !theInfo->IsActiveNow() && !isPush)
        {   delete theInfo;
            return NULL;
        }
        
        if (!InfoPortsOK(inParams, theInfo, inPath))
        {   delete theInfo;
            return NULL;
        }
        //
        // Setup a ReflectorSession and bind the sockets. If we are negotiating,
        // make sure to let the session know that this is a Push Session so
        // ports may be modified.
        UInt32 theSetupFlag = ReflectorSession::kMarkSetup;
        if (isPush)
            theSetupFlag |= ReflectorSession::kIsPushSession;
        
        theSession = NEW ReflectorSession(inPath);
        
        // SetupReflectorSession stores theInfo in theSession so DONT delete the Info if we fail here, leave it alone.
        // deleting the session will delete the info.
        QTSS_Error theErr = theSession->SetupReflectorSession(theInfo, inParams, theSetupFlag,sOneSSRCPerStream, sTimeoutSSRCSecs);
        if (theErr != QTSS_NoErr || theSession == NULL)
        {   delete theSession;
            return NULL;
        }
       
        //printf("Created reflector session = %x,theInfo=%x \n",  theSession,theInfo);
        //put the session's ID into the session map.
        theErr = sSessionMap->Register(theSession->GetRef());
        Assert(theErr == QTSS_NoErr);

        //unless we do this, the refcount won't increment (and we'll delete the session prematurely
        if (!isPush)
        {   OSRef* debug = sSessionMap->Resolve(inPath);
            Assert(debug == theSession->GetRef());
        }
    }
    else
    {
    
        if (isPush) 
            sSessionMap->Release(theSessionRef); // don't need if a push;// don't need if a push; A Release is necessary or we will leak ReflectorSessions.

        if (foundSessionPtr)
            *foundSessionPtr = true;
            
        StrPtrLen theFileData;
        SDPSourceInfo* theInfo = NULL;
        
        if (inData == NULL)
            (void)QTSSModuleUtils::ReadEntireFile(inPath->Ptr, &theFileData);
        OSCharArrayDeleter charArrayDeleter(theFileData.Ptr);
            
        if (theFileData.Len <= 0)
            return NULL;
        
        theInfo = NEW SDPSourceInfo(theFileData.Ptr, theFileData.Len);
        if (theInfo == NULL) 
            return NULL;
            
        if ( !theInfo->IsActiveNow() && !isPush)
        {   delete theInfo;
            return NULL;
        }
        
        if (!InfoPortsOK(inParams, theInfo, inPath))
        {   delete theInfo;
            return NULL;
        }
        
        delete theInfo;
        
        theSession = (ReflectorSession*)theSessionRef->GetObject(); 
        if (isPush && theSession)
        {
            UInt32 theSetupFlag = ReflectorSession::kMarkSetup | ReflectorSession::kIsPushSession;
            QTSS_Error theErr = theSession->SetupReflectorSession(NULL, inParams, theSetupFlag,sOneSSRCPerStream, sTimeoutSSRCSecs);
            if (theErr != QTSS_NoErr)
            {   return NULL;
            }
        }
    }
            
    Assert(theSession != NULL);

	// Turn off overbuffering if the "disable_overbuffering" pref says so
	if (sDisableOverbuffering)
		(void)QTSS_SetValue(inParams->inClientSession, qtssCliSesOverBufferEnabled, 0, &sFalse, sizeof(sFalse));

    return theSession;
}

// ONLY call when performing a setup.
void DeleteReflectorPushSession(QTSS_StandardRTSP_Params* inParams, ReflectorSession* theSession, Bool16 foundSession)
{
    ReflectorSession* stopSessionProcessing = NULL;
    QTSS_Error theErr  = QTSS_SetValue(inParams->inClientSession, sClientBroadcastSessionAttr, 0, &stopSessionProcessing, sizeof(stopSessionProcessing));
    Assert(theErr == QTSS_NoErr);

    if (foundSession) 
        return; // we didn't allocate the session so don't delete

    OSRef* theSessionRef = theSession->GetRef();
    if (theSessionRef != NULL) 
    {               
        theSession->TearDownAllOutputs(); // just to be sure because we are about to delete the session.
        sSessionMap->UnRegister(theSessionRef);// we had an error while setting up-- don't let anyone get the session
        delete theSession;  
    }
}

QTSS_Error AddRTPStream(ReflectorSession* theSession,QTSS_StandardRTSP_Params* inParams, QTSS_RTPStreamObject *newStreamPtr)
{       
    // Ok, this is completely crazy but I can't think of a better way to do this that's
    // safe so we'll do it this way for now. Because the ReflectorStreams use this session's
    // stream queue, we need to make sure that each ReflectorStream is not reflecting to this
    // session while we call QTSS_AddRTPStream. One brutal way to do this is to grab each
    // ReflectorStream's mutex, which will stop every reflector stream from running.
    Assert(newStreamPtr != NULL);
    
    if (theSession != NULL)
        for (UInt32 x = 0; x < theSession->GetNumStreams(); x++)
            theSession->GetStreamByIndex(x)->GetMutex()->Lock();
    
    //
    // Turn off reliable UDP transport, because we are not yet equipped to
    // do overbuffering.
    QTSS_Error theErr = QTSS_AddRTPStream(inParams->inClientSession, inParams->inRTSPRequest, newStreamPtr, qtssASFlagsForceUDPTransport);

    if (theSession != NULL)
        for (UInt32 y = 0; y < theSession->GetNumStreams(); y++)
            theSession->GetStreamByIndex(y)->GetMutex()->Unlock();

    return theErr;
}

QTSS_Error DoSetup(QTSS_StandardRTSP_Params* inParams)
{
    if(CheckProfile(inParams) <0)
    {
        return QTSS_NoErr;
    } 
    
    UInt32 theTrackID =1;
    
    ReflectorSession* theSession = NULL;
    UInt32 l_callid = 0, l_len = sizeof(UInt32);   
    QTSS_ClientSessionObject     theClientSession   = inParams->inClientSession; 
    QTSS_Error err = QTSS_GetValue(theClientSession, sARTSClientSessionAttr, 0, &l_callid, &l_len);
    RTSPSessionInterface      *theRTSPSess       = (RTSPSessionInterface*)inParams->inRTSPSession;
    RLogRequest(INFO_ARTS_MODULE, l_callid,"Entry");
    
    if(sSupportOtherChannel)
    {
    
        theRTSPSess->SetSupportOtherChannel();
    } 
    
    
    char       *theRequestAttributes = NULL;
    UInt32     attributeLen          = 0,stb_module=0;
    err = QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqFullRequest, 0, (void **)&theRequestAttributes, &attributeLen); 
    if(attributeLen >0 && strstr(theRequestAttributes,"motorola"))
    {            
       stb_module = SC_MOTO_MODULE;
    }
    
    if(stb_module == SC_MOTO_MODULE)
    {
        RTSPRequestInterface      *theRTSPRequest    = (RTSPRequestInterface*)inParams->inRTSPRequest;  
        theRTSPRequest ->SetContentType (qtssRTSPMHType);
        theRTSPSess->NoCleanObjectHolderCount = true;
    }
    
    // See if this is a push from a Broadcaster
    UInt32 theLen = 0;
    UInt32 *transportModePtr = NULL;
    QTSS_Error theErr  = QTSS_GetValuePtr(inParams->inRTSPRequest, qtssRTSPReqTransportMode, 0, (void**)&transportModePtr, &theLen);
	Bool16 isPush = (transportModePtr != NULL && *transportModePtr == qtssRTPTransportModeRecord) ? true : false;
    Bool16 foundSession = false;
    
    // Check to see if we have a RTPSessionOutput for this Client Session. If we don't,
    // we should make one
    RTPSessionOutput** theOutput = NULL;
    theErr = QTSS_GetValuePtr(inParams->inClientSession, sOutputAttr, 0, (void**)&theOutput, &theLen);
    if (theLen != sizeof(RTPSessionOutput*))
    {
        //
        // This may be an incoming data session. If that's the case, there will be a Reflector
        // Session in the ClientSession
        //theLen = sizeof(theSession);
        //theErr = QTSS_GetValue(inParams->inClientSession, sSessionAttr, 0, &theSession, &theLen);
        
        if (theErr != QTSS_NoErr  && !isPush)
        {
            // This is not an incoming data session...
            // Do the standard ReflectorSession setup, create an RTPSessionOutput
            theSession = DoSessionSetup(inParams, qtssRTSPReqFilePathTrunc);
            
            if (theSession == NULL)
                return QTSS_RequestFailed;
            
            RLogRequest(DEBUG_ARTS_MODULE, l_callid,"reflectorSess_ref:%d",theSession->GetRef()->GetRefCount());
            
            
            RTPSessionOutput* theNewOutput = NEW RTPSessionOutput(inParams->inClientSession, theSession, sServerPrefs, sStreamCookieAttr);
            theSession->AddOutput(theNewOutput,true);
            (void)QTSS_SetValue(inParams->inClientSession, sOutputAttr, 0, &theNewOutput, sizeof(theNewOutput));
            theNewOutput->callid = l_callid;
        }
        else 
        {           
            theSession = DoSessionSetup(inParams, qtssRTSPReqFilePathTrunc,isPush,&foundSession); 
            if (theSession == NULL)
                return QTSS_RequestFailed;  
                
            // This is an incoming data session. Set the Reflector Session in the ClientSession
            theErr = QTSS_SetValue(inParams->inClientSession, sClientBroadcastSessionAttr, 0, &theSession, sizeof(theSession));
            Assert(theErr == QTSS_NoErr);
            //qtss_printf("QTSSReflectorModule.cpp:SETsession    sClientBroadcastSessionAttr=%"_U32BITARG_" theSession=%"_U32BITARG_" err=%"_S32BITARG_" \n",(UInt32)sClientBroadcastSessionAttr, (UInt32) theSession,theErr);
            (void) QTSS_SetValue(inParams->inClientSession, qtssCliSesTimeoutMsec, 0, &sBroadcasterSessionTimeoutMilliSecs, sizeof(sBroadcasterSessionTimeoutMilliSecs));
       }
    }
    else
    {   theSession = (*theOutput)->GetReflectorSession();
        if (theSession == NULL)
            return QTSS_RequestFailed;  
    }
    
    char *fullRequest = NULL;
    (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqFullRequest, 0, &fullRequest);
    OSCharArrayDeleter reqDeleter(fullRequest);
    if(fullRequest != NULL && theSession != NULL)
    {   
       
        if(strstr(fullRequest,"MP2T")!= NULL)
        {
            theSession->SessionTransportType = qtssRTPTransportTypeMPEG2;
            RLogRequest(DEBUG_ARTS_MODULE, l_callid,"Entry MP2T");
        }     
    }
    
    //unless there is a digit at the end of this path (representing trackID), don't
    //even bother with the request
    char* theDigitStr = NULL;
    (void)QTSS_GetValueAsString(inParams->inRTSPRequest, qtssRTSPReqFileDigit, 0, &theDigitStr);
    QTSSCharArrayDeleter theDigitStrDeleter(theDigitStr);
    if (theDigitStr == NULL && theSession->SessionTransportType != qtssRTPTransportTypeMPEG2)
    {
       RLogRequest(INFO_ARTS_MODULE, l_callid,"theDigitStr is null");
        if (isPush)
            DeleteReflectorPushSession(inParams,theSession, foundSession);
        return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssClientBadRequest,sExpectedDigitFilenameErr);
    }else if(theDigitStr)
    {
    
        theTrackID = ::strtol(theDigitStr, NULL, 10);
    }
    
    //
    // If this is an incoming data session, skip everything having to do with setting up a new
    // RTP Stream. 
    if (isPush)
    {
        
        // Get info about this trackID
        SourceInfo::StreamInfo* theStreamInfo = theSession->GetSourceInfo()->GetStreamInfoByTrackID(theTrackID);
        // If theStreamInfo is NULL, we don't have a legit track, so return an error
        if (theStreamInfo == NULL)
        {   RLogRequest(INFO_ARTS_MODULE, l_callid,"theStreamInfo is NULL");
            DeleteReflectorPushSession(inParams,theSession, foundSession);
            return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssClientBadRequest,
                                                        sReflectorBadTrackIDErr);
        }
        
        if (!sAllowDuplicateBroadcasts && theStreamInfo->fSetupToReceive) 
        {
            DeleteReflectorPushSession(inParams,theSession, foundSession);
            RLogRequest(INFO_ARTS_MODULE, l_callid,"Err");  
            return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssPreconditionFailed, sDuplicateBroadcastStreamErr);
        }

        UInt16 theReceiveBroadcastStreamPort = theStreamInfo->fPort;
        theErr = QTSS_SetValue(inParams->inRTSPRequest, qtssRTSPReqSetUpServerPort, 0, &theReceiveBroadcastStreamPort, sizeof(theReceiveBroadcastStreamPort));
        Assert(theErr == QTSS_NoErr);
        

        QTSS_RTPStreamObject newStream = NULL;
        theErr = AddRTPStream(theSession,inParams,&newStream);
        
        if (theErr != QTSS_NoErr)
        {   
            DeleteReflectorPushSession(inParams,theSession, foundSession);
            RLogRequest(DEBUG_ARTS_MODULE, l_callid,"AddRTPStream failed!\n");
            return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssClientBadRequest, 0);
        }
            
        //send the setup response

        (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssCacheControlHeader,
                                    kCacheControlHeader.Ptr, kCacheControlHeader.Len);

        (void)QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, newStream, 0);
        
        theStreamInfo->fSetupToReceive = true;
        // This is an incoming data session. Set the Reflector Session in the ClientSession
        theErr = QTSS_SetValue(inParams->inClientSession, sClientBroadcastSessionAttr, 0, &theSession, sizeof(theSession));
        Assert(theErr == QTSS_NoErr);
            
        if (theSession != NULL)
            theSession->AddBroadcasterClientSession(inParams);
            
        return QTSS_NoErr;      
    }
    RLogRequest(DEBUG_ARTS_MODULE, l_callid,"!isPush");
    
    // Get info about this trackID
    SourceInfo::StreamInfo* theStreamInfo = theSession->GetSourceInfo()->GetStreamInfoByTrackID(theTrackID);
    // If theStreamInfo is NULL, we don't have a legit track, so return an error
    if (theStreamInfo == NULL)
        return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssClientBadRequest,
                                                    sReflectorBadTrackIDErr);
                                                    
    StrPtrLen* thePayloadName = &theStreamInfo->fPayloadName;
    QTSS_RTPPayloadType thePayloadType = theStreamInfo->fPayloadType;

    StringParser parser(thePayloadName);
    
    parser.GetThru(NULL, '/');
    theStreamInfo->fTimeScale = parser.ConsumeInteger(NULL);
    if (theStreamInfo->fTimeScale == 0)
        theStreamInfo->fTimeScale = 90000;
    
    QTSS_RTPStreamObject newStream = NULL;
    {
        // Ok, this is completely crazy but I can't think of a better way to do this that's
        // safe so we'll do it this way for now. Because the ReflectorStreams use this session's
        // stream queue, we need to make sure that each ReflectorStream is not reflecting to this
        // session while we call QTSS_AddRTPStream. One brutal way to do this is to grab each
        // ReflectorStream's mutex, which will stop every reflector stream from running.
        
        for (UInt32 x = 0; x < theSession->GetNumStreams(); x++)
            theSession->GetStreamByIndex(x)->GetMutex()->Lock();
            
        theErr = QTSS_AddRTPStream(inParams->inClientSession, inParams->inRTSPRequest, &newStream, qtssASFlagsAllowDestination);

        for (UInt32 y = 0; y < theSession->GetNumStreams(); y++)
            theSession->GetStreamByIndex(y)->GetMutex()->Unlock();
            
        if (theErr != QTSS_NoErr)
        {
            RLogRequest(INFO_ARTS_MODULE, l_callid,"Add stream failed,err:%d",theErr);
            return theErr;
         }
    }
    
    // Set up dictionary items for this stream
    theErr = QTSS_SetValue(newStream, qtssRTPStrPayloadName, 0, thePayloadName->Ptr, thePayloadName->Len);
    Assert(theErr == QTSS_NoErr);
    theErr = QTSS_SetValue(newStream, qtssRTPStrPayloadType, 0, &thePayloadType, sizeof(thePayloadType));
    Assert(theErr == QTSS_NoErr);
    theErr = QTSS_SetValue(newStream, qtssRTPStrTrackID, 0, &theTrackID, sizeof(theTrackID));
    Assert(theErr == QTSS_NoErr);
    theErr = QTSS_SetValue(newStream, qtssRTPStrTimescale, 0, &theStreamInfo->fTimeScale, sizeof(theStreamInfo->fTimeScale));
    Assert(theErr == QTSS_NoErr);

    // We only want to allow over buffering to dynamic rate clients   
    SInt32  canDynamicRate = -1;
    theLen = sizeof(canDynamicRate);
    (void) QTSS_GetValue(inParams->inRTSPRequest, qtssRTSPReqDynamicRateState, 0, (void*) &canDynamicRate, &theLen);
    if (canDynamicRate < 1) // -1 no rate field, 0 off
        (void)QTSS_SetValue(inParams->inClientSession, qtssCliSesOverBufferEnabled, 0, &sFalse, sizeof(sFalse));

    // Place the stream cookie in this stream for future reference
    void* theStreamCookie = theSession->GetStreamCookie(theTrackID);
    Assert(theStreamCookie != NULL);
    theErr = QTSS_SetValue(newStream, sStreamCookieAttr, 0, &theStreamCookie, sizeof(theStreamCookie));
    Assert(theErr == QTSS_NoErr);

    // Set the number of quality levels.
    static UInt32 sNumQualityLevels = ReflectorSession::kNumQualityLevels;
    theErr = QTSS_SetValue(newStream, qtssRTPStrNumQualityLevels, 0, &sNumQualityLevels, sizeof(sNumQualityLevels));
    Assert(theErr == QTSS_NoErr);
    
    //send the setup response
    (void)QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssCacheControlHeader,
                                kCacheControlHeader.Ptr, kCacheControlHeader.Len);
    (void)QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, newStream, qtssSetupRespDontWriteSSRC);
    
    RLogRequest(INFO_ARTS_MODULE, l_callid,"Exiting");
    
    return QTSS_NoErr;
}



Bool16 HaveStreamBuffers(QTSS_StandardRTSP_Params* inParams, ReflectorSession* inSession)
{
    if (inSession == NULL || inParams == NULL)
        return false;
        
    UInt32 firstTimeStamp = 0;
    UInt16 firstSeqNum = 0;            
    ReflectorSender* theSender = NULL;
    ReflectorStream* theReflectorStream = NULL;
    QTSS_RTPStreamObject* theRef = NULL;
    UInt32 theStreamIndex = 0;
    UInt32 theLen = 0; 
    QTSS_Error theErr = QTSS_NoErr;
    Bool16 haveBufferedStreams = true; // set to false and return if we can't set the packets
    UInt32 y = 0;
    
    
    SInt64 packetArrivalTime = 0;

    //lock all streams
    for (y = 0; y < inSession->GetNumStreams(); y++)
        inSession->GetStreamByIndex(y)->GetMutex()->Lock();

           
    for (   theStreamIndex = 0;
            QTSS_GetValuePtr(inParams->inClientSession, qtssCliSesStreamObjects, theStreamIndex, (void**)&theRef, &theLen) == QTSS_NoErr;
            theStreamIndex++)
    {        
        theReflectorStream = inSession->GetStreamByIndex(theStreamIndex);

      //  if (!theReflectorStream->HasFirstRTCP())
      //      printf("theStreamIndex =%"_U32BITARG_" no rtcp\n", theStreamIndex);
        
      //  if (!theReflectorStream->HasFirstRTP())
      //      printf("theStreamIndex = %"_U32BITARG_" no rtp\n", theStreamIndex);
            
        if ((theReflectorStream == NULL) || (false == theReflectorStream->HasFirstRTP()) )
        {    
            haveBufferedStreams = false;
            //printf("1 breaking no buffered streams\n");
             break;
        }                
        
        theSender = theReflectorStream->GetRTPSender();                
        haveBufferedStreams =  theSender->GetFirstPacketInfo(&firstSeqNum, &firstTimeStamp, &packetArrivalTime);
        //printf("theStreamIndex= %"_U32BITARG_" haveBufferedStreams=%d, seqnum=%d, timestamp=%"_U32BITARG_"\n", theStreamIndex, haveBufferedStreams, firstSeqNum, firstTimeStamp);
       
       if (!haveBufferedStreams)
        {    
            //printf("2 breaking no buffered streams\n");
            break;
        }                        
        
        theErr = QTSS_SetValue(*theRef, qtssRTPStrFirstSeqNumber, 0, &firstSeqNum, sizeof(firstSeqNum));
        Assert(theErr == QTSS_NoErr);
        
        theErr = QTSS_SetValue(*theRef, qtssRTPStrFirstTimestamp, 0, &firstTimeStamp, sizeof(firstTimeStamp));
        Assert(theErr == QTSS_NoErr);
        
   
    }     
    //unlock all streams
    for (y = 0; y < inSession->GetNumStreams(); y++)
        inSession->GetStreamByIndex(y)->GetMutex()->Unlock();
            
    return haveBufferedStreams;
}

QTSS_Error DoPlay(QTSS_StandardRTSP_Params* inParams, ReflectorSession* inSession)
{
    if(CheckProfile(inParams) <0)
    {
        return QTSS_NoErr;
    }
    
    QTSS_Error theErr = QTSS_NoErr;
    UInt32 flags = 0;
    UInt32 theLen = 0;
    UInt32 attributeLen = 0;
    Bool16 rtpInfoEnabled = false; 
    char * theRequestAttributes = NULL;
    
    UInt32 l_callid = 0, l_len = sizeof(UInt32);   
    QTSS_ClientSessionObject     theClientSession   = inParams->inClientSession; 
    QTSS_Error err = QTSS_GetValue(theClientSession, sARTSClientSessionAttr, 0, (void *)&l_callid, &l_len);
    
    
    
    RTSPSessionInterface * theRTSPSession =(RTSPSessionInterface *) inParams->inRTSPSession;
    
    RTPSessionInterface * theRTPSession = (RTPSessionInterface *)inParams->inClientSession;	   
    StrPtrLen* sessionIDStr= theRTPSession->GetValue(qtssCliSesRTSPSessionID);	
    char *sessIDCStr = sessionIDStr->GetAsCString();  
    RLogRequest(INFO_ARTS_MODULE, l_callid,"Entry,sess:%s\n",sessIDCStr);
    
    if(sessIDCStr && strlen(sessIDCStr) && sSupportOtherChannel)
    {
        co_socket_t *p = get_co_socket_node(sessIDCStr);
        if(p && p->used == false) 
        {
            RLogRequest(DEBUG_ARTS_MODULE, l_callid,"find rtspsessionObj"); 
	        if(p->sock != NULL)      
	        {
	              
	              theRTSPSession->GetOutputStream()->SetDataSocket((TCPSocket*)p->sock);
	              p->used = true;	              
	        }
	        
	        
	        SInt64 timeoutInMs = 5*3600*1000;  
	        RTPSessionInterface * theRTPsession = (RTPSessionInterface *)p->rtpSessObj;  
            theRTPsession->SetTimeOuts(timeoutInMs);
	       
        }  
        
    }    
    delete sessIDCStr;
    
    
    if (inSession == NULL) // it is a broadcast session so store the broadcast session.
    {   if (!sDefaultBroadcastPushEnabled)
            return QTSS_RequestFailed;

        theLen = sizeof(inSession);
        theErr = QTSS_GetValue(inParams->inClientSession, sClientBroadcastSessionAttr, 0, &inSession, &theLen);
        if (theErr != QTSS_NoErr)
            return QTSS_RequestFailed;
            
        theErr = QTSS_SetValue(inParams->inClientSession, sKillClientsEnabledAttr, 0, &sTearDownClientsOnDisconnect, sizeof(sTearDownClientsOnDisconnect));
        if (theErr != QTSS_NoErr)
            return QTSS_RequestFailed;

        Assert(inSession != NULL);
        
        theErr = QTSS_SetValue(inParams->inRTSPSession, sRTSPBroadcastSessionAttr, 0, &inSession, sizeof(inSession));
        if (theErr != QTSS_NoErr)
            return QTSS_RequestFailed;
            
            
    
        //qtss_printf("QTSSReflectorModule:SET for att err=%"_S32BITARG_" id=%"_S32BITARG_"\n",theErr,inParams->inRTSPSession);
            
        // this code needs to be cleaned up
        // Check and see if the full path to this file matches an existing ReflectorSession
        StrPtrLen thePathPtr;
        OSCharArrayDeleter sdpPath(QTSSModuleUtils::GetFullPath(    inParams->inRTSPRequest,
                                                                    qtssRTSPReqFilePath,
                                                                    &thePathPtr.Len, &sSDPSuffix));
        
        thePathPtr.Ptr = sdpPath.GetObject();
        

        // remove trackID designation from the path if it is there
        char *trackStr = thePathPtr.FindString("/trackID=");
        if (trackStr != NULL && *trackStr != 0)
        {
            *trackStr = 0; // terminate the string.
            thePathPtr.Len = ::strlen(thePathPtr.Ptr);
        }
        
        // If the actual file path has a .sdp in it, first look for the URL without the extra .sdp
        if (thePathPtr.Len > (sSDPSuffix.Len * 2))
        {
            // Check and see if there is a .sdp in the file path.
            // If there is, truncate off our extra ".sdp", cuz it isn't needed
            StrPtrLen endOfPath(&sdpPath.GetObject()[thePathPtr.Len - (sSDPSuffix.Len * 2)], sSDPSuffix.Len);
            if (endOfPath.Equal(sSDPSuffix))
            {
                sdpPath.GetObject()[thePathPtr.Len - sSDPSuffix.Len] = '\0';
                thePathPtr.Len -= sSDPSuffix.Len;
            }
        }

         // do all above so we can add the session to the map with Resolve here.
        // we must only do this once.
        OSRef* debug = sSessionMap->Resolve(&thePathPtr);
        if (debug != inSession->GetRef())
        {   
             return QTSSModuleUtils::SendErrorResponse(inParams->inRTSPRequest, qtssClientBadRequest, 0);
        }
        
    
        KeepSession(inParams->inRTSPRequest,true);
        //qtss_printf("QTSSReflectorModule.cpp:DoPlay (PUSH) inRTSPSession=%"_U32BITARG_" inClientSession=%"_U32BITARG_"\n",(UInt32)inParams->inRTSPSession,(UInt32)inParams->inClientSession);
    }
    else// it is NOT a broadcaster session
    {  
    
        RTPSessionOutput**  theOutput = NULL;
        theLen = 0;
        theErr = QTSS_GetValuePtr(inParams->inClientSession, sOutputAttr, 0, (void**)&theOutput, &theLen);
        if ((theErr != QTSS_NoErr) || (theLen != sizeof(RTPSessionOutput*)) || (theOutput == NULL))
            return QTSS_RequestFailed;
        (*theOutput)->InitializeStreams();
   
    
    
        // Tell the session what the bitrate of this reflection is. This is nice for logging,
        // it also allows the server to scale the TCP buffer size appropriately if we are
        // interleaving the data over TCP. This must be set before calling QTSS_Play so the
        // server can use it from within QTSS_Play
        UInt32 bitsPerSecond =  inSession->GetBitRate();
        (void)QTSS_SetValue(inParams->inClientSession, qtssCliSesMovieAverageBitRate, 0, &bitsPerSecond, sizeof(bitsPerSecond));
   
        if (sPlayResponseRangeHeader)
        {
            StrPtrLen temp;
            theErr = QTSS_GetValuePtr(inParams->inClientSession, sRTPInfoWaitTimeAttr, 0, (void**) &temp.Ptr, &temp.Len);
            if (theErr != QTSS_NoErr)
            {
		StrPtrLen range("npt=0-");
                QTSS_AppendRTSPHeader(inParams->inRTSPRequest, qtssRangeHeader,range.Ptr, range.Len);
                StrPtrLen scale("1.0");
		QTSS_AppendRTSPHeader(inParams->inRTSPRequest,qtssScaleHeader,scale.Ptr,scale.Len);
             }
        }
        
     
        if (sPlayerCompatibility )
            rtpInfoEnabled = QTSSModuleUtils::HavePlayerProfile(sServerPrefs,inParams, QTSSModuleUtils::kRequiresRTPInfoSeqAndTime);

        if (sForceRTPInfoSeqAndTime)
            rtpInfoEnabled = true;

        if (sRTPInfoDisabled )
            rtpInfoEnabled = false; 

        if (rtpInfoEnabled)
        {
            flags = qtssPlayRespWriteTrackInfo; //write first timestampe and seq num to rtpinfo
        
            Bool16 haveBufferedStreams = HaveStreamBuffers(inParams,inSession);
            if (haveBufferedStreams) // send the cached rtp time and seq number in the response.
            {    
 
                QTSS_Error theErr = QTSS_Play(inParams->inClientSession, inParams->inRTSPRequest, qtssPlayRespWriteTrackInfo);
                if (theErr != QTSS_NoErr)
                    return theErr;
            
            }
            else
            {   
                SInt32 waitTimeLoopCount = 0;
                theLen = sizeof(waitTimeLoopCount);
                theErr = QTSS_GetValue(inParams->inClientSession, sRTPInfoWaitTimeAttr, 0, &waitTimeLoopCount, &theLen);
                if (theErr != QTSS_NoErr)
                    (void)QTSS_SetValue(inParams->inClientSession, sRTPInfoWaitTimeAttr, 0, &sWaitTimeLoopCount, sizeof(sWaitTimeLoopCount));
                else
                {
                    if (waitTimeLoopCount < 1)
                        return QTSSModuleUtils::SendErrorResponseWithMessage(inParams->inRTSPRequest, qtssClientNotFound, &sBroadcastNotActive); 
                    
                    waitTimeLoopCount --; 
                    (void)QTSS_SetValue(inParams->inClientSession, sRTPInfoWaitTimeAttr, 0, &waitTimeLoopCount, sizeof(waitTimeLoopCount));
                
                }
                
                //qtss_printf("QTSSReflectorModule:DoPlay  wait 100ms waitTimeLoopCount=%ld\n", waitTimeLoopCount);

                SInt64 interval = 1 * 100; // 100 millisecond
                QTSS_SetIdleTimer( interval );
                return QTSS_NoErr;
            }
            
 
        }
        else
        {    
            if(  inSession != NULL && inSession->SessionTransportType == qtssRTPTransportTypeMPEG2){
            
                ((RTSPRequestInterface*)(inParams->inRTSPRequest))->SetTransportType(qtssRTPTransportTypeMPEG2);
                theErr = QTSS_Play(inParams->inClientSession, inParams->inRTSPRequest, qtssWriteFlagsNoFlags);
           
            }else
            {
                 theErr = QTSS_Play(inParams->inClientSession, inParams->inRTSPRequest, qtssPlayFlagsAppendServerInfo);
            }
           
            if (theErr != QTSS_NoErr)
                return theErr;
                
        }  
    }   
    
    (void)QTSS_SendStandardRTSPResponse(inParams->inRTSPRequest, inParams->inClientSession, flags);
    RLogRequest(INFO_ARTS_MODULE, l_callid,"Exiting");
    return QTSS_NoErr;
}


Bool16 KillSession(StrPtrLen *sdpPathStr, Bool16 killClients)
{
    OSRef* theSessionRef = sSessionMap->Resolve(sdpPathStr);
    if (theSessionRef != NULL)
    {
        ReflectorSession*   theSession = (ReflectorSession*)theSessionRef->GetObject();
        RemoveOutput(NULL, theSession, killClients);
        (void)QTSS_Teardown(theSession->GetBroadcasterSession());
        return true;
    }
    return false;
}
 
   
void KillCommandPathInList()
{
    char filePath[128] = "";
    ResizeableStringFormatter commandPath( (char*) filePath, sizeof(filePath)); // ResizeableStringFormatter is safer and more efficient than StringFormatter for most paths.
    OSMutexLocker locker (sSessionMap->GetMutex());

    for (OSRefHashTableIter theIter(sSessionMap->GetHashTable()); !theIter.IsDone(); theIter.Next())
    {
        OSRef* theRef = theIter.GetCurrent();
        if (theRef == NULL)
            continue;
        
        commandPath.Reset();
        commandPath.Put(*(theRef->GetString()));
        commandPath.Put(sSDPKillSuffix);
        commandPath.PutTerminator();
        
        char *theCommandPath =  commandPath.GetBufPtr();
        QTSS_Object outFileObject;
        QTSS_Error  err = QTSS_OpenFileObject(theCommandPath, qtssOpenFileNoFlags, &outFileObject);
        if (err == QTSS_NoErr)
        {   
           (void)  QTSS_CloseFileObject(outFileObject);
           ::unlink(theCommandPath);
           KillSession(theRef->GetString(), true);
        }        
    }
    
}

int  CheckProfile(QTSS_StandardRTSP_Params* inParams)
{
    char       *theRequestAttributes = NULL;
    UInt32     attributeLen          = 0;
    QTSS_Error err                   = QTSS_NoErr;
     
    QTSS_RTSPRequestObject  theRequest  = inParams->inRTSPRequest;
     
    err = QTSS_GetValueAsString(theRequest, qtssRTSPReqFilePathTrunc, 0, &theRequestAttributes);
    if (err != QTSS_NoErr)
    {        
       return -1;
    }

    //LogRequest(INFO_ARTS_MODULE, 0, "DoDescribe: Role = QTSS_RTSPPreProcessor_Role: Path = %s", theRequestAttributes);

    if(sARTSNumHandleDir == 0 )
    {
		RLogRequest(INFO_ARTS_MODULE, 0, "DoDescribe: Role = No Handle Dir In Pref");
		err =-1;
		goto finish;
    }
	
    for (UInt32 theIndex = 0; theIndex < sARTSNumHandleDir; theIndex++)
    {
		if(sARTSHandleDir[theIndex]!= NULL)
		{
			int len = strlen( sARTSHandleDir[theIndex] );// + 1;
		
			if(! strncmp(theRequestAttributes, sARTSHandleDir[theIndex], len))
				break;
		}
		if(theIndex == (sARTSNumHandleDir - 1))
		{
		    err =-1;
			goto finish;
         }	
    }

    RLogRequest(INFO_ARTS_MODULE, 0, "Role = OpenFilePreprocess: Ourfile");
finish:
    QTSS_Delete(theRequestAttributes);
    
    return err;
}

 
QTSS_Error DestroySession(QTSS_ClientSessionClosing_Params* inParams)
{
    RTPSessionOutput**  theOutput = NULL;
    ReflectorOutput*    outputPtr = NULL;
    ReflectorSession*   theSession = NULL;
    
  
    OSMutexLocker locker (sSessionMap->GetMutex());
    UInt32 l_callid = 0, l_len = sizeof(UInt32);   
    QTSS_ClientSessionObject     theClientSession   = inParams->inClientSession; 
    QTSS_Error err = QTSS_GetValue(theClientSession, sARTSClientSessionAttr, 0, (void *)&l_callid, &l_len);
    if(l_callid ==0 || err != QTSS_NoErr)
    {
        return QTSS_NoErr;
    }
    UInt32 theLen = sizeof(theSession);
    QTSS_Error theErr = QTSS_GetValue(inParams->inClientSession, sClientBroadcastSessionAttr, 0, &theSession, &theLen);
    
    
     RTPSessionInterface * theRTPSession = (RTPSessionInterface *)inParams->inClientSession;	   
    StrPtrLen* sessionIDStr= theRTPSession->GetValue(qtssCliSesRTSPSessionID);	
    char *sessIDCStr = sessionIDStr->GetAsCString(); 
    
    RLogRequest(INFO_ARTS_MODULE, l_callid,"Entry, err:%d,l_len:%d,sessinID:%s",err,l_len,sessIDCStr);
    
   
   
    
    if(sessIDCStr && sSupportOtherChannel)
    {
        co_socket_t * cur_p = get_co_socket_node(sessIDCStr);
        if(cur_p) 
        {
            RTSPSession * theRTSPSess = (RTSPSession *) (cur_p->rtspSessObj);
            Assert(theRTSPSess!= NULL); 
                     
            /* 
            SInt64 timeoutInMs = 50;
	        RTPSessionInterface * theRTPsession = (RTPSessionInterface *)cur_p->rtpSessObj;  
            theRTPsession->SetTimeOuts(timeoutInMs);          
             */
             
             
            del_co_socket_node(cur_p);            
            RLogRequest(INFO_ARTS_MODULE, 0,"ARTSMOdule cleanup,obj:%x\n",theRTSPSess); 
           
            theRTSPSess->Signal(Task::kKillEvent); 
            theRTSPSess->cleanObjectHolderCount(true);
        }      
    } 
    
    delete sessIDCStr;
    
    
    RLogRequest(INFO_ARTS_MODULE, l_callid,"sClientBroadcastSessionAttr=%x theSession=%x err=%d ",sClientBroadcastSessionAttr, theSession,theErr);
    if (theSession != NULL) // it is a broadcaster session
    {   
        
        ReflectorSession*   deletedSession = NULL;
        theErr = QTSS_SetValue(inParams->inClientSession, sClientBroadcastSessionAttr, 0, &deletedSession, sizeof(deletedSession));

        SourceInfo* theSoureInfo = theSession->GetSourceInfo(); 
        if (theSoureInfo == NULL)
            return QTSS_NoErr;
            
        UInt32  numStreams = theSession->GetNumStreams();
        SourceInfo::StreamInfo* theStreamInfo = NULL;
            
        for (UInt32 index = 0; index < numStreams; index++)
        {   theStreamInfo = theSoureInfo->GetStreamInfo(index);
            if (theStreamInfo != NULL)
                theStreamInfo->fSetupToReceive = false;
        }

        Bool16 killClients = false; // the pref as the default
        UInt32 theLen = sizeof(killClients);
        (void) QTSS_GetValue(inParams->inClientSession, sKillClientsEnabledAttr, 0, &killClients, &theLen);


        //qtss_printf("QTSSReflectorModule.cpp:DestroySession broadcaster theSession=%"_U32BITARG_"\n", (UInt32) theSession);
        theSession->RemoveSessionFromOutput(inParams->inClientSession);
        RemoveOutput(NULL, theSession, killClients);
    }
    else
    {
        
        theLen = 0;
        theErr = QTSS_GetValuePtr(inParams->inClientSession, sOutputAttr, 0, (void**)&theOutput, &theLen);
        if ((theErr != QTSS_NoErr) || (theLen != sizeof(RTPSessionOutput*)) || (theOutput == NULL) || (*theOutput == NULL))
            return QTSS_RequestFailed;
           
        theSession = (*theOutput)->GetReflectorSession();
    
        if (theOutput != NULL)
            outputPtr = (ReflectorOutput*) *theOutput;
            
        if (outputPtr != NULL)
        {    
            RemoveOutput(outputPtr, theSession, false);
            RTPSessionOutput* theOutput = NULL;
            (void)QTSS_SetValue(inParams->inClientSession, sOutputAttr, 0, &theOutput, sizeof(theOutput));
            
        }
                
    }
    RLogRequest(INFO_ARTS_MODULE, l_callid,"Exiting");

    return QTSS_NoErr;
}

void RemoveOutput(ReflectorOutput* inOutput, ReflectorSession* inSession, Bool16 killClients)
{
    
    //This function removes the output from the ReflectorSession, then
    Assert(inSession);
    if (inSession != NULL)
    {
        if (inOutput != NULL)
        {   
            inSession->RemoveOutput(inOutput,true);
        }
        else
        {   // it is a Broadcaster session
            qtss_printf("QTSSReflectorModule.cpp:RemoveOutput it is a broadcaster session\n");
            SourceInfo* theInfo = inSession->GetSourceInfo();         
            Assert(theInfo);
            
            if (theInfo->IsRTSPControlled())
            {   
                FileDeleter(inSession->GetSourcePath());
            }
                
        
            if (killClients || sTearDownClientsOnDisconnect)
            {    
                inSession->TearDownAllOutputs();
            }
        }
        
        qtss_printf("QTSSReflectorModule.cpp:RemoveOutput refcount =%"_U32BITARG_",session:%x\n", inSession->GetRef()->GetRefCount() ,inSession->GetRef());
        
        //check if the ReflectorSession should be deleted
        //it should if its ref count has dropped to 0
        OSRef* theSessionRef = inSession->GetRef();
        if (theSessionRef != NULL) 
        {               
            //qtss_printf("QTSSReflectorModule.cpp:RemoveOutput UnRegister session =%p refcount=%"_U32BITARG_"\n", theSessionRef, theSessionRef->GetRefCount() ) ;       
            
            for (UInt32 x = 0; x < inSession->GetNumStreams(); x++)
            {
                if (inSession->GetStreamByIndex(x) == NULL)
                    continue; 
                    
                Assert(theSessionRef->GetRefCount() > 0) //this shouldn't happen.
                if (theSessionRef->GetRefCount() > 0)
                    sSessionMap->Release(theSessionRef); //  one of the sessions on the ref is ending so decrement the count for each valid stream
            }
            //qtss_printf("theSessionRef:%x,theSessionRef:%d\n",theSessionRef,theSessionRef->GetRefCount());
            if (theSessionRef->GetRefCount() == 0)
            {   
                qtss_printf("QTSSReflectorModule.cpp:RemoveOutput UnRegister and delete session =%x refcount=%"_U32BITARG_"\n", theSessionRef, theSessionRef->GetRefCount() ) ;       
                sSessionMap->UnRegister(theSessionRef);
                delete inSession;
            }

        }
    }

    delete inOutput;
}


Bool16 AcceptSession(QTSS_StandardRTSP_Params* inParams)
{   
	QTSS_RTSPSessionObject inRTSPSession = inParams->inRTSPSession;
    QTSS_RTSPRequestObject theRTSPRequest = inParams->inRTSPRequest;

    QTSS_ActionFlags action = QTSSModuleUtils::GetRequestActions(theRTSPRequest);
    if(action != qtssActionFlagsWrite)
        return false;

 	if (QTSSModuleUtils::UserInGroup(QTSSModuleUtils::GetUserProfileObject(theRTSPRequest), sBroadcasterGroup.Ptr, sBroadcasterGroup.Len))
   		return true; // ok we are allowing this broadcaster user
 
    char remoteAddress[20] = {0};
    StrPtrLen theClientIPAddressStr(remoteAddress,sizeof(remoteAddress));
    QTSS_Error err = QTSS_GetValue(inRTSPSession, qtssRTSPSesRemoteAddrStr, 0, (void*)theClientIPAddressStr.Ptr, &theClientIPAddressStr.Len);
    if (err != QTSS_NoErr) 
        return false;
    
    if (IPComponentStr(&theClientIPAddressStr).IsLocal())
    {   
        if (sAuthenticateLocalBroadcast)
           return false;
        else
           return true;
    }

    if  (QTSSModuleUtils::AddressInList(sPrefs, sIPAllowListID, &theClientIPAddressStr))
        return true;

    return false;
}

QTSS_Error ReflectorAuthorizeRTSPRequest(QTSS_StandardRTSP_Params* inParams)
{   
    if ( AcceptSession(inParams) )
    {    
        Bool16 allowed = true;
        QTSS_RTSPRequestObject request = inParams->inRTSPRequest;
        (void) QTSSModuleUtils::AuthorizeRequest(request,  &allowed,  &allowed,  &allowed);
        return QTSS_NoErr;
    }

    Bool16 allowNoAccessFiles = false;
    QTSS_ActionFlags noAction = ~qtssActionFlagsWrite; //no action anything but a write
    QTSS_ActionFlags authorizeAction = QTSSModuleUtils::GetRequestActions(inParams->inRTSPRequest);
    //printf("ReflectorAuthorizeRTSPRequest authorizeAction=%d qtssActionFlagsWrite=%d\n", authorizeAction, qtssActionFlagsWrite);
    Bool16 outAllowAnyUser = false;
    Bool16 outAuthorized = false;
    QTAccessFile accessFile;
    accessFile.AuthorizeRequest(inParams,allowNoAccessFiles, noAction, authorizeAction, &outAuthorized, &outAllowAnyUser);
    
    if( (outAuthorized == false) && (authorizeAction & qtssActionFlagsWrite) ) //handle it
    {     
        //printf("ReflectorAuthorizeRTSPRequest SET not allowed\n");
        Bool16 allowed = false;
        (void) QTSSModuleUtils::AuthorizeRequest(inParams->inRTSPRequest, &allowed, &allowed, &allowed);
    }
    return QTSS_NoErr;
}

QTSS_Error RedirectBroadcast(QTSS_StandardRTSP_Params* inParams)
{
	QTSS_RTSPRequestObject theRequest = inParams->inRTSPRequest;
	
	char* requestPathStr;
	(void)QTSS_GetValueAsString(theRequest, qtssRTSPReqFilePath, 0, &requestPathStr);
    QTSSCharArrayDeleter requestPathStrDeleter(requestPathStr);
	StrPtrLen theRequestPath(requestPathStr);
	StringParser theRequestPathParser(&theRequestPath);
	
	// request path begins with a '/' for ex. /mysample.mov or /redirect_broadcast_keyword/mysample.mov
	theRequestPathParser.Expect(kPathDelimiterChar);
		
	StrPtrLen theFirstPath;
	theRequestPathParser.ConsumeUntil(&theFirstPath, kPathDelimiterChar);
	Assert (theFirstPath.Len != 0);

	// If the redirect_broadcast_keyword and redirect_broadcast_dir prefs are set & the first part of the path matches the keyword
	if ( (sRedirectBroadcastsKeyword && sRedirectBroadcastsKeyword[0] != 0)
		&& (sBroadcastsRedirectDir && sBroadcastsRedirectDir[0] != 0)
		&& theFirstPath.EqualIgnoreCase(sRedirectBroadcastsKeyword, ::strlen(sRedirectBroadcastsKeyword)) )
	{
		// set qtssRTSPReqRootDir
		(void)QTSS_SetValue(theRequest, qtssRTSPReqRootDir, 0, sBroadcastsRedirectDir, ::strlen(sBroadcastsRedirectDir));
	
		// set the request file path to the new path with the keyword stripped
		StrPtrLen theStrippedRequestPath;
		theRequestPathParser.ConsumeLength(&theStrippedRequestPath, theRequestPathParser.GetDataRemaining());
		(void) QTSS_SetValue(theRequest, qtssRTSPReqFilePath, 0, theStrippedRequestPath.Ptr, theStrippedRequestPath.Len);	
	}

	return QTSS_NoErr;
}

Bool16 AllowBroadcast(QTSS_RTSPRequestObject inRTSPRequest)
{	
	// If reflection of broadcasts is disabled, return false
	if (!sReflectBroadcasts)
		return false;

	// If request path is not in any of the broadcast_dir paths, return false
	if (!InBroadcastDirList(inRTSPRequest))
		return false;
		
	return true;
}

Bool16 InBroadcastDirList(QTSS_RTSPRequestObject inRTSPRequest)
{
	Bool16 allowed = false;
	
	char* theURIPathStr;
	(void)QTSS_GetValueAsString(inRTSPRequest, qtssRTSPReqFilePath, 0, &theURIPathStr);
	QTSSCharArrayDeleter requestPathStrDeleter(theURIPathStr);
	
	char* theLocalPathStr;
	(void)QTSS_GetValueAsString(inRTSPRequest, qtssRTSPReqLocalPath, 0, &theLocalPathStr);
	StrPtrLenDel requestPath(theLocalPathStr);
	
	char* theRequestPathStr = NULL;
    char* theBroadcastDirStr = NULL;
	Bool16 isURI = true;
	
	UInt32 index = 0;
	UInt32 numValues = 0;


    (void) QTSS_GetNumValues(sPrefs, sBroadcastDirListID, &numValues);
    
	if (numValues == 0)
		return true;
		
    while (!allowed && (index < numValues))
    {
        (void) QTSS_GetValueAsString(sPrefs, sBroadcastDirListID, index, &theBroadcastDirStr);
		StrPtrLen theBroadcastDir(theBroadcastDirStr);
		
        if (theBroadcastDir.Len == 0) // an empty dir matches all
            return true;

		if (IsAbsolutePath(&theBroadcastDir))
		{
			theRequestPathStr = theLocalPathStr;
			isURI = false;
		}
		else
			theRequestPathStr = theURIPathStr;

		StrPtrLen requestPath(theRequestPathStr);
		StringParser requestPathParser(&requestPath);
		StrPtrLen pathPrefix;
		if (isURI)
			requestPathParser.Expect(kPathDelimiterChar);
		requestPathParser.ConsumeLength(&pathPrefix, theBroadcastDir.Len);
		
		// if the first part of the request path matches the broadcast_dir path, return true
		if (pathPrefix.Equal(theBroadcastDir))
			allowed = true;
		
        (void) QTSS_Delete(theBroadcastDirStr);		
        
        index ++;
	}

    return allowed;
}

Bool16 IsAbsolutePath(StrPtrLen *inPathPtr)
{
	StringParser thePathParser(inPathPtr);
	
#ifdef __Win32__
	if ((thePathParser[1] == ':') && (thePathParser[2] == kPathDelimiterChar))
#else
	if (thePathParser.PeekFast() == kPathDelimiterChar)
#endif
		return true;
		
	return false;
}
