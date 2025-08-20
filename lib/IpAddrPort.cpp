#include "IpAddrPort.hpp"
#include "Exceptions.hpp"
#include "ErrorStrings.hpp"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <cctype>
#include <sstream>

using namespace std;

// Global IP version preference
static IpVersionPreference globalIpVersionPreference = IpVersionPreference::IPv4Only;

void setGlobalIpVersionPreference(IpVersionPreference preference)
{
    globalIpVersionPreference = preference;
}

IpVersionPreference getGlobalIpVersionPreference()
{
    return globalIpVersionPreference;
}


shared_ptr<addrinfo> getAddrInfo ( const string& addr, uint16_t port, bool isV4, bool passive )
{
    addrinfo addrConf, *addrRes = 0;
    ZeroMemory ( &addrConf, sizeof ( addrConf ) );

    addrConf.ai_family = ( isV4 ? AF_INET : AF_INET6 );

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
    
    // If there's only one colon or two colons (like ::1:port), treat the last colon as port separator
    // For IPv6 without brackets, this is ambiguous, but we do our best
    if ( colonCount == 1 || ( colonCount == 2 && addrPort.substr(0, 2) == "::" ) )
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
        return ( _addrInfo = ::getAddrInfo ( addr, port, isV4 ) );
}
