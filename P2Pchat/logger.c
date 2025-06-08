#include "logger.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

//////////////////////////////////////////
//
//          INTERNAL GLOBALS
//
//////////////////////////////////////////

#define TIMESTAMP_BUFFER_SIZE     24       // YYYY-MM-DD HH:MM:SS.mmm
#define DATE_BUFFER_SIZE          16       // YYYY-MM-DD

#define MAXIMUM_LOG_MESSAGE_SIZE  2048     // Maximum size of a single log message
#define MAXIMUM_FILENAME_SIZE     MAX_PATH // Maximum size of a filename

#define DEFAULT_FILE_AGE 14                // Default maximum age of log files to be kept in days

typedef struct _LOGGER_STATE
{
    CRITICAL_SECTION Lock;                                   // Critical section for thread safety
    FILE*            FileStream;                             // File stream for logging
    INT64            MaxFileSize;                            // Maximum file size before rotation
    CHAR             BaseFilename[MAXIMUM_FILENAME_SIZE];    // Base filename for log files
    CHAR             CurrentFilename[MAXIMUM_FILENAME_SIZE]; // Current log file name
    INT64            WrittenBytes;                           // Total bytes written to log file
    LOG_LEVEL        Level;                                  // Current log level
    BOOL             IsInitialized;                          // Flag to check if logger is initialized
    SYSTEMTIME       LastRotationTime;                       // Last time the log file was rotated
    ULONG            MaximumFileAgeDays;                     // Maximum age of log files in days

} LOGGER_STATE, * PLOGGER_STATE;

static LOGGER_STATE GlobalLoggerState = { 0 };

// Simple mapping of log levels to names
static 
const char* LOG_LEVEL_NAMES[] = {
    "NONE",
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};


//////////////////////////////////////////
//
//          INTERNAL HELPERS
//
//////////////////////////////////////////

/**
* Internal helper for getting current time.
*/
static 
BOOL 
LoggerTimestamp(
    _Out_ PSTR  Buffer,
    _In_  ULONG BufferSize
)
{
    if (Buffer == NULL || BufferSize < TIMESTAMP_BUFFER_SIZE)
    {
        return; // Invalid buffer or size
    }

    SYSTEMTIME SystemTime;
    GetLocalTime_s(&SystemTime);

    INT Result = _snprintf_s(
        Buffer,
        BufferSize,
        _TRUNCATE,
        "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        SystemTime.wYear,
        SystemTime.wMonth,
        SystemTime.wDay,
        SystemTime.wHour,
        SystemTime.wMinute,
        SystemTime.wSecond,
        SystemTime.wMilliseconds
    );

    return Result > 0;
}

/**
* Converts SYSTEMTIME to a string in the format YYYY-MM-DD.
*/
static
BOOL
LoggerDateString(
    _In_  const SYSTEMTIME* SystemTime,
    _Out_ PSTR DateString,
    _In_  ULONG DateStringSize
)
{
    if (SystemTime == NULL || DateString == NULL || DateStringSize < TIMESTAMP_BUFFER_SIZE)
    {
        return FALSE;
    }

    INT Result = _snprintf_s(
        DateString,
        DateStringSize,
        _TRUNCATE,
        "%04d-%02d-%02d",
        SystemTime->wYear,
        SystemTime->wMonth,
        SystemTime->wDay
    );
    return Result > 0; 
}

/**
* Creates a file, with the name based on the current date.
*/
LoggerCreateFilename(
    const SYSTEMTIME* SystemTime,
    PSTR Filename,
    ULONG FilenameSize
)
{
    if (SystemTime == NULL || Filename == NULL || FilenameSize < MAXIMUM_FILENAME_SIZE)
    {
        return FALSE;
    }

    CHAR DateString[TIMESTAMP_BUFFER_SIZE] = { 0 };
    if (!LoggerGetDateString(SystemTime, DateString, sizeof(DateString)))
    {
        return FALSE;
    }

    INT Result = _snprintf_s(
        Filename,
        FilenameSize,
        _TRUNCATE,
        "%s_%s.txt",
        GlobalLoggerState.BaseFilename,
        DateString
    );

    return Result > 0;
}

/**
*
*/
static
BOOL 
LoggerCompareDate(
    const SYSTEMTIME* Date1,
    const SYSTEMTIME* Date2
)
{
    if (Date1 == NULL || Date2 == NULL)
    {
        return FALSE;
    }

    return (Date1->wYear  == Date2->wYear &&
            Date1->wMonth == Date2->wMonth &&
            Date1->wDay   == Date2->wDay);
}

