#include "winnet.h"

#define DEFAULT_IP  "0.0.0.0"
#define DEFAULT_PORT 5050
#define MAX_BUFFER_SIZE 1024
#define MAX_CLIENTS 10 

typedef struct _CLIENT_INFO
{
    SOCKET SocketHandle;
    struct sockaddr_in Address;
    CHAR IpAddress[INET_ADDRSTRLEN];
    BOOL ThreadStarted;  // Add flag to ensure thread has started
} CLIENT_INFO, * PCLIENT_INFO;

/**
 * Client thread function
 */
DWORD
WINAPI
ClientThread(
    _In_ LPVOID lpData
);

/**
 * Initialize server socket
 */
BOOL
InitialiseServer(
    _In_ SOCKET* pServerSocket,
    _In_ INT Port
);

/**
 * Clean up client resources
 */
VOID
CleanUpClient(
    _In_ PCLIENT_INFO pClient
);

INT main(
    VOID
)
{
    PCSTR ServerIp = DEFAULT_IP;
    INT ServerPort = DEFAULT_PORT;

    if (!InitWinSock())
    {
        printf("Failed to initialise WinSock\n");
        return -1;
    }

    SOCKET ServerSocket = INVALID_SOCKET;
    if (!InitialiseServer(&ServerSocket, ServerPort))
    {
        CleanUpWinSock();
        return -1;
    }

    printf("Server initialised. Listening on port %d...\n", ServerPort);

    while (TRUE)
    {
        SOCKET ClientSocket;
        struct sockaddr_in ClientAddress;
        INT ClientSize = sizeof(ClientAddress);

        printf("Waiting for client connection...\n");
        ClientSocket = accept(ServerSocket, (struct sockaddr*)&ClientAddress, &ClientSize);
        if (ClientSocket == INVALID_SOCKET)
        {
            INT error = WSAGetLastError();
            printf("Accepting client socket failed: %d\n", error);
            continue;
        }

        printf("Client socket accepted, creating client info...\n");

        PCLIENT_INFO pClientInfo = (PCLIENT_INFO)malloc(sizeof(CLIENT_INFO));
        if (pClientInfo == NULL)
        {
            printf("Allocation memory for client information has failed\n");
            closesocket(ClientSocket);
            continue;
        }

        memset(pClientInfo, 0, sizeof(CLIENT_INFO));
        pClientInfo->SocketHandle = ClientSocket;
        pClientInfo->Address = ClientAddress;
        pClientInfo->ThreadStarted = FALSE;

        // convert IP address to string
        if (inet_ntop(AF_INET, &(ClientAddress.sin_addr), pClientInfo->IpAddress, INET_ADDRSTRLEN) == NULL)
        {
            printf("Failed to convert IP address\n");
            strcpy_s(pClientInfo->IpAddress, INET_ADDRSTRLEN, "Unknown");
        }

        printf("Client connected from %s\n", pClientInfo->IpAddress);

        HANDLE hClientThread = CreateThread(
            NULL,
            0,
            ClientThread,
            (LPVOID)pClientInfo,
            0,
            NULL
        );

        if (hClientThread == NULL)
        {
            printf("Unable to create client thread: %d\n", GetLastError());
            free(pClientInfo);
            closesocket(ClientSocket);
        }
        else
        {
            printf("Client thread created successfully\n");
            // hacky, giving it a little bit of time before closing the handle
            Sleep(10);
            CloseHandle(hClientThread);
        }
    }

    closesocket(ServerSocket);
    CleanUpWinSock();
    return 0;
}

