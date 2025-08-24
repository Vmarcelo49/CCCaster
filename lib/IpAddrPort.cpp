#include "IpAddrPort.hpp"
#include "Exceptions.hpp"
#include "ErrorStrings.hpp"
#include "StringUtils.hpp"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <cctype>
#include <sstream>

using namespace std;

// Global IP version preference - use DualStack for better hostname resolution
static IpVersionPreference globalIpVersionPreference = IpVersionPreference::DualStack;

void setGlobalIpVersionPreference(IpVersionPreference preference)
{
    globalIpVersionPreference = preference;
}

IpVersionPreference getGlobalIpVersionPreference()
{
    return globalIpVersionPreference;
}

std::string getLoopbackAddress()
{
    IpVersionPreference preference = getGlobalIpVersionPreference();
    
    switch ( preference )
    {
        case IpVersionPreference::IPv4Only:
            return "127.0.0.1";
        case IpVersionPreference::IPv6Only:
            return "::1";
        case IpVersionPreference::DualStack:
        default:
            // For DualStack, prefer IPv4 loopback for better compatibility
            // since most internal communication expects IPv4
            return "127.0.0.1";
    }
}

bool isLoopbackAddress(const std::string& addr)
{
    return (addr == "127.0.0.1" || addr == "::1" || addr == "localhost");
}


shared_ptr<addrinfo> getAddrInfo ( const string& addr, uint16_t port, bool isV4, bool passive )
{
    // Use the global preference setting for better hostname resolution
    IpVersionPreference preference = getGlobalIpVersionPreference();
    
    // If preference is DualStack, use the enhanced function
    if ( preference == IpVersionPreference::DualStack )
    {
        return getAddrInfoWithPreference ( addr, port, preference, passive );
    }
    
    // Otherwise use the legacy behavior but respect the preference  
    // For DualStack, we should have been handled above, so this is IPv4Only or IPv6Only
    bool useV4 = ( preference == IpVersionPreference::IPv4Only );
    
    addrinfo addrConf, *addrRes = 0;
    ZeroMemory ( &addrConf, sizeof ( addrConf ) );

    addrConf.ai_family = ( useV4 ? AF_INET : AF_INET6 );

    if ( passive )
        addrConf.ai_flags = AI_PASSIVE;

    int error = getaddrinfo ( addr.empty() ? 0 : addr.c_str(), format ( port ).c_str(), &addrConf, &addrRes );

    if ( error != 0 )
        THROW_WIN_EXCEPTION ( error, ERROR_INVALID_HOSTNAME, "", addr );

    return shared_ptr<addrinfo> ( addrRes, freeaddrinfo );
}

shared_ptr<addrinfo> getAddrInfoWithPreference ( const string& addr, uint16_t port, 
                                                 IpVersionPreference preference, bool passive )
{
    switch ( preference )
    {
        case IpVersionPreference::IPv4Only:
            return getAddrInfo ( addr, port, true, passive );
            
        case IpVersionPreference::IPv6Only:
            return getAddrInfo ( addr, port, false, passive );
            
        case IpVersionPreference::DualStack:
        {
            // For dual-stack, try IPv6 first (which can handle IPv4-mapped addresses)
            // If that fails, fall back to IPv4
            try
            {
                addrinfo addrConf, *addrRes = 0;
                ZeroMemory ( &addrConf, sizeof ( addrConf ) );
                
                // Use AF_UNSPEC for dual-stack
                addrConf.ai_family = AF_UNSPEC;
                
                if ( passive )
                {
                    addrConf.ai_flags = AI_PASSIVE;
                    // For server sockets, prefer IPv6 to handle both IPv4 and IPv6 connections
                    addrConf.ai_flags |= AI_V4MAPPED | AI_ALL;
                }
                
                int error = getaddrinfo ( addr.empty() ? 0 : addr.c_str(), format ( port ).c_str(), &addrConf, &addrRes );
                
                if ( error != 0 )
                    THROW_WIN_EXCEPTION ( error, ERROR_INVALID_HOSTNAME, "", addr );
                
                return shared_ptr<addrinfo> ( addrRes, freeaddrinfo );
            }
            catch ( ... )
            {
                // Fall back to IPv4 if dual-stack fails
                return getAddrInfo ( addr, port, true, passive );
            }
        }
        
        default:
            return getAddrInfo ( addr, port, true, passive );
    }
}

