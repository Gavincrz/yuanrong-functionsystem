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

#include <async/future.hpp>

#include "utils/port_helper.h"

#define private public
#include "../../../integration/utils/future_test_helper.h"
#include "common/heartbeat/heartbeat_client.h"
#include "common/heartbeat/heartbeat_observer.h"

namespace functionsystem::test {
class Heartbeat : public ::testing::Test {};

TEST_F(Heartbeat, ObserverWithInvalidActorName)
{
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::AID aid(HEARTBEAT_OBSERVER_BASENAME + "Test", "127.0.0.1:" + std::to_string(port));
    aid.SetProtocol(litebus::BUS_TCP);
    auto heartbeatClient = std::make_shared<HeartbeatClient>("pinger", 3, 50, [](const litebus::AID &) {});
    litebus::Spawn(heartbeatClient);
    heartbeatClient->Start(aid);
    EXPECT_EQ(aid, heartbeatClient->dst_);
    EXPECT_TRUE(heartbeatClient->GetStatus().IsOk());
    ASSERT_AWAIT_TRUE([=]() -> bool {
        return heartbeatClient->GetStatus().IsError();
    });
    heartbeatClient->Stop();
    litebus::Terminate(heartbeatClient->GetAID());
    litebus::Await(heartbeatClient);
}

TEST_F(Heartbeat, ObserverSuccessWithoutAddNode)
{
    auto observer = std::make_shared<HeartbeatObserveDriver>("Test");
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::AID aid(HEARTBEAT_OBSERVER_BASENAME + "Test", "127.0.0.1:" + std::to_string(port));
    aid.SetProtocol(litebus::BUS_TCP);
    auto heartbeatClient = std::make_shared<HeartbeatClient>("pinger", 3, 100, [](const litebus::AID &) {});
    litebus::Spawn(heartbeatClient);
    heartbeatClient->Start(aid);
    EXPECT_EQ(aid, heartbeatClient->dst_);
    EXPECT_TRUE(heartbeatClient->GetStatus().IsOk());
    ASSERT_AWAIT_TRUE([=]() -> bool {
        return heartbeatClient->GetStatus().IsOk();
    });
    litebus::Terminate(heartbeatClient->GetAID());
}

TEST_F(Heartbeat, ObserverSuccessAddNode)
{
    auto observer = std::make_shared<HeartbeatObserveDriver>("Test");
    bool heartbeatLost = false;
    observer->AddNewHeartbeatNode(
        HEARTBEAT_CLIENT_BASENAME + "pinger", 300,
        [&heartbeatLost](const litebus::AID &, HeartbeatConnection) { heartbeatLost = true; });
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::AID aid(HEARTBEAT_OBSERVER_BASENAME + "Test", "127.0.0.1:" + std::to_string(port));
    aid.SetProtocol(litebus::BUS_TCP);
    auto heartbeatClient = std::make_shared<HeartbeatClient>("pinger", 3, 100, [](const litebus::AID &) {});
    litebus::Spawn(heartbeatClient);
    heartbeatClient->Start(aid);
    EXPECT_EQ(aid, heartbeatClient->dst_);
    EXPECT_TRUE(heartbeatClient->GetStatus().IsOk());
    ASSERT_AWAIT_TRUE([=]() -> bool {
        return heartbeatClient->GetStatus().IsOk();
    });
    litebus::Terminate(heartbeatClient->GetAID());
}

TEST_F(Heartbeat, ObserverSuccessClientDied)
{
    auto observer = std::make_shared<HeartbeatObserveDriver>("Test");
    bool heartbeatLost = false;
    HeartbeatConnection reason = HeartbeatConnection::EXITED;
    observer->AddNewHeartbeatNode(HEARTBEAT_CLIENT_BASENAME + "pinger", 300,
                                  [&heartbeatLost, &reason](const litebus::AID &, HeartbeatConnection connection) {
                                      heartbeatLost = true;
                                      reason = connection;
                                  });
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::AID aid(HEARTBEAT_OBSERVER_BASENAME + "Test", "127.0.0.1:" + std::to_string(port));
    aid.SetProtocol(litebus::BUS_TCP);
    auto heartbeatClient = std::make_shared<HeartbeatClient>("pinger", 3, 100, [](const litebus::AID &) {});
    litebus::Spawn(heartbeatClient);
    heartbeatClient->Start(aid);
    EXPECT_EQ(aid, heartbeatClient->dst_);
    EXPECT_TRUE(heartbeatClient->GetStatus().IsOk());
    litebus::Terminate(heartbeatClient->GetAID());
    litebus::Await(heartbeatClient->GetAID());
    ASSERT_AWAIT_TRUE([&heartbeatLost]() -> bool {
        return heartbeatLost;
    });
    EXPECT_EQ(reason, HeartbeatConnection::LOST);
}

TEST_F(Heartbeat, ObserverSuccessObserverDied)
{
    auto observer =
        std::make_shared<HeartbeatObserver>(HEARTBEAT_OBSERVER_BASENAME + "Test", DEFAULT_PING_PONG_TIMEOUT);
    litebus::Spawn(observer);
    bool heartbeatLost = false;
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::AID aid(HEARTBEAT_OBSERVER_BASENAME + "Test", "127.0.0.1:" + std::to_string(port));
    aid.SetProtocol(litebus::BUS_TCP);
    auto heartbeatClient = std::make_shared<HeartbeatClient>(
        "pinger", 2, 100, [&heartbeatLost](const litebus::AID &) { heartbeatLost = true; });
    litebus::Spawn(heartbeatClient);
    heartbeatClient->Start(aid);
    EXPECT_EQ(aid, heartbeatClient->dst_);
    EXPECT_TRUE(heartbeatClient->GetStatus().IsOk());
    litebus::Terminate(observer->GetAID());
    litebus::Await(observer->GetAID());
    ASSERT_AWAIT_TRUE([&heartbeatLost]() -> bool {
        return heartbeatLost;
    });
    litebus::Terminate(heartbeatClient->GetAID());
    litebus::Await(heartbeatClient->GetAID());
}

TEST_F(Heartbeat, ObserverSuccessClinetExit)
{
    auto observer = std::make_shared<HeartbeatObserveDriver>("Test");
    bool heartbeatLost = false;
    HeartbeatConnection reason = HeartbeatConnection::LOST;
    observer->AddNewHeartbeatNode(HEARTBEAT_CLIENT_BASENAME + "pinger", 300,
                                  [&heartbeatLost, &reason](const litebus::AID &, HeartbeatConnection connection) {
                                      heartbeatLost = true;
                                      reason = connection;
                                  });
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::AID aid(HEARTBEAT_OBSERVER_BASENAME + "Test", "127.0.0.1:" + std::to_string(port));
    aid.SetProtocol(litebus::BUS_TCP);
    auto heartbeatClient = std::make_shared<HeartbeatClient>("pinger", 3, 100, [](const litebus::AID &) {});
    litebus::Spawn(heartbeatClient);
    heartbeatClient->Start(aid);
    EXPECT_EQ(aid, heartbeatClient->dst_);
    EXPECT_TRUE(heartbeatClient->GetStatus().IsOk());
    heartbeatClient->Stop();
    ASSERT_AWAIT_TRUE([&heartbeatLost]() -> bool {
        return heartbeatLost;
    });
    EXPECT_EQ(reason, HeartbeatConnection::LOST);
    litebus::Terminate(heartbeatClient->GetAID());
    litebus::Await(heartbeatClient->GetAID());
}

TEST_F(Heartbeat, ObserverResetHandler)
{
    auto observer = std::make_shared<HeartbeatObserveDriver>("Test");
    bool heartbeatLost = false;
    HeartbeatConnection reason = HeartbeatConnection::LOST;
    observer->AddNewHeartbeatNode(HEARTBEAT_CLIENT_BASENAME + "pinger", 300,
                                  [&heartbeatLost, &reason](const litebus::AID &, HeartbeatConnection connection) {
                                      heartbeatLost = true;
                                      reason = connection;
                                  });
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::AID aid(HEARTBEAT_OBSERVER_BASENAME + "Test", "127.0.0.1:" + std::to_string(port));
    aid.SetProtocol(litebus::BUS_TCP);
    auto heartbeatClient = std::make_shared<HeartbeatClient>("pinger", 3, 100, [](const litebus::AID &) {});
    litebus::Spawn(heartbeatClient);
    heartbeatClient->Start(aid);
    EXPECT_EQ(aid, heartbeatClient->dst_);
    EXPECT_TRUE(heartbeatClient->GetStatus().IsOk());
    heartbeatClient->Stop();
    for (auto i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        observer->ResetHeartbeatNode(HEARTBEAT_CLIENT_BASENAME + "pinger", 300,
                                     [&heartbeatLost, &reason](const litebus::AID &, HeartbeatConnection connection) {
                                         heartbeatLost = true;
                                         reason = connection;
                                     });
    }
    EXPECT_FALSE(heartbeatLost);
    ASSERT_AWAIT_TRUE([&heartbeatLost]() -> bool {
        return heartbeatLost;
    });
    litebus::Terminate(heartbeatClient->GetAID());
    litebus::Await(heartbeatClient->GetAID());
}
}  // namespace functionsystem::test
