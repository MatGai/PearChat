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


/**
*  Initalises the winsock dll.
* 
*  @returns TRUE if successful, FALSE otherwise.
*/
extern BOOL InitWinSock(

);


#endif // !WINNET_H