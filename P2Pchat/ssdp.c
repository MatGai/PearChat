#include "ssdp.h"


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
        printf("Failed to create SSDP socket: %d\n", WSAGetLastError());
        return FALSE;
    }

    DWORD Timeout = SSDP_TIMEOUT; // 5 second timeout for receiving responses
    if (setsockopt(SsdpSocket, SOL_SOCKET, SO_RCVTIMEO, (PCSTR)&Timeout, sizeof(Timeout)) == SOCKET_ERROR)
    {
        printf("Failed to set socket timeout: %d\n", WSAGetLastError());
        closesocket(SsdpSocket);
        return FALSE;
    }

    // setup a multicast address for SSDP, this is the standard address for SSDP
    memset(&MulticastAddress, 0, sizeof(MulticastAddress));
    MulticastAddress.sin_family = AF_INET;
    MulticastAddress.sin_port = htons(SSDP_PORT); // SSDP port 1900
    //MulticastAddress.sin_addr.s_addr = inet_addr(SSDP_MULTICAST); // multicast address 239.255.255.250

    if (sendto(
            SsdpSocket, 
            SSDP_MSEARCH, 
            (INT)strlen(SSDP_MSEARCH),
            0,
            (struct sockaddr*)&MulticastAddress,
            sizeof(MulticastAddress)
        ) == SOCKET_ERROR )
    {
        printf("Failed to send SSDP M-SEARCH request: %d\n", WSAGetLastError());
        closesocket(SsdpSocket);
        return FALSE;
    }

    printf("Sent SSDP M-SEARCH request to %s:%d\n", SSDP_MULTICAST, SSDP_PORT);

    while( TRUE )
    {
        BytesReceived = recvfrom(
            SsdpSocket,
            SsdpResponse,
            sizeof(SsdpResponse) - 1, // leave space for null terminator
            0,
            (struct sockaddr*)&LocalAddress,
            &LocalAddressSize
        );

        if (BytesReceived == SOCKET_ERROR)
        {
            INT Error = WSAGetLastError();
            if (Error == WSAETIMEDOUT)
            {
                printf("No more SSDP responses received, timeout reached.\n");
                break; // no more responses
            }
            else
            {
                printf("Error receiving SSDP response: %d\n", Error);
                closesocket(SsdpSocket);
                return FALSE;
            }
        }

        SsdpResponse[BytesReceived] = '\0'; // null terminate response

        //printf("Received SSDP response from %s:%d\n", inet_ntoa(LocalAddress.sin_addr), ntohs(LocalAddress.sin_port));

        // Check if the response contains a location URL
        LocationUrl = XmlGetLocationUrl(SsdpResponse);
        if (LocationUrl)
        {
            printf("Found UPnP device at location: %s\n", LocationUrl);
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
                printf("Failed to parse location URL: %s\n", LocationUrl);
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
    return TRUE;
}


BOOL 
GetPublicIpAddress(
    _In_  PUPNP_DEVICE pDevice,
    _Out_ PSTR PublicIpBuffer,
    _In_  INT BufferSize
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
    return NULL;
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