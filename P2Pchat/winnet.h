#ifndef WINNET_H
#define WINNET_H


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib") // For WSAIoctl
#pragma comment(lib, "Iphlpapi.lib") // For GetAdaptersAddresses

typedef struct _MESSAGE_HEADER
{
    UINT16 Type;
    UINT16 Length;
    
} MESSAGE_HEADER, *PMESSAGE_HEADER ;

/**
*  Initalises the winsock dll.
* 
*  @returns TRUE if successful, FALSE otherwise.
*/
BOOL
InitWinSock(
    VOID
);

/**
* Calls win sock dll clean up routines (WSACleanup) as-well as internel cleanup.
* 
* @return TRUE if successful, FALSE otherwise.
*/
VOID 
CleanUpWinSock(
    VOID
);



#endif // !WINNET_H