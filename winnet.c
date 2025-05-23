#include "winnet.h"





BOOL InitWinSock(

)
{
    WSADATA wsadata;
    LONG result;
    // Initialize Winsock
    result = WSAStartup(MAKEWORD(2, 2), &wsadata);
    if (result != 0)
    {
        printf("WSAStartup failed: %d\n", result);
        return FALSE;
    }
    return TRUE;
}
