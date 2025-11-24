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

#include "async/async.hpp"
#include "future_test_helper.h"

namespace functionsystem::test {

const std::string MOCK_SERVER_NAME = "FutureTestServer123";
const std::string TEST_AGENT_NAME = "FutureTestAgent123";
const std::string REPLY_MSG = "registered msg";
const std::string REG_MSG = "register msg";

class FutureTestServer : public litebus::ActorBase {
public:
    FutureTestServer() : ActorBase(MOCK_SERVER_NAME){};
    virtual ~FutureTestServer() = default;
    virtual void Register(const litebus::AID from, std::string &&name, std::string &&msg) = 0;
};

class MockServer : public FutureTestServer {
public:
    MockServer() = default;
    ~MockServer() = default;

    void Init() override
    {
        Receive("Register", &MockServer::Register);
    }

    void Register(const litebus::AID from, std::string &&name, std::string &&msg) final
    {
        MockRegister(from, name, msg);
        std::string replyMsg = REPLY_MSG;
        Send(from, "Registered", std::move(replyMsg));
    }

    MOCK_METHOD3(MockRegister, void(const litebus::AID, std::string, std::string));
};

class FutureTestAgent : public litebus::ActorBase {
public:
    FutureTestAgent() : ActorBase(TEST_AGENT_NAME)
    {
    }
    ~FutureTestAgent() = default;

    void Init() override
    {
        Receive("Registered", &FutureTestAgent::Registered);
    }

    void RegisterToServer(const litebus::AID server)
    {
        std::string msg = REG_MSG;
        Send(server, "Register", std::move(msg));
    }

    void Registered(const litebus::AID from, std::string &&name, std::string &&msg)
    {
        if (msg == REPLY_MSG) {
            registered_.SetValue(true);
            msg_ = msg;
        }
    }

    litebus::Future<bool> IsRegistered()
    {
        return registered_.GetFuture();
    }

    std::string GetMsg()
    {
        return msg_;
    }

private:
    litebus::Promise<bool> registered_;
    std::string msg_;
};

class FutureHelperExample : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
    }
    [[maybe_unused]] static void TearDownTestSuite()
    {
    }
    void SetUp()
    {
    }
    void TearDown()
    {
    }

private:
};

TEST_F(FutureHelperExample, RegisterMethodTest)
{
    std::shared_ptr<MockServer> mockServer123 = std::make_shared<MockServer>();
    litebus::Spawn(mockServer123);

    std::shared_ptr<FutureTestAgent> agent = std::make_shared<FutureTestAgent>();
    litebus::Spawn(agent);

    litebus::Future<std::string> msgName;
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*mockServer123.get(), MockRegister(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(FutureArg<1>(&msgName), FutureArg<2>(&msgValue)));

    litebus::Async(agent->GetAID(), &FutureTestAgent::RegisterToServer, mockServer123->GetAID());

    ASSERT_AWAIT_READY(msgName);
    EXPECT_TRUE(msgName.Get() == "Register");

    ASSERT_AWAIT_READY(msgValue);
    EXPECT_TRUE(msgValue.Get() == REG_MSG);

    ASSERT_AWAIT_READY(agent->IsRegistered());
    EXPECT_TRUE(agent->IsRegistered().Get());

    ASSERT_AWAIT_TRUE([=]() -> bool { return agent->GetMsg() == REPLY_MSG; });

    litebus::Terminate(mockServer123->GetAID());
    litebus::Terminate(agent->GetAID());
    litebus::Await(mockServer123);
    litebus::Await(agent);
}

}  // namespace functionsystem::test