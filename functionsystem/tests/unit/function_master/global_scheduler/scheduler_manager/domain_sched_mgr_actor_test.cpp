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

#include "function_master/global_scheduler/scheduler_manager/domain_sched_mgr_actor.h"

#include "common/constants/actor_name.h"
#include "common/heartbeat/heartbeat_client.h"
#include "common/utils/generate_message.h"
#include "domain_scheduler/domain_scheduler_service/uplayer_stub.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mock_domain_sched_srv_actor.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"

namespace functionsystem::test {
const long long HEARTBEAT_TIMEOUT = 30000;

using namespace functionsystem::global_scheduler;

class DomainSchedMgrActorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }

public:
    static void DomainSchedulerRegister(std::string &&name, std::string &&responseMsg, std::string &&registerMsg)
    {
        auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                             HEARTBEAT_TIMEOUT);
        litebus::Spawn(actor);

        litebus::Async(actor->GetAID(), &DomainSchedMgrActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));

        auto scheduler = std::make_shared<MockDomainSchedSrvActor>("MockDomainScheduler");
        litebus::Spawn(scheduler);

        litebus::Future<std::string> funcName;
        litebus::Future<std::string> registeredResponse;
        EXPECT_CALL(*scheduler.get(), MockRegistered(testing::_, testing::_, testing::_))
            .WillOnce(testing::DoAll(FutureArg<1>(&funcName), FutureArg<2>(&registeredResponse)));

        litebus::Async(scheduler->GetAID(), &MockDomainSchedSrvActor::RegisterToGlobalScheduler, actor->GetAID(),
                       std::move(registerMsg));

        ASSERT_AWAIT_READY(funcName);
        EXPECT_TRUE(funcName.Get() == name);

        ASSERT_AWAIT_READY(registeredResponse);
        EXPECT_TRUE(registeredResponse.Get() == responseMsg);

        litebus::Terminate(actor->GetAID());
        litebus::Terminate(scheduler->GetAID());
        litebus::Await(actor->GetAID());
        litebus::Await(scheduler->GetAID());
    }
};

/**
 * Feature: DomainSchedMgrActor
 * Description: domain scheduler register to DomainSchedMgrActor with Invalid request
 * Steps:
 * 1. give ""
 * 2. give "~"
 * 3. give without name
 * 4. give without address
 * Expectation:
 * 1. StatusCode::GS_REGISTER_REQUEST_INVALID
 * 2. StatusCode::GS_REGISTER_REQUEST_INVALID
 * 3. StatusCode::GS_REGISTER_REQUEST_INVALID
 * 4. StatusCode::GS_REGISTER_REQUEST_INVALID
 */
TEST_F(DomainSchedMgrActorTest, DomainSchedulerRegisterWithInvalidRequest)
{
    // given
    std::string givens[] = {
        "",
        "~",
        GenRegister("", "TestAddress").SerializeAsString(),
        GenRegister("TestName", "").SerializeAsString(),
    };

    // want
    std::string wants[] = {
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTER_REQUEST_INVALID), "invalid request message")
            .SerializeAsString(),
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTER_REQUEST_INVALID), "invalid request message")
            .SerializeAsString(),
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTER_REQUEST_INVALID), "invalid request message")
            .SerializeAsString(),
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTER_REQUEST_INVALID), "invalid request message")
            .SerializeAsString(),
    };

    // got
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(std::string); i++) {
        DomainSchedMgrActorTest::DomainSchedulerRegister("Registered", std::move(wants[i]), std::move(givens[i]));
    }
}

/**
 * Feature: DomainSchedMgrActor
 * Description: domain scheduler register to DomainSchedMgrActor with Valid request
 * Steps:
 * 1. give correct request
 * Expectation:
 * 1. call callback function with correct parameters
 */
