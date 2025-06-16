#include "ssdp.h"
#include "logger.h"

static
PCSTR SSDP_MSEARCH =
"M-SEARCH * HTTP/1.1\r\n"
"HOST: " SSDP_MULTICAST ":" "1900" "r\n"
"MAN: \"ssdp:discover\"\r\n"
"ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
"MX: 3\r\n\r\n";

static
PCSTR SOAP_REQUEST_TEMPLATE =
"POST %s HTTP/1.1\r\n"
"HOST: %s:%d\r\n"
"CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
"CONTENT-LENGTH: %d\r\n"
"SOAPACTION: \"urn:schemas-upnp-org:service:WANIPConnection:1#GetExternalIPAddress\"\r\n"
"\r\n"
"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
"<s:Envelope s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">\r\n"
"<s:Body>\r\n"
"<u:GetExternalIPAddress xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\r\n"
"</u:GetExternalIPAddress>\r\n"
"</s:Body>\r\n"
"</s:Envelope>\r\n";

BOOL
DiscoverUPnPDevice(
    _Out_ PUPNP_DEVICE pDevice
)
{
    SOCKET SsdpSocket = INVALID_SOCKET;
    struct sockaddr_in  MulticastAddress;
    struct sockaddr_in  LocalAddress;
    INT64 LocalAddressSize = sizeof(LocalAddress);
    CHAR SsdpResponse[SSDP_MAX_RESPONSE_SIZE];
    INT64 BytesReceived = 0;
    PSTR LocationUrl = NULL;

    // create a UDP socket for SSDP discovery
    SsdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (SsdpSocket == INVALID_SOCKET)
    {
        LOG_DEBUG("Failed to create SSDP socket: %d\n", WSAGetLastError());
        return FALSE;
    }

    DWORD Timeout = SSDP_TIMEOUT; // 5 second timeout for receiving responses
    if (setsockopt(SsdpSocket, SOL_SOCKET, SO_RCVTIMEO, (PCSTR)&Timeout, sizeof(Timeout)) == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to set socket timeout: %d\n", WSAGetLastError());
        closesocket(SsdpSocket);
        return FALSE;
    }

    // setup a multicast address for SSDP, this is the standard address for SSDP
    memset(&MulticastAddress, 0, sizeof(MulticastAddress));
    MulticastAddress.sin_family = AF_INET;
    MulticastAddress.sin_port = htons(SSDP_PORT); // SSDP port 1900
    inet_pton(AF_INET, SSDP_MULTICAST, &MulticastAddress.sin_addr);

    if (sendto(
            SsdpSocket, 
            SSDP_MSEARCH, 
            (INT)strlen(SSDP_MSEARCH),
            0,
            (struct sockaddr*)&MulticastAddress,
            sizeof(MulticastAddress)
        ) == SOCKET_ERROR )
    {
        LOG_DEBUG("Failed to send SSDP M-SEARCH request: %d\n", WSAGetLastError());
        closesocket(SsdpSocket);
        return FALSE;
    }

    LOG_INFO("Sent SSDP M-SEARCH request to %s:%d\n", SSDP_MULTICAST, SSDP_PORT);

    while( TRUE )
    {
        BytesReceived = recvfrom(
            SsdpSocket,
            SsdpResponse,
            sizeof(SsdpResponse) - 1, // leave space for null terminator
            0,
            (struct sockaddr*)&LocalAddress,
            &(INT)LocalAddressSize
        );

        if (BytesReceived == SOCKET_ERROR)
        {
            INT Error = WSAGetLastError();
            if (Error == WSAETIMEDOUT)
            {
                LOG_TRACE("No more SSDP responses received, timeout reached.\n");
                break; // no more responses
            }
            else
            {
                LOG_DEBUG("Error receiving SSDP response: %d\n", Error);
                closesocket(SsdpSocket);
                return FALSE;
            }
        }

        SsdpResponse[BytesReceived] = '\0'; // null terminate response

        CHAR AddrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(LocalAddress.sin_addr), AddrStr, INET_ADDRSTRLEN);
        LOG_INFO("Received SSDP response from %s:%d\n", AddrStr, ntohs(LocalAddress.sin_port));

        // Check if the response contains a location URL
        LocationUrl = XmlGetLocationUrl(SsdpResponse);
        if (LocationUrl)
        {
            LOG_TRACE("Found UPnP device at location: %s\n", LocationUrl);
            // Parse the URL to get host, path, and port
            if (ParseUrl(LocationUrl, pDevice->Host, pDevice->Path, &pDevice->Port))
            {
                // Store the control URL for SOAP requests
                snprintf(pDevice->ControlUrl, SSDP_MAX_URL_SIZE, "%s%s", pDevice->Path, "/control");
                free(LocationUrl);
                closesocket(SsdpSocket);
                return TRUE; // successfully discovered UPnP device
            }
            else
            {
                LOG_DEBUG("Failed to parse location URL: %s\n", LocationUrl);
            }
        }
    }

   /* strchr();*/

    closesocket(SsdpSocket);
    return FALSE;
};

