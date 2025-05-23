#ifndef WINNET_H
#define WINNET_H


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib") // For WSAIoctl
#pragma comment(lib, "Iphlpapi.lib") // For GetAdaptersAddresses

/**
*  Initalises the winsock dll.
*
*  @returns TRUE if successful, FALSE otherwise.
*/
BOOL InitWinSock(

);

/**
* Calls win sock dll clean up routines (WSACleanup) as-well as internel cleanup.
*
* @return TRUE if successful, FALSE otherwise.
*/
BOOL CleanUpWinSock(

);



#endif // !WINNET_H