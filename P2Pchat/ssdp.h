#include "winnet.h"
#include <malloc.h>

/**
    * UPnP SSDP (Simple Service Discovery Protocol) definitions
    *
    * This header file defines constants and structures used for discovering UPnP devices
    * and retrieving their public IP addresses using SSDP and SOAP requests on local networks. 
    * Mainly to be used in discovering host machine's public IP address.
*/

#define SSDP_PORT 1900
#define SSDP_MULTICAST "239.255.255.250"
#define SSDP_MAX_RESPONSE_SIZE 4096 // OxFFF bytes
#define SSDP_MAX_URL_SIZE 512
#define SSDP_TIMEOUT 5000 // 5 secondss




typedef struct _UPNP_DEVICE 
{
    CHAR  Host[SSDP_MAX_URL_SIZE]; // Host URL
    CHAR  Path[SSDP_MAX_URL_SIZE]; 
    CHAR  ControlUrl[SSDP_MAX_URL_SIZE]; // Control URL for SOAP requests
    INT16 Port;
} UPNP_DEVICE, *PUPNP_DEVICE;

/**
*
*/
BOOL
DiscoverUPnPDevice(
    _Out_ PUPNP_DEVICE pDevice
);

/**
*
*/
BOOL 
GetDeviceDescription(
    _In_  PUPNP_DEVICE pDevice
);

/**
*
*/
BOOL GetPublicIpAddress(
    _In_  PUPNP_DEVICE pDevice,
    _Out_ PSTR PublicIpBuffer,
    _In_  INT64 BufferSize
);

//////////////////////////////////////////
//
//             XML PARSING
//
//////////////////////////////////////////

/**
*
*/
PSTR XmlGetValue(
    _In_ PCSTR Xml,
    _In_ PCSTR RootTag,
    _In_ PCSTR EndTag
);

/**
*
*/
PSTR XmlGetLocationUrl(
    _In_ PCSTR Xml
);

/**
*
*/
BOOL ParseUrl(
    _In_ PCSTR Url,
    _Out_ PSTR Host,
    _Out_ PSTR Path,
    _Out_ PINT16 Port
);