TEST_F(DomainSchedMgrActorTest, DomainSchedulerRegisterWithValidRequest)
{
    // given
    std::string givens[] = {
        GenRegister("TestName", "TestAddress").SerializeAsString(),
    };

    // want
    std::string wants[][2] = {
        { "TestName", "TestAddress" },
    };

    // got
    auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                         HEARTBEAT_TIMEOUT);
    auto scheduler = std::make_shared<MockDomainSchedSrvActor>("MockDomainScheduler");
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(std::string); i++) {
        auto wantName = wants[i][0];
        auto wantAddress = wants[i][1];
        actor->AddDomainSchedCallback(
            [wantName, wantAddress](const litebus::AID &from, const std::string &name, const std::string &address) {
                EXPECT_TRUE(wantName == name);
                EXPECT_TRUE(wantAddress == address);
            });
        litebus::Async(scheduler->GetAID(), &MockDomainSchedSrvActor::RegisterToGlobalScheduler, actor->GetAID(),
                       std::move(givens[i]));
    }
}

/**
 * Feature: DomainSchedMgrActor
 * Description: domain scheduler inform DomainSchedMgrActor abort scheduler abnormal
 * Steps:
 * 1. give ""
 * 2. given "~"
 * 3. set schedname: LOCAL_SCHED_SRV_ACTOR_NAME
 * 4. set schedname: LOCAL_SCHED_SRV_ACTOR_NAMEabc
 * 5. set schedname: DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX
 * 6. set schedname: DOMAIN_SCHED_SRV_ACTOR_NAMEabc
 * Expectation:
 * 1. does not call callback function
 * 2. does not call callback function
 * 3. call DelLocalSchedCallback function with LOCAL_SCHED_SRV_ACTOR_NAME
 * 4. call DelLocalSchedCallback function with LOCAL_SCHED_SRV_ACTOR_NAMEabc
 * 5. call DelDomainSchedCallback function with DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX
 * 6. call DelDomainSchedCallback function with DOMAIN_SCHED_SRV_ACTOR_NAMEabc
 */
TEST_F(DomainSchedMgrActorTest, NotifySchedAbnormal)
{
    // given
    std::string givens[] = {
        "",
        // "~",
        GenNotifySchedAbnormalRequest(LOCAL_SCHED_SRV_ACTOR_NAME).SerializeAsString(),
        GenNotifySchedAbnormalRequest(LOCAL_SCHED_SRV_ACTOR_NAME + "abc").SerializeAsString(),
        GenNotifySchedAbnormalRequest(DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX).SerializeAsString(),
        GenNotifySchedAbnormalRequest(DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX + "abc").SerializeAsString(),
    };

    // want
    std::string wants[] = {
        "",
        // "",
        LOCAL_SCHED_SRV_ACTOR_NAME,
        LOCAL_SCHED_SRV_ACTOR_NAME + "abc",
        DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX,
        DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX + "abc",
    };

    // got
    for (uint32_t i = 0; i < sizeof(wants) / sizeof(std::string); i++) {
        auto actor = std::make_shared<global_scheduler::DomainSchedMgrActor>("TestDomainSchedActor", HEARTBEAT_TIMEOUT);
        auto scheduler = std::make_shared<MockDomainSchedSrvActor>("MockDomainScheduler");
        litebus::Spawn(actor);
        litebus::Spawn(scheduler);

        litebus::Async(actor->GetAID(), &DomainSchedMgrActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));

        auto wantName = wants[i];

        actor->DelDomainSchedCallback(
            [wantName](const std::string &name, const std::string &ip) { EXPECT_TRUE(wantName == name); });

        actor->DelLocalSchedCallback(
            [wantName](const std::string &name, const std::string &ip) { EXPECT_TRUE(wantName == name); });

        litebus::Future<std::string> funcName;
        EXPECT_CALL(*scheduler.get(), MockResponseNotifySchedAbnormal(testing::_, testing::_, testing::_))
            .WillOnce(testing::DoAll(FutureArg<1>(&funcName)));

        litebus::Async(scheduler->GetAID(), &MockDomainSchedSrvActor::NotifySchedAbnormal, actor->GetAID(),
                       std::move(givens[i]));
        ASSERT_AWAIT_READY(funcName);
        EXPECT_TRUE(funcName.Get() == "ResponseNotifySchedAbnormal");

        litebus::Terminate(actor->GetAID());
        litebus::Terminate(scheduler->GetAID());
        litebus::Await(actor->GetAID());
        litebus::Await(scheduler->GetAID());
    }
}

