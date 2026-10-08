#include <gtest/gtest.h>

#include <string>
extern "C" {
#include "wifi_network_validation.h"
}
TEST(WifiNetworks, ByteLimitsAndOpenNetworks)
{
    EXPECT_TRUE(wifi_network_credentials_valid(std::string(32, 's').c_str(), ""));
    EXPECT_FALSE(wifi_network_credentials_valid(std::string(33, 's').c_str(), ""));
    EXPECT_FALSE(wifi_network_credentials_valid("", ""));
    EXPECT_TRUE(wifi_network_credentials_valid("Gift", "passphrase"));
    EXPECT_TRUE(wifi_network_credentials_valid("Gift", std::string(63, 'x').c_str()));
    EXPECT_FALSE(wifi_network_credentials_valid("Gift", "short"));
    EXPECT_FALSE(wifi_network_credentials_valid("line\nbreak", "passphrase"));
    EXPECT_FALSE(wifi_network_credentials_valid("Gift", "pässword"));
}
TEST(WifiNetworks, RawKeyRequiresExactly64HexCharacters)
{
    EXPECT_TRUE(wifi_network_credentials_valid("Gift", std::string(64, 'f').c_str()));
    EXPECT_FALSE(wifi_network_credentials_valid("Gift", std::string(64, 'g').c_str()));
    EXPECT_FALSE(wifi_network_credentials_valid("Gift", std::string(65, 'a').c_str()));
    EXPECT_FALSE(wifi_network_credentials_valid(nullptr, "password"));
    EXPECT_FALSE(wifi_network_credentials_valid("Gift", nullptr));
}
