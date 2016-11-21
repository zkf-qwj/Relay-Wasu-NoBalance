#ifndef QTSSARTSReflectorAccessLog_H
#define QTSSARTSReflectorAccessLog_H
#include <cstdarg>
#include "QTSSRollingLog.h"
#include "QTSSModuleUtils.h"
extern QTSS_ModulePrefsObject sPrefs;
static char*    sDefaultLogDir = NULL;
static UInt32   sRollInterval   = 7;
static UInt32   sMaxLogBytes    = 10240000;
static QTSS_ServerObject sServer = NULL;
static Bool16   sLogEnabled     = true;
static Bool16   sLogTimeInGMT   = true;
static char*    sDefaultLogName     = "arts_access";
static char* sLogHeader =   "#Software: %s\n"
                                "#Version: %s\n"    //%s == version
                                "#Date: %s\n"       //%s == date/time
                                "#Remark: All date values are in %s.\n" //%s == "GMT" or "local time"
                                "#Remark: c-duration is in seconds.\n"
                                "#Fields: c-ip c-user-agent [date time] cs-uri c-status c-bytes c-duration\n";
                                
         

extern int ARTS_MODULE_DEBUG_LEVEL;
                
class QTSSARTSReflectorAccessLog : public QTSSRollingLog
{
    public:
    
        QTSSARTSReflectorAccessLog();
        virtual ~QTSSARTSReflectorAccessLog() {}
    
        virtual char* GetLogName() { return QTSSModuleUtils::GetStringAttribute(sPrefs, "request_logfile_name", sDefaultLogName); }
        virtual char* GetLogDir()  { return QTSSModuleUtils::GetStringAttribute(sPrefs, "request_logfile_dir", sDefaultLogDir); }
        virtual UInt32 GetRollIntervalInDays()  { return sRollInterval; }
        virtual UInt32 GetMaxLogBytes()         { return sMaxLogBytes; }
        virtual time_t WriteLogHeader(FILE *inFile);
};

static QTSSARTSReflectorAccessLog*    sARTSReflectorAccessLog   = NULL;

// ---------------------------------------------------------------------------
// CLASS IMPLEMENTATIONS
// ---------------------------------------------------------------------------

// ****************************************************************************
// QTSSARTSReflectorAccessLog -- subclass of QTSSRollingLog
// ****************************************************************************
inline QTSSARTSReflectorAccessLog::QTSSARTSReflectorAccessLog() : QTSSRollingLog() 
{
    this->SetTaskName("QTSSARTSReflectorAccessLog");
}

inline time_t QTSSARTSReflectorAccessLog::WriteLogHeader(FILE *inFile)
{
    //The point of this header is to record the exact time the log file was created,
    //in a format that is easy to parse through whenever we open the file again.
    //This is necessary to support log rolling based on a time interval, and POSIX doesn't
    //support a create date in files.
    time_t calendarTime = ::time(NULL);
    Assert(-1 != calendarTime);
    if (-1 == calendarTime)
        return -1;

    struct tm  timeResult;
    struct tm* theLocalTime = qtss_localtime(&calendarTime, &timeResult);
    Assert(NULL != theLocalTime);
    if (NULL == theLocalTime)
        return -1;
    
    char tempBuffer[1024] = { 0 };
    qtss_strftime(tempBuffer, sizeof(tempBuffer), "#Log File Created On: %m/%d/%Y %H:%M:%S\n", theLocalTime);
    this->WriteToLog(tempBuffer, !kAllowLogToRoll);
    tempBuffer[0] = '\0';
    
    //format a date for the startup time
    char theDateBuffer[QTSSRollingLog::kMaxDateBufferSizeInBytes] = { 0 };
    Bool16 result = QTSSRollingLog::FormatDate(theDateBuffer, false);
    
    if (result)
    {
        StrPtrLen serverName;
        (void)QTSS_GetValuePtr(sServer, qtssSvrServerName, 0, (void**)&serverName.Ptr, &serverName.Len);
        StrPtrLen serverVersion;
        (void)QTSS_GetValuePtr(sServer, qtssSvrServerVersion, 0, (void**)&serverVersion.Ptr, &serverVersion.Len);
        qtss_sprintf(tempBuffer, sLogHeader, serverName.Ptr , serverVersion.Ptr, 
                            theDateBuffer, sLogTimeInGMT ? "GMT" : "local time");
        this->WriteToLog(tempBuffer, !kAllowLogToRoll);
    }
        
    return calendarTime;
}


#define RLogRequest(LEVEL, CID,fmt,...) { \
    if (sLogEnabled && LEVEL <= ARTS_MODULE_DEBUG_LEVEL) { \
        char time_ [100] = { 0 }; \
        time_t t = time (0); \
        strftime(time_, 100, "%F %T", localtime (&t)); \
        RLogRequest_ ("%s [%s:%d] %d:%d:%d | " fmt "\n", time_, __FUNCTION__, __LINE__, (CID >> 24), ((CID & 0xFFFFF)>>4), (CID & 0xF), ## __VA_ARGS__); \
    } \
}
inline void RLogRequest_ (const char *fmt, ...)
{
    if(fmt != NULL)
    {       
        va_list args;
        va_start(args,fmt);
        char tempBuffer[1024] = { 0 };
        ::vsnprintf(tempBuffer, 1024, fmt , args);
        va_end(args);       
        sARTSReflectorAccessLog->WriteToLog(tempBuffer, kAllowLogToRoll);       
    }
    
   
}

inline void    WriteStartupMessage()
{
    
    //format a date for the startup time
    char theDateBuffer[QTSSRollingLog::kMaxDateBufferSizeInBytes];
    Bool16 result = QTSSRollingLog::FormatDate(theDateBuffer, false);
    
    char tempBuffer[1024];
    if (result)
        qtss_sprintf(tempBuffer, "#Remark: Streaming beginning STARTUP %s\n", theDateBuffer);
        
    // log startup message to error log as well.
    if ((result) && (sARTSReflectorAccessLog != NULL))
        sARTSReflectorAccessLog->WriteToLog(tempBuffer, kAllowLogToRoll);
}

inline void    WriteShutdownMessage()
{
    
    //log shutdown message
    //format a date for the shutdown time
    char theDateBuffer[QTSSRollingLog::kMaxDateBufferSizeInBytes];
    Bool16 result = QTSSRollingLog::FormatDate(theDateBuffer, false);
    
    char tempBuffer[1024];
    if (result)
        qtss_sprintf(tempBuffer, "#Remark: Streaming beginning SHUTDOWN %s\n", theDateBuffer);

    if ( result && sARTSReflectorAccessLog != NULL )
        sARTSReflectorAccessLog->WriteToLog(tempBuffer, kAllowLogToRoll);
}


#endif