// test Notify worker status from other scheduler
TEST_F(DomainSchedMgrActorTest, NotifyWorkerStatus)
{
    auto actor = std::make_shared<global_scheduler::DomainSchedMgrActor>("TestDomainSchedActor", HEARTBEAT_TIMEOUT);
    auto scheduler = std::make_shared<MockDomainSchedSrvActor>("MockDomainScheduler");
    litebus::Spawn(actor);
    litebus::Spawn(scheduler);
    litebus::Async(actor->GetAID(), &DomainSchedMgrActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));
    litebus::Future<std::string> funcName;
    EXPECT_CALL(*scheduler.get(), MockResponseNotifyWorkerStatus(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(FutureArg<1>(&funcName)));
    messages::NotifyWorkerStatusRequest request;
    request.set_healthy(true);
    request.set_workerip("10.10.10.10");
    litebus::Async(scheduler->GetAID(), &MockDomainSchedSrvActor::NotifyWorkerStatus, actor->GetAID(),
                   request.SerializeAsString());
    ASSERT_AWAIT_READY(funcName);
    EXPECT_TRUE(funcName.Get() == "ResponseNotifyWorkerStatus");

    litebus::Terminate(actor->GetAID());
    litebus::Terminate(scheduler->GetAID());
    litebus::Await(actor->GetAID());
    litebus::Await(scheduler->GetAID());
}

/**
 * Feature: DomainSchedMgrActor
 * Description: domain scheduler send query agent resquest
 * Steps:
 * 1.
 * 2. given normal scheduleRequest but requestID is not existed
 * Expectation:
 */
TEST_F(DomainSchedMgrActorTest, QueryAgentInfo)
{
    auto actor = std::make_shared<global_scheduler::DomainSchedMgrActor>("TestDomainSchedActor", HEARTBEAT_TIMEOUT);
    auto scheduler =
        std::make_shared<MockDomainSchedSrvActor>("MockDomainScheduler" + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX);
    litebus::Spawn(actor);
    litebus::Spawn(scheduler);
    auto req = std::make_shared<messages::QueryAgentInfoRequest>();
    req->set_requestid("request");
    EXPECT_CALL(*scheduler.get(), MockQueryAgentInfo(testing::_, testing::_, testing::_)).Times(1);
    auto future = litebus::Async(actor->GetAID(), &DomainSchedMgrActor::QueryAgentInfo, "MockDomainScheduler",
                                 scheduler->GetAID().Url(), req);

    scheduler->ResponseQueryAgentInfo(actor->GetAID(), "");
    messages::QueryAgentInfoResponse rsp;
    rsp.set_requestid("request");
    scheduler->ResponseQueryAgentInfo(actor->GetAID(), rsp.SerializeAsString());

    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    litebus::Terminate(actor->GetAID());
    litebus::Terminate(scheduler->GetAID());
    litebus::Await(actor->GetAID());
    litebus::Await(scheduler->GetAID());
}

TEST_F(DomainSchedMgrActorTest, GetSchedulingQueue)
{
    auto actor = std::make_shared<global_scheduler::DomainSchedMgrActor>("TestDomainSchedActor", HEARTBEAT_TIMEOUT);
    auto scheduler =
        std::make_shared<MockDomainSchedSrvActor>("MockDomainScheduler" + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX);
    litebus::Spawn(actor);
    litebus::Spawn(scheduler);

    auto req = std::make_shared<messages::QueryInstancesInfoRequest>();
    req->set_requestid("request");
    EXPECT_CALL(*scheduler.get(), MockGetSchedulingQueue(testing::_, testing::_, testing::_)).Times(1);

    auto future = litebus::Async(actor->GetAID(), &DomainSchedMgrActor::GetSchedulingQueue, "MockDomainScheduler",
                                 scheduler->GetAID().Url(), req);

    scheduler->ResponseGetSchedulingQueue(actor->GetAID(), "");
    messages::QueryInstancesInfoResponse rsp;
    rsp.set_requestid("request");
    scheduler->ResponseGetSchedulingQueue(actor->GetAID(), rsp.SerializeAsString());

    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    litebus::Terminate(actor->GetAID());
    litebus::Terminate(scheduler->GetAID());
    litebus::Await(actor->GetAID());
    litebus::Await(scheduler->GetAID());
}

