#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


#include "winnet.h"


#define DEFAULT_PORT "5050"
#define DEFAULT_IP "162.55.179.66"
#define MAX_BUFFER_SIZE 1024
#define CONNECTION_TIMEOUT 10000

/**
* 
*/
static
INT
GetLine(
    _In_    PSTR Prompt,
    _Inout_ PSTR Buffer,
    _In_    INT  Size
);

/**
*
*/
BOOL 
ConnectToServer(
    _In_  PCSTR ServerIp,
    _In_  PCSTR ServerPort,
    _Out_ SOCKET* ConnectSocket
);

/**
*
*/
VOID 
CleanUpConnection(
    SOCKET Socket
);


INT main(
    VOID
)
{
    PCSTR ServerIp   = DEFAULT_IP;
    PCSTR ServerPort = DEFAULT_PORT;

    // Initialise Winsock dll
    if( !InitWinSock() )
    {
        printf("Failed to initalise winsock\n");
        return 1;
    }

    SOCKET ConnectServer = NULL;
    if( !ConnectToServer( ServerIp, ServerPort, &ConnectServer ) )
    {
        CleanUpWinSock();
        return 1;
    }

    printf( "Connected to server at %s:%s\n", ServerIp, ServerPort );
    printf( "Type 'exit' to quit\n\n" );

    CHAR SendBuffer[MAX_BUFFER_SIZE];
    CHAR RecvBuffer[MAX_BUFFER_SIZE];

    INT Result;
    BOOL Connected = TRUE;

    while( Connected )
    {
        Result = GetLine( "Enter message to send: ", SendBuffer, sizeof( SendBuffer ) );

        if( Result )
        {
            printf( "Either no input or input too large (max %d characters)\n", MAX_BUFFER_SIZE - 1 );
            continue;
        }

        if( _stricmp(SendBuffer, "exit") == 0 )
        {
            Connected = FALSE;
        }

        INT TotalBytesSent = 0;
        INT BytesRemaining = (INT)strlen( SendBuffer );

        while( TotalBytesSent < BytesRemaining )
        {
            INT BytesSent = send( 
                ConnectServer, 
                SendBuffer + TotalBytesSent, 
                BytesRemaining - TotalBytesSent, 
                0 
            );

            if( BytesSent == SOCKET_ERROR )
            {
                printf( "Send failed: %d\n", WSAGetLastError( ) );
                Connected = FALSE;
                break;
            }

            TotalBytesSent += BytesSent;

        }

        if( !Connected )
        {
            break;
        }

        ZeroMemory( RecvBuffer, sizeof( RecvBuffer ) );
        INT BytesRecieved = recv( ConnectServer, RecvBuffer, sizeof( RecvBuffer ) - 1, 0 );

        if( BytesRecieved > 0 )
        {
            RecvBuffer[BytesRecieved] = '\0';
            printf( "Recieved '%s' from %s:%s\n", RecvBuffer, ServerIp, ServerPort );
        }
        else if ( BytesRecieved == 0 )
        {
            printf( "Connection closed by server\n" );
            Connected = FALSE;
        }
        else
        {
            printf( "Recieve failed: %d\n", WSAGetLastError( ) );
            Connected = FALSE;
        }

    }

    CleanUpConnection( ConnectServer );
    CleanUpWinSock( );

    printf( "Terminating...\n" );
    
    return 0;
}

static
INT
GetLine(
    PSTR Prompt,
    PSTR Buffer,
    INT Size
)
{
    INT Ch, ExtraChars;

    // display prompt if provided
    if (Prompt != NULL)
    {
        printf("%s", Prompt);
        fflush(stdout);
    }

    // get line with buffer overflow protection
    if (fgets(Buffer, Size, stdin) == NULL)
    {
        return 1;
    }

    // check for line too long (no newline)
    size_t InputLength = strlen(Buffer);
    if (Buffer[InputLength - 1] != '\n')
    {
        ExtraChars = 0;
        // clear excess input
        while (((Ch = getchar()) != '\n') && (Ch != EOF))
        {
            ExtraChars = 1;
        }
        return (ExtraChars == 1) ? 1 : 0;
    }

    // remove trailing newline
    Buffer[InputLength - 1] = '\0';
    return 0;
}

BOOL 
ConnectToServer(
    _In_  PCSTR ServerIp,
    _In_  PCSTR ServerPort,
    _Out_ SOCKET* ConnectSocket
)
{
    struct addrinfo* Result = NULL, * Ptr = NULL, Hints;
    INT ErrResult; // used to validate winsock function return values

    ZeroMemory(&Hints, sizeof(Hints));
    Hints.ai_family   = AF_UNSPEC;   // allow IPv4 or IPv6 addressing
    Hints.ai_socktype = SOCK_STREAM; // stream socket
    Hints.ai_protocol = IPPROTO_TCP; // use TCP protocol

    ErrResult = getaddrinfo(ServerIp, ServerPort, &Hints, &Result);
    if (ErrResult != 0)
    {
        printf("GetAddrInfo failed: %d\n", ErrResult);
        return FALSE;
    }

    for( Ptr = Result; Ptr != NULL; Ptr = Ptr->ai_next )
    {
        *ConnectSocket = socket( Ptr->ai_family, Ptr->ai_socktype, Ptr->ai_protocol );
        if( *ConnectSocket == INVALID_SOCKET )
        {
            printf("Error creating socket: %d\n", WSAGetLastError());
            freeaddrinfo(Result);
            return FALSE;
        }

        // set timeout for when attempting to connect
        DWORD Timeout = CONNECTION_TIMEOUT;
        if( setsockopt( *ConnectSocket, SOL_SOCKET, SO_RCVTIMEO, (PCSTR)&Timeout, sizeof( Timeout ) ) == SOCKET_ERROR )
        {
            printf( "Failed to set connection timeout: %d\n", WSAGetLastError( ) );
        }

        // try connect to server
        ErrResult = connect( *ConnectSocket, Ptr->ai_addr, (INT)Ptr->ai_addrlen );
        if( ErrResult == SOCKET_ERROR )
        {
            closesocket( *ConnectSocket );
            *ConnectSocket = INVALID_SOCKET;
            continue; // try next address
        }

        break; // connection successful

    }

    freeaddrinfo( Result ); // always remember to free stuff!

    if( *ConnectSocket == INVALID_SOCKET )
    {
        printf( "Unable to connect to server at %s:%s\n", ServerIp, ServerPort );
        return FALSE;
    }

    return TRUE;
}


VOID
CleanUpConnection(
    SOCKET Socket
)
{
    if( shutdown( Socket, SD_SEND) == SOCKET_ERROR )
    {
        printf("Shutdown failed: %d\n", WSAGetLastError());
    }

    closesocket( Socket );
}