string getAddrFromSockAddr ( const sockaddr *sa )
{
    char addr[INET6_ADDRSTRLEN];

    if ( sa->sa_family == AF_INET )
        inet_ntop ( sa->sa_family, & ( ( ( sockaddr_in * ) sa )->sin_addr ), addr, sizeof ( addr ) );
    else
        inet_ntop ( sa->sa_family, & ( ( ( sockaddr_in6 * ) sa )->sin6_addr ), addr, sizeof ( addr ) );

    return addr;
}

uint16_t getPortFromSockAddr ( const sockaddr *sa )
{
    if ( sa->sa_family == AF_INET )
        return ntohs ( ( ( sockaddr_in * ) sa )->sin_port );
    else
        return ntohs ( ( ( sockaddr_in6 * ) sa )->sin6_port );
}

/*
const char *inet_ntop ( int af, const void *src, char *dst, size_t size )
{
    if ( af == AF_INET )
    {
        sockaddr_in in;
        memset ( &in, 0, sizeof ( in ) );
        in.sin_family = AF_INET;
        memcpy ( &in.sin_addr, src, sizeof ( in_addr ) );
        getnameinfo ( ( sockaddr * ) &in, sizeof ( sockaddr_in ), dst, size, 0, 0, NI_NUMERICHOST );
        return dst;
    }
    else if ( af == AF_INET6 )
    {
        sockaddr_in6 in;
        memset ( &in, 0, sizeof ( in ) );
        in.sin6_family = AF_INET6;
        memcpy ( &in.sin6_addr, src, sizeof ( in_addr6 ) );
        getnameinfo ( ( sockaddr * ) &in, sizeof ( sockaddr_in6 ), dst, size, 0, 0, NI_NUMERICHOST );
        return dst;
    }

    return 0;
}
*/

