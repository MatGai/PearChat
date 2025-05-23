#include "winnet.h"

BOOL 
InitWinSock(
    VOID
)
{
    WSADATA WsaData;
    INT Result;

    // initialize winsock dll
    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Result != 0)
    {
        printf("WSAStartup failed: %d\n", Result);
        return FALSE;
    }

    // check if winsock dll supports ver 2.2
    if( LOBYTE( WsaData.wVersion ) != 2 || HIBYTE(WsaData.wVersion) != 2 )
    {
        printf("Win sock dll does not support 2.2");
        CleanUpWinSock();
        return FALSE;
    }

    return TRUE;
}

VOID CleanUpWinSock(
    VOID
)
{
    WSACleanup();
}