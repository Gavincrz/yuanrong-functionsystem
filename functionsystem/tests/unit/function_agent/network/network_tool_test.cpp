/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include "function_agent/network/network_tool.h"
#include "common/utils/exec_utils.h"
#include "common/utils/path.h"

namespace functionsystem::test {

class NetworkToolTest : public testing::Test {

};

/**
 * Feature: SetFirewallTest
 * Description: Test Network Tool Set Firewall
 * Steps:
 * 1. iptables add command with lacking field
 * 2. iptables delete command with lacking field
 * Expectation:
 * 1. return false
 * 2. return false
 * 3. return false
 * 4. return false
 * 5. return false
 * 6. return false
 */
TEST_F(NetworkToolTest, SetFirewallTest)
{
    // given
    function_agent::FirewallConfig configs[6] = {
        {"", "filter", "add", "1.1.1.1", "-j ACCEPT"},
        {"", "filter", "delete", "", ""},
        {"", "", "add", "1.1.1.1", "-j ACCEPT"},
        {"INPUT", " ", "add", "1.1.1.1", "-j ACCEPT"},
        {"INPUT", "filter", "delete", "1.1.1.1", "-j ACCEPT"},
        {"INPUT", "filter", "get", "1.1.1.1", "-j ACCEPT"},
    };

    // want
    bool wants[6] = {false, false, false, false, false, false};

    // got
    for (size_t i = 0; i < sizeof(configs) / sizeof(function_agent::FirewallConfig); i++) {
        EXPECT_TRUE(function_agent::NetworkTool::SetFirewall(configs[i]) == wants[i]);
    }
}

/**
 * Feature: SetRouteTest
 * Description: Test Network Tool Set Route
 * Steps:
 * 1. route config with lacking field
 * Expectation:
 * 1. return false
 */
TEST_F(NetworkToolTest, SetRouteTest)
{
    auto res = ExecuteCommand("route -n");
    if (!res.error.empty()) {
        std::cout << "execute route failed, error: " << res.error << std::endl;
        return;
    }

    if (res.output.find("eth0") == res.output.npos) {
        std::cout << "does not contain interface eth0" << std::endl;
        return;
    }

    // given
    function_agent::RouteConfig configs[1] = {
        {"",        "0.0.0.0/20", "eth0"},
    };

    // want
    bool wants[1] = {false};

    // got
    for (size_t i = 0; i < sizeof(configs) / sizeof(function_agent::RouteConfig); i++) {
        EXPECT_TRUE(function_agent::NetworkTool::SetRoute(configs[i]) == wants[i]);
    }

}

/**
 * Feature: SetTunnelTest
 * Description: Test Network Tool Set Tunnel
 * Steps:
 * 1. tunnel config with lacking field
 * Expectation:
 * 1. return false
 * 2. return false
 * 3. return false
 * 4. return false
 * 5. return false
 */
TEST_F(NetworkToolTest, SetTunnelTest)
{
    // given
    function_agent::TunnelConfig configs[5] = {
        {"", "127.0.0.1", "ipip", "0.0.0.0"},
        {"tun1", "127.0.0.1", "", "0.0.0.0"},
        {"tun1", "", "ipip", "0.0.0.0"},
        {"tun1", "localip", "ipip", "0.0.0.0"},
        {"tun1", "127.0.0.1", " ", "0.0.0.0"},
    };

    // want
    bool wants[5] = {false, false, false, false, false};

    // got
    for (size_t i = 0; i < sizeof(configs) / sizeof(function_agent::TunnelConfig); i++) {
        EXPECT_TRUE(function_agent::NetworkTool::SetTunnel(configs[i]) == wants[i]);
    }

}

/**
 * Feature: GetAddrTest
 * Description: Test Network Tool Get Addr Info By ip
 * Steps:
 * 1. get address information by 127.0.0.1
 * Expectation:
 * 1. get interface lo
 */
TEST_F(NetworkToolTest, GetAddrTest)
{
    auto addr = function_agent::NetworkTool::GetAddr("127.0.0.1");
    EXPECT_TRUE(addr.IsSome());
    EXPECT_TRUE(addr.Get().interface == "lo");
}

/**
 * Feature: ParseNetworkConfigTest
 * Description: Test Network Tool Parse NetworkConfig
 * Steps:
 * 1. give network config string
 * Expectation:
 * get correct value
 */
TEST_F(NetworkToolTest, ParseNetworkConfigTest)
{
    std::string str = R"([{"routeConfig":{"gateway":"1.1.1.1","cidr":"1.1.1.0/24"}, "tunnelConfig":{"tunnelName":"test","remoteIP":"1.1.1.1","mode":"ipip"},"firewallConfig":{"chain":"OUTPUT","table":"filter","operation":"add","target":"1.1.1.1","args":"-j ACCEPT"}}])";
    auto config = function_agent::NetworkTool::ParseNetworkConfig(str);
    EXPECT_TRUE(config[0].routeConfig.IsSome());
    EXPECT_TRUE(config[0].routeConfig.Get().gateway == "1.1.1.1");
    EXPECT_TRUE(config[0].routeConfig.Get().cidr == "1.1.1.0/24");

    EXPECT_TRUE(config[0].tunnelConfig.IsSome());
    EXPECT_TRUE(config[0].tunnelConfig.Get().tunnelName == "test");
    EXPECT_TRUE(config[0].tunnelConfig.Get().remoteIP == "1.1.1.1");
    EXPECT_TRUE(config[0].tunnelConfig.Get().mode == "ipip");

    EXPECT_TRUE(config[0].firewallConfig.IsSome());
    EXPECT_TRUE(config[0].firewallConfig.Get().chain == "OUTPUT");
    EXPECT_TRUE(config[0].firewallConfig.Get().table == "filter");
    EXPECT_TRUE(config[0].firewallConfig.Get().operation == "add");
    EXPECT_TRUE(config[0].firewallConfig.Get().target == "1.1.1.1");
    EXPECT_TRUE(config[0].firewallConfig.Get().args == "-j ACCEPT");

    std::string str1 = R"([{"routeConfig":{"gateway":1,"cidr":1}, "tunnelConfig":{"tunnelName":12,"remoteIP":1,"mode":1},"firewallConfig":{"chain":1,"table":1,"operation":1,"target":1,"args":1}}])";
    auto config1 = function_agent::NetworkTool::ParseNetworkConfig(str1);
    EXPECT_TRUE(config1[0].routeConfig.IsSome());
    EXPECT_TRUE(config1[0].routeConfig.Get().gateway == "");
    EXPECT_TRUE(config1[0].routeConfig.Get().cidr == "default");

    EXPECT_TRUE(config1[0].tunnelConfig.IsSome());
    EXPECT_TRUE(config1[0].tunnelConfig.Get().tunnelName == "");
    EXPECT_TRUE(config1[0].tunnelConfig.Get().remoteIP == "");
    EXPECT_TRUE(config1[0].tunnelConfig.Get().mode == "");

    EXPECT_TRUE(config1[0].firewallConfig.IsSome());
    EXPECT_TRUE(config1[0].firewallConfig.Get().chain == "");
    EXPECT_TRUE(config1[0].firewallConfig.Get().table == "");
    EXPECT_TRUE(config1[0].firewallConfig.Get().operation == "");
    EXPECT_TRUE(config1[0].firewallConfig.Get().target == "");
    EXPECT_TRUE(config1[0].firewallConfig.Get().args == "");

    str1 = "fake_json";
    config1 = function_agent::NetworkTool::ParseNetworkConfig(str1);
    EXPECT_EQ(config1.size(), 0);
}

/**
 * Feature: ParseNetworkConfigWithIncorrectStrTest
 * Description: Test Network Tool Parse NetworkConfig
 * Steps:
 * 1. give Incorrect network config string
 * Expectation:
 * return configs with zero size
 */
TEST_F(NetworkToolTest, ParseNetworkConfigWithErrorStrTest)
{
    std::string str = R"(["routeConfig":{"gateway":"1.1.1.1","cidr":"1.1.1.0/24"}, "tunnelConfig":{"tunnelName":"test","remoteIP":"1.1.1.1","mode":"ipip"},"firewallConfig":{"chain":"OUTPUT","table":"filter","operation":"add","target":"1.1.1.1","args":"-j ACCEPT"}}])";
    auto config = function_agent::NetworkTool::ParseNetworkConfig(str);
    EXPECT_TRUE(config.size() == 0);

    str = R"({"routeConfig":{"gateway":"1.1.1.1","cidr":"1.1.1.0/24"}, "tunnelConfig":{"tunnelName":"test","remoteIP":"1.1.1.1","mode":"ipip"},"firewallConfig":{"chain":"OUTPUT","table":"filter","operation":"add","target":"1.1.1.1","args":"-j ACCEPT"}}])";
    config = function_agent::NetworkTool::ParseNetworkConfig(str);
    EXPECT_TRUE(config.size() == 0);
}

/**
 * Feature: ParseProberConfigTest
 * Description: Test Network Tool Parse Probe Config
 * Steps:
 * 1. give Probe config string
 * Expectation:
 * get correct value
 */
TEST_F(NetworkToolTest, ParseProberConfigTest)
{
    std::string str = R"([{"protocol":"ICMP","address":"1.1.1.1","interval":10,"timeout":10,"failureThreshold":1}])";
    auto config = function_agent::NetworkTool::ParseProberConfig(str);
    EXPECT_TRUE(config[0].protocol == "ICMP");
    EXPECT_TRUE(config[0].address == "1.1.1.1");
    EXPECT_TRUE(config[0].interval == 10);
    EXPECT_TRUE(config[0].timeout == 10);
    EXPECT_TRUE(config[0].failureThreshold == 1);

    std::string str1 = R"([{"protocol":1,"address":1,"interval":"10","timeout":10,"failureThreshold":1}])";
    auto config1 = function_agent::NetworkTool::ParseProberConfig(str1);
    EXPECT_TRUE(config1[0].protocol == "");
    EXPECT_TRUE(config1[0].address == "");
    EXPECT_TRUE(config1[0].interval == 0);
    EXPECT_TRUE(config1[0].timeout == 10);
    EXPECT_TRUE(config1[0].failureThreshold == 1);

    EXPECT_FALSE(function_agent::NetworkTool::Probe(config1));
}

/**
 * Feature: ParseProberConfigWithIncorrectStrTest
 * Description: Test Network Tool Parse Probe Config
 * Steps:
 * 1. give Incorrect Probe config string
 * Expectation:
 * return configs with zero size
 */
TEST_F(NetworkToolTest, ParseProberConfigWithIncorrectStrTest)
{
    std::string str = R"(["protocol":"ICMP","address":"1.1.1.1","interval":10,"timeout":10,"failureThreshold":1}])";
    auto config = function_agent::NetworkTool::ParseProberConfig(str);
    EXPECT_TRUE(config.size() == 0);

    str = R"({"protocol":"ICMP","address":"1.1.1.1","interval":10,"timeout":10,"failureThreshold":1}])";
    config = function_agent::NetworkTool::ParseProberConfig(str);
    EXPECT_TRUE(config.size() == 0);
}

/**
 * Feature: PingSuccessTest
 * Description: Test Ping
 * Steps:
 * give correct Probe config string
 * Expectation:
 * ping success
 */
TEST_F(NetworkToolTest, PingSuccessTest)
{
    if (LookPath("ping").IsNone()) {
        return;
    }
    std::string str = R"([{"protocol":"ICMP","address":"127.0.0.1","interval":1,"timeout":1,"failureThreshold":1}])";
    auto configs = function_agent::NetworkTool::ParseProberConfig(str);
    for (const auto &config: configs) {
        EXPECT_TRUE(function_agent::NetworkTool::Ping(config));
    }
}

/**
 * Feature: PingFailedTest
 * Description: Test Ping
 * Steps:
 * give Incorrect Probe config string
 * Expectation:
 * ping failed
 */
TEST_F(NetworkToolTest, PingFailedTest)
{
    if (LookPath("ping").IsNone()) {
        return;
    }
    std::string str = R"([{"protocol":"ICMP","address":"1.1.1.1","interval":1,"timeout":1,"failureThreshold":1}])";
    auto configs = function_agent::NetworkTool::ParseProberConfig(str);
    for (const auto &config: configs) {
        EXPECT_FALSE(function_agent::NetworkTool::Ping(config));
    }

    std::string str1 = R"([{"protocol":"ICMP","address":"1.1.1.1.","interval":1,"timeout":1,"failureThreshold":1}])";
    auto configs1 = function_agent::NetworkTool::ParseProberConfig(str);
    for (const auto &config: configs1) {
        EXPECT_FALSE(function_agent::NetworkTool::Ping(config));
    }
}

/**
 * Feature: GetRouteConfigTest
 * Description: Test RouteConfig
 * Steps:
 * give Incorrect AND correct RouteConfig
 * Expectation:
 * get failed
 */

TEST_F(NetworkToolTest, GetRouteConfigTest)
{
    std::string emptyStr = "";
    const int32_t DEFAULT_EMPTY_SIZE = 0;
    EXPECT_TRUE(function_agent::NetworkTool::GetRouteConfig(emptyStr).size() == DEFAULT_EMPTY_SIZE);

    std::string wrongStr = "/home";
    EXPECT_TRUE(function_agent::NetworkTool::GetRouteConfig(wrongStr).size() == DEFAULT_EMPTY_SIZE);

    wrongStr = "192.168.123.2/234";
    EXPECT_TRUE(function_agent::NetworkTool::GetRouteConfig(wrongStr).size() == DEFAULT_EMPTY_SIZE);

    std::string rightStr = "dev eth0";
    auto result = function_agent::NetworkTool::GetRouteConfig(rightStr);
    EXPECT_TRUE(function_agent::NetworkTool::GetRouteConfig(rightStr).size() >= DEFAULT_EMPTY_SIZE);

    rightStr = "192.168.1.1/24";
    EXPECT_TRUE(function_agent::NetworkTool::GetRouteConfig(rightStr).size() >= DEFAULT_EMPTY_SIZE);
}

/**
 * Feature: SetRouteParameterTest
 * Description: Test set route parameter
 * Steps:
 * give Incorrect RouteConfig
 * Expectation:
 * set route failed
 */
TEST_F(NetworkToolTest, SetRouteParameterTest)
{
    functionsystem::function_agent::RouteConfig wrongCidr1;
    wrongCidr1.cidr = "";
    wrongCidr1.gateway = "0.0.0.0";
    wrongCidr1.interface = "ens33";
    EXPECT_FALSE(function_agent::NetworkTool::SetRoute(wrongCidr1));

    functionsystem::function_agent::RouteConfig wrongCidr2;
    wrongCidr2.cidr = "dev eth0";
    wrongCidr2.gateway = "0.0.0.0";
    wrongCidr2.interface = "ens33";
    EXPECT_FALSE(function_agent::NetworkTool::SetRoute(wrongCidr2));

    functionsystem::function_agent::RouteConfig wrongCidr3;
    wrongCidr3.cidr = "192.168.1.1/24";
    wrongCidr3.gateway = "0.0.0.0";
    wrongCidr3.interface = "ens33";
    EXPECT_FALSE(function_agent::NetworkTool::SetRoute(wrongCidr3));

    functionsystem::function_agent::RouteConfig wrongCidr4;
    wrongCidr4.cidr = "192.168.1.1/24";
    wrongCidr4.gateway = "0.0.0.0";
    wrongCidr4.interface = "";
    EXPECT_FALSE(function_agent::NetworkTool::SetRoute(wrongCidr4));

    functionsystem::function_agent::RouteConfig wrongInterface1;
    wrongInterface1.cidr = "dev eth0";
    wrongInterface1.gateway = "0.0.0.0";
    wrongInterface1.interface = "";
    EXPECT_FALSE(function_agent::NetworkTool::SetRoute(wrongInterface1));

    functionsystem::function_agent::RouteConfig wrongGateway1;
    wrongGateway1.cidr = "dev eth0";
    wrongGateway1.gateway = "";
    wrongGateway1.interface = "ens33";
    EXPECT_FALSE(function_agent::NetworkTool::SetRoute(wrongGateway1));

    functionsystem::function_agent::RouteConfig wrongGateway2;
    wrongGateway2.cidr = "dev eth0";
    wrongGateway2.gateway = "gateway";
    wrongGateway2.interface = "ens33";
    EXPECT_FALSE(function_agent::NetworkTool::SetRoute(wrongGateway2));

    functionsystem::function_agent::RouteConfig right1;
    right1.cidr = "dev eth0";
    right1.gateway = "0.0.0.0";
    right1.interface = "ens33";

    EXPECT_FALSE(function_agent::NetworkTool::SetRoute(right1));
}

/**
 * Feature: RestoreRouteTest
 * Description: Test restore route
 * Steps:
 * give incorrect config
 * Expectation:
 * restore route failed
 */
TEST_F(NetworkToolTest, RestoreRouteTest)
{
    std::vector<std::string> config = {"192.168.2.0/24 via 192.168.1.1",
                                       "",
                                       "192.168.2.1/24 via 192.168.1.2",
                                       "192.168.2.2/24 via 192.168.1.3"};
    EXPECT_TRUE(function_agent::NetworkTool::RestoreRoute(config));
}

/**
 * Feature: GetNameServerListTest
 * Description: Test get name server list
 * Steps:
 * call function to get name server list
 * Expectation:
 * restore route failed
 */
TEST_F(NetworkToolTest, GetNameServerListTest)
{
    auto result = function_agent::NetworkTool::GetNameServerList();
    const int32_t DEFAULT_SIZE = 0;
    EXPECT_TRUE(result.size() > DEFAULT_SIZE);
}

/**
 * Feature: PingTest
 * Description: Test Ping parameters
 * Steps:
 * give correct Probe config string
 * Expectation:
 * ping success
 */
TEST_F(NetworkToolTest, PingTest)
{
    // given
    function_agent::ProberConfig configs[6] = {
        {"ICMP", "", 200, 2, 3},
        {"ICMP", "localip", 200, 2, 3},
        {"ICMP", "1.1.1.1", -1, 2, 3},
        {"ICMP", "1.1.1.1", 256, 2, 3},
        {"ICMP", "1.1.1.1", 123, -1, 3},
        {"ICMP", "1.1.1.1", 123, 2, 3},
    };

    // want
    bool wants[6] = {false, false, false, false, false, false};

    // got
    for (size_t i = 0; i < 6; i++) {
        EXPECT_TRUE(function_agent::NetworkTool::Ping(configs[i]) == wants[i]);
    }

    EXPECT_FALSE(function_agent::NetworkTool::IsIpsetExists("name"));
}

TEST_F(NetworkToolTest, CheckIllegalCharsTest)
{
    // given
    std::string command1 = "cmd1;cmd2";
    std::string command2 = "$(cmd)";
    std::string command3 = "`cmd`";
    std::string command4 = "cmd1||cmd2";
    std::string command5 = "cmd1&&cmd2";
    std::string command6 = ">(cmd1)|<(cmd2)";
    std::string command7 = "[cmd1]";
    std::string command8 = "{cmd1}";
    std::string command9 = "cmd1* cmd2";
    std::string command10 = "cmd1?cmd2";
    std::string command11 = "cmd1\ncmd2";
    std::string command12 = "cmd1\\cmd2";
    EXPECT_FALSE(CheckIllegalChars(command1));
    EXPECT_FALSE(CheckIllegalChars(command2));
    EXPECT_FALSE(CheckIllegalChars(command3));
    EXPECT_FALSE(CheckIllegalChars(command4));
    EXPECT_FALSE(CheckIllegalChars(command5));
    EXPECT_FALSE(CheckIllegalChars(command6));
    EXPECT_FALSE(CheckIllegalChars(command7));
    EXPECT_FALSE(CheckIllegalChars(command8));
    EXPECT_FALSE(CheckIllegalChars(command9));
    EXPECT_FALSE(CheckIllegalChars(command10));
    EXPECT_FALSE(CheckIllegalChars(command11));
    EXPECT_FALSE(CheckIllegalChars(command12));
}
}
