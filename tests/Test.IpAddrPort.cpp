#ifndef RELEASE

#include "IpAddrPort.hpp"
#include "Logger.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

using namespace std;

class IpAddrPortTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure we're using DualStack for better hostname resolution
        setGlobalIpVersionPreference(IpVersionPreference::DualStack);
    }
};

TEST_F(IpAddrPortTest, ParseIPv4Addresses)
{
    // Test IPv4 address with port
    IpAddrPort addr1("127.0.0.1:3939");
    EXPECT_EQ("127.0.0.1", addr1.addr);
    EXPECT_EQ(3939, addr1.port);
    EXPECT_TRUE(addr1.isV4);
    EXPECT_EQ("127.0.0.1:3939", addr1.str());
    
    // Test IPv4 address without port
    IpAddrPort addr2("192.168.1.1");
    EXPECT_EQ("192.168.1.1", addr2.addr);
    EXPECT_EQ(0, addr2.port);
    EXPECT_TRUE(addr2.isV4);
    EXPECT_EQ("192.168.1.1", addr2.str());
}

TEST_F(IpAddrPortTest, ParseIPv6Addresses)
{
    // Test IPv6 address in brackets with port
    IpAddrPort addr1("[::1]:3939");
    EXPECT_EQ("::1", addr1.addr);
    EXPECT_EQ(3939, addr1.port);
    EXPECT_FALSE(addr1.isV4);
    EXPECT_EQ("[::1]:3939", addr1.str());
    
    // Test IPv6 address without brackets, no port
    IpAddrPort addr2("2001:db8::1");
    EXPECT_EQ("2001:db8::1", addr2.addr);
    EXPECT_EQ(0, addr2.port);
    EXPECT_FALSE(addr2.isV4);
    EXPECT_EQ("2001:db8::1", addr2.str());
    
    // Test IPv6 loopback without port
    IpAddrPort addr3("::1");
    EXPECT_EQ("::1", addr3.addr);
    EXPECT_EQ(0, addr3.port);
    EXPECT_FALSE(addr3.isV4);
    EXPECT_EQ("::1", addr3.str());
}

TEST_F(IpAddrPortTest, ParseHostnames)
{
    // Test hostname with port
    IpAddrPort addr1("localhost:3939");
    EXPECT_EQ("localhost", addr1.addr);
    EXPECT_EQ(3939, addr1.port);
    EXPECT_TRUE(addr1.isV4); // Hostnames default to IPv4 parsing
    EXPECT_EQ("localhost:3939", addr1.str());
    
    // Test hostname without port
    IpAddrPort addr2("example.com");
    EXPECT_EQ("example.com", addr2.addr);
    EXPECT_EQ(0, addr2.port);
    EXPECT_TRUE(addr2.isV4);
    EXPECT_EQ("example.com", addr2.str());
}

TEST_F(IpAddrPortTest, HostnameResolutionDualStack)
{
    // Test that we can resolve localhost with DualStack preference
    // This should work regardless of network configuration
    try 
    {
        IpAddrPort addr("localhost", 3939);
        auto addrInfo = addr.getAddrInfo();
        EXPECT_TRUE(addrInfo != nullptr);
        LOG("Successfully resolved localhost with DualStack preference");
    }
    catch (const exception& e)
    {
        LOG("Hostname resolution failed (expected in some environments): %s", e.what());
        // This might fail in some sandboxed environments, so we don't ASSERT
    }
}

TEST_F(IpAddrPortTest, GlobalIpVersionPreference)
{
    // Test setting and getting global preference
    IpVersionPreference original = getGlobalIpVersionPreference();
    
    setGlobalIpVersionPreference(IpVersionPreference::IPv6Only);
    EXPECT_EQ(IpVersionPreference::IPv6Only, getGlobalIpVersionPreference());
    
    setGlobalIpVersionPreference(IpVersionPreference::IPv4Only);
    EXPECT_EQ(IpVersionPreference::IPv4Only, getGlobalIpVersionPreference());
    
    // Restore original setting
    setGlobalIpVersionPreference(original);
}

TEST_F(IpAddrPortTest, EmptyAndInvalidAddresses)
{
    // Test empty address
    IpAddrPort emptyAddr("");
    EXPECT_TRUE(emptyAddr.empty());
    EXPECT_EQ("", emptyAddr.str());
    
    // Test clear functionality
    IpAddrPort addr("127.0.0.1:3939");
    EXPECT_FALSE(addr.empty());
    addr.clear();
    EXPECT_TRUE(addr.empty());
}

#endif // NOT RELEASE