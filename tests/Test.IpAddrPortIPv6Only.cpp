#ifndef RELEASE

#include "IpAddrPort.hpp"
#include "Logger.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

using namespace std;

class IpAddrPortIPv6OnlyTest : public ::testing::Test
{
protected:
    IpVersionPreference originalPreference;
    
    void SetUp() override
    {
        // Save original preference
        originalPreference = getGlobalIpVersionPreference();
    }
    
    void TearDown() override
    {
        // Restore original preference
        setGlobalIpVersionPreference(originalPreference);
    }
};

TEST_F(IpAddrPortIPv6OnlyTest, IPv6OnlyEmptyAddressResolution)
{
    // Set IPv6-only mode
    setGlobalIpVersionPreference(IpVersionPreference::IPv6Only);
    
    // Test that empty address resolves in IPv6-only mode (for server binding)
    IpAddrPort serverAddr("", 3939);
    
    try 
    {
        auto addrInfo = serverAddr.getAddrInfo();
        EXPECT_TRUE(addrInfo != nullptr);
        
        // Should resolve to IPv6 address family
        EXPECT_EQ(AF_INET6, addrInfo->ai_family);
        
        LOG("Successfully resolved empty address in IPv6-only mode");
    }
    catch (const exception& e)
    {
        FAIL() << "Empty address resolution failed in IPv6-only mode: " << e.what();
    }
}

TEST_F(IpAddrPortIPv6OnlyTest, IPv6OnlyExplicitIPv6AddressResolution)
{
    // Set IPv6-only mode
    setGlobalIpVersionPreference(IpVersionPreference::IPv6Only);
    
    // Test that explicit IPv6 address works in IPv6-only mode
    IpAddrPort addr("::1", 3939);
    
    try 
    {
        auto addrInfo = addr.getAddrInfo();
        EXPECT_TRUE(addrInfo != nullptr);
        EXPECT_EQ(AF_INET6, addrInfo->ai_family);
        
        LOG("Successfully resolved ::1 in IPv6-only mode");
    }
    catch (const exception& e)
    {
        FAIL() << "IPv6 address resolution failed in IPv6-only mode: " << e.what();
    }
}

TEST_F(IpAddrPortIPv6OnlyTest, IPv6OnlyIPv4AddressFailure)
{
    // Set IPv6-only mode  
    setGlobalIpVersionPreference(IpVersionPreference::IPv6Only);
    
    // Test that explicit IPv4 address fails in IPv6-only mode
    IpAddrPort addr("127.0.0.1", 3939);
    
    EXPECT_THROW({
        auto addrInfo = addr.getAddrInfo();
    }, std::exception);
    
    LOG("IPv4 address correctly failed in IPv6-only mode");
}

TEST_F(IpAddrPortIPv6OnlyTest, IPv6OnlyBracketedAddressResolution)
{
    // Set IPv6-only mode
    setGlobalIpVersionPreference(IpVersionPreference::IPv6Only);
    
    // Test that bracketed IPv6 address works
    IpAddrPort addr("[::1]:3939");
    
    try 
    {
        auto addrInfo = addr.getAddrInfo();
        EXPECT_TRUE(addrInfo != nullptr);
        EXPECT_EQ(AF_INET6, addrInfo->ai_family);
        
        LOG("Successfully resolved [::1]:3939 in IPv6-only mode");
    }
    catch (const exception& e)
    {
        FAIL() << "Bracketed IPv6 address resolution failed: " << e.what();
    }
}

#endif // NOT RELEASE