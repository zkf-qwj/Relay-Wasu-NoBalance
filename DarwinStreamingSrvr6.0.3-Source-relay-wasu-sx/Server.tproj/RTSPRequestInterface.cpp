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
    File:       RTSPRequestInterface.cp

    Contains:   Implementation of class defined in RTSPRequestInterface.h
    

    

*/


//INCLUDES:
#ifndef __Win32__
#include <sys/types.h>
#include <sys/uio.h>
#endif

#include "RTSPRequestInterface.h"
#include "RTSPSessionInterface.h"
#include "RTSPRequestStream.h"

#include "StringParser.h"
#include "OSMemory.h"
#include "OSThread.h"
#include "DateTranslator.h"
#include "QTSSDataConverter.h"
#include "OSArrayObjectDeleter.h"
#include "QTSSPrefs.h"
#include "QTSServerInterface.h"

char        RTSPRequestInterface::sPremadeHeader[kStaticHeaderSizeInBytes];
StrPtrLen   RTSPRequestInterface::sPremadeHeaderPtr(sPremadeHeader, kStaticHeaderSizeInBytes);

char        RTSPRequestInterface::sPremadeNoHeader[kStaticHeaderSizeInBytes];
StrPtrLen   RTSPRequestInterface::sPremadeNoHeaderPtr(sPremadeNoHeader, kStaticHeaderSizeInBytes);


StrPtrLen   RTSPRequestInterface::sColonSpace(": ", 2);

