#include "logger.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

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

#define DEFAULT_LOG_FILE_EXETENSION ".txt" 

typedef struct _LOGGER_STATE
{
    CRITICAL_SECTION Lock;                                   // Critical section for thread safety
    FILE*            FileStream;                             // File stream for logging
    CHAR             BaseFilePath[MAXIMUM_FILENAME_SIZE];    // Base filename for log files
    CHAR             CurrentFilename[MAXIMUM_FILENAME_SIZE]; // Current log file name
    LOG_LEVEL        Level;                                  // Current log level
    BOOL             IsInitialized;                          // Flag to check if logger is initialized
    SYSTEMTIME       LastRotationTime;                       // Last time the log file was rotated
    INT64            MaximumFileAgeDays;                     // Maximum age of log files in days
} LOGGER_STATE, * PLOGGER_STATE;

static LOGGER_STATE GlobalLoggerState = { 0 };

// Simple mapping of log levels to names
static 
PCSTR LOG_LEVEL_NAMES[] = {
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
* Internal helper for getting current time in format YYYY-MM-DD HH:MM:SS.mmm.
* 
* @param Buffer     Pointer to a buffer where the timestamp will be stored.]
* @param BufferSize Size of the buffer.
* 
* @return TRUE if the timestamp was created successfully, FALSE otherwise.
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
        return FALSE; // Invalid buffer or size
    }

    SYSTEMTIME SystemTime;
    GetLocalTime(&SystemTime);

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
* Converts SYSTEMTIME to a string in the format YYYY-MM-DD to given buffer.
* 
* @param SystemTime     Pointer to SYSTEMTIME structure containing the date.
* @param DateString     Pointer to a buffer where the date string will be stored.
* @param DateStringSize Size of the DateString buffer.
* 
* @return TRUE if the date string was created successfully, FALSE otherwise.
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
* Creates a file, with the name based on the current date and base file path.
* 
* @param SystemTime Pointer to SYSTEMTIME structure containing the current date and time.
* @param Filename   Pointer to a buffer where the filename will be stored.
* @param FilenameSize Size of the Filename buffer.
* 
* @return TRUE if the filename was created successfully, FALSE otherwise.
*/
static 
BOOL
LoggerCreateFilename(
    _In_  const SYSTEMTIME* SystemTime,
    _Out_ PSTR Filename,
    _Out_ ULONG FilenameSize
)
{
    if (SystemTime == NULL || Filename == NULL || FilenameSize < MAXIMUM_FILENAME_SIZE)
    {
        return FALSE;
    }

    CHAR DateString[TIMESTAMP_BUFFER_SIZE] = { 0 };
    if (!LoggerDateString(SystemTime, DateString, sizeof(DateString)))
    {
        return FALSE;
    }

    INT Result = _snprintf_s(
        Filename,
        FilenameSize,
        _TRUNCATE,
        "%s\\%s"DEFAULT_LOG_FILE_EXETENSION"",
        GlobalLoggerState.BaseFilePath,
        DateString
    );

    return Result > 0;
}

