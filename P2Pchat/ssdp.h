#include "winnet.h"

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


typedef struct _UPNP_DEVICE 
{
    CHAR  Host[SSDP_MAX_URL_SIZE]; // Host URL
    CHAR  Path[SSDP_MAX_URL_SIZE]; 
    CHAR  ControlUrl[SSDP_MAX_URL_SIZE]; // Control URL for SOAP requests
    INT16 Port;
} UPNP_DEVICE, *PUPNP_DEVICE;


BOOL
DiscoverUPnPDevice(
    _Out_ PUPNP_DEVICE pDevice
);

BOOL 
GetDeviceDescription(
    _In_  PUPNP_DEVICE pDevice
);


BOOL GetPublicIpAddress(
    _In_  PUPNP_DEVICE pDevice,
    _Out_ PSTR PublicIpBuffer,
    _In_  INT BufferSize
);


// XML parsing   

PSTR XmlGetValue(
    _In_ PCSTR Xml,
    _In_ PCSTR RootTag,
    _In_ PCSTR EndTag
);


PSTR XmlGetLocationUrl(
    _In_ PCSTR Xml
);

BOOL ParseUrl(
    _In_ PCSTR Url,
    _Out_ PSTR Host,
    _Out_ PSTR Path,
    _Out_ PINT16 Port
);