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

#include "function_master/global_scheduler/scheduler_manager/domain_sched_mgr.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/constants/actor_name.h"
#include "common/utils/generate_message.h"
#include "mock_domain_sched_srv_actor.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
const long long HEARTBEAT_TIMEOUT = 30000;

class DomainSchedMgrTest : public ::testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }

    static void Registered(litebus::Option<messages::ScheduleTopology> topology, std::string &name,
                           std::string &responseMsg)
    {
        auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                             HEARTBEAT_TIMEOUT);
        auto schedMgr = std::make_shared<functionsystem::global_scheduler::DomainSchedMgr>(actor);
        schedMgr->Start();
        actor->curStatus_ = MASTER_BUSINESS;

        auto domainScheduler = std::make_shared<MockDomainSchedSrvActor>("MockDomainScheduler");
        litebus::Spawn(domainScheduler);

        litebus::Future<std::string> funcName;
        litebus::Future<std::string> registeredResponse;
        EXPECT_CALL(*domainScheduler.get(), MockRegistered(testing::_, testing::_, testing::_))
            .WillOnce(testing::DoAll(FutureArg<1>(&funcName), FutureArg<2>(&registeredResponse)));

        schedMgr->Registered(domainScheduler->GetAID(), topology);

        ASSERT_AWAIT_READY(funcName);
        EXPECT_EQ(funcName.Get(), name);

        ASSERT_AWAIT_READY(registeredResponse);
        EXPECT_EQ(registeredResponse.Get(), responseMsg);

        litebus::Terminate(domainScheduler->GetAID());
        litebus::Await(domainScheduler->GetAID());
        schedMgr->Stop();
    }
};

/**
 * Feature: DomainSchedMgr
 * Description: DomainSchedMgrActor set addCallback
 * Steps:
 * 1. give nullptr
 * 2. given function
 * Expectation:
 * 1. StatusCode::FAILED
 * 2. StatusCode::SUCCESS
 */
TEST_F(DomainSchedMgrTest, AddCallback)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                         HEARTBEAT_TIMEOUT);
    auto schedMgr = std::make_shared<functionsystem::global_scheduler::DomainSchedMgr>(actor);
    schedMgr->Start();

    // given
    global_scheduler::CallbackAddFunc givens[] = {
        nullptr,
        [](const litebus::AID &from, const std::string &name, const std::string &address) {},
    };

    // want
    Status wants[] = {
        Status(StatusCode::FAILED),
        Status(StatusCode::SUCCESS),
    };

    // got
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(global_scheduler::CallbackAddFunc); i++) {
        EXPECT_EQ(schedMgr->AddDomainSchedCallback(givens[i]), wants[i]);
    }
    schedMgr->Stop();
}

/**
 * Feature: DomainSchedMgr
 * Description: DomainSchedMgrActor set delCallback
 * Steps:
 * 1. give nullptr
 * 2. given function
 * Expectation:
 * 1. StatusCode::FAILED
 * 2. StatusCode::SUCCESS
 */
TEST_F(DomainSchedMgrTest, DelCallback)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                         HEARTBEAT_TIMEOUT);
    auto schedMgr = std::make_shared<functionsystem::global_scheduler::DomainSchedMgr>(actor);
    schedMgr->Start();

    // given
    global_scheduler::CallbackDelFunc givens[] = {
        nullptr,
        [](const std::string &name, const std::string &ip) {},
    };

    // want
    Status wants[] = {
        Status(StatusCode::FAILED),
        Status(StatusCode::SUCCESS),
    };

    // got
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(global_scheduler::CallbackAddFunc); i++) {
        EXPECT_EQ(schedMgr->DelDomainSchedCallback(givens[i]), wants[i]);
        EXPECT_EQ(schedMgr->DelLocalSchedCallback(givens[i]), wants[i]);
    }
    schedMgr->Stop();
}

/**
 * Feature: DomainSchedMgr
 * Description: DomainSchedMgrActor inform domain scheduler update topology
 * Steps:
 * Expectation:
 * receive correct message
 */
TEST_F(DomainSchedMgrTest, UpdateSchedTopoView)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                         HEARTBEAT_TIMEOUT);
    auto schedMgr = std::make_shared<functionsystem::global_scheduler::DomainSchedMgr>(actor);
    schedMgr->Start();

    auto domainScheduler = std::make_shared<MockDomainSchedSrvActor>("test" + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX);
    litebus::Spawn(domainScheduler);

    litebus::Future<std::string> funcName;
    litebus::Future<std::string> registeredResponse;
    EXPECT_CALL(*domainScheduler.get(), MockUpdateSchedTopoView(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(FutureArg<1>(&funcName), FutureArg<2>(&registeredResponse)));

    schedMgr->UpdateSchedTopoView("test", domainScheduler->GetAID().Url(), messages::ScheduleTopology());

    ASSERT_AWAIT_READY(funcName);
    EXPECT_TRUE(funcName.Get() == "UpdateSchedTopoView");

    ASSERT_AWAIT_READY(registeredResponse);
    EXPECT_TRUE(registeredResponse.Get() == messages::ScheduleTopology().SerializeAsString());

    litebus::Terminate(domainScheduler->GetAID());
    litebus::Await(domainScheduler->GetAID());
    schedMgr->Stop();
}

/**
 * Feature: DomainSchedMgr
 * Description: DomainSchedMgrActor inform DomainScheduler registered
 * Steps:
 * 1. no topology
 * 2. with topology
 * 3. with topology including contents
 * Expectation:
 * 1. StatusCode::GS_REGISTERED_SCHEDULER_TOPOLOGY_IS_NONE
 * 2. StatusCode::SUCCESS
 * 3. StatusCode::SUCCESS
 */