BOOL
GetDeviceDescription(
    _In_  PUPNP_DEVICE pDevice
)
{
    SOCKET HttpSocket;
    struct sockaddr_in ServerAddress;
    CHAR Request[1024];
    CHAR Response[SSDP_MAX_RESPONSE_SIZE];
    INT BytesSent;
    INT BytesRecieved;
    PSTR ControlUrl;

    HttpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (HttpSocket == INVALID_SOCKET) 
    {
        LOG_DEBUG( "Failed to create HTTP socket: %d\n", WSAGetLastError() );
        return FALSE;
    }

    memset(&ServerAddress, 0, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    ServerAddress.sin_port = htons(pDevice->Port);
    inet_pton(AF_INET, pDevice->Port, &ServerAddress.sin_addr);

    if (connect(HttpSocket, (struct sockaddr*)&ServerAddress, sizeof(ServerAddress)) == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to connect to device %d\n", WSAGetLastError() );
        closesocket(HttpSocket);
        return FALSE;
    }

    snprintf( Request, sizeof(Request), 
         "GET %s HTTP/1.1\r\n"
         "HOST: %s:%d\r\n"
         "CONNECTION: close\r\n"
         "\r\n",
        pDevice->Path, pDevice->Host, pDevice->Port
    );
     
    BytesSent = send(HttpSocket, Request, (INT)strlen(Request), NULL);
    if (BytesSent == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to recieve HTTP request %d\n", WSAGetLastError());
        closesocket(HttpSocket);
        return FALSE;
    }

    BytesRecieved = recv( HttpSocket, Response, sizeof( Response ) - 1, NULL );
    if( BytesRecieved == SOCKET_ERROR )
    {
        LOG_DEBUG("Failed to recieve HTTP response %d\n", WSAGetLastError());
        closesocket(HttpSocket);
        return FALSE;
    }

    Response[BytesRecieved] = '\0';
    closesocket(HttpSocket);

    ControlUrl = XmlGetValue(
        Response,
        "<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>",
        "</service>"
    );

    if (ControlUrl)
    {
        PSTR UrlStart = strstr(ControlUrl, "<ControlUrl>");
        if (UrlStart == NULL)
        {
            return FALSE;
        }
        UrlStart += 12; // strlen("<controlURL>")

        PSTR UrlEnd = strstr(UrlStart, "</controlURL>");
        if (UrlEnd == NULL)
        {
            return FALSE;
        }

        INT64 UrlLen = UrlEnd - UrlStart;
        if( UrlLen < sizeof( pDevice->ControlUrl ))
        {
            strncpy(pDevice->ControlUrl, UrlStart, UrlLen);
            pDevice->ControlUrl[UrlLen] = '\0';
        }

        free(ControlUrl);
    }

    return TRUE;
}


BOOL 
GetPublicIpAddress(
    _In_  PUPNP_DEVICE pDevice,
    _Out_ PSTR PublicIpBuffer,
    _In_  INT64 BufferSize
)
{
    return TRUE;
}


// XML parsing   

PSTR 
XmlGetValue(
    _In_ PCSTR Xml,
    _In_ PCSTR RootTag,
    _In_ PCSTR EndTag
)
{
    PSTR Start = strstr( Xml, RootTag );
    if ( Start == NULL )
    {
        return NULL;
    }

    Start += strlen(RootTag);
    PSTR End = strstr(Start, EndTag);
    if ( End == NULL )
    {
        return NULL;
    }

    INT64 Length = End - Start;
    PSTR Value = malloc( Length + 1 );
    if ( Value == NULL )
    {
        free(Value);
        return NULL;
    }

    strncpy( Value, Start, Length );
    Value[Length] = '\0';
    return Value;
}


PSTR 
XmlGetLocationUrl(
    _In_ PCSTR Xml
)
{
    return NULL;
}

BOOL 
ParseUrl(
    _In_ PCSTR Url,
    _Out_ PSTR Host,
    _Out_ PSTR Path,
    _Out_ PINT16 Port
)
{
    return FALSE;
}