/**
* Compares two SYSTEMTIME dates.
* 
* @param Date1 First date to compare.
* @param Date2 Second date to compare.
* 
* @return TRUE if the dates are equal, FALSE otherwise.
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
* Calculates the difference in days between the given date and the current date.
* 
* @param Date Pointer to SYSTEMTIME structure containing the date to adjust.
* @param Days Number of days to subtract from the date.
*/
static 
void
LoggerDifferenceInDays(
    SYSTEMTIME* Date,
    INT64       Days
)
{
    if (Date == NULL)
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
* Cleans up old log files based on the maximum file age set in the logger state.
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


    CHAR SearchPattern[MAXIMUM_FILENAME_SIZE] = { 0 };
    _snprintf_s(
        SearchPattern, 
        sizeof(SearchPattern), 
        _TRUNCATE,
        "%s\\*.txt", 
        GlobalLoggerState.BaseFilePath
    );

    WIN32_FIND_DATAA FindData;
    HANDLE FindHandle = FindFirstFileA(SearchPattern, &FindData);

    if (FindHandle == INVALID_HANDLE_VALUE)
    {
        return; // No files found
    }

    BOOL FileFound = TRUE;

    while( FileFound )
    {
        PSTR DateStart = FindData.cFileName; 

        if (DateStart == NULL)
        {
            continue; // Invalid filename format
        }

        PSTR DateEnd = strstr(DateStart, ".txt"); // Find the end of the date part

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
            "%4hu-%2hu-%2hu",
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
            // Create full path to the file to delete
            CHAR FullPath[MAXIMUM_FILENAME_SIZE] = { 0 };
            _snprintf_s(
                FullPath,
                sizeof(FullPath),
                _TRUNCATE,
                "%s\\%s",
                GlobalLoggerState.BaseFilePath,
                FindData.cFileName
            );

            DeleteFileA(FullPath);
        }

        FileFound = FindNextFileA(FindHandle, &FindData); // Get next file
    } 

    FindClose(FindHandle);
}

/**
* Checks if the log file rotation is needed based on the current date and last rotation time.
* 
* @param CurrentTime Pointer to SYSTEMTIME structure containing the current date and time.
* 
* @return TRUE if rotation is needed, FALSE otherwise.
*/
static 
BOOL
LoggerIsRotationNeeded(
    PSYSTEMTIME CurrentTime
)
{
    SYSTEMTIME LocalTime = { 0 };

    if (CurrentTime == NULL)
    {
        CurrentTime = &LocalTime;
    }

    GetLocalTime(CurrentTime);

    return LoggerCompareDate(
        CurrentTime,
        &GlobalLoggerState.LastRotationTime
    ) == FALSE; // If current date is different from last rotation date, rotation is needed.
}

/**
* Creates a new log file name based on the current date.
* 
* @param NewFilename     Pointer to a buffer where the new filename will be stored.
* @param NewFilenameSize Size of the NewFilename buffer.
* 
* @return TRUE if the new filename was created successfully, FALSE otherwise.
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

    INT Result = _snprintf_s(
        NewFilename,
        NewFilenameSize,
        _TRUNCATE,
        "%s\\%s"DEFAULT_LOG_FILE_EXETENSION"",
        GlobalLoggerState.BaseFilePath,
        TimestampBuffer
    );

    return Result > 0; // TRUE if filename was created successfully
}

/**
* Attempts to rotate the log file if needed.
* 
* @return TRUE if rotation was successful, FALSE otherwise.
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
    if( !LoggerCreateRotateFile( NewFilename, sizeof(NewFilename)) )
    {
        return FALSE; // Failed to create new filename
    }

    errno_t ErrorOpen = fopen_s(&GlobalLoggerState.FileStream, NewFilename, "a");
    if ( ErrorOpen != 0 || GlobalLoggerState.FileStream == NULL )
    {
        return FALSE; // Failed to open new log file
    }

    strncpy_s(
        GlobalLoggerState.CurrentFilename,
        sizeof(GlobalLoggerState.CurrentFilename),
        NewFilename,
        _TRUNCATE
    );

    GlobalLoggerState.LastRotationTime = CurrentTime; // Update last rotation time

    LoggerCleanUpDatedFiles(); // Clean up old files

    return TRUE;
}

/**
* Writes a log message to the file stream with the specified log level, filename, line number, and format string.
* 
* @param Level      The log level of the message.
* @param Filename   The name of the file where the log message is being written.
* @param LineNumber The line number in the file where the log message is being written.
* @param Format     The format string for the log message.
* @param Args       The variable argument list containing the values to format into the log message.
* 
* @return VOID
*/
static
VOID
LoggerWriteToFile(
    _In_ LOG_LEVEL Level,
    _In_ PCSTR Filename,
    _In_ UINT64 LineNumber,
    _In_ PCSTR Format,
    _In_ va_list Args
)
{
    if( Filename == NULL || Format == NULL )
    {
        return; // Invalid parameters
    }

    if( Level < LOG_LEVEL_NONE || Level > LOG_LEVEL_FATAL )
    {
        return; // Invalid log level
    }

    CHAR Timestamp[TIMESTAMP_BUFFER_SIZE] = { 0 };
    if( !LoggerTimestamp(Timestamp, sizeof(Timestamp)) )
    {
        return; // Failed to get timestamp
    }

    CHAR LogMessage[MAXIMUM_LOG_MESSAGE_SIZE] = { 0 };
    INT64 MessageLength = _vsnprintf_s(
        LogMessage,
        sizeof(LogMessage),
        _TRUNCATE,
        Format,
        Args
    );

    if (MessageLength < 0)
    {
        return; // Failed to format log message
    }

    EnterCriticalSection(&GlobalLoggerState.Lock);

    SYSTEMTIME CurrentTime;
    GetLocalTime(&CurrentTime);
    BOOL IsRotationNeeded = LoggerIsRotationNeeded(&CurrentTime);

    if( IsRotationNeeded && !LoggerTryRotation() )
    {
        LeaveCriticalSection(&GlobalLoggerState.Lock);
        return; // Rotation failed, do not log
    }

    if (GlobalLoggerState.FileStream == NULL)
    {
        LeaveCriticalSection(&GlobalLoggerState.Lock);
        return; // No file stream available
    }

    INT64 WrittenBytes = fprintf(
        GlobalLoggerState.FileStream,
        "[%s] [%s] [%s:%llu] %s\n",
        Timestamp,
        LOG_LEVEL_NAMES[Level],
        Filename,
        LineNumber,
        LogMessage
    );

    if (WrittenBytes > 0)
    {
        fflush(GlobalLoggerState.FileStream); // Flush the stream to ensure the message is written
    }

    LeaveCriticalSection(&GlobalLoggerState.Lock);
}