TEST_F(DomainSchedMgrActorTest, QueryResourcesInfo)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                         HEARTBEAT_TIMEOUT);
    litebus::Spawn(actor);

    auto scheduler =
        std::make_shared<MockDomainSchedSrvActor>("MockDomainScheduler" + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX);
    litebus::Spawn(scheduler);
    auto req = std::make_shared<messages::QueryResourcesInfoRequest>();
    req->set_requestid("request");
    auto future = litebus::Async(actor->GetAID(), &DomainSchedMgrActor::QueryResourcesInfo, "MockDomainScheduler",
                                 scheduler->GetAID().Url(), req);

    messages::QueryResourcesInfoResponse rsp;
    rsp.set_requestid("request");
    scheduler->ResponseQueryResourcesInfo(actor->GetAID(), rsp.SerializeAsString());

    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    litebus::Terminate(actor->GetAID());
    litebus::Terminate(scheduler->GetAID());
    litebus::Await(scheduler->GetAID());
    litebus::Await(actor->GetAID());
}

/**
 * Feature: DomainSchedMgrActor
 * Description: domain scheduler receive schedule response
 * Steps:
 * 1. give empty str
 * 2. given normal scheduleRequest but requestID is not existed
 * Expectation:
 */
TEST_F(DomainSchedMgrActorTest, ResponseScheduleWithInvalidResponse)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                         HEARTBEAT_TIMEOUT);
    actor->Init();
    actor->ResponseSchedule("domainSchedA", "ResponseSchedule", "");
    messages::ScheduleResponse response;
    response.set_requestid("request-123");
    actor->ResponseSchedule("domainSchedA", "ResponseSchedule", std::move(response.SerializeAsString()));
}

/**
 * Feature: DomainSchedMgrActor
 * Description: connect to an address
 * Steps:
 * 1. connect
 * 2. disconnect
 * Expectation:
 */
TEST_F(DomainSchedMgrActorTest, ConnectFail)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                         HEARTBEAT_TIMEOUT);
    actor->DelDomainSchedCallback([](const std::string &name, const std::string &ip) { EXPECT_TRUE(name == "test"); });
    litebus::Spawn(actor);

    litebus::Async(actor->GetAID(), &DomainSchedMgrActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));

    litebus::Async(actor->GetAID(), &DomainSchedMgrActor::Connect, "test", "127.0.0.1:9999");
    litebus::Async(actor->GetAID(), &DomainSchedMgrActor::Disconnect);
    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
}

/**
 * Feature: DomainSchedMgrActor
 * Description: re-connect to an address
 * Steps:
 * 1. connect
 * 2. re-connect
 * Expectation:
 */
TEST_F(DomainSchedMgrActorTest, ReConnect)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::DomainSchedMgrActor>("TestDomainSchedMgrActor",
                                                                                         HEARTBEAT_TIMEOUT);
    actor->DelDomainSchedCallback([](const std::string &name, const std::string &ip) { EXPECT_TRUE(name == "test"); });
    litebus::Spawn(actor);

    litebus::Async(actor->GetAID(), &DomainSchedMgrActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));
    HeartbeatClientDriver pingpong("pinged", [](const litebus::AID &aid) {});
    pingpong.Start(actor->GetAID());
    litebus::Async(actor->GetAID(), &DomainSchedMgrActor::Connect, "pinged",
                   pingpong.GetActorAID().GetIp() + ":" + std::to_string(pingpong.GetActorAID().GetPort()))
        .Get();
    litebus::Async(actor->GetAID(), &DomainSchedMgrActor::Connect, "pinged",
                   pingpong.GetActorAID().GetIp() + ":" + std::to_string(pingpong.GetActorAID().GetPort()))
        .Get();
    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
}

}  // namespace functionsystem::test