/**
*
*/
static 
void
LoggerDifferenceInDays(
    const SYSTEMTIME* Date,
    INT32  Days
)
{
    if (Date == NULL || Days == NULL)
    {
        return;
    }

    FILETIME FileTime;
    SystemTimeToFileTime(Date, &FileTime);

    ULARGE_INTEGER LargeInt;
    LargeInt.LowPart = FileTime.dwLowDateTime;
    LargeInt.HighPart = FileTime.dwHighDateTime;

    ULONGLONG DaysInFileTime = (ULONGLONG)Days * 24 * 60 * 60 * 10000000ULL; // Convert days to FILETIME units (100-nanosecond intervals)
    LargeInt.QuadPart -= DaysInFileTime; // Get difference in days

    FileTime.dwLowDateTime = LargeInt.LowPart;
    FileTime.dwHighDateTime = LargeInt.HighPart;

    FileTimeToSystemTime(&FileTime, Date);
}

/**
*
*/
static
VOID
LoggerCleanUpDatedFiles(
    VOID
)
{
    if (GlobalLoggerState.MaximumFileAgeDays <= 0)
    {
        return; 
    }

    SYSTEMTIME CurrentTime;
    GetLocalTime(&CurrentTime);
    LoggerDifferenceInDays(&CurrentTime, GlobalLoggerState.MaximumFileAgeDays);

    CHAR Filename[MAXIMUM_FILENAME_SIZE] = { 0 };

    WIN32_FIND_DATAA FindData;
    HANDLE FindHandle = FindFirstFileA(GlobalLoggerState.BaseFilename, &FindData);

    CHAR SearchPattern[MAXIMUM_FILENAME_SIZE] = { 0 };
    _snprintf_s(
        SearchPattern, 
        sizeof(SearchPattern), 
        _TRUNCATE,
        "%s_*.log", 
        GlobalLoggerState.BaseFilename
    );

    if (FindHandle == INVALID_HANDLE_VALUE)
    {
        return; // No files found
    }

    BOOL FileFound = TRUE;

    while( FileFound )
    {
        PSTR DateStart = strstr(FindData.cFileName, "_"); // Find the date part in the filename

        if (DateStart == NULL)
        {
            continue; // Invalid filename format
        }

        DateStart++; // Move past underscore

        PSTR DateEnd = strstr(DateStart, ".log"); // Find the end of the date part

        if (DateEnd == NULL || ( DateEnd - DateStart ) != 10 )
        {
            continue; // Invalid filename format
        }

        CHAR DateString[TIMESTAMP_BUFFER_SIZE] = { 0 };
        strncpy_s(
            DateString, 
            sizeof(DateString),
            DateStart, 
            DateEnd - DateStart
        ); // Copy date part

        SYSTEMTIME FindDataTime = { 0 };
        if( sscanf_s(
            DateString,
            "%4d-%2d-%2d",
            &FindDataTime.wYear,
            &FindDataTime.wMonth,
            &FindDataTime.wDay
        ) != 3)
        {
            continue; // Failed to parse date
        }

        FILETIME FileTime;
        FILETIME CutoffTime;

        SystemTimeToFileTime(&FindDataTime, &FileTime);
        SystemTimeToFileTime(&CurrentTime, &CutoffTime);

        if (CompareFileTime(&FileTime, &CutoffTime) < 0)
        {

            CHAR FullPath[MAXIMUM_FILENAME_SIZE] = { 0 };

            PSTR LastSlash = strrchr(GlobalLoggerState.BaseFilename, '\\');
            if (LastSlash == NULL)
            {
                LastSlash = strrchr(GlobalLoggerState.BaseFilename, '/');
            }

            if (LastSlash != NULL)
            {
                strncpy_s(FullPath, sizeof(FullPath), GlobalLoggerState.BaseFilename, LastSlash - GlobalLoggerState.BaseFilename + 1);
                strncat_s(FullPath, sizeof(FullPath), FindData.cFileName, _TRUNCATE); // Append filename to path
            }
            else
            {
                strncpy_s(FullPath, sizeof(FullPath), FindData.cFileName, _TRUNCATE); // Just filename
            }

            DeleteFileA(FullPath); // Delete the file
        }

        FileFound = FindNextFileA(FindHandle, &FindData); // Get next file
    } 

    FindClose(FindHandle);

}

/**
*
*/
static 
BOOL
LoggerIsRotationNeeded(
    LPSYSTEMTIME CurrentTime
)
{
    if (CurrentTime == NULL)
    {
        GetLocalTime( CurrentTime );
    }

    return LoggerCompareDate(
        CurrentTime,
        &GlobalLoggerState.LastRotationTime
    ) == FALSE; // If current date is different from last rotation date, rotation is needed
}

/**
*
*/
static
BOOL
LoggerCreateRotateFile(
    PSTR NewFilename,
    ULONG NewFilenameSize
)
{
    if (NewFilename == NULL || NewFilenameSize < MAXIMUM_FILENAME_SIZE)
    {
        return FALSE; // Invalid parameters
    }

    CHAR TimestampBuffer[TIMESTAMP_BUFFER_SIZE] = { 0 };
    if (!LoggerTimestamp( TimestampBuffer, sizeof(TimestampBuffer) ) )
    {
        return FALSE; // Failed to get timestamp
    }
    
    for ( PSTR p = TimestampBuffer; *p; ++p )
    {
        if (*p == ' ' || *p == ':')
        {
            *p = '_'; // Make sure filename is vlaid 
        }
    }

    int Result = _snprintf_s(
        NewFilename,
        NewFilenameSize,
        _TRUNCATE,
        "%s_%s.log",
        GlobalLoggerState.BaseFilename,
        TimestampBuffer
    );

    return Result > 0; // TRUE if filename was created successfully
}

