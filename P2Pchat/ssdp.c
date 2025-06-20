#include "ssdp.h"
#include "logger.h"

static
PCSTR SSDP_MSEARCH =
"M-SEARCH * HTTP/1.1\r\n"
"HOST: 239.255.255.250:1900\r\n"
"MAN: \"ssdp:discover\"\r\n"
"ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
"MX: 3\r\n\r\n";

static
PCSTR
SOAP_HEADER_TEMPLATE =
"POST %s HTTP/1.1\r\n"
"HOST: %s:%d\r\n"
"CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
"CONTENT-LENGTH: %d\r\n"
"SOAPACTION: \"urn:schemas-upnp-org:service:WANIPConnection:1#GetExternalIPAddress\"\r\n"
"\r\n";

static
PCSTR SOAP_CONTENT_TEMPLATE =
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
    SOCKET SsdpSocket;
    struct sockaddr_in MulticastAddress;
    INT64 BytesReceived = 0;
    PSTR LocationUrl = NULL;

    // Create a UDP socket for SSDP discovery
    SsdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (SsdpSocket == INVALID_SOCKET)
    {
        LOG_DEBUG("Failed to create SSDP socket: %d\n", WSAGetLastError());
        return FALSE;
    }

    // Set socket to allow reuse of address
    BOOL ReuseAddr = TRUE;
    if (setsockopt(SsdpSocket, SOL_SOCKET, SO_REUSEADDR, (PCSTR)&ReuseAddr, sizeof(ReuseAddr)) == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to set socket option SO_REUSEADDR: %d\n", WSAGetLastError());
        closesocket(SsdpSocket);
        return FALSE;
    }

    // Set socket timeout for receiving responses
    DWORD Timeout = SSDP_TIMEOUT; // 5 second timeout
    if (setsockopt(SsdpSocket, SOL_SOCKET, SO_RCVTIMEO, (PCSTR)&Timeout, sizeof(Timeout)) == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to set socket timeout: %d\n", WSAGetLastError());
        closesocket(SsdpSocket);
        return FALSE;
    }

    // Setup multicast address for SSDP
    memset(&MulticastAddress, 0, sizeof(MulticastAddress));
    MulticastAddress.sin_family = AF_INET;
    MulticastAddress.sin_port = htons(SSDP_PORT); // SSDP port 1900
    inet_pton(AF_INET, SSDP_MULTICAST, &MulticastAddress.sin_addr);

    // Send M-SEARCH request
    if (sendto(
        SsdpSocket,
        SSDP_MSEARCH,
        (INT)strlen(SSDP_MSEARCH),
        0,
        (struct sockaddr*)&MulticastAddress,
        sizeof(MulticastAddress)
    ) == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to send SSDP M-SEARCH request: %d\n", WSAGetLastError());
        closesocket(SsdpSocket);
        return FALSE;
    }

    LOG_INFO("Sent SSDP M-SEARCH request to %s:%d\n", SSDP_MULTICAST, SSDP_PORT);

    CHAR SsdpResponse[SSDP_MAX_RESPONSE_SIZE];
    struct sockaddr_in ResponseAddress;
    INT ResponseAddressSize = sizeof(ResponseAddress);

    // Wait for responses
    while (TRUE)
    {
        BytesReceived = recvfrom(
            SsdpSocket,
            SsdpResponse,
            sizeof(SsdpResponse) - 1, // leave space for null terminator
            0,
            (struct sockaddr*)&ResponseAddress,
            &ResponseAddressSize
        );

        if (BytesReceived == SOCKET_ERROR)
        {
            INT Error = WSAGetLastError();
            if (Error == WSAETIMEDOUT)
            {
                LOG_TRACE("No more SSDP responses received, timeout reached.\n");
                break;
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
        inet_ntop(AF_INET, &(ResponseAddress.sin_addr), AddrStr, INET_ADDRSTRLEN);
        LOG_INFO("Received SSDP response from %s:%d\n", AddrStr, ntohs(ResponseAddress.sin_port));

        // Check if the response contains a location URL
        LocationUrl = XmlGetLocationUrl(SsdpResponse);
        if (LocationUrl)
        {
            LOG_TRACE("Found UPnP device at location: %s\n", LocationUrl);
            // Parse the URL to get host, path, and port
            if (ParseUrl(LocationUrl, pDevice->Host, pDevice->Path, &pDevice->Port))
            {
                // Store the control URL for SOAP requests
                snprintf(pDevice->ControlUrl, SSDP_MAX_URL_SIZE, "%s", pDevice->Path);
                free(LocationUrl);
                closesocket(SsdpSocket);
                return TRUE; // successfully discovered UPnP device
            }
            else
            {
                LOG_DEBUG("Failed to parse location URL: %s\n", LocationUrl);
                free(LocationUrl);
            }
        }
    }

    closesocket(SsdpSocket);
    return FALSE;
}

BOOL
GetDeviceDescription(
    _In_ PUPNP_DEVICE pDevice
)
{
    SOCKET HttpSocket;
    struct sockaddr_in ServerAddress;
    CHAR Request[1024];
    CHAR Response[SSDP_MAX_RESPONSE_SIZE];
    INT BytesSent;
    INT BytesReceived;
    PSTR ControlUrl;

    HttpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (HttpSocket == INVALID_SOCKET)
    {
        LOG_DEBUG("Failed to create HTTP socket: %d\n", WSAGetLastError());
        return FALSE;
    }

    memset(&ServerAddress, 0, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    ServerAddress.sin_port = htons(pDevice->Port);
    // FIXED: Use Host instead of Port for inet_pton
    inet_pton(AF_INET, pDevice->Host, &ServerAddress.sin_addr);

    if (connect(HttpSocket, (struct sockaddr*)&ServerAddress, sizeof(ServerAddress)) == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to connect to device: %d\n", WSAGetLastError());
        closesocket(HttpSocket);
        return FALSE;
    }

    snprintf(Request, sizeof(Request),
        "GET %s HTTP/1.1\r\n"
        "HOST: %s:%d\r\n"
        "CONNECTION: close\r\n"
        "\r\n",
        pDevice->Path, pDevice->Host, pDevice->Port
    );

    BytesSent = send(HttpSocket, Request, (INT)strlen(Request), 0);
    if (BytesSent == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to send HTTP request: %d\n", WSAGetLastError());
        closesocket(HttpSocket);
        return FALSE;
    }

    BytesReceived = recv(HttpSocket, Response, sizeof(Response) - 1, 0);
    if (BytesReceived == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to receive HTTP response: %d\n", WSAGetLastError());
        closesocket(HttpSocket);
        return FALSE;
    }

    Response[BytesReceived] = '\0';
    closesocket(HttpSocket);

    // Look for WANIPConnection service
    ControlUrl = XmlGetValue(
        Response,
        "<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>",
        "</service>"
    );

    if (ControlUrl)
    {
        PSTR UrlStart = strstr(ControlUrl, "<controlURL>");
        if (UrlStart == NULL)
        {
            // Try case variations
            UrlStart = strstr(ControlUrl, "<ControlURL>");
            if (UrlStart == NULL)
            {
                free(ControlUrl);
                return FALSE;
            }
        }
        UrlStart += 12; // strlen("<controlURL>")

        PSTR UrlEnd = strstr(UrlStart, "</controlURL>");
        if (UrlEnd == NULL)
        {
            UrlEnd = strstr(UrlStart, "</ControlURL>");
            if (UrlEnd == NULL)
            {
                free(ControlUrl);
                return FALSE;
            }
        }

        INT64 UrlLen = UrlEnd - UrlStart;
        if (UrlLen < sizeof(pDevice->ControlUrl))
        {
            strncpy_s(pDevice->ControlUrl, SSDP_MAX_URL_SIZE, UrlStart, UrlLen);
            pDevice->ControlUrl[UrlLen] = '\0';
        }

        free(ControlUrl);
    }

    return TRUE;
}

BOOL
GetPublicIpAddress(
    _In_ PUPNP_DEVICE pDevice,
    _Out_ PSTR PublicIpBuffer,
    _In_ INT64 BufferSize
)
{
    SOCKET HttpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (HttpSocket == INVALID_SOCKET)
    {
        LOG_DEBUG("Failed to create HTTP socket: %d\n", WSAGetLastError());
        return FALSE;
    }

    struct sockaddr_in ServerAddress;
    memset(&ServerAddress, 0, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    ServerAddress.sin_port = htons(pDevice->Port);
    inet_pton(AF_INET, pDevice->Host, &ServerAddress.sin_addr);

    if (connect(HttpSocket, (struct sockaddr*)&ServerAddress, sizeof(ServerAddress)) == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to connect to device: %d\n", WSAGetLastError());
        closesocket(HttpSocket);
        return FALSE;
    }

    CHAR Request[2048];
    snprintf(
        Request,
        sizeof(Request),
        "POST %s HTTP/1.1\r\n"
        "HOST: %s:%d\r\n"
        "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
        "CONTENT-LENGTH: %d\r\n"
        "SOAPACTION: \"urn:schemas-upnp-org:service:WANIPConnection:1#GetExternalIPAddress\"\r\n"
        "\r\n"
        "%s",
        pDevice->ControlUrl, pDevice->Host, pDevice->Port,
        (INT)strlen(SOAP_CONTENT_TEMPLATE),
        SOAP_CONTENT_TEMPLATE
    );

    INT BytesSent = send(HttpSocket, Request, (INT)strlen(Request), 0);
    if (BytesSent == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to send HTTP request: %d\n", WSAGetLastError());
        closesocket(HttpSocket);
        return FALSE;
    }

    CHAR Response[SSDP_MAX_RESPONSE_SIZE];
    INT BytesReceived = recv(HttpSocket, Response, sizeof(Response) - 1, 0);
    if (BytesReceived == SOCKET_ERROR)
    {
        LOG_DEBUG("Failed to receive HTTP response: %d\n", WSAGetLastError());
        closesocket(HttpSocket);
        return FALSE;
    }

    Response[BytesReceived] = '\0';
    closesocket(HttpSocket);

    PSTR IpAddress = XmlGetValue(
        Response,
        "<NewExternalIPAddress>",
        "</NewExternalIPAddress>"
    );

    if (IpAddress == NULL)
    {
        LOG_DEBUG("Failed to parse public IP address from response.\n");
        return FALSE;
    }

    strncpy_s(PublicIpBuffer, BufferSize, IpAddress, _TRUNCATE);
    PublicIpBuffer[BufferSize - 1] = '\0'; // ensure null termination
    free(IpAddress);

    LOG_INFO("Public IP Address: %s\n", PublicIpBuffer);

    return TRUE;
}

// XML parsing functions

PSTR
XmlGetValue(
    _In_ PCSTR Xml,
    _In_ PCSTR StartTag,
    _In_ PCSTR EndTag
)
{
    PSTR Start = strstr(Xml, StartTag);
    if (Start == NULL)
    {
        return NULL;
    }

    Start += strlen(StartTag);
    PSTR End = strstr(Start, EndTag);
    if (End == NULL)
    {
        return NULL;
    }

    INT64 Length = End - Start;
    PSTR Value = malloc(Length + 1);
    if (Value == NULL)
    {
        return NULL;
    }

    strncpy_s(Value, Length + 1, Start, Length);
    Value[Length] = '\0';
    return Value;
}

PSTR
XmlGetLocationUrl(
    _In_ PCSTR Xml
)
{
    PSTR Start = strstr(Xml, "location:");
    if (Start == NULL)
    {
        Start = strstr(Xml, "Location:"); // case insensitive
        if (Start == NULL)
        {
            Start = strstr(Xml, "LOCATION:");
            if (Start == NULL)
            {
                return NULL;
            }
        }
    }

    PSTR UrlStart = strchr(Start, ':');
    if (UrlStart == NULL)
    {
        return NULL;
    }
    UrlStart++; // move past the colon

    while (*UrlStart == ' ' || *UrlStart == '\t') // skip whitespace
    {
        UrlStart++;
    }

    PSTR UrlEnd = UrlStart;
    while (*UrlEnd != '\r' && *UrlEnd != '\n' && *UrlEnd != '\0') // find end of URL
    {
        UrlEnd++;
    }

    INT64 UrlLength = UrlEnd - UrlStart;
    PSTR Url = malloc(UrlLength + 1);
    if (Url == NULL)
    {
        return NULL;
    }

    strncpy_s(Url, UrlLength + 1, UrlStart, UrlLength);
    Url[UrlLength] = '\0'; // null terminate the URL
    return Url;
}

BOOL
ParseUrl(
    _In_ PCSTR Url,
    _Out_ PSTR Host,
    _Out_ PSTR Path,
    _Out_ PINT16 Port
)
{
    if (strncmp(Url, "http://", 7) != 0)
    {
        LOG_DEBUG("URL does not start with http://\n");
        return FALSE;
    }

    PSTR HostStart = (PSTR)(Url + 7); // skip "http://"
    PSTR PortStart = strchr(HostStart, ':');
    PSTR PathStart = strchr(HostStart, '/');

    if (PathStart == NULL)
    {
        LOG_DEBUG("Invalid URL format, no path found.\n");
        return FALSE;
    }

    INT64 HostLength = 0;
    if (PortStart != NULL && PortStart < PathStart)
    {
        HostLength = PortStart - HostStart;
        *Port = (INT16)atoi(PortStart + 1); // Port is after the colon
    }
    else
    {
        HostLength = PathStart - HostStart;
        *Port = 80; // default HTTP port
    }

    strncpy_s(Host, SSDP_MAX_URL_SIZE, HostStart, HostLength);
    Host[HostLength] = '\0'; // null terminate the host string

    strcpy_s(Path, SSDP_MAX_URL_SIZE, PathStart); // copy path

    return TRUE;
}