BOOL
InitialiseServer(
    _In_ SOCKET* pServerSocket,
    _In_ INT Port
)
{
    printf("Creating server socket...\n");
    *pServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*pServerSocket == INVALID_SOCKET)
    {
        printf("Unable to create server socket: %d\n", WSAGetLastError());
        return FALSE;
    }

    printf("Setting socket options...\n");
    // Set socket to reuse address to prevent 'address already in use' errors
    INT OptVal = 1;
    if (setsockopt(*pServerSocket, SOL_SOCKET, SO_REUSEADDR, (PSTR)&OptVal, sizeof(OptVal)) == SOCKET_ERROR)
    {
        printf("setsockopt failed: %d\n", WSAGetLastError());
        closesocket(*pServerSocket);
        return FALSE;
    }

    printf("Binding to port %d...\n", Port);
    struct sockaddr_in ServerAddress;
    ZeroMemory(&ServerAddress, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    ServerAddress.sin_port = htons((u_short)Port);

    if (bind(*pServerSocket, (struct sockaddr*)&ServerAddress, sizeof(ServerAddress)) == SOCKET_ERROR)
    {
        printf("Cannot bind to port %d: %d\n", Port, WSAGetLastError());
        closesocket(*pServerSocket);
        return FALSE;
    }

    printf("Setting socket to listen...\n");
    if (listen(*pServerSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("Cannot put server into listen state: %d\n", WSAGetLastError());
        closesocket(*pServerSocket);
        return FALSE;
    }

    return TRUE;
}

DWORD
WINAPI
ClientThread(
    _In_ LPVOID lpData
)
{
    PCLIENT_INFO pClientInfo = (PCLIENT_INFO)lpData;

    if ( !pClientInfo )
    {
        printf("ERROR: ClientThread received NULL client info\n");
        return 1;
    }

    // Mark that the thread has started
    pClientInfo->ThreadStarted = TRUE;

    printf("Client thread started for %s...\n", pClientInfo->IpAddress);

    CHAR RecvBuffer[MAX_BUFFER_SIZE];
    INT BytesReceived;
    BOOL Connected = TRUE;

    // Set socket timeout 
    DWORD Timeout = 30000; // 30 seconds
    if (setsockopt(pClientInfo->SocketHandle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&Timeout, sizeof(Timeout)) == SOCKET_ERROR)
    {
        printf("Warning: Failed to set socket timeout for %s: %d\n", pClientInfo->IpAddress, WSAGetLastError());
    }

    printf("Starting message loop for client %s...\n", pClientInfo->IpAddress);

    while (Connected)
    {
        ZeroMemory(RecvBuffer, sizeof(RecvBuffer));

        BytesReceived = recv(pClientInfo->SocketHandle, RecvBuffer, sizeof(RecvBuffer) - 1, 0);

        if (BytesReceived > 0)
        {
            RecvBuffer[BytesReceived] = '\0';
            printf("Received '%s' from %s\n", RecvBuffer, pClientInfo->IpAddress);

            // Check for quit command
            if (_stricmp(RecvBuffer, "quit") == 0 || _stricmp(RecvBuffer, "exit") == 0)
            {
                printf("Client %s requested disconnect\n", pClientInfo->IpAddress);
                Connected = FALSE;
                continue;
            }

            // Echo the message back
            INT TotalBytesSent = 0;
            INT BytesToSend = BytesReceived;

            while (TotalBytesSent < BytesToSend)
            {
                INT BytesSent = send(
                    pClientInfo->SocketHandle,
                    RecvBuffer + TotalBytesSent,
                    BytesToSend - TotalBytesSent,
                    0
                );

                if (BytesSent == SOCKET_ERROR)
                {
                    printf("Error sending to %s: %d\n", pClientInfo->IpAddress, WSAGetLastError());
                    Connected = FALSE;
                    break;
                }

                TotalBytesSent += BytesSent;
            }
        }
        else if (BytesReceived == 0)
        {
            printf("Client %s disconnected gracefully\n", pClientInfo->IpAddress);
            Connected = FALSE;
        }
        else
        {
            INT Error = WSAGetLastError();
            if (Error == WSAETIMEDOUT)
            {
                printf("Connection to %s timed out\n", pClientInfo->IpAddress);
            }
            else if (Error == WSAECONNRESET)
            {
                printf("Connection to %s was reset\n", pClientInfo->IpAddress);
            }
            else
            {
                printf("Error receiving data from %s: %d\n", pClientInfo->IpAddress, Error);
            }
            Connected = FALSE;
        }
    }

    printf("Client thread ending for %s\n", pClientInfo->IpAddress);
    CleanUpClient(pClientInfo);
    return 0;
}

VOID
CleanUpClient(
    _In_ PCLIENT_INFO pClient
)
{
    if (pClient != NULL)
    {
        printf("Cleaning up client %s\n", pClient->IpAddress);

        // Shutdown the socket gracefully
        int result = shutdown(pClient->SocketHandle, SD_BOTH);
        if (result == SOCKET_ERROR)
        {
            printf("Shutdown failed for %s: %d\n", pClient->IpAddress, WSAGetLastError());
        }

        // Close the socket
        closesocket(pClient->SocketHandle);

        // Free the memory
        free(pClient);
        printf("Client cleanup completed\n");
    }
}