/**
*
*/
static 
BOOL
LoggerTryRotation(
    VOID
)
{
    SYSTEMTIME CurrentTime;
    GetLocalTime(&CurrentTime);

    if( !LoggerIsRotationNeeded( &CurrentTime ) )
    {
        return FALSE; // Rotation is not needed
    }

    if( GlobalLoggerState.FileStream != NULL )
    {
        fclose( GlobalLoggerState.FileStream );
        GlobalLoggerState.FileStream = NULL;
    }

    CHAR NewFilename[MAXIMUM_FILENAME_SIZE] = { 0 };
    if( !LoggerCreateFilename( &CurrentTime, NewFilename, sizeof( NewFilename ) ) )
    {
        return FALSE; // Failed to create new filename
    }

    if ( fopen_s(&GlobalLoggerState.FileStream, NewFilename, "a") != 0 )
    {
        return FALSE; // Failed to open new log file
    }
}

/**
*
*/
static 
void
Logger__WriteToFile(
    LOG_LEVEL Level,
    const char* Filename,
    __int64 LineNumber,
    const char* Format,
    va_list Args
)
{
    if (gLoggerFileStream == NULL)
    {
        return; // No file stream to write to
    }
    
    char Timestamp[TIMESTAMP_BUFFER_SIZE] = { 0 };
    LoggerTimestamp(Timestamp);
    
    char LogMessage[2048] = { 0 };
    vsnprintf_s(LogMessage, sizeof(LogMessage), _TRUNCATE, Format, Args);
    
    EnterCriticalSection(&gLoggerLock);

    LoggerTryRotation();

    fprintf(
        gLoggerFileStream,
        "[%s] [%s:%d] [%s] %s\n",
        Timestamp,
        Filename,
        LineNumber,
        gLoggerLevelNames[Level],
        LogMessage
    );

    fflush(gLoggerFileStream); // Ensure the message is written immediately
    gLoggerWrittenBytes += strlen(LogMessage) + TIMESTAMP_BUFFER_SIZE ; //size of log entry
    
    LeaveCriticalSection(&gLoggerLock);
}

__int64
LoggerInitConsole(
    FILE* Filestream
)
{
    InitializeCriticalSection( &gLoggerLock );

    if (Filestream == NULL)
    {
        gLoggerFileStream = stdout; // default to stdout if no file stream provided
    }
    else
    {
        gLoggerFileStream = Filestream;
    }

    return 0;
}

__int64
LoggerInitFile(
    const char* Path,
    __int64 MaxFileSize
)
{
    InitializeCriticalSection( &gLoggerLock );

    strncpy_s( 
        gLoggerBaseFilename,
        sizeof(gLoggerBaseFilename),
        Path,
        _TRUNCATE
    );

    gLoggerMaxFileSize = MaxFileSize;

    // open log file
    if( fopen_s(&gLoggerFileStream, gLoggerBaseFilename, "a") != 0 )
    {
        return -1; // failed to open log file
    }
    
    fseek(gLoggerFileStream, 0, SEEK_END);
    gLoggerWrittenBytes = ftell(gLoggerFileStream); // get current size of the log file

    return 0;
}

void
LoggerCleanUp(
    void
)
{
    EnterCriticalSection( &gLoggerLock );

    if (gLoggerFileStream && gLoggerFileStream != stdout && gLoggerFileStream != stderr )
    {
        fclose(gLoggerFileStream);
    }   

    gLoggerFileStream = NULL;
    LeaveCriticalSection(&gLoggerLock);
    DeleteCriticalSection(&gLoggerLock);
}

void 
LoggerSetLevel(
    LOG_LEVEL Level
)
{
    EnterCriticalSection(&gLoggerLock);
    gLoggerLevel = Level;
    LeaveCriticalSection(&gLoggerLock);
}

LOG_LEVEL
LoggerGetLevel(
    void
)
{
    LOG_LEVEL Level;
    EnterCriticalSection(&GlobalLoggerState.Lock);
    Level = GlobalLoggerState.Level;
    LeaveCriticalSection(&GlobalLoggerState.Lock);
    return Level;
}

void
LoggerWrite(
    LOG_LEVEL Level,
    const char* Filename,
    __int64 LineNumber,
    const char* Format,
    ...
)
{
    if (Level < GlobalLoggerState.Level)
    {
        return; // level is lower than the current logger level, so do not log
    }

    va_list Args;
    va_start(Args, Format);
    Logger__WriteToFile(Level, Filename, LineNumber, Format, Args);
    va_end(Args);
}
