#include "ExternalIpAddress.hpp"
#include "IpAddrPort.hpp"
#include "Logger.hpp"

#include <ws2tcpip.h>
#include <winsock2.h>
#include <wininet.h>

#include <vector>

using namespace std;


// Unknown IP address
const string ExternalIpAddress::Unknown = "Unknown";

// Web services to query for external IP address
static const vector<string> ExternalIpServices =
{
    "http://checkip.amazonaws.com",
    "http://ipv4.wtfismyip.com/text",
    "http://ipv4.icanhazip.com",
    "http://ifcfg.net",
};


ExternalIpAddress::ExternalIpAddress ( Owner *owner ) : owner ( owner ) {}

void ExternalIpAddress::httpResponse ( HttpGet *httpGet, int code, const string& data, uint32_t remainingBytes )
{
    ASSERT ( _httpGet.get() == httpGet );

    LOG ( "Received HTTP response (%d): '%s'", code, data );

    if ( code != 200 || data.size() < 7 ) // Min IPv4 length, eg "1.1.1.1" TODO actually validate this
    {
        httpFailed ( httpGet );
        return;
    }

    address = trimmed ( data );

    _httpGet.reset();

    if ( owner )
        owner->externalIpAddrFound ( this, address );
}

void ExternalIpAddress::httpFailed ( HttpGet *httpGet )
{
    ASSERT ( _httpGet.get() == httpGet );

    LOG ( "HTTP GET failed for: %s", _httpGet->url );

    _httpGet.reset();

    if ( _nextQueryIndex == ExternalIpServices.size() )
    {
        address = Unknown;

        if ( owner )
            owner->externalIpAddrUnknown ( this );
        return;
    }

    _httpGet.reset ( new HttpGet ( this, ExternalIpServices[_nextQueryIndex++] ) );
    _httpGet->start();
}

void ExternalIpAddress::start()
{
    address.clear();

    _nextQueryIndex = 1;

    _httpGet.reset ( new HttpGet ( this, ExternalIpServices[0] ) );
    _httpGet->start();
}

void ExternalIpAddress::stop()
{
    _httpGet.reset();
}

std::vector<std::string> getInternalIpAddresses() {

    // this is just a system call, i can do it here. above is not done this way, bc threading, http delay, me not wanting to cause a stall on melty start
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return std::vector<std::string>({"Failed to get Internal IP"});
    }

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
        WSACleanup();
        return std::vector<std::string>({"Failed to get Internal IP"});
    }

    addrinfo hints = {};
    // Respect global IP version preference
    IpVersionPreference preference = getGlobalIpVersionPreference();
    switch ( preference )
    {
        case IpVersionPreference::IPv4Only:
            hints.ai_family = AF_INET;
            break;
        case IpVersionPreference::IPv6Only:
            hints.ai_family = AF_INET6;
            break;
        case IpVersionPreference::DualStack:
        default:
            hints.ai_family = AF_UNSPEC; // Allow both IPv4 and IPv6
            break;
    }
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addrResult = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &addrResult) != 0) {
        WSACleanup();
        return std::vector<std::string>({"Failed to get Internal IP"});
    }

    std::vector<std::string> res;
    for (addrinfo* ptr = addrResult; ptr != nullptr; ptr = ptr->ai_next) {
        if (ptr->ai_family == AF_INET) {
            sockaddr_in* sockaddr_ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(sockaddr_ipv4->sin_addr), ipStr, sizeof(ipStr));
            res.push_back(std::string(ipStr));
        }
        else if (ptr->ai_family == AF_INET6) {
            sockaddr_in6* sockaddr_ipv6 = reinterpret_cast<sockaddr_in6*>(ptr->ai_addr);
            char ipStr[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &(sockaddr_ipv6->sin6_addr), ipStr, sizeof(ipStr));
            res.push_back(std::string(ipStr));
        }
    }

    freeaddrinfo(addrResult);
    WSACleanup();

    return res;
}