QTSSAttrInfoDict::AttrInfo  RTSPRequestInterface::sAttributes[] =
{   /*fields:   fAttrName, fFuncPtr, fAttrDataType, fAttrPermission */
    /* 0 */ { "qtssRTSPReqFullRequest",         NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 1 */ { "qtssRTSPReqMethodStr",           NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 2 */ { "qtssRTSPReqFilePath",            NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModeWrite },
    /* 3 */ { "qtssRTSPReqURI",                 NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 4 */ { "qtssRTSPReqFilePathTrunc",       GetTruncatedPath,       qtssAttrDataTypeCharArray,  qtssAttrModeRead },
    /* 5 */ { "qtssRTSPReqFileName",            GetFileName,            qtssAttrDataTypeCharArray,  qtssAttrModeRead },
    /* 6 */ { "qtssRTSPReqFileDigit",           GetFileDigit,           qtssAttrDataTypeCharArray,  qtssAttrModeRead },
    /* 7 */ { "qtssRTSPReqAbsoluteURL",         NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 8 */ { "qtssRTSPReqTruncAbsoluteURL",    GetAbsTruncatedPath,    qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeCacheable },
    /* 9 */ { "qtssRTSPReqMethod",              NULL,                   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 10 */ { "qtssRTSPReqStatusCode",         NULL,                   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 11 */ { "qtssRTSPReqStartTime",          NULL,                   qtssAttrDataTypeFloat64,    qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 12 */ { "qtssRTSPReqStopTime",           NULL,                   qtssAttrDataTypeFloat64,    qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 13 */ { "qtssRTSPReqRespKeepAlive",      NULL,                   qtssAttrDataTypeBool16,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 14 */ { "qtssRTSPReqRootDir",            NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModeWrite },
    /* 15 */ { "qtssRTSPReqRealStatusCode",     GetRealStatusCode,      qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 16 */ { "qtssRTSPReqStreamRef",          NULL,                   qtssAttrDataTypeQTSS_StreamRef, qtssAttrModeRead | qtssAttrModePreempSafe },
    
    /* 17 */ { "qtssRTSPReqUserName",           NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 18 */ { "qtssRTSPReqUserPassword",       NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 19 */ { "qtssRTSPReqUserAllowed",        NULL,                   qtssAttrDataTypeBool16,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 20 */ { "qtssRTSPReqURLRealm",           NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 21 */ { "qtssRTSPReqLocalPath",          GetLocalPath,			qtssAttrDataTypeCharArray,  qtssAttrModeRead },
    /* 22 */ { "qtssRTSPReqIfModSinceDate",     NULL,                   qtssAttrDataTypeTimeVal,    qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 23 */ { "qtssRTSPReqQueryString",        NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 24 */ { "qtssRTSPReqRespMsg",            NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 25 */ { "qtssRTSPReqContentLen",         NULL,                   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 26 */ { "qtssRTSPReqSpeed",              NULL,                   qtssAttrDataTypeFloat32,    qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 27 */ { "qtssRTSPReqLateTolerance",      NULL,                   qtssAttrDataTypeFloat32,    qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 28 */ { "qtssRTSPReqTransportType",      NULL,                   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 29 */ { "qtssRTSPReqTransportMode",      NULL,                   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 30 */ { "qtssRTSPReqSetUpServerPort",    NULL,                   qtssAttrDataTypeUInt16,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite},
    /* 31 */ { "qtssRTSPReqAction",             NULL,                   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 32 */ { "qtssRTSPReqUserProfile",        NULL,                   qtssAttrDataTypeQTSS_Object, qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 33 */ { "qtssRTSPReqPrebufferMaxTime",   NULL,                   qtssAttrDataTypeFloat32,    qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 34 */ { "qtssRTSPReqAuthScheme",         NULL,                   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 35 */ { "qtssRTSPReqSkipAuthorization",  NULL,                   qtssAttrDataTypeBool16,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 36 */ { "qtssRTSPReqNetworkMode",		NULL,					qtssAttrDataTypeUInt32,		qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 37 */ { "qtssRTSPReqDynamicRateValue",	NULL,					qtssAttrDataTypeSInt32,		qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 38 */ { "qtssRTSPReq3GPPRequestObject",	NULL,					qtssAttrDataTypeQTSS_Object,qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 39 */ { "qtssRTSPReqBandwidthBits",	    NULL,					qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 40 */ { "qtssRTSPReqUserFound",          NULL,                   qtssAttrDataTypeBool16,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 41 */ { "qtssRTSPReqAuthHandled",        NULL,                   qtssAttrDataTypeBool16,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 42 */ { "qtssRTSPReqDigestChallenge",    NULL,                   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 43 */ { "qtssRTSPReqDigestResponse",     GetAuthDigestResponse,  qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe }
    
    
    
 
 };


void  RTSPRequestInterface::Initialize(void)
{
    //make a partially complete header
    StringFormatter headerFormatter(sPremadeHeaderPtr.Ptr, kStaticHeaderSizeInBytes);
    PutStatusLine(&headerFormatter, qtssSuccessOK, RTSPProtocol::k10Version);
    
    headerFormatter.Put(QTSServerInterface::GetServerHeader());
    headerFormatter.PutEOL();
    headerFormatter.Put(RTSPProtocol::GetHeaderString(qtssCSeqHeader));
    headerFormatter.Put(sColonSpace);
    sPremadeHeaderPtr.Len = headerFormatter.GetCurrentOffset();
    Assert(sPremadeHeaderPtr.Len < kStaticHeaderSizeInBytes);
    
        
    StringFormatter noServerInfoHeaderFormatter(sPremadeNoHeaderPtr.Ptr, kStaticHeaderSizeInBytes);
    PutStatusLine(&noServerInfoHeaderFormatter, qtssSuccessOK, RTSPProtocol::k10Version);
    noServerInfoHeaderFormatter.Put(RTSPProtocol::GetHeaderString(qtssCSeqHeader));
    noServerInfoHeaderFormatter.Put(sColonSpace);
    sPremadeNoHeaderPtr.Len = noServerInfoHeaderFormatter.GetCurrentOffset();
    Assert(sPremadeNoHeaderPtr.Len < kStaticHeaderSizeInBytes);
    
    //Setup all the dictionary stuff
    for (UInt32 x = 0; x < qtssRTSPReqNumParams; x++)
        QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kRTSPRequestDictIndex)->
            SetAttribute(x, sAttributes[x].fAttrName, sAttributes[x].fFuncPtr,
                            sAttributes[x].fAttrDataType, sAttributes[x].fAttrPermission);
    
    QTSSDictionaryMap* theHeaderMap = QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kRTSPHeaderDictIndex);
    for (UInt32 y = 0; y < qtssNumHeaders; y++)
        theHeaderMap->SetAttribute(y, RTSPProtocol::GetHeaderString(y).Ptr, NULL, qtssAttrDataTypeCharArray, qtssAttrModeRead | qtssAttrModePreempSafe);
}

//CONSTRUCTOR / DESTRUCTOR: very simple stuff
RTSPRequestInterface::RTSPRequestInterface(RTSPSessionInterface *session)
:   QTSSDictionary(QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kRTSPRequestDictIndex)),
	fMethod(qtssIllegalMethod),
	fStatus(qtssSuccessOK),
    fRealStatusCode(0),
    fRequestKeepAlive(true),
    //fResponseKeepAlive(true), //parameter need not be set
    fVersion(RTSPProtocol::k10Version),
    fStartTime(-1),
    fStopTime(-1),
    fClientPortA(0),
    fClientPortB(0),
    fTtl(0),
    fDestinationAddr(0),
    fSourceAddr(0),
    fTransportType(qtssRTPTransportTypeUDP),
    isMPEG2(false),
    isIpqam(false),
    noDescribe(false),
    fNetworkMode(qtssRTPNetworkModeDefault),    
    fContentLength(0),
    fIfModSinceDate(0),
    fSpeed(0),
    fLateTolerance(-1),
    fPrebufferAmt(-1),
    fWindowSize(0),
    fMovieFolderPtr(&fMovieFolderPath[0]),
    fHeaderDictionary(QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kRTSPHeaderDictIndex)),
    fAllowed(true),
    fHasUser(false),
    fAuthHandled(false),
    fTransportMode(qtssRTPTransportModePlay),
    fSetUpServerPort(0),
    fAction(qtssActionFlagsNoFlags),
    fAuthScheme(qtssAuthNone),
    fAuthQop(RTSPSessionInterface::kNoQop),
    fUserProfile(),
    fUserProfilePtr(&fUserProfile),
    fStale(false),
    fSkipAuthorization(false),
    fEnableDynamicRateState(-1),// -1 undefined, 0 disabled, 1 enabled
	// DJM PROTOTYPE
	fRandomDataSize(0),
    fRequest3GPP( QTSServerInterface::GetServer()->GetPrefs()->Get3GPPEnabled() ),
    fRequest3GPPPtr(&fRequest3GPP),
    fBandwidthBits(0),
    	
	// private storage initializes after protected and public storage above
    fSession(session),
    fOutputStream(session->GetOutputStream()),
    fStandardHeadersWritten(false) // private initializes after protected and public storage above
       
{
    //Setup QTSS parameters that can be setup now. These are typically the parameters that are actually
    //pointers to binary variable values. Because these variables are just member variables of this object,
    //we can properly initialize their pointers right off the bat.

    fStreamRef = this;
    RTSPRequestStream* input = session->GetInputStream();
    this->SetVal(qtssRTSPReqFullRequest, input->GetRequestBuffer()->Ptr, input->GetRequestBuffer()->Len);
    this->SetVal(qtssRTSPReqMethod, &fMethod, sizeof(fMethod));
    this->SetVal(qtssRTSPReqStatusCode, &fStatus, sizeof(fStatus));
    this->SetVal(qtssRTSPReqRespKeepAlive, &fResponseKeepAlive, sizeof(fResponseKeepAlive));
    this->SetVal(qtssRTSPReqStreamRef, &fStreamRef, sizeof(fStreamRef));
    this->SetVal(qtssRTSPReqContentLen, &fContentLength, sizeof(fContentLength));
    this->SetVal(qtssRTSPReqSpeed, &fSpeed, sizeof(fSpeed));
    this->SetVal(qtssRTSPReqLateTolerance, &fLateTolerance, sizeof(fLateTolerance));
    this->SetVal(qtssRTSPReqPrebufferMaxTime, &fPrebufferAmt, sizeof(fPrebufferAmt));
 
    // Get the default root directory from QTSSPrefs, and store that in the proper parameter
    // Note that the GetMovieFolderPath function may allocate memory, so we check for that
    // in this object's destructor and free that memory if necessary.
    UInt32 pathLen = kMovieFolderBufSizeInBytes;
    fMovieFolderPtr = QTSServerInterface::GetServer()->GetPrefs()->GetMovieFolder(fMovieFolderPtr, &pathLen);
    //this->SetVal(qtssRTSPReqRootDir, fMovieFolderPtr, pathLen);
	this->SetValue(qtssRTSPReqRootDir, 0, fMovieFolderPtr, pathLen, QTSSDictionary::kDontObeyReadOnly);
	    
    //There are actually other attributes that point to member variables that we COULD setup now, but they are attributes that
    //typically aren't set for every request, so we lazy initialize those when we parse the request

    this->SetVal(qtssRTSPReqUserAllowed, &fAllowed, sizeof(fAllowed));
    this->SetVal(qtssRTSPReqUserFound, &fHasUser, sizeof(fHasUser));
    this->SetVal(qtssRTSPReqAuthHandled, &fAuthHandled, sizeof(fAuthHandled));
    
    this->SetVal(qtssRTSPReqTransportType, &fTransportType, sizeof(fTransportType));
    this->SetVal(qtssRTSPReqTransportMode, &fTransportMode, sizeof(fTransportMode));
    this->SetVal(qtssRTSPReqSetUpServerPort, &fSetUpServerPort, sizeof(fSetUpServerPort));
    this->SetVal(qtssRTSPReqAction, &fAction, sizeof(fAction));
    this->SetVal(qtssRTSPReqUserProfile, &fUserProfilePtr, sizeof(fUserProfilePtr));
    this->SetVal(qtssRTSPReqAuthScheme, &fAuthScheme, sizeof(fAuthScheme));
    this->SetVal(qtssRTSPReqSkipAuthorization, &fSkipAuthorization, sizeof(fSkipAuthorization));

    this->SetVal(qtssRTSPReqDynamicRateState, &fEnableDynamicRateState, sizeof(fEnableDynamicRateState));
    
    this->SetVal(qtssRTSPReq3GPPRequestObject, &fRequest3GPPPtr, sizeof(fRequest3GPPPtr));
    this->SetVal(qtssRTSPReqBandwidthBits, &fBandwidthBits, sizeof(fBandwidthBits));
    
    this->SetVal(qtssRTSPReqDigestResponse, &fAuthDigestResponse, sizeof(fAuthDigestResponse));
 
     fContentType =0;
       
 }

void RTSPRequestInterface::AppendHeader(QTSS_RTSPHeader inHeader, StrPtrLen* inValue)
{
    if (!fStandardHeadersWritten)
        this->WriteStandardHeaders();
        
    fOutputStream->Put(RTSPProtocol::GetHeaderString(inHeader));
    fOutputStream->Put(sColonSpace);
    fOutputStream->Put(*inValue);
    fOutputStream->PutEOL();
}

void RTSPRequestInterface::PutStatusLine(StringFormatter* putStream, QTSS_RTSPStatusCode status,
                                        RTSPProtocol::RTSPVersion version)
{
    putStream->Put(RTSPProtocol::GetVersionString(version));
    putStream->PutSpace();
    putStream->Put(RTSPProtocol::GetStatusCodeAsString(status));
    putStream->PutSpace();
    putStream->Put(RTSPProtocol::GetStatusCodeString(status));
    putStream->PutEOL();    
}


void RTSPRequestInterface::AppendContentLength(UInt32 contentLength)
{
    if (!fStandardHeadersWritten)
        this->WriteStandardHeaders();

    char dataSize[10];
    dataSize[sizeof(dataSize) -1] = 0;
    qtss_snprintf(dataSize, sizeof(dataSize) -1, "%"_U32BITARG_"", contentLength);
    StrPtrLen contentLengthStr(dataSize);
    this->AppendHeader(qtssContentLengthHeader, &contentLengthStr);
    //qtss_printf("AppendContentLength:%d\n",contentLength);
    
}

void RTSPRequestInterface::AppendDateAndExpires()
{
    if (!fStandardHeadersWritten)
        this->WriteStandardHeaders();

    Assert(OSThread::GetCurrent() != NULL);
    DateBuffer* theDateBuffer = OSThread::GetCurrent()->GetDateBuffer();
    theDateBuffer->InexactUpdate(); // Update the date buffer to the current date & time
    StrPtrLen theDate(theDateBuffer->GetDateBuffer(), DateBuffer::kDateBufferLen);
    
    // Append dates, and have this response expire immediately
     this->AppendHeader(qtssLastModifiedHeader, &theDate);
    this->AppendHeader(qtssDateHeader, &theDate);
    this->AppendHeader(qtssExpiresHeader, &theDate);
}


void RTSPRequestInterface::AppendSessionHeaderWithTimeout( StrPtrLen* inSessionID, StrPtrLen* inTimeout )
{

    // Append a session header if there wasn't one already
    if ( GetHeaderDictionary()->GetValue(qtssSessionHeader)->Len == 0)
    {   
        if (!fStandardHeadersWritten)
            this->WriteStandardHeaders();

        static StrPtrLen    sTimeoutString(";timeout=");

        // Just write out the session header and session ID
        if (inSessionID != NULL && inSessionID->Len > 0)
        {
            fOutputStream->Put( RTSPProtocol::GetHeaderString(qtssSessionHeader ) );
            fOutputStream->Put(sColonSpace);
            fOutputStream->Put( *inSessionID );
        
        
            if ( inTimeout != NULL && inTimeout->Len != 0)
            {
                fOutputStream->Put( sTimeoutString );
                fOutputStream->Put( *inTimeout );
            }
        
        
            fOutputStream->PutEOL();
            
            if(fSession->szModule == true)
            {
                fOutputStream->Put( RTSPProtocol::GetHeaderString(qtssOnDemandSessionId) );
                fOutputStream->Put(sColonSpace);
                fOutputStream->Put( *inSessionID );
           
                GetHeaderDictionary()->SetVal(qtssOnDemandSessionId, inSessionID);                  
        
                fOutputStream->PutEOL();
            }
        }
    }

}

void RTSPRequestInterface::PutTransportStripped(StrPtrLen &fullTransportHeader, StrPtrLen &fieldToStrip)
{
       
        // skip the fieldToStrip and echo the rest back
        UInt32 offset = (UInt32) (fieldToStrip.Ptr - fullTransportHeader.Ptr);        
        StrPtrLen transportStart(fullTransportHeader.Ptr,offset);
        /*
        while (transportStart.Len > 0) // back up removing chars up to and including ;
        {  
            transportStart.Len --;
            if (transportStart[transportStart.Len] == ';')
                break;
        }*/// forbidden in ningxia
    
        StrPtrLen transportRemainder(fieldToStrip.Ptr,fullTransportHeader.Len - offset);        
        StringParser transportParser(&transportRemainder);
        transportParser.ConsumeUntil(&fieldToStrip, ';'); //remainder starts with ;       
        transportRemainder.Set(transportParser.GetCurrentPosition(),transportParser.GetDataRemaining());
        
        fOutputStream->Put(transportStart);	
        fOutputStream->Put(transportRemainder);	

}

void RTSPRequestInterface::AppendTransportHeader(StrPtrLen* serverPortA,
                                                    StrPtrLen* serverPortB,
                                                    StrPtrLen* channelA,
                                                    StrPtrLen* channelB,
                                                    StrPtrLen* serverIPAddr,
                                                    StrPtrLen* ssrc,StrPtrLen* inSessionID)
{
    static StrPtrLen    sServerPortString(";server_port=");
    static StrPtrLen    sClientPortString(";client_port=");
    static StrPtrLen    sSourceString(";source=");
    static StrPtrLen    sDestinationString(";destination=");
    static StrPtrLen    sInterleavedString(";interleaved=");
    static StrPtrLen    sSSRC(";ssrc=");
    static StrPtrLen    sInterLeaved("interleaved");//match the interleaved tag
    static StrPtrLen    sClientPort(";client_port=");
    
	static StrPtrLen    mode(";mode=\"PLAY\"");
	static StrPtrLen    sKeyString(";keystring=");
	
	int  server_port = 0;
	static StrPtrLen sServerPort;
	
    
    if (!fStandardHeadersWritten)
        this->WriteStandardHeaders();

/*   used in ningxia-wasu*/
     StrPtrLen transport("Transport: MP2T/TCP;interleaved=0-1\r\n");
        fOutputStream->Put(transport);  
     return;

// used in jianshan
/*
    StrPtrLen transport("Transport: MP2T\r\n");
    fOutputStream->Put(transport);       
    return;
*/
//end used in jianshan
    if(noDescribe == true)
    {
        StrPtrLen method_str("Method-Code: SETUP\r\n");
        fOutputStream->Put(method_str);
        StrPtrLen tianshan("TianShan-Transport: MP2T/DVBC/QAM;program-number=1;frequency=307000000;qam-mode=64;symbol-rate=6875000\r\n");
  
        fOutputStream->Put(tianshan);    
   
        StrPtrLen transport("Transport: MP2T/DVBC/QAM;unicast\r\n");
        fOutputStream->Put(transport);   
    
        return;
    }

    // Just write out the same transport header the client sent to us.
    fOutputStream->Put(RTSPProtocol::GetHeaderString(qtssTransportHeader));
    fOutputStream->Put(sColonSpace);

    StrPtrLen outFirstTransport(fFirstTransport.GetAsCString());
    OSCharArrayDeleter outFirstTransportDeleter(outFirstTransport.Ptr);
    outFirstTransport.RemoveWhitespace();
    while (outFirstTransport[outFirstTransport.Len - 1] == ';')
        outFirstTransport.Len --;

    // see if it contains an interleaved field or client port field
    StrPtrLen stripClientPortStr;
    StrPtrLen stripInterleavedStr;
    (void) outFirstTransport.FindStringIgnoreCase(sClientPort, &stripClientPortStr);
    (void) outFirstTransport.FindStringIgnoreCase(sInterLeaved, &stripInterleavedStr);
    
    // echo back the transport without the interleaved or client ports fields we will add those in ourselves
    if (stripClientPortStr.Len != 0)
        PutTransportStripped(outFirstTransport, stripClientPortStr);
    else if (stripInterleavedStr.Len != 0) 
         PutTransportStripped(outFirstTransport, stripInterleavedStr);     
    else
        fOutputStream->Put(outFirstTransport);
         
     //qtss_printf("fSession->supportOtherChannel%d\n",fSession->supportOtherChannel);
    //The source IP addr is optional, only append it if it is provided
// Chris-Burns@yahoo.com (2009/03/19) [next 1 line/s]
    if ((serverIPAddr != NULL) && (serverIPAddr->Len > 0))
    {
         if(GetContentType() == qtssRTSPMHType)
        {
            fOutputStream->Put(sDestinationString);
        }else
        fOutputStream->Put(sSourceString);
        fOutputStream->Put(*serverIPAddr);
    }
    
    
    else if(isMPEG2 == true && fSession->supportOtherChannel == true ){   
  
        char srcIPAddrBuf[20];
        StrPtrLen theSrcIPAddress(srcIPAddrBuf, 20);
        QTSServerInterface::GetServer()->GetPrefs()->GetTransportSrcAddr(&theSrcIPAddress);
       
        if(theSrcIPAddress.Len >0)
        {
            if(GetContentType() == qtssRTSPMHType)
            {
                fOutputStream->Put(sDestinationString);
            }else
            fOutputStream->Put(sSourceString);
            fOutputStream->Put(theSrcIPAddress);
        }else
        {
            StrPtrLen* serverHost = fHeaderDictionary.GetValue(qtssHostHeader);
        
            StringParser hostParse(serverHost);
            hostParse.ConsumeWhitespace();
            StrPtrLen hostName;
            hostParse.ConsumeUntil(&hostName,':');
             
            hostParse.ConsumeLength(NULL,1);
            hostParse.ConsumeLength(&sServerPort,10);         
      
        
            if(GetContentType() == qtssRTSPMHType)
            {
                fOutputStream->Put(sDestinationString);
            }else
                fOutputStream->Put(sSourceString);
            fOutputStream->Put(hostName);            
        }     
    
    }
    
    
    // Append the client ports,
    if (stripClientPortStr.Len != 0)
    {
        fOutputStream->Put(sClientPortString);
        UInt16 portA = this->GetClientPortA();
        UInt16 portB = this->GetClientPortB();
        StrPtrLenDel clientPortA(QTSSDataConverter::ValueToString( &portA, sizeof(portA), qtssAttrDataTypeUInt16));
        StrPtrLenDel clientPortB(QTSSDataConverter::ValueToString( &portB, sizeof(portB), qtssAttrDataTypeUInt16));
        
        fOutputStream->Put(clientPortA);
        if(fTransportType != qtssRTPTransportTypeMPEG2 && isMPEG2 == false)
        {		
			fOutputStream->PutChar('-');
			fOutputStream->Put(clientPortB);        
		}
    }
    
    // Append the server ports, if provided.
    
    if (serverPortA != NULL && fSession->supportOtherChannel == false)
    {
        fOutputStream->Put(sServerPortString);
        fOutputStream->Put(*serverPortA);
		if(fTransportType != qtssRTPTransportTypeMPEG2 && isMPEG2 == false)
        {
			fOutputStream->PutChar('-');
			fOutputStream->Put(*serverPortB);
		}
    }else if (isMPEG2 == true  && fSession->supportOtherChannel == true )
    {
       UInt32 theNumPorts = 0;                
       QTSServerPrefs* inPrefs =  QTSServerInterface::GetServer()->GetPrefs();
       Assert(inPrefs != NULL);
       theNumPorts = inPrefs->GetNumValues(qtssPrefsRTSPPorts);
             
        if(theNumPorts >0)
        {
        
            UInt32 theLen = sizeof(UInt16),server_port =0;
            QTSS_Error theErr = QTSS_NoErr;
            theErr = inPrefs->GetValue(qtssPrefsRTSPPorts, 0, &server_port, &theLen);
            Assert(theErr == QTSS_NoErr); 
                          
            char server_port_cstr[64]={'\0'};
            sprintf(server_port_cstr,"%d",server_port);
           
            StrPtrLen port_str(server_port_cstr); 
             
            if(GetContentType() == qtssRTSPMHType)
            {            
                fOutputStream->Put(sClientPortString);
            }else
            fOutputStream->Put(sServerPortString);        
            fOutputStream->Put(port_str);          
        }else if(sServerPort.Len >0)
        {
            if(GetContentType() == qtssRTSPMHType)
            {            
                fOutputStream->Put(sClientPortString);
            }else
            fOutputStream->Put(sServerPortString);        
            fOutputStream->Put(sServerPort); 
        }
        
    }
    
    // Append channel #'s, if provided
    if (channelA != NULL && GetContentType() != qtssRTSPMHType)
    {
        fOutputStream->Put(sInterleavedString);
        
        //StrPtrLen channelAA("0");
       // StrPtrLen channelBB("1");
        fOutputStream->Put(*channelA);
        //fOutputStream->Put(channelAA);
        fOutputStream->PutChar('-');
        //fOutputStream->Put(channelBB);
        fOutputStream->Put(*channelB);
    }
    
    if (ssrc != NULL && ssrc->Ptr != NULL && ssrc->Len != 0 && fNetworkMode == qtssRTPNetworkModeUnicast && fTransportMode == qtssRTPTransportModePlay && fTransportType != qtssRTPTransportTypeMPEG2 && isMPEG2 == false)
    {
        char* theCString = ssrc->GetAsCString();
        OSCharArrayDeleter cStrDeleter(theCString);
        
        UInt32 ssrcVal = 0;
        ::sscanf(theCString, "%"_U32BITARG_"", &ssrcVal);
        ssrcVal = htonl(ssrcVal);
        
        StrPtrLen hexSSRC(QTSSDataConverter::ValueToString( &ssrcVal, sizeof(ssrcVal), qtssAttrDataTypeUnknown));
        OSCharArrayDeleter hexStrDeleter(hexSSRC.Ptr);

        fOutputStream->Put(sSSRC);
        fOutputStream->Put(hexSSRC);
    }
    //kk - mode="PLAY" required for amino stb
   // if (fTransportType == qtssRTPTransportTypeMPEG2)
   //     fOutputStream->Put(mode);
   
  
    if(inSessionID != NULL && inSessionID->Len >0  && fSession->supportOtherChannel == true)
    {
        //qtss_printf("trans session ID:%s\n",inSessionID->GetAsCString());
        if( (isMPEG2 == true || fTransportType == qtssRTPTransportTypeMPEG2) )
        {
            fOutputStream->Put(sKeyString);
            fOutputStream->Put(*inSessionID);
        }
    }
	
    fOutputStream->PutEOL();
}

void RTSPRequestInterface::AppendContentBaseHeader(StrPtrLen* theURL)
{
    if (!fStandardHeadersWritten)
        this->WriteStandardHeaders();

    fOutputStream->Put(RTSPProtocol::GetHeaderString(qtssContentBaseHeader));
    fOutputStream->Put(sColonSpace);
    fOutputStream->Put(*theURL);
    fOutputStream->PutChar('/');
    fOutputStream->PutEOL();
}

void RTSPRequestInterface::AppendRetransmitHeader(UInt32 inAckTimeout)
{
    static const StrPtrLen kAckTimeout("ack-timeout=");

    fOutputStream->Put(RTSPProtocol::GetHeaderString(qtssXRetransmitHeader));
    fOutputStream->Put(sColonSpace);
    fOutputStream->Put(RTSPProtocol::GetRetransmitProtocolName());
    fOutputStream->PutChar(';');
    fOutputStream->Put(kAckTimeout);
    fOutputStream->Put(inAckTimeout);
    
    if (fWindowSizeStr.Len > 0)
    {
        //
        // If the client provided a window size, append that as well.
        fOutputStream->PutChar(';');
        fOutputStream->Put(fWindowSizeStr);
    }
    
    fOutputStream->PutEOL();
    
}


void RTSPRequestInterface::AppendRTPInfoHeader(QTSS_RTSPHeader inHeader,
                                                StrPtrLen* url, StrPtrLen* seqNumber,
                                                StrPtrLen* ssrc, StrPtrLen* rtpTime, Bool16 lastRTPInfo)
{
    //kk-hack required for amino stb
    if(fTransportType == qtssRTPTransportTypeMPEG2)
    {
    
       // static StrPtrLen sURL1("x-Ts-info: 1000000",18);
        char * scale_s = "Scale: 1.0";
        static StrPtrLen sURL2(scale_s,strlen(scale_s));
     
        if (!fStandardHeadersWritten)
            this->WriteStandardHeaders();   
       
    }
    else
    {	
		static StrPtrLen sURL("url=", 4);
		static StrPtrLen sSeq(";seq=", 5);
		static StrPtrLen sSsrc(";ssrc=", 6);
		static StrPtrLen sRTPTime(";rtptime=", 9);

		if (!fStandardHeadersWritten)
			this->WriteStandardHeaders();

		fOutputStream->Put(RTSPProtocol::GetHeaderString(inHeader));
		if (inHeader != qtssSameAsLastHeader)
			fOutputStream->Put(sColonSpace);
			
		//Only append the various bits of RTP information if they actually have been
		//providied
		if ((url != NULL) && (url->Len > 0))
		{
			fOutputStream->Put(sURL);

			if (true) //3gpp requires this and it follows RTSP RFC.
			{
				RTSPRequestInterface* theRequest = (RTSPRequestInterface*)this;
				StrPtrLen *path = (StrPtrLen *) theRequest->GetValue(qtssRTSPReqAbsoluteURL);
				
				if (path != NULL && path->Len > 0)
				{   fOutputStream->Put(*path);
					if(path->Ptr[path->Len-1] != '/')
						fOutputStream->PutChar('/');
				}
			}

			fOutputStream->Put(*url);
		}
		if ((seqNumber != NULL) && (seqNumber->Len > 0))
		{
			fOutputStream->Put(sSeq);
			fOutputStream->Put(*seqNumber);
		}
		if ((ssrc != NULL) && (ssrc->Len > 0))
		{
			fOutputStream->Put(sSsrc);
			fOutputStream->Put(*ssrc);
		}
		if ((rtpTime != NULL) && (rtpTime->Len > 0))
		{
			fOutputStream->Put(sRTPTime);
			fOutputStream->Put(*rtpTime);
		}
		
		if (lastRTPInfo)
			fOutputStream->PutEOL();
	}
}



void RTSPRequestInterface::WriteStandardHeaders()
{
    static StrPtrLen    sCloseString("Close", 5);

    Assert(sPremadeHeader != NULL);
    fStandardHeadersWritten = true; //must be done here to prevent recursive calls
    
    //if this is a "200 OK" response (most HTTP responses), we have some special
    //optmizations here
    Bool16 sendServerInfo = QTSServerInterface::GetServer()->GetPrefs()->GetRTSPServerInfoEnabled();
    if (fStatus == qtssSuccessOK)
    {
        
        /*
        if (sendServerInfo)
        {   fOutputStream->Put(sPremadeHeaderPtr);
        }
        else
        {
            fOutputStream->Put(sPremadeNoHeaderPtr);
        }*/
        
        char * str="RTSP/1.0 200 OK\r\nServer: ARTSStreamingServer\r\nCSeq: ";
        StrPtrLen Cstr(str,strlen(str));
        fOutputStream->Put(Cstr);
        
        StrPtrLen* cSeq = fHeaderDictionary.GetValue(qtssCSeqHeader);
        Assert(cSeq != NULL);
        if (cSeq->Len > 1)
            fOutputStream->Put(*cSeq);
        else if (cSeq->Len == 1)
            fOutputStream->PutChar(*cSeq->Ptr);
        fOutputStream->PutEOL();
        
    }
    else
    {
#if 0
		// if you want the connection to stay alive when we don't grok
		// the specfied parameter than eneable this code. - [sfu]
		if (fStatus == qtssClientParameterNotUnderstood) {
			fResponseKeepAlive = true;
		}
#endif 
        //other status codes just get built on the fly
        PutStatusLine(fOutputStream, fStatus, RTSPProtocol::k10Version);
        if (sendServerInfo)
        {
            fOutputStream->Put(QTSServerInterface::GetServerHeader());
            fOutputStream->PutEOL();
        }
        AppendHeader(qtssCSeqHeader, fHeaderDictionary.GetValue(qtssCSeqHeader));
    }

    //append sessionID header
    StrPtrLen* incomingID = fHeaderDictionary.GetValue(qtssSessionHeader);
    if ((incomingID != NULL) && (incomingID->Len > 0))
        AppendHeader(qtssSessionHeader, incomingID);
        
        
    StrPtrLen * inClientID = fHeaderDictionary.GetValue(qtssClientSessionId);
    if((inClientID != NULL) && (inClientID->Len >0))
	AppendHeader(qtssClientSessionId, inClientID);
	
    StrPtrLen * onCommand = fHeaderDictionary.GetValue(qtssOnDemandSessionId);
    if((onCommand != NULL) && (onCommand->Len >0))
        AppendHeader(qtssOnDemandSessionId, onCommand);
     /*   
     char *cseq="CSeq: ";
     StrPtrLen Ccseq(cseq,strlen(cseq));
     fOutputStream->Put(Ccseq);
     
     StrPtrLen* cSeq = fHeaderDictionary.GetValue(qtssCSeqHeader);
     Assert(cSeq != NULL);
        if (cSeq->Len > 1)
            fOutputStream->Put(*cSeq);
        else if (cSeq->Len == 1)
            fOutputStream->PutChar(*cSeq->Ptr);
        fOutputStream->PutEOL();
    */
    //follows the HTTP/1.1 convention: if server wants to close the connection, it
    //tags the response with the Connection: close header
    if (!fResponseKeepAlive)
        AppendHeader(qtssConnectionHeader, &sCloseString);
        
    // 3gpp release 6 rate adaptation calls for echoing the rate adapt header back
    // some clients use this header in the response to signal whether to send rate adapt
    // NADU rtcp reports.
     Bool16 doRateAdaptation = QTSServerInterface::GetServer()->GetPrefs()->Get3GPPEnabled() && QTSServerInterface::GetServer()->GetPrefs()->Get3GPPRateAdaptationEnabled();
     if (doRateAdaptation)
     {   StrPtrLen* rateAdaptHeader = fHeaderDictionary.GetValue(qtss3GPPAdaptationHeader);
         if (rateAdaptHeader && rateAdaptHeader->Ptr && rateAdaptHeader->Len > 0)
            AppendHeader(qtss3GPPAdaptationHeader, fHeaderDictionary.GetValue(qtss3GPPAdaptationHeader));
     }
    
}

void RTSPRequestInterface::SendHeader()
{
    //qtss_printf("fStandardHeadersWritten:%d\n",fStandardHeadersWritten);
    if (!fStandardHeadersWritten)
        this->WriteStandardHeaders();
   
    fOutputStream->PutEOL();
}

QTSS_Error
RTSPRequestInterface::Write(void* inBuffer, UInt32 inLength, UInt32* outLenWritten, UInt32 /*inFlags*/)
{
    //now just write whatever remains into the output buffer
    fOutputStream->Put((char*)inBuffer, inLength);
    
    if (outLenWritten != NULL)
        *outLenWritten = inLength;
        
    return QTSS_NoErr;
}

QTSS_Error
RTSPRequestInterface::WriteV(iovec* inVec, UInt32 inNumVectors, UInt32 inTotalLength, UInt32* outLenWritten)
{
    (void)fOutputStream->WriteV(inVec, inNumVectors, inTotalLength, NULL,
                                                RTSPResponseStream::kAlwaysBuffer);
    if (outLenWritten != NULL)
        *outLenWritten = inTotalLength;
    return QTSS_NoErr;  
}

//param retrieval functions described in .h file
void* RTSPRequestInterface::GetAbsTruncatedPath(QTSSDictionary* inRequest, UInt32* /*outLen*/)
{
    // This function gets called only once
	
    RTSPRequestInterface* theRequest = (RTSPRequestInterface*)inRequest;
    theRequest->SetVal(qtssRTSPReqTruncAbsoluteURL, theRequest->GetValue(qtssRTSPReqAbsoluteURL));

    //Adjust the length to truncate off the last file in the path
    
    StrPtrLen* theAbsTruncPathParam = theRequest->GetValue(qtssRTSPReqTruncAbsoluteURL);
    theAbsTruncPathParam->Len--;
    while (theAbsTruncPathParam->Ptr[theAbsTruncPathParam->Len] != kPathDelimiterChar)
        theAbsTruncPathParam->Len--;
    
    return NULL;
}

void* RTSPRequestInterface::GetTruncatedPath(QTSSDictionary* inRequest, UInt32* /*outLen*/)
{
    // This function always gets called
	
    RTSPRequestInterface* theRequest = (RTSPRequestInterface*)inRequest;
    theRequest->SetVal(qtssRTSPReqFilePathTrunc, theRequest->GetValue(qtssRTSPReqFilePath));
    //qtss_printf("File:%s\n",theRequest->GetValue(qtssRTSPReqFilePath)->Ptr);

    //Adjust the length to truncate off the last file in the path
    StrPtrLen* theTruncPathParam = theRequest->GetValue(qtssRTSPReqFilePathTrunc);

    if (theTruncPathParam->Len > 0)
    {    
        if(((RTSPRequestInterface*)inRequest)->GetMethod() == qtssSetupMethod )
        {
            int len = sizeof(char) * theTruncPathParam->Len +1;
            char *buf=(char *)malloc(len);
            memset(buf,0,len);
            memcpy(buf,theTruncPathParam->Ptr,theTruncPathParam->Len);
            if(strstr(buf,"trackID") != NULL)
            {
                theTruncPathParam->Len--;
                while ( (theTruncPathParam->Len != 0) && (theTruncPathParam->Ptr[theTruncPathParam->Len] != kPathDelimiterChar) )
                    theTruncPathParam->Len--;
            }
            free(buf);
         }    
    }

    return NULL;
}

void* RTSPRequestInterface::GetFileName(QTSSDictionary* inRequest, UInt32* /*outLen*/)
{
    // This function always gets called
    
    RTSPRequestInterface* theRequest = (RTSPRequestInterface*)inRequest;
    theRequest->SetVal(qtssRTSPReqFileName, theRequest->GetValue(qtssRTSPReqFilePath));

    StrPtrLen* theFileNameParam = theRequest->GetValue(qtssRTSPReqFileName);

    //paranoid check
    if (theFileNameParam->Len == 0)
        return theFileNameParam;
        
    //walk back in the file name until we hit a /
    SInt32 x = theFileNameParam->Len - 1;
    for (; x > 0; x--)
        if (theFileNameParam->Ptr[x] == kPathDelimiterChar)
            break;
    //once we do, make the tempPtr point to the next character after the slash,
    //and adjust the length accordingly
    if (theFileNameParam->Ptr[x] == kPathDelimiterChar )
    {
        theFileNameParam->Ptr = (&theFileNameParam->Ptr[x]) + 1;
        theFileNameParam->Len -= (x + 1);
    }
    
    return NULL;        
}


void* RTSPRequestInterface::GetFileDigit(QTSSDictionary* inRequest, UInt32* /*outLen*/)
{
    // This function always gets called
    
    RTSPRequestInterface* theRequest = (RTSPRequestInterface*)inRequest;
    theRequest->SetVal(qtssRTSPReqFileDigit, theRequest->GetValue(qtssRTSPReqFilePath));

    StrPtrLen* theFileDigit = theRequest->GetValue(qtssRTSPReqFileDigit);

    UInt32  theFilePathLen = theRequest->GetValue(qtssRTSPReqFilePath)->Len;
    theFileDigit->Ptr += theFilePathLen - 1;
    theFileDigit->Len = 0;
    while ((StringParser::sDigitMask[(unsigned int) *(*theFileDigit).Ptr] != '\0') &&
            (theFileDigit->Len <= theFilePathLen))
    {
        theFileDigit->Ptr--;
        theFileDigit->Len++;
    }
    //termination condition means that we aren't actually on a digit right now.
    //Move pointer back onto the digit
    theFileDigit->Ptr++;
    
    return NULL;
}

void* RTSPRequestInterface::GetRealStatusCode(QTSSDictionary* inRequest, UInt32* outLen)
{
    // Set the fRealStatusCode variable based on the current fStatusCode.
	// This function always gets called
    RTSPRequestInterface* theReq = (RTSPRequestInterface*)inRequest;
    theReq->fRealStatusCode = RTSPProtocol::GetStatusCode(theReq->fStatus);
    *outLen = sizeof(UInt32);
    return &theReq->fRealStatusCode;
}

void* RTSPRequestInterface::GetLocalPath(QTSSDictionary* inRequest, UInt32* outLen)
{
	// This function always gets called	
	RTSPRequestInterface* theRequest = (RTSPRequestInterface*)inRequest;
	QTSS_AttributeID theID = qtssRTSPReqFilePath;
	
    // Get the truncated path on a setup, because setups have the trackID appended
	if (theRequest->GetMethod() == qtssSetupMethod)
	{
        theID = qtssRTSPReqFilePathTrunc;
		// invoke the param retrieval function here so that we can use the internal GetValue function later  
		RTSPRequestInterface::GetTruncatedPath(inRequest, outLen);
	}
    
	StrPtrLen* thePath = theRequest->GetValue(theID);
	StrPtrLen filePath(thePath->Ptr, thePath->Len);
	
	//qtss_printf("filePath:%s,Len:%d\n",filePath.Ptr,filePath.Len);
	StrPtrLen* theRootDir = theRequest->GetValue(qtssRTSPReqRootDir);
	
	//if (theRequest->GetMethod() == qtssSetupMethod)
	{
	    //if(strstr(filePath.Ptr,"trackID") != NULL)
	    {
	     
	       if (theRootDir->Len && theRootDir->Ptr[theRootDir->Len -1] == kPathDelimiterChar
	        && thePath->Len  && thePath->Ptr[0] == kPathDelimiterChar)
	        {
                char *thePathEnd = &(filePath.Ptr[filePath.Len]);
                while (filePath.Ptr != thePathEnd)
                {
                    if (*filePath.Ptr != kPathDelimiterChar)
                        break;
	            
                    filePath.Ptr ++;
                    filePath.Len --;
	            }
            }   
        }
   }
	
	
	//qtss_printf("filePath:%s,filePath.Len:%d\n",filePath.Ptr,filePath.Len);
	UInt32 fullPathLen = filePath.Len + theRootDir->Len;
	char* theFullPath = NEW char[fullPathLen+1];
	theFullPath[fullPathLen] = '\0';
	
	::memcpy(theFullPath, theRootDir->Ptr, theRootDir->Len);
	::memcpy(theFullPath + theRootDir->Len, filePath.Ptr, filePath.Len);
	
	//qtss_printf("theFullPath:%s\n",theFullPath);
	(void)theRequest->SetValue(qtssRTSPReqLocalPath, 0, theFullPath,fullPathLen , QTSSDictionary::kDontObeyReadOnly);
	
	// delete our copy of the data
	delete [] theFullPath;
	*outLen = 0;
	
	return NULL;
}

void* RTSPRequestInterface::GetAuthDigestResponse(QTSSDictionary* inRequest, UInt32* )
{
	RTSPRequestInterface* theRequest = (RTSPRequestInterface*)inRequest;
	(void)theRequest->SetValue(qtssRTSPReqDigestResponse, 0, theRequest->fAuthDigestResponse.Ptr,theRequest->fAuthDigestResponse.Len , QTSSDictionary::kDontObeyReadOnly);
	return NULL;
}
