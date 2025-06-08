#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <windows.h>

typedef enum
{
    LOG_LEVEL_NONE  = 0,
    LOG_LEVEL_TRACE = 1,
    LOG_LEVEL_DEBUG = 2,
    LOG_LEVEL_INFO  = 3,
    LOG_LEVEL_WARN  = 4,
    LOG_LEVEL_ERROR = 5,
    LOG_LEVEL_FATAL = 6
} LOG_LEVEL;

static
__forceinline
PCSTR
ShortFileName(
    PCSTR Path
)
{
    if (Path == NULL || *Path == '\0')
    {
        return "error";
    }

    PCSTR p = strrchr(Path, '\\');
    if (p == NULL)
    {
        p = strrchr(Path, '/');
    }

    return (p != NULL) ? p + 1 : Path;
}
#define FILENAME ( ShortFileName( __FILE__ ) )

#define LOG_TRACE( fmt, ... )                                                         \
    do {                                                                              \
        LoggerWrite( LOG_LEVEL_TRACE, FILENAME, __LINE__, fmt, ##__VA_ARGS__ )        \
    }  while (0)

#define LOG_DEBUG( fmt, ... )                                                         \
    do {                                                                              \
        LoggerWrite( LOG_LEVEL_DEBUG, FILENAME, __LINE__, fmt, ##__VA_ARGS__ )        \
    } while (0)

#define LOG_INFO(  fmt, ... )                                                         \
    do {                                                                              \
        LoggerWrite( LOG_LEVEL_INFO, FILENAME, __LINE__, fmt, ##__VA_ARGS__ )         \
    } while (0)

#define LOG_WARN(  fmt, ... )                                                         \
    do {                                                                              \
        LoggerWrite( LOG_LEVEL_WARN, FILENAME, __LINE__, fmt, ##__VA_ARGS__ )         \
    } while (0)

#define LOG_ERROR( fmt, ... )                                                         \
    do {                                                                              \
        LoggerWrite( LOG_LEVEL_ERROR, FILENAME, __LINE__, fmt, ##__VA_ARGS__ )        \
    } while (0)

#define LOG_FATAL( fmt, ... )                                                         \
    do {                                                                              \
        LoggerWrite( LOG_LEVEL_DEBUG, FILENAME, __LINE__, fmt, ##__VA_ARGS__ )        \
    } while (0)

/**
*
*/
INT64
LoggerInitConsole(
    _In_ FILE* Filestream
);


/**
*
*/
VOID 
LoggerCleanUp(
    VOID
);


/**
*
*/
INT64 
LoggerInitFile(
    _In_ PCSTR Path,
    _In_ INT64 RetentionDays
);

/**
*
*/
VOID
LoggerSetLevel(
    _In_ LOG_LEVEL Level
);


/**
*
*/
LOG_LEVEL 
LoggerGetLevel(
    VOID
);

/**
*
*/
BOOL
LoggerLevelEnabled(
    _In_ LOG_LEVEL Level
);


VOID
LoggerWrite(
    _In_ LOG_LEVEL Level,
    _In_ PCSTR File,
    _In_ ULONG Line,
    _In_ PCSTR Format,
    ...
);

#endif // !LOGGER_H