TEST_F(DomainSchedMgrTest, Registered)
{
    // given
    litebus::Option<::messages::ScheduleTopology> givens[] = {
        {},
        { messages::ScheduleTopology() },
        []() -> litebus::Option<messages::ScheduleTopology> {
            messages::ScheduleTopology topology;
            topology.mutable_leader();
            topology.mutable_members();
            return { topology };
        }(),
    };

    // want
    std::string wants[] = {
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTERED_SCHEDULER_TOPOLOGY_IS_NONE),
                      "topology message is none")
            .SerializeAsString(),
        []() -> std::string {
            auto response = GenRegistered(static_cast<int32_t>(StatusCode::SUCCESS), "registered success");
            response.mutable_topo()->CopyFrom(messages::ScheduleTopology());
            return response.SerializeAsString();
        }(),
        []() -> std::string {
            messages::ScheduleTopology topology;
            topology.mutable_members();
            topology.mutable_leader();
            auto response = GenRegistered(static_cast<int32_t>(StatusCode::SUCCESS), "registered success");
            response.mutable_topo()->CopyFrom(topology);
            return response.SerializeAsString();
        }(),
    };

    // got
    for (uint32_t i = 0; i < sizeof(wants) / sizeof(std::string); i++) {
        std::string funcName = "Registered";
        DomainSchedMgrTest::Registered(givens[i], funcName, wants[i]);
    }
}

/**
 * Feature: DomainSchedMgr
 * Description: DomainSchedMgr call schedule to DomainSchedMgrActor with invalid parameters
 * Steps:
 * 1. request is nullptr
 * 2. requestId is empty
 * Expectation:
 * return StatusCode::FAILED
 * return StatusCode::FAILED
 */
TEST_F(DomainSchedMgrTest, ScheduleWithInvalidParameters)
{
    auto actor = std::make_shared<global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor", HEARTBEAT_TIMEOUT);
    auto client = std::make_shared<global_scheduler::DomainSchedMgr>(actor);
    client->Start();

    // given
    std::shared_ptr<messages::ScheduleRequest> given[] = {
        nullptr,
        std::make_shared<messages::ScheduleRequest>(),
    };

    // want
    Status want[] = {
        Status(StatusCode::FAILED),
        Status(StatusCode::FAILED),
    };

    // got
    for (uint32_t i = 0; i < sizeof(want) / sizeof(Status); i++) {
        EXPECT_TRUE(client->Schedule("", "TestAddress", given[i]).Get() == want[i]);
    }
    client->Stop();
}

/**
 * Feature: DomainSchedMgr
 * Description: DomainSchedMgr call schedule to DomainSchedMgrActor with valid parameters
 * Steps:
 * Expectation:
 * domain scheduler get correct request
 */
TEST_F(DomainSchedMgrTest, ScheduleWithValidParameters)
{
    auto actor = std::make_shared<global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor", HEARTBEAT_TIMEOUT);
    auto client = std::make_shared<global_scheduler::DomainSchedMgr>(actor);
    client->Start();
    actor->curStatus_ = MASTER_BUSINESS;

    std::string domainName = "test";
    auto scheduler = std::make_shared<MockDomainSchedSrvActor>(domainName + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX);
    litebus::Spawn(scheduler);

    litebus::Future<std::string> funcName, scheduleResponse;
    EXPECT_CALL(*scheduler.get(), MockSchedule(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(FutureArg<1>(&funcName), FutureArg<2>(&scheduleResponse)));

    // given
    auto given = std::make_shared<messages::ScheduleRequest>();
    given->set_requestid("requestID");

    // want
    auto want = given->SerializeAsString();

    // got
    client->Schedule(domainName, scheduler->GetAID().Url(), given);
    EXPECT_TRUE(funcName.Get() == "Schedule");
    EXPECT_TRUE(scheduleResponse.Get() == want);

    litebus::Terminate(scheduler->GetAID());
    litebus::Await(scheduler->GetAID());
    client->Stop();
}

/**
 * Feature: DomainSchedMgr
 * Description: DomainSchedMgr call schedule to DomainSchedMgrActor repeat when not receive response
 * Steps:
 * Expectation:
 * domain scheduler get correct request
 */
TEST_F(DomainSchedMgrTest, ScheduleRepeat)
{
    auto actor = std::make_shared<global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor", HEARTBEAT_TIMEOUT);
    auto client = std::make_shared<global_scheduler::DomainSchedMgr>(actor);
    client->Start();
    actor->curStatus_ = MASTER_BUSINESS;

    std::string domainName = "test";
    auto scheduler = std::make_shared<MockDomainSchedSrvActor>(domainName + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX);
    litebus::Spawn(scheduler);

    litebus::Future<std::string> firstName;
    litebus::Future<std::string> secondName;
    int count = 0;
    litebus::Promise<bool> promise;
    auto isRetried = promise.GetFuture();
    EXPECT_CALL(*scheduler.get(), MockSchedule(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::Invoke([&count, promise](const litebus::AID from, std::string name, std::string msg) {
            EXPECT_EQ(name, "Schedule");
            count++;
            if (count >= 2) {
                promise.SetValue(true);
            }
            return;
        }));

    // given
    auto given = std::make_shared<messages::ScheduleRequest>();
    given->set_requestid("requestID");

    // want
    auto want = given->SerializeAsString();

    // got
    client->Schedule(domainName, scheduler->GetAID().Url(), given, 1);

    ASSERT_AWAIT_READY_FOR(isRetried, 1000);
    EXPECT_EQ(isRetried.Get(), true);
    EXPECT_GE(count, 1);
    litebus::Terminate(scheduler->GetAID());
    litebus::Await(scheduler->GetAID());
    client->Stop();
}

}  // namespace functionsystem::test