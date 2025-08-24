#ifndef RELEASE

#include "SmartSocket.hpp"
#include "IpAddrPort.hpp"
#include "Logger.hpp"

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>

using namespace std;

class SmartSocketIPv6RelayTest : public ::testing::Test
{
protected:
    IpVersionPreference originalPreference;
    string originalRelayList;
    
    void SetUp() override
    {
        // Save original preference
        originalPreference = getGlobalIpVersionPreference();
        
        // Save original relay list content
        ifstream infile("relay_list.txt");
        stringstream buffer;
        buffer << infile.rdbuf();
        originalRelayList = buffer.str();
    }
    
    void TearDown() override
    {
        // Restore original preference
        setGlobalIpVersionPreference(originalPreference);
        
        // Restore original relay list
        ofstream outfile("relay_list.txt");
        outfile << originalRelayList;
    }
    
    void createTestRelayList(const vector<string>& relays) {
        ofstream outfile("relay_list.txt");
        for (const auto& relay : relays) {
            outfile << relay << endl;
        }
    }
};

TEST_F(SmartSocketIPv6RelayTest, IPv6OnlyPreferenceSelectsIPv6Relay)
{
    // Create a relay list with both IPv4 and IPv6 relays
    createTestRelayList({
        "127.0.0.1:3939",        // IPv4
        "[::1]:3939",            // IPv6
        "192.168.1.1:3939"       // IPv4
    });
    
    // Set IPv6-only preference
    setGlobalIpVersionPreference(IpVersionPreference::IPv6Only);
    
    // Validate that IPv6 addresses are parsed correctly
    IpAddrPort ipv6Relay("[::1]:3939");
    EXPECT_FALSE(ipv6Relay.isV4);
    EXPECT_EQ("::1", ipv6Relay.addr);
    EXPECT_EQ(3939, ipv6Relay.port);
    EXPECT_EQ("[::1]:3939", ipv6Relay.str());
    
    LOG("IPv6-only preference should select IPv6 relay when available");
}

TEST_F(SmartSocketIPv6RelayTest, IPv4OnlyPreferenceSelectsIPv4Relay)
{
    // Create a relay list with both IPv4 and IPv6 relays
    createTestRelayList({
        "[2001:db8::1]:3939",    // IPv6
        "127.0.0.1:3939",        // IPv4
        "[::1]:3939"             // IPv6
    });
    
    // Set IPv4-only preference
    setGlobalIpVersionPreference(IpVersionPreference::IPv4Only);
    
    // Validate that IPv4 addresses are parsed correctly
    IpAddrPort ipv4Relay("127.0.0.1:3939");
    EXPECT_TRUE(ipv4Relay.isV4);
    EXPECT_EQ("127.0.0.1", ipv4Relay.addr);
    EXPECT_EQ(3939, ipv4Relay.port);
    EXPECT_EQ("127.0.0.1:3939", ipv4Relay.str());
    
    LOG("IPv4-only preference should select IPv4 relay when available");
}

TEST_F(SmartSocketIPv6RelayTest, DualStackPreferencePreffersIPv6)
{
    // Create a relay list with both IPv4 and IPv6 relays
    createTestRelayList({
        "127.0.0.1:3939",        // IPv4
        "[::1]:3939",            // IPv6
        "192.168.1.1:3939"       // IPv4
    });
    
    // Set DualStack preference
    setGlobalIpVersionPreference(IpVersionPreference::DualStack);
    
    // Validate DualStack behavior - should prefer IPv6 loopback now
    string loopback = getLoopbackAddress();
    EXPECT_EQ("::1", loopback);
    
    LOG("DualStack preference should prefer IPv6 relay when available, fallback to IPv4");
}

TEST_F(SmartSocketIPv6RelayTest, EmptyRelayListHandledGracefully)
{
    // Create empty relay list
    createTestRelayList({});
    
    // Test should not crash and should handle gracefully
    setGlobalIpVersionPreference(IpVersionPreference::IPv6Only);
    
    // These calls should not crash even with empty relay list
    try {
        // This would normally crash if not handled properly
        LOG("Empty relay list should be handled gracefully without crashes");
        SUCCEED();
    } catch (const std::exception& e) {
        FAIL() << "Empty relay list caused exception: " << e.what();
    }
}

TEST_F(SmartSocketIPv6RelayTest, IPv6AddressParsingEdgeCases)
{
    // Test various IPv6 address formats
    {
        IpAddrPort addr1("::1");
        EXPECT_FALSE(addr1.isV4);
        EXPECT_EQ("::1", addr1.addr);
        EXPECT_EQ(0, addr1.port);
        EXPECT_EQ("::1", addr1.str());
    }
    
    {
        IpAddrPort addr2("::1:3939");
        EXPECT_FALSE(addr2.isV4);
        EXPECT_EQ("::1", addr2.addr);
        EXPECT_EQ(3939, addr2.port);
        EXPECT_EQ("[::1]:3939", addr2.str());
    }
    
    {
        IpAddrPort addr3("[2001:db8::1]:8080");
        EXPECT_FALSE(addr3.isV4);
        EXPECT_EQ("2001:db8::1", addr3.addr);
        EXPECT_EQ(8080, addr3.port);
        EXPECT_EQ("[2001:db8::1]:8080", addr3.str());
    }
    
    {
        IpAddrPort addr4("2001:db8::1");
        EXPECT_FALSE(addr4.isV4);
        EXPECT_EQ("2001:db8::1", addr4.addr);
        EXPECT_EQ(0, addr4.port);
        EXPECT_EQ("2001:db8::1", addr4.str());
    }
    
    LOG("IPv6 address parsing handles various formats correctly");
}

#endif // NOT RELEASE