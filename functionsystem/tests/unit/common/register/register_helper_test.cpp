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

#include "common/register/register_helper.h"

#include <gtest/gtest.h>

#include "common/heartbeat/heartbeat_client.h"
#include "common/heartbeat/heartbeat_observer.h"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

const std::string UPSTREAM_ACTOR_NAME = "UpstreamActor";
const std::string DOWNSTREAM_ACTOR_NAME = "DownstreamActor";

class UpstreamComp {
public:
    void Start()
    {
        heartbeatTimeout_ = std::make_shared<litebus::Promise<bool>>();
        registerHelper_ = std::make_shared<RegisterHelper>(UPSTREAM_ACTOR_NAME);
        registerHelper_->SetRegisterCallback(std::bind(&UpstreamComp::RegisterHandler, this, std::placeholders::_1));
    }

    void RegisterHandler(const std::string &msg)
    {
        messages::Register registerMsg;
        registerMsg.ParseFromString(msg);
        YRLOG_INFO("register name: {}, address: {}", registerMsg.name(), registerMsg.address());
        registerTimes_++;
        if (registerTimes_ <= registerFailureTimes_) {
            YRLOG_INFO("register fail");
            return;
        }
        uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
        registerHelper_->SetHeartbeatObserveDriver(DOWNSTREAM_ACTOR_NAME,
            "127.0.0.1:" + std::to_string(port),
            1000,
            [heartbeatTimeout(heartbeatTimeout_)](const litebus::AID &aid, HeartbeatConnection) {
                YRLOG_INFO("upstream heartbeat timeout, aid: {}", aid.HashString());
                if (heartbeatTimeout != nullptr) {
                    heartbeatTimeout->SetValue(true);
                }
            }, UPSTREAM_ACTOR_NAME);
        messages::Registered registeredMsg;
        registeredMsg.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
        registeredMsg.set_message("register successfully");
        registerHelper_->SendRegistered(registerMsg.name(), registerMsg.address(), registeredMsg.SerializeAsString());
        registerTimes_ = 0;
    }

    void SetRegisterFailureTimes(const uint32_t times)
    {
        registerFailureTimes_ = times;
    }

    litebus::Future<bool> HeartbeatTimeoutFuture()
    {
        return heartbeatTimeout_->GetFuture();
    };

private:
    std::shared_ptr<RegisterHelper> registerHelper_;
    uint32_t registerTimes_ = 0;
    uint32_t registerFailureTimes_ = 0;
    std::shared_ptr<litebus::Promise<bool>> heartbeatTimeout_;
};

class DownstreamComp {
public:
    void Start()
    {
        heartbeatTimeout_ = std::make_shared<litebus::Promise<bool>>();
        registerTimeout_ = std::make_shared<litebus::Promise<bool>>();
        registerHelper_ = std::make_shared<RegisterHelper>(DOWNSTREAM_ACTOR_NAME);
        registerHelper_->SetRegisteredCallback(
            std::bind(&DownstreamComp::RegisteredHandler, this, std::placeholders::_1));
        registerHelper_->SetRegisterTimeoutCallback(std::bind(&DownstreamComp::RegisterTimeoutHandler, this));
        registerHelper_->SetRegisterInterval(100);
    }

    void RegisteredHandler(const std::string &msg)
    {
        messages::Registered registeredMsg;
        registeredMsg.ParseFromString(msg);
        YRLOG_INFO("registered code: {}, message: {}", registeredMsg.code(), registeredMsg.message());
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        std::string actorAddress = "127.0.0.1:" + std::to_string(port);
        registerHelper_->SetPingPongDriver(DOWNSTREAM_ACTOR_NAME, actorAddress,
            1000, [heartbeatTimeout(heartbeatTimeout_)](const litebus::AID &aid) {
                if (heartbeatTimeout != nullptr) {
                    heartbeatTimeout->SetValue(true);
                }
            }, UPSTREAM_ACTOR_NAME);
    }

    void RegisterTimeoutHandler()
    {
        registerTimeout_->SetValue(true);
    }

    void RegisterToUpstream(uint32_t maxRegistersTimes = 10)
    {
        messages::Register registerMsg;
        registerMsg.set_name(DOWNSTREAM_ACTOR_NAME);
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        std::string actorAddress = "127.0.0.1:" + std::to_string(port);
        registerMsg.set_address(actorAddress);
        registerHelper_->StartRegister(
            UPSTREAM_ACTOR_NAME, actorAddress, registerMsg.SerializeAsString(), maxRegistersTimes);
    }

    bool IsRegistered()
    {
        return registerHelper_->IsRegistered().Get();
    }

    litebus::Future<bool> HeartbeatTimeoutFuture() const
    {
        return heartbeatTimeout_->GetFuture();
    }

    litebus::Future<bool> RegisterTimeoutFuture() const
    {
        return registerTimeout_->GetFuture();
    }

private:
    std::shared_ptr<RegisterHelper> registerHelper_;
    std::shared_ptr<litebus::Promise<bool>> heartbeatTimeout_;
    std::shared_ptr<litebus::Promise<bool>> registerTimeout_;
};

class RegisterHelperTest : public ::testing::Test {
public:
    void SetUp() override
    {
        upstreamComp_ = std::make_shared<UpstreamComp>();
        upstreamComp_->Start();
        downstreamComp_ = std::make_shared<DownstreamComp>();
        downstreamComp_->Start();
    }

    void TearDown() override
    {
        upstreamComp_ = nullptr;
        downstreamComp_ = nullptr;
    }

protected:
    std::shared_ptr<UpstreamComp> upstreamComp_;
    std::shared_ptr<DownstreamComp> downstreamComp_;
};

TEST_F(RegisterHelperTest, RegisterSuccess)
{
    downstreamComp_->RegisterToUpstream();
    ASSERT_AWAIT_TRUE([&]() { return downstreamComp_->IsRegistered(); });
}

TEST_F(RegisterHelperTest, ReRegisterSuccess)
{
    upstreamComp_->SetRegisterFailureTimes(3);
    downstreamComp_->RegisterToUpstream();
    ASSERT_AWAIT_TRUE([&]() { return downstreamComp_->IsRegistered(); });
}

TEST_F(RegisterHelperTest, ReRegisterTimeout)
{
    upstreamComp_->SetRegisterFailureTimes(3);
    downstreamComp_->RegisterToUpstream(2);
    ASSERT_AWAIT_READY(downstreamComp_->RegisterTimeoutFuture());
    EXPECT_TRUE(downstreamComp_->RegisterTimeoutFuture().Get());
}

TEST_F(RegisterHelperTest, NotReceiveFirstPing)
{
    downstreamComp_->RegisterToUpstream();
    ASSERT_AWAIT_TRUE([&]() { return downstreamComp_->IsRegistered(); });
    upstreamComp_ = nullptr;
    ASSERT_AWAIT_READY(downstreamComp_->HeartbeatTimeoutFuture());
}

TEST_F(RegisterHelperTest, NotReceivePongTimeout)
{
    downstreamComp_->RegisterToUpstream();
    ASSERT_AWAIT_TRUE([&]() { return downstreamComp_->IsRegistered(); });
    downstreamComp_ = nullptr;
    ASSERT_AWAIT_READY(upstreamComp_->HeartbeatTimeoutFuture());
}

}  // namespace functionsystem::test
