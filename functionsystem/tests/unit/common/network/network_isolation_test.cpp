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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>

#include "common/network/network_isolation.h"
#include "mocks/mock_exec_utils.h"

namespace functionsystem::test {
using ::testing::_;
using ::testing::Return;

class NetworkIsolationTest : public ::testing::Test {
};

class IpsetIpv4NetworkIsolationTest : public NetworkIsolationTest {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
    }

    void SetUp()
    {
        commandRunner_ = std::make_shared<MockCommandRunner>();
        isolation_ = std::make_shared<IpsetIpv4NetworkIsolation>("test_ipset");
        isolation_->SetCommandRunner(commandRunner_);
    }

    void TearDown()
    {
    }

protected:
    std::shared_ptr<IpsetIpv4NetworkIsolation> isolation_;
    std::shared_ptr<MockCommandRunner> commandRunner_;
};

TEST_F(IpsetIpv4NetworkIsolationTest, IpsetExists) {
    CommandExecResult result;
    result.output = "Name: test_ipset\nMembers:\n192.168.1.1";
    result.error = "";
    EXPECT_CALL(*commandRunner_, ExecuteCommandWrapper(_)).Times(1).WillRepeatedly(Return(result));

    bool exists = isolation_->IsIpsetExist();
    EXPECT_TRUE(exists);
}

TEST_F(IpsetIpv4NetworkIsolationTest, GetAllRulesSuccess) {
    CommandExecResult result;
    result.output = "Members:\n192.168.1.1\n192.168.1.2";
    EXPECT_CALL(*commandRunner_, ExecuteCommandWrapper(_)).Times(1).WillRepeatedly(Return(result));

    auto rules = isolation_->GetAllRules();
    EXPECT_EQ(rules.size(), (size_t)2);
    EXPECT_THAT(rules, ::testing::ElementsAre("192.168.1.1", "192.168.1.2"));
}

TEST_F(IpsetIpv4NetworkIsolationTest, AddRuleSuccess) {
    EXPECT_CALL(*commandRunner_, CheckAndRunCommandWrapper(_)).Times(1).WillRepeatedly(Return(true));

    int result = isolation_->AddRule("192.168.1.1");
    EXPECT_EQ(result, 0);
}

TEST_F(IpsetIpv4NetworkIsolationTest, AddRulesSuccess) {
    EXPECT_CALL(*commandRunner_, CheckAndRunCommandWrapper(_)).Times(1).WillRepeatedly(Return(true));

    std::vector<std::string> rules = {"192.168.1.1", "192.168.1.2"};
    int result = isolation_->AddRules(rules);
    EXPECT_EQ(result, 0);
}

TEST_F(IpsetIpv4NetworkIsolationTest, RemoveRuleSuccess) {
    EXPECT_CALL(*commandRunner_, CheckAndRunCommandWrapper(_)).Times(1).WillRepeatedly(Return(true));

    int result = isolation_->RemoveRule("192.168.1.1");
    EXPECT_EQ(result, 0);
}

TEST_F(IpsetIpv4NetworkIsolationTest, RemoveAllRulesSuccess) {
    EXPECT_CALL(*commandRunner_, CheckAndRunCommandWrapper(_)).Times(1).WillRepeatedly(Return(true));

    int result = isolation_->RemoveAllRules();
    EXPECT_EQ(result, 0);
}

}  // namespace functionsystem::test