//////////////////////////////////////////
//
//          PUBLIC FUNCTIONS
//
//////////////////////////////////////////

INT64
LoggerInitConsole(
    _In_ FILE* Filestream
)
{

    if( GlobalLoggerState.IsInitialized )
    {
        return -1; // Logger is already initialized
    }

    InitializeCriticalSection( &GlobalLoggerState.Lock );

    GlobalLoggerState.FileStream = Filestream;
    GlobalLoggerState.Level = LOG_LEVEL_INFO; // Default log level
    GlobalLoggerState.IsInitialized = TRUE;
    
    memset(GlobalLoggerState.BaseFilePath, 0, sizeof(GlobalLoggerState.BaseFilePath));
    memset(GlobalLoggerState.CurrentFilename, 0, sizeof(GlobalLoggerState.CurrentFilename));
    memset(&GlobalLoggerState.LastRotationTime, 0, sizeof(GlobalLoggerState.LastRotationTime));

    return 0;
}

INT64
LoggerInitFile(
    _In_ PCSTR Path,
    _In_ INT64 RetentionDays
)
{
    if (Path == NULL || GlobalLoggerState.IsInitialized)
    {
        return -1; // Invalid path or logger already initialized
    }

    size_t PathLength = strnlen_s(Path, MAXIMUM_FILENAME_SIZE);
    if( PathLength == 0 || PathLength >= MAXIMUM_FILENAME_SIZE )
    {
        return -1; // Path is too long
    }

    InitializeCriticalSection(&GlobalLoggerState.Lock);

    strncpy_s(
        GlobalLoggerState.BaseFilePath,
        sizeof(GlobalLoggerState.BaseFilePath),
        Path,
        _TRUNCATE
    );

    PSTR Extension = strstr(GlobalLoggerState.BaseFilePath, DEFAULT_LOG_FILE_EXETENSION);
    if( Extension != NULL )
    {
        *Extension = '\0'; // Remove .txt extension if present
    }

    Extension = strstr(GlobalLoggerState.BaseFilePath, ".exe");
    if( Extension != NULL )
    {
        *Extension = '\0'; // Remove .exe extension if present
    }

    GlobalLoggerState.MaximumFileAgeDays = RetentionDays;
    GlobalLoggerState.Level = LOG_LEVEL_INFO; // Default log level

    SYSTEMTIME CurrentTime;
    GetLocalTime(&CurrentTime);

    if( !LoggerCreateFilename(&CurrentTime, GlobalLoggerState.CurrentFilename, sizeof(GlobalLoggerState.CurrentFilename)) )
    {
        DeleteCriticalSection(&GlobalLoggerState.Lock);
        return -1; // Failed to create initial filename
    }

    errno_t ErrorOpen = fopen_s(&GlobalLoggerState.FileStream, GlobalLoggerState.CurrentFilename, "a");
    if( ErrorOpen != 0 || GlobalLoggerState.FileStream == NULL )
    {
        DeleteCriticalSection(&GlobalLoggerState.Lock);
        return -1; // Failed to open log file
    }

    GlobalLoggerState.LastRotationTime = CurrentTime; // Set last rotation time to current time
    GlobalLoggerState.IsInitialized = TRUE;

    LoggerCleanUpDatedFiles(); // Clean up old files

    return 0; // Success
}