IpAddrPort::IpAddrPort ( const string& addrPort ) : addr ( addrPort ), port ( 0 ), isV4 ( true )
{
    if ( addrPort.empty() )
        return;

    // Handle IPv6 addresses in brackets: [address]:port
    if ( addrPort[0] == '[' )
    {
        size_t closeBracket = addrPort.find ( ']' );
        if ( closeBracket == string::npos )
            THROW_EXCEPTION ( "addrPort=%s", ERROR_INVALID_ADDR_PORT, addrPort );
        
        // Extract address from within brackets
        addr = addrPort.substr ( 1, closeBracket - 1 );
        isV4 = false; // This is an IPv6 address
        
        // Look for port after the closing bracket
        if ( closeBracket + 1 < addrPort.size() && addrPort[closeBracket + 1] == ':' )
        {
            stringstream ss ( addrPort.substr ( closeBracket + 2 ) );
            if ( ! ( ss >> port ) )
                THROW_EXCEPTION ( "addrPort=%s", ERROR_INVALID_ADDR_PORT, addrPort );
        }
        return;
    }

    // Handle IPv4 addresses or IPv6 addresses without brackets (for backwards compatibility)
    int lastColonPos = -1;
    int colonCount = 0;
    
    // Count colons to distinguish IPv4 from IPv6
    for ( size_t i = 0; i < addrPort.size(); ++i )
    {
        if ( addrPort[i] == ':' )
        {
            colonCount++;
            lastColonPos = i;
        }
    }
    
    // If there's only one colon, treat it as port separator for IPv4:port
    // For IPv6 addresses like ::1 without port, we need to be more careful
    if ( colonCount == 1 )
    {
        // Likely IPv4:port or ::1:port format
        if ( lastColonPos == ( int ) addrPort.size() - 1 )
            THROW_EXCEPTION ( "addrPort=%s", ERROR_INVALID_ADDR_PORT, addrPort );

        stringstream ss ( addrPort.substr ( lastColonPos + 1 ) );
        if ( ! ( ss >> port ) )
            THROW_EXCEPTION ( "addrPort=%s", ERROR_INVALID_ADDR_PORT, addrPort );

        addr = addrPort.substr ( 0, lastColonPos );
        
        // Determine if this looks like IPv6
        if ( addr.find ( ':' ) != string::npos )
            isV4 = false;
    }
    else if ( colonCount == 2 )
    {
        // Special case for IPv6 addresses like ::1 or ::1:port
        if ( addrPort.substr(0, 2) == "::" )
        {
            // Check if the last part after colon could be a valid port number
            string lastPart = addrPort.substr(lastColonPos + 1);
            stringstream ss(lastPart);
            uint16_t testPort;
            // For it to be a port, it must be numeric and in valid port range (1-65535)
            // Also, it shouldn't be part of an IPv6 address like ::1
            if (ss >> testPort && lastPart.length() >= 2 && testPort > 0 && testPort <= 65535)
            {
                // This looks like ::1:port format
                port = testPort;
                addr = addrPort.substr(0, lastColonPos);
                isV4 = false;
            }
            else
            {
                // This is likely just an IPv6 address like ::1
                addr = addrPort;
                isV4 = false;
                port = 0;
            }
        }
        else
        {
            // Other 2-colon case - treat as IPv6 address without port
            addr = addrPort;
            isV4 = false;
            port = 0;
        }
    }
    else if ( colonCount > 2 )
    {
        // Likely IPv6 address without port
        addr = addrPort;
        isV4 = false;
        port = 0; // No port specified
    }
    else
    {
        // Fallback to original logic for IPv4
        int i;
        for ( i = addr.size() - 1; i >= 0; --i )
            if ( addr[i] == ':' )
                break;

        if ( i == ( int ) addr.size() - 1 )
            THROW_EXCEPTION ( "addrPort=%s", ERROR_INVALID_ADDR_PORT, addrPort );

        stringstream ss ( addr.substr ( i + 1 ) );
        if ( ! ( ss >> port ) )
            THROW_EXCEPTION ( "addrPort=%s", ERROR_INVALID_ADDR_PORT, addrPort );

        for ( ; i >= 0; --i )
            if ( isalnum ( addr[i] ) )
                break;

        if ( i < 0 )
        {
            addr.clear();
            return;
        }

        addr = addr.substr ( 0, i + 1 );
    }
}

IpAddrPort::IpAddrPort ( const sockaddr *sa )
    : addr ( getAddrFromSockAddr ( sa ) )
    , port ( getPortFromSockAddr ( sa ) )
    , isV4 ( sa->sa_family == AF_INET ) {}

const shared_ptr<addrinfo>& IpAddrPort::getAddrInfo() const
{
    if ( _addrInfo.get() )
        return _addrInfo;
    else
    {
        // Use global IP version preference for address resolution, but still respect 
        // explicit IPv6 addresses (isV4 = false) to maintain backward compatibility
        IpVersionPreference preference = getGlobalIpVersionPreference();
        
        // If this is explicitly marked as IPv6 (like [::1]:port), use IPv6-only
        if ( !isV4 )
            preference = IpVersionPreference::IPv6Only;
        // If this is an empty address (like "" for server binding), use global preference
        else if ( addr.empty() )
            preference = getGlobalIpVersionPreference();
        // Otherwise use the global preference unless it would break explicit IPv4 addresses
        else if ( preference == IpVersionPreference::IPv6Only )
        {
            // Check if this looks like an IPv4 address - if so, this will fail in IPv6-only mode
            // Allow the failure to occur so the user knows about the incompatibility
            preference = IpVersionPreference::IPv6Only;
        }
        
        return ( _addrInfo = getAddrInfoWithPreference ( addr, port, preference, false ) );
    }
}