VOID
LoggerCleanUp(
    VOID
)
{

    if( !GlobalLoggerState.IsInitialized )
    {
        return; // Logger is not initialized
    }

    EnterCriticalSection( &GlobalLoggerState.Lock );

    if (GlobalLoggerState.FileStream != NULL && 
        GlobalLoggerState.FileStream != stdout && 
        GlobalLoggerState.FileStream != stderr )
    {
        fclose(GlobalLoggerState.FileStream);
    }   

    GlobalLoggerState.FileStream = NULL;
    GlobalLoggerState.IsInitialized = FALSE;

    LeaveCriticalSection(&GlobalLoggerState.Lock);
    DeleteCriticalSection(&GlobalLoggerState.Lock);

    memset(&GlobalLoggerState, 0, sizeof(GlobalLoggerState)); // Reset global logger state
}

VOID 
LoggerSetLevel(
    _In_ LOG_LEVEL Level
)
{
    EnterCriticalSection(&GlobalLoggerState.Lock);
    GlobalLoggerState.Level = Level;
    LeaveCriticalSection(&GlobalLoggerState.Lock);
}

LOG_LEVEL
LoggerGetLevel(
    VOID
)
{
    if( !GlobalLoggerState.IsInitialized )
    {
        return LOG_LEVEL_NONE; // Logger is not initialized, return NONE level
    }

    EnterCriticalSection(&GlobalLoggerState.Lock);
    LOG_LEVEL Level = GlobalLoggerState.Level;
    LeaveCriticalSection(&GlobalLoggerState.Lock);

    return Level;
}

BOOL
LoggerLevelEnabled(
    _In_ LOG_LEVEL Level
)
{
    if( !GlobalLoggerState.IsInitialized || Level < LOG_LEVEL_NONE || Level > LOG_LEVEL_FATAL )
    {
        return FALSE;
    }

    EnterCriticalSection(&GlobalLoggerState.Lock);
    BOOL IsEnabled = (Level >= GlobalLoggerState.Level);
    LeaveCriticalSection(&GlobalLoggerState.Lock);

    return IsEnabled;
}

VOID
LoggerWrite(
    _In_ LOG_LEVEL Level,
    _In_ PCSTR Filename,
    _In_ ULONG LineNumber,
    _In_ PCSTR Format,
    ...
)
{
    if ( !GlobalLoggerState.IsInitialized || !LoggerLevelEnabled( Level ) )
    {
        return; // level is lower than the current logger level, so do not log
    }

    va_list Args;
    va_start(Args, Format);
    LoggerWriteToFile(Level, Filename, LineNumber, Format, Args);
    va_end(Args);
}
