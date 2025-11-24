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

#include <async/async.hpp>
#include <async/future.hpp>

#include "common/constants/actor_name.h"
#include "common/explorer/explorer.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/view_utils.h"
#include "common/utils/generate_message.h"
#include "constants.h"
#include "domain_sched_stub_actor.h"
#include "global_sched_stub_actor.h"
#include "group_manager_stub_actor.h"
#include "instance_manager_stub_actor.h"
#include "mocks/group_ctrl_stub_actor.h"
#include "local_sched_srv_actor_test_driver.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_ping_pong_driver.h"
#include "mocks/mock_resource_view.h"
#include "mocks/mock_function_agent_mgr.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"

#include "local_scheduler/local_scheduler_service/local_sched_srv_actor.h"

using namespace ::testing;

namespace functionsystem::test {
using namespace functionsystem::local_scheduler;
using namespace functionsystem::explorer;
class LocalSchedSrvActorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        (void)explorer::Explorer::NewStandAloneExplorerActorForMaster(explorer::ElectionInfo{}, GetLeaderInfo(litebus::AID("function_master")));
        driverActor_ = std::make_shared<LocalSchedSrvActorTestDriver>();
        litebus::Spawn(driverActor_);

        functionAgentMgr_ = std::make_shared<MockFunctionAgentMgr>("FunctionAgentMgr", nullptr);
        subscriptionMgr_ = SubscriptionMgr::Init("SubscriptionMgr", SubscriptionMgrConfig{ .isPartialWatchInstances = true });
        mockInstanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        LocalSchedSrvActor::Param param = { .nodeID = "localSchedSrvDstActor",
                                            .globalSchedAddress = driverActor_->GetAID().UnfixUrl(),
                                            .isK8sEnabled = true,
                                            .registerCycleMs = 200,
                                            .pingTimeOutMs = 5000,
                                            .updateResourceCycleMs = 1000,
                                            .forwardRequestTimeOutMs = 200,
                                            .groupScheduleTimeout = 100,
                                            .groupKillTimeout = 100 };
        dstActor_ = std::make_shared<LocalSchedSrvActor>(param);
        auto resourceViewMgr = std::make_shared<resource_view::ResourceViewMgr>();
        primary_ = MockResourceView::CreateMockResourceView();
        virtual_ = MockResourceView::CreateMockResourceView();
        resourceViewMgr->primary_ = primary_;
        resourceViewMgr->virtual_ = virtual_;
        dstActor_->BindResourceView(resourceViewMgr);
        dstActor_->BindInstanceCtrl(mockInstanceCtrl_);
        dstActor_->BindFunctionAgentMgr(functionAgentMgr_);
        dstActor_->BindSubscriptionMgr(subscriptionMgr_);
        litebus::Spawn(dstActor_);

        globalSchedStubActor_ = std::make_shared<GlobalSchedStubActor>(LOCAL_SCHED_MGR_ACTOR_NAME);
        litebus::Spawn(globalSchedStubActor_);

        domainSchedStubActor_ = std::make_shared<DomainSchedStubActor>(REGISTERED_DOMAIN_SCHED_NAME +
                                                                       DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
        litebus::Spawn(domainSchedStubActor_);

        YRLOG_INFO("doamin stub actor: {}", std::string(domainSchedStubActor_->GetAID()));

        messages::ScheduleTopology topo;
        ::messages::ScheduleTopology_Scheduler leader;
        leader.set_name(REGISTERED_DOMAIN_SCHED_NAME);
        leader.set_address(domainSchedStubActor_->GetAID().UnfixUrl());
        topo.mutable_leader()->CopyFrom(leader);
        litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::UpdateDomainSchedulerAddress,
                       domainSchedStubActor_->GetAID());
        litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::UpdateGlobalSchedulerAddress,
                       globalSchedStubActor_->GetAID());
        litebus::Async(globalSchedStubActor_->GetAID(), &GlobalSchedStubActor::UpdateSchedTopoView, dstActor_->GetAID(),
                       topo);
    }

    void TearDown() override
    {
        litebus::Terminate(dstActor_->GetAID());
        litebus::Terminate(driverActor_->GetAID());
        litebus::Terminate(globalSchedStubActor_->GetAID());
        litebus::Terminate(domainSchedStubActor_->GetAID());

        litebus::Await(dstActor_);
        litebus::Await(driverActor_);
        litebus::Await(globalSchedStubActor_);
        litebus::Await(domainSchedStubActor_);

        explorer::Explorer::GetInstance().Clear();

        dstActor_ = nullptr;
        driverActor_ = nullptr;
        mockInstanceCtrl_ = nullptr;
        globalSchedStubActor_ = nullptr;
        domainSchedStubActor_ = nullptr;
        primary_ = nullptr;
        virtual_ = nullptr;
    }

    void RegisterLocalScheduler()
    {
        // registry response of global scheduler
        messages::Registered registeredToGlobal;
        registeredToGlobal.set_code(StatusCode::SUCCESS);
        registeredToGlobal.set_message(REGISTERED_GLOBAL_SCHED_SUCCESS_MSG);
        messages::ScheduleTopology topo;
        topo.mutable_leader()->set_name(REGISTERED_DOMAIN_SCHED_NAME);
        topo.mutable_leader()->set_address(domainSchedStubActor_->GetAID().UnfixUrl());
        registeredToGlobal.mutable_topo()->CopyFrom(topo);
        EXPECT_CALL(*globalSchedStubActor_.get(), MockRegister)
            .WillOnce(Return(registeredToGlobal.SerializeAsString()));

        // registry response of domain scheduler
        messages::Registered registeredToDomain;
        registeredToDomain.set_code(StatusCode::SUCCESS);
        registeredToDomain.set_message(REGISTERED_DOMAIN_SCHED_SUCCESS_MSG);
        EXPECT_CALL(*domainSchedStubActor_.get(), MockRegister)
            .WillRepeatedly(Return(registeredToDomain.SerializeAsString()));

        auto unit = view_utils::Get1DResourceUnit();
        EXPECT_CALL(*primary_, GetFullResourceView())
            .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(unit))));
        EXPECT_CALL(*virtual_, GetFullResourceView())
            .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(unit))));

        litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::UpdateMasterInfo,
                       GetLeaderInfo(globalSchedStubActor_->GetAID()));
        litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::ToReady);
        ASSERT_AWAIT_TRUE([=]() -> bool {
            auto enable = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::GetEnableFlag).Get();
            return enable;
        });
        ASSERT_AWAIT_TRUE([=]() -> bool { return !dstActor_->HeartBeatInvalid(); });
        auto future = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::IsRegisteredToGlobal);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().IsOk(), true);
    }

protected:
    std::shared_ptr<MockInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockResourceView> primary_;
    std::shared_ptr<MockResourceView> virtual_;
    std::shared_ptr<LocalSchedSrvActor> dstActor_;
    std::shared_ptr<LocalSchedSrvActorTestDriver> driverActor_;
    std::shared_ptr<GlobalSchedStubActor> globalSchedStubActor_;
    std::shared_ptr<DomainSchedStubActor> domainSchedStubActor_;
    std::shared_ptr<MockFunctionAgentMgr> functionAgentMgr_;
    std::shared_ptr<SubscriptionMgr> subscriptionMgr_;
};

// test for LocalSchedSrvActor::Schedule
// receive schedule instance request from domain scheduler
TEST_F(LocalSchedSrvActorTest, ScheduleSuccess)
{
    RegisterLocalScheduler();
    auto successCode = StatusCode::SUCCESS;
    auto successMsg = "schedule success";
    auto instanceID = "instanceA";
    auto requestID = "requestA";

    messages::ScheduleResponse rsp;
    rsp.set_code(successCode);
    rsp.set_message(successMsg);
    rsp.set_instanceid(instanceID);
    rsp.set_requestid(requestID);
    EXPECT_CALL(*mockInstanceCtrl_, Schedule).WillOnce(testing::Return(AsyncReturn(rsp)));
    auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
    EXPECT_CALL(*primary_, GetResourceViewChanges()).WillRepeatedly(Return(AsyncReturn(changes)));
    EXPECT_CALL(*virtual_, GetResourceViewChanges()).WillRepeatedly(Return(AsyncReturn(changes)));
    dstActor_->domainSchedRegisterInfo_.aid = driverActor_->GetAID();
    messages::ScheduleRequest req;
    req.set_requestid(requestID);
    auto rspFuture =
        litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::Schedule, dstActor_->GetAID(), req);
    const auto &getRsp = rspFuture.Get();
    EXPECT_EQ(getRsp.code(), successCode);
    EXPECT_EQ(getRsp.message(), successMsg);
    EXPECT_EQ(getRsp.instanceid(), instanceID);
    EXPECT_EQ(getRsp.requestid(), requestID);
}

/**
 * Feature: LocalSchedSrvActor
 * Description: Simulates receiving a Schedule request from DomainSchedule and LocalScheduler resource not enough
 * Steps:
 * 1. mock InstanceCtrl Schedule with resource not enough.
 * 2. send Schedule request to LocalSchedSrvActorTestDriver(LocalSchedSrvActorTestDriver simulates DomainScheduler
 *    and send schedule request to LocalSchedSrvActor).
 * 3. get Schedule result.
 * Expectation: schedule error code result is ScheduleResourceNotEnough
 */
TEST_F(LocalSchedSrvActorTest, ScheduleResourceNotEnough)
{
    RegisterLocalScheduler();
    auto successCode = StatusCode::ERR_RESOURCE_NOT_ENOUGH;
    auto successMsg = "CPU is not enough";
    auto instanceID = "instanceA";
    auto requestID = "requestA";

    messages::ScheduleResponse rsp;
    rsp.set_code(successCode);
    rsp.set_message(successMsg);
    rsp.set_instanceid(instanceID);
    rsp.set_requestid(requestID);
    EXPECT_CALL(*mockInstanceCtrl_, Schedule).WillOnce(testing::Return(AsyncReturn(rsp)));
    auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
    EXPECT_CALL(*primary_, GetResourceViewChanges()).WillRepeatedly(Return(AsyncReturn(changes)));
    EXPECT_CALL(*virtual_, GetResourceViewChanges()).WillRepeatedly(Return(AsyncReturn(changes)));
    dstActor_->domainSchedRegisterInfo_.aid = driverActor_->GetAID();
    messages::ScheduleRequest req;
    req.set_requestid(requestID);
    auto rspFuture =
        litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::Schedule, dstActor_->GetAID(), req);
    const auto &getRsp = rspFuture.Get();
    EXPECT_EQ(getRsp.code(), successCode);
    EXPECT_EQ(getRsp.message(), successMsg);
    EXPECT_EQ(getRsp.instanceid(), instanceID);
    EXPECT_EQ(getRsp.requestid(), requestID);
}
#if 0
/**
 * Feature: LocalSchedSrvActor
 * Description: Simulates receiving a Schedule request from DomainSchedule and LocalScheduler resource not enough
 * Steps:
 * 1. ScheduleRequest param is empty requestID
 * 2. send Schedule request to LocalSchedSrvActorTestDriver(LocalSchedSrvActorTestDriver simulates DomainScheduler
 *    and send schedule request to LocalSchedSrvActor).
 * 3. get Schedule result.
 * Expectation: schedule error code result is ScheduleResourceNotEnough
 */
TEST_F(LocalSchedSrvActorTest, ScheduleWithEmptyRequest)
{
    RegisterLocalScheduler();
    messages::ScheduleRequest req;
    auto rspFuture =
        litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::Schedule, dstActor_->GetAID(), req);
    const auto &getRsp = rspFuture.Get();
    EXPECT_EQ(getRsp.code(), StatusCode::PARAMETER_ERROR);
}
#endif

// test for LocalSchedSrvActor::UpdateSchedTopoViewTest
// receive update domain scheduler request from global scheduler
TEST_F(LocalSchedSrvActorTest, UpdateSchedTopoView) {
    auto future = litebus::Async(driverActor_->GetAID(),
                               &LocalSchedSrvActorTestDriver::GetDomainSchedulerAID, 
                               dstActor_->GetAID());

    EXPECT_AWAIT_READY_FOR(future, 100);
    
    auto domainSchedulerAID = future.Get();
    EXPECT_EQ(std::string(domainSchedulerAID), std::string(domainSchedStubActor_->GetAID()));
}

// test for LocalSchedSrvActor::Register
// send register request to global scheduler(response success) then send register request to domain scheduler(response
// success)
TEST_F(LocalSchedSrvActorTest, RegisterSuccess)
{
    RegisterLocalScheduler();
}

// test for LocalSchedSrvActor::Register
// send register request to global scheduler(response success) then send register request to domain scheduler(response
// success)
#if 0
TEST_F(LocalSchedSrvActorTest, RegisterSuccessButHearbeatWithoutPing)
{
    // registry response of global scheduler
    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::BindPingPongDriver, nullptr);
    messages::Registered registeredToGlobal;
    registeredToGlobal.set_code(StatusCode::SUCCESS);
    registeredToGlobal.set_message(REGISTERED_GLOBAL_SCHED_SUCCESS_MSG);
    messages::ScheduleTopology topo;
    topo.mutable_leader()->set_name(REGISTERED_DOMAIN_SCHED_NAME);
    topo.mutable_leader()->set_address(domainSchedStubActor_->GetAID().UnfixUrl());
    registeredToGlobal.mutable_topo()->CopyFrom(topo);
    EXPECT_CALL(*globalSchedStubActor_.get(), MockRegister).WillRepeatedly([registeredToGlobal]() {
        // avoid re-register too fast, failed to get heartbeat
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return registeredToGlobal.SerializeAsString();
    });

    // registry response of domain scheduler
    messages::Registered registeredToDomain;
    registeredToDomain.set_code(StatusCode::SUCCESS);
    registeredToDomain.set_message(REGISTERED_DOMAIN_SCHED_SUCCESS_MSG);
    EXPECT_CALL(*domainSchedStubActor_.get(), MockRegister)
        .WillRepeatedly(Return(registeredToDomain.SerializeAsString()));

    EXPECT_CALL(*primary_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));
    EXPECT_CALL(*virtual_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));

    litebus::AID pingpong;
    pingpong.SetName("localSchedSrvDstActor" + PINGPONG_BASENAME);
    pingpong.SetUrl(dstActor_->GetAID().Url());
    auto heartbeatDriver =
        std::make_shared<HeartbeatObserveDriver>("localSchedSrvDstActor", pingpong, [](const litebus::AID &) {});
    EXPECT_CALL(*domainSchedStubActor_, RegisterCall)
        .WillOnce(Return())
        .WillOnce([&heartbeatDriver]() { heartbeatDriver->Start(); })
        .WillRepeatedly(Return());

    litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::UpdateMasterInfo, dstActor_->GetAID(),
                   GetLeaderInfo(globalSchedStubActor_->GetAID()));
    ASSERT_AWAIT_TRUE(
        [=]() -> bool { return !litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::HeartBeatInvalid).Get(); });
    ASSERT_AWAIT_TRUE(
        [=]() -> bool { return litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::GetEnableFlag).Get(); });
    ASSERT_AWAIT_TRUE(
        [=]() -> bool { return litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::HeartBeatInvalid).Get(); });
    ASSERT_AWAIT_TRUE(
        [=]() -> bool { return !litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::HeartBeatInvalid).Get(); });
    // sleep to heartbeat available which would not influence the test result
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    heartbeatDriver->Stop();
    ASSERT_AWAIT_TRUE(
        [=]() -> bool { return litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::HeartBeatInvalid).Get(); });
    ASSERT_AWAIT_TRUE(
        [=]() -> bool { return !litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::HeartBeatInvalid).Get(); });
}
#endif

// test for LocalSchedSrvActor::Register
// send register request to global scheduler(response failed) then send register request to domain scheduler(response
// success)
TEST_F(LocalSchedSrvActorTest, RegisterFailedToGlobalSchedudler)
{
    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::UpdateGlobalSchedulerAddress, litebus::AID());

    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::UpdateMasterInfo,
                   GetLeaderInfo(globalSchedStubActor_->GetAID()));
    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::ToReady);
    EXPECT_TRUE(dstActor_->HeartBeatInvalid());
}

// test for LocalSchedSrvActor::Register and LocalSchedSrvActor::Registered
// send register request to global scheduler(response success) then send register request to domain scheduler(response
// failed)
TEST_F(LocalSchedSrvActorTest, RegisterFailedToDomainSchedudler)
{
    // registry response of global scheduler
    messages::Registered registeredToGlobal;
    registeredToGlobal.set_code(StatusCode::SUCCESS);
    registeredToGlobal.set_message(REGISTERED_GLOBAL_SCHED_SUCCESS_MSG);
    messages::ScheduleTopology topo;
    topo.mutable_leader()->set_name(REGISTERED_DOMAIN_SCHED_NAME);
    topo.mutable_leader()->set_address(domainSchedStubActor_->GetAID().UnfixUrl());
    registeredToGlobal.mutable_topo()->CopyFrom(topo);
    EXPECT_CALL(*globalSchedStubActor_.get(), MockRegister)
        .WillRepeatedly(Return(registeredToGlobal.SerializeAsString()));

    // registry response of domain scheduler
    messages::Registered registeredToDomainFail;
    registeredToDomainFail.set_code(StatusCode::FAILED);
    messages::Registered registeredToDomainSuccess;
    registeredToDomainSuccess.set_code(StatusCode::SUCCESS);
    EXPECT_CALL(*domainSchedStubActor_.get(), MockRegister)
        .WillOnce(Return(registeredToDomainFail.SerializeAsString()))
        .WillOnce(Return(registeredToDomainSuccess.SerializeAsString()));

    EXPECT_CALL(*primary_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));
    EXPECT_CALL(*virtual_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));

    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::UpdateMasterInfo,
                   GetLeaderInfo(globalSchedStubActor_->GetAID()));
    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::ToReady);
    ASSERT_AWAIT_TRUE([=]() -> bool {
        auto enable = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::GetEnableFlag).Get();
        return enable;
    });
    ASSERT_AWAIT_TRUE([=]() -> bool { return !dstActor_->HeartBeatInvalid(); });
}

// test for LocalSchedSrvActor::Register and LocalSchedSrvActor::Registered
// send register request to global scheduler(response success) then global address is changed
// send register request to domain scheduler(response success) then domain address is changed
TEST_F(LocalSchedSrvActorTest, RegisteredFailedFromScheduler)
{
    // registry response of global scheduler
    messages::Registered registeredToGlobal;
    registeredToGlobal.set_code(StatusCode::FAILED);
    registeredToGlobal.set_message(REGISTERED_GLOBAL_SCHED_SUCCESS_MSG);
    messages::ScheduleTopology topo;
    topo.mutable_leader()->set_name(REGISTERED_DOMAIN_SCHED_NAME);
    topo.mutable_leader()->set_address(domainSchedStubActor_->GetAID().UnfixUrl());
    registeredToGlobal.mutable_topo()->CopyFrom(topo);

    litebus::AID masterAID;
    masterAID.SetName(LOCAL_SCHED_MGR_ACTOR_NAME);
    masterAID.SetUrl("10.10.10.10:11111");
    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::ToReady);
    dstActor_->UpdateMasterInfo(GetLeaderInfo(masterAID));
    globalSchedStubActor_->Send(dstActor_->GetAID(), "Registered", registeredToGlobal.SerializeAsString());
    dstActor_->domainSchedRegisterInfo_.aid.SetName(REGISTERED_DOMAIN_SCHED_NAME +
                                                    DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    dstActor_->domainSchedRegisterInfo_.aid.SetUrl("10.10.10.10:11111");
    messages::Registered registeredToDomainSuccess;
    registeredToDomainSuccess.set_code(StatusCode::SUCCESS);
    domainSchedStubActor_->Send(dstActor_->GetAID(), "Registered", registeredToDomainSuccess.SerializeAsString());
    EXPECT_AWAIT_TRUE([&]() -> bool { return dstActor_->domainSchedRegisterInfo_.aid.Url() == "10.10.10.10:11111"; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return dstActor_->globalSchedRegisterInfo_.aid.Url() == "10.10.10.10:11111"; });
}

// test for LocalSchedSrvActor::NotifyWorkerStatus
// send NotifyWorkerStatus request to domain scheduler
TEST_F(LocalSchedSrvActorTest, NotifyWorkerStatus)
{
    RegisterLocalScheduler();
    auto counter = std::make_shared<std::atomic<int>>(0);
    EXPECT_CALL(*domainSchedStubActor_.get(), MockNotifyWorkerStatus).Times(2)
        .WillOnce(Invoke([cnt(counter)]() { (*cnt)++; return "127.0.0.2"; }))
        .WillOnce(Invoke([cnt(counter)]() { (*cnt)++; return "127.0.0.2"; }));
    auto future = litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::NotifyDsHealthy,
                                 dstActor_->GetAID(), true);
    ASSERT_AWAIT_TRUE([&]() -> bool { return *counter == 1; });
    dstActor_->notifyWorkerStatusSync_.RequestTimeout("127.0.0.1_1");
    ASSERT_AWAIT_TRUE([&]() -> bool { return *counter == 2; });
    dstActor_->dsWorkerHealthy_ = false;
    dstActor_->notifyWorkerStatusSync_.RequestTimeout("127.0.0.1_1");
    EXPECT_TRUE(future.Get().IsOk());
}

// test for LocalSchedSrvActor::ForwardSchedule and LocalSchedSrvActor::ResponseForwardSchedule
// send forward schedule request to domain(response success)
TEST_F(LocalSchedSrvActorTest, ForwardScheduleSuccess)
{
    std::string requestID = "forwardSchedule123";
    StatusCode rspCode = StatusCode::SUCCESS;
    std::string rspMsg = "forward schedule success";

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);

    messages::ScheduleResponse rsp;
    rsp.set_code(rspCode);
    rsp.set_message(rspMsg);
    rsp.set_requestid(requestID);
    EXPECT_CALL(*domainSchedStubActor_.get(), MockForwardSchedule).WillOnce(Return(rsp.SerializeAsString()));

    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    auto forwardScheduleFuture = litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::ForwardSchedule,
                                                dstActor_->GetAID(), req);
    const auto &forwardScheduleRsp = forwardScheduleFuture.Get();

    EXPECT_EQ(forwardScheduleRsp.requestid(), requestID);
    EXPECT_EQ(forwardScheduleRsp.code(), rspCode);
    EXPECT_EQ(forwardScheduleRsp.message(), rspMsg);
    req = nullptr;
}

// test for LocalSchedSrvActor::ForwardSchedule and LocalSchedSrvActor::ResponseForwardSchedule
// send forward schedule request to domain(response failed)
TEST_F(LocalSchedSrvActorTest, ForwardScheduleFailedTest)
{
    std::string traceID = "forwardSchedule123456";
    std::string requestID = "forwardSchedule123456";
    StatusCode rspCode = StatusCode::FAILED;
    std::string rspMsg = "forward schedule failed";

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);

    auto rsp = GenScheduleResponse((int32_t)rspCode, rspMsg, traceID, requestID);
    EXPECT_CALL(*domainSchedStubActor_.get(), MockForwardSchedule).WillOnce(Return(rsp.SerializeAsString()));

    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    auto forwardScheduleFuture = litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::ForwardSchedule,
                                                dstActor_->GetAID(), req);
    const auto &forwardScheduleRsp = forwardScheduleFuture.Get();

    EXPECT_EQ(forwardScheduleRsp.requestid(), requestID);
    EXPECT_EQ(forwardScheduleRsp.code(), rspCode);
    EXPECT_EQ(forwardScheduleRsp.message(), rspMsg);
    req = nullptr;
}

// test for LocalSchedSrvActor::ForwardSchedule and LocalSchedSrvActor::ResponseForwardSchedule
// send forward schedule request to domain(timeout)
TEST_F(LocalSchedSrvActorTest, ForwardScheduleTimeoutTest)
{
    std::string requestID = "forwardSchedule123456";
    StatusCode rspCode = StatusCode::LS_FORWARD_DOMAIN_TIMEOUT;
    std::string rspMsg = "forward to domain scheduler timeout";

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    auto forwardScheduleFuture = litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::ForwardSchedule,
                                                dstActor_->GetAID(), req);
    const auto &forwardScheduleRsp = forwardScheduleFuture.Get();

    EXPECT_EQ(forwardScheduleRsp.requestid(), requestID);
    EXPECT_EQ(forwardScheduleRsp.code(), rspCode);
    EXPECT_EQ(forwardScheduleRsp.message(), rspMsg);
    req = nullptr;
}

// test for LocalSchedSrvActor::ForwardScheduleRetry
// send forward schedule request to domain(timeout)
TEST_F(LocalSchedSrvActorTest, ForwardScheduleRetryTest)
{
    std::string requestID = "forwardSchedule123456";
    StatusCode rspCode = StatusCode::LS_FORWARD_DOMAIN_TIMEOUT;
    std::string rspMsg = "forward to domain scheduler timeout";

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->mutable_instance()->mutable_scheduleoption()->set_initcalltimeout(2);
    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    auto forwardScheduleFuture = litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::ForwardSchedule,
                                                dstActor_->GetAID(), req);
    const auto &forwardScheduleRsp = forwardScheduleFuture.Get();

    EXPECT_EQ(forwardScheduleRsp.requestid(), requestID);
    EXPECT_EQ(forwardScheduleRsp.code(), rspCode);
    EXPECT_EQ(forwardScheduleRsp.message(), rspMsg);
    req = nullptr;
}

/**
 * Feature: LocalSchedSrvActor
 * Description: check param of LocalScheduler ForwardSchedule to DomainScheduler
 * Steps:
 * 1. mock GetResourceView.
 * 2. send ForwardSchedule to LocalSchedSrvActor.
 * 3. check ForwardSchedule of DomainScheduler param.
 * Expectation: ForwardSchedule CPU param check success
 */
TEST_F(LocalSchedSrvActorTest, ForwardScheduleParamCheck)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("forwardSchedule123");

    messages::ScheduleResponse rsp;
    rsp.set_code(StatusCode::SUCCESS);
    rsp.set_message("forward schedule success");
    rsp.set_requestid("forwardSchedule123");
    EXPECT_CALL(*domainSchedStubActor_.get(), MockForwardSchedule).WillOnce(Return(rsp.SerializeAsString()));
    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>(view_utils::Get1DResourceUnitChanges()))));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>(view_utils::Get1DResourceUnitChanges()))));

    litebus::Future<std::string> msgName;
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*domainSchedStubActor_.get(), MockForwardScheduleWithParam(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(FutureArg<1>(&msgName), FutureArg<2>(&msgValue)));

    auto forwardScheduleFuture = litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::ForwardSchedule,
                                                dstActor_->GetAID(), req);
    ASSERT_AWAIT_READY(msgName);
    EXPECT_TRUE(msgName.Get() == "ForwardSchedule");

    ASSERT_AWAIT_READY(msgValue);
    ASSERT_AWAIT_READY(forwardScheduleFuture);
    messages::ScheduleRequest scheduleReq;
    EXPECT_TRUE(scheduleReq.ParseFromString(msgValue.Get()));
    auto cpuValue = scheduleReq.updateresources().at(0).changes(0).addition().resourceunit()
                        .capacity()
                        .resources()
                        .find(resource_view::CPU_RESOURCE_NAME)
                        ->second.scalar()
                        .value();
    auto memoryValue = scheduleReq.updateresources().at(0).changes(0).addition().resourceunit()
                           .capacity()
                           .resources()
                           .find(resource_view::MEMORY_RESOURCE_NAME)
                           ->second.scalar()
                           .value();
    YRLOG_INFO("resource cpu: {}, memory: {}", cpuValue, memoryValue);
    EXPECT_EQ(cpuValue, 100.1);
    EXPECT_EQ(memoryValue, 100.1);
    forwardScheduleFuture.Get();
}

/**
 * Feature: LocalSchedSrvActor
 * Description: check forward kill request method to instance manager
 * Steps:
 * 1. mock MockInstanceManagerActor.
 * 2. send ForwardKillRequest to MockInstanceManagerActor.
 * 3. receive the response and check it
 * Expectation:
 *  forward kill succeed
 */
TEST_F(LocalSchedSrvActorTest, ForwardKillToInstanceManager)
{
    auto mockInstanceManagerActor = std::make_shared<MockInstanceManagerActor>();
    litebus::Spawn(mockInstanceManagerActor);

    auto killReq = std::make_shared<messages::ForwardKillRequest>();
    killReq->set_requestid("forwardKill123");

    auto forwardKillFuture =
        litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::ForwardKillToInstanceManager, killReq);

    ASSERT_AWAIT_READY(forwardKillFuture);

    litebus::Terminate(mockInstanceManagerActor->GetAID());
    litebus::Await(mockInstanceManagerActor->GetAID());
}

/**
 * Feature: LocalSchedSrvActor
 * Description: test evict agent
 * case1: request body invalid
 * case2: Evict failed
 * case3: Evict successful
 */
TEST_F(LocalSchedSrvActorTest, EvictAgent)
{
    // case1 request body invalid
    auto aid = dstActor_->GetAID();
    {
        auto future = globalSchedStubActor_->SendEvictAgent(aid, "");
        EXPECT_AWAIT_READY(future);
        auto rsp = future.Get();
        messages::EvictAgentAck ack;
        ASSERT_EQ(ack.ParseFromString(rsp), true);
        EXPECT_EQ(ack.code(), static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
    }

    // case2: Evict failed
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        req->set_agentid("agentID");
        req->set_requestid("agentID");
        req->set_timeoutsec(1);
        EXPECT_CALL(*functionAgentMgr_, EvictAgent(_)).WillOnce(Return(AsyncReturn(Status(FAILED, "falied to evict"))));
        auto future = globalSchedStubActor_->SendEvictAgent(aid, req->SerializeAsString());
        EXPECT_AWAIT_READY(future);
        auto rsp = future.Get();
        messages::EvictAgentAck ack;
        ASSERT_EQ(ack.ParseFromString(rsp), true);
        EXPECT_EQ(ack.code(), static_cast<int32_t>(StatusCode::FAILED));
    }

    // Evict successful
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        req->set_agentid("agentID");
        req->set_requestid("agentID");
        req->set_timeoutsec(1);
        EXPECT_CALL(*functionAgentMgr_, EvictAgent(_)).WillOnce(Return(AsyncReturn(Status::OK())));
        auto future = globalSchedStubActor_->SendEvictAgent(aid, req->SerializeAsString());
        EXPECT_AWAIT_READY(future);
        auto rsp = future.Get();
        messages::EvictAgentAck ack;
        ASSERT_EQ(ack.ParseFromString(rsp), true);
        EXPECT_EQ(ack.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    }
    // Evict instance for preempt successful
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        req->set_agentid("agentID");
        req->set_requestid("agentID");
        req->set_timeoutsec(1);
        req->add_instances("ins1");
        req->add_instances("ins2");
        EXPECT_CALL(*mockInstanceCtrl_, EvictInstances).WillOnce(testing::Return(AsyncReturn(Status::OK())));
        auto future = globalSchedStubActor_->SendPreemptInstance(aid, req->SerializeAsString());
        EXPECT_AWAIT_READY(future);
        auto rsp = future.Get();
        messages::EvictAgentAck ack;
        ASSERT_EQ(ack.ParseFromString(rsp), true);
        EXPECT_EQ(ack.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    }
}

/**
 * Feature: LocalSchedSrvActor
 * Description: notify evict agent result
 */
TEST_F(LocalSchedSrvActorTest, NotifyEvictAgentResult)
{
    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::UpdateMasterInfo,
                   GetLeaderInfo(globalSchedStubActor_->GetAID()));
    auto future = globalSchedStubActor_->InitEvictResult();
    auto req = std::make_shared<messages::EvictAgentResult>();
    req->set_agentid("agentID");
    req->set_requestid("agentID");
    req->set_code(0);
    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::NotifyEvictResult, req);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().requestid(), req->requestid());
    EXPECT_EQ(future.Get().agentid(), req->agentid());
    EXPECT_EQ(future.Get().code(), req->code());
}

/**
 * Feature: LocalSchedSrvActor
 * Description: forward group schedule
 * 1. successful
 * 2. timeout
 */
TEST_F(LocalSchedSrvActorTest, ForwardScheduleGroup)
{
    RegisterLocalScheduler();
    auto group = std::make_shared<messages::GroupInfo>();
    group->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    group->set_traceid("traceID");
    group->set_groupid("groupID");
    group->mutable_groupopts()->set_timeout(1);
    auto groupCtrlStub = std::make_shared<DomainGroupCtrlActorStub>(DOMAIN_GROUP_CTRL_ACTOR_NAME);
    litebus::Spawn(groupCtrlStub);
    {
        messages::GroupResponse rsp;
        rsp.set_requestid(group->requestid());
        EXPECT_CALL(*groupCtrlStub, MockForwardGroupSchedule).WillOnce(Return(rsp.SerializeAsString()));
        group->mutable_groupopts()->set_timeout(-1);
        auto future = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::ForwardGroupSchedule, group);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    }
    litebus::Terminate(groupCtrlStub->GetAID());
    litebus::Await(groupCtrlStub);
}

/**
 * Feature: LocalSchedSrvActor
 * Description: forward kill group
 */
TEST_F(LocalSchedSrvActorTest, ForwardKillGroup)
{
    RegisterLocalScheduler();
    auto kill = std::make_shared<messages::KillGroup>();
    kill->set_srcinstanceid("instanceID");
    kill->set_groupid("groupID-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    auto groupMgrStub = std::make_shared<GroupManagerStubActor>(GROUP_MANAGER_ACTOR_NAME);
    litebus::Spawn(groupMgrStub);
    {
        messages::KillGroupResponse rsp;
        rsp.set_groupid(kill->groupid());
        EXPECT_CALL(*groupMgrStub, MockKillGroup).WillOnce(Return(rsp.SerializeAsString()));
        auto future = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::KillGroup, kill);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);
    }
    {
        EXPECT_CALL(*groupMgrStub, MockKillGroup).WillRepeatedly(Return("xxxxxxx"));
        auto future = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::KillGroup, kill);
        litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::Disable);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), StatusCode::ERR_INNER_COMMUNICATION);
    }

    litebus::Terminate(groupMgrStub->GetAID());
    litebus::Await(groupMgrStub);
}

/**
 * Feature: LocalSchedSrvActor
 * Description: Delete Pod Request
 */
TEST_F(LocalSchedSrvActorTest, DeletePodRequest)
{
    bool isFinished = false;
    EXPECT_CALL(*domainSchedStubActor_.get(), MockDeletePodResponse)
        .Times(2)
        .WillOnce(Return(111))
        .WillOnce(DoAll(Assign(&isFinished, true), Return(0)));
    litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::DeletePod, "function-agent-001", "req123", "delete pod");
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });
}

TEST_F(LocalSchedSrvActorTest, TryCancelSchedule)
{
    auto domainSchedStubActor = std::make_shared<DomainSchedStubActor>(REGISTERED_DOMAIN_SCHED_NAME +
                                                                       DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX);
    litebus::Spawn(domainSchedStubActor);
    YRLOG_INFO("domain stub actor: {}", std::string(domainSchedStubActor->GetAID()));
    bool isFinished = false;
    auto req = std::make_shared<messages::CancelSchedule>();
    req->set_msgid("cancelSchedule001");
    req->set_type(messages::CancelType::REQUEST);
    req->set_reason("cancel");
    req->set_id("cancelSchedule001");
    EXPECT_CALL(*domainSchedStubActor.get(), MockCancelScheduleResponse)
        .WillOnce(DoAll(Assign(&isFinished, true), Return(0)));
    auto future = litebus::Async(driverActor_->GetAID(), &LocalSchedSrvActorTestDriver::TryCancelSchedule,
                                 dstActor_->GetAID(), req);
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });
    EXPECT_TRUE(future.Get().IsOk());
    req = nullptr;
    litebus::Terminate(domainSchedStubActor->GetAID());
    litebus::Await(domainSchedStubActor);
}

/**
 * Feature: LocalSchedSrvActor
 * Description: graceful shutdown
 */
TEST_F(LocalSchedSrvActorTest, GracefulShutdownTest)
{
    RegisterLocalScheduler();
    EXPECT_CALL(*functionAgentMgr_, GracefulShutdown()).WillOnce(Return(AsyncReturn(Status::OK()))).WillOnce(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*mockInstanceCtrl_, SetAbnormal()).WillOnce(Return()).WillOnce(Return());
    EXPECT_CALL(*mockInstanceCtrl_, GracefulShutdown()).WillOnce(Return(AsyncReturn(Status::OK()))).WillOnce(Return(AsyncReturn(Status::OK())));
    messages::Registered unRegisteredToGlobal;
    unRegisteredToGlobal.set_code(StatusCode::SUCCESS);
    EXPECT_CALL(*globalSchedStubActor_.get(), MockUnRegister)
        .WillOnce(Return(unRegisteredToGlobal.SerializeAsString()));
    auto future = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::GracefulShutdown);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    future = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::GracefulShutdown);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
}

// test for QueryMasterIP
TEST_F(LocalSchedSrvActorTest, QueryMasterIPTest)
{
    // test for ip is empty
    auto masterIP = dstActor_->QueryMasterIP().Get();
    auto future = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::QueryMasterIP);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().empty(), true);

    // test for ip is updated
    std::string expectAddress = "192.167.0.4:19247";
    dstActor_->masterAid_ = litebus::AID(LOCAL_SCHED_MGR_ACTOR_NAME, expectAddress);
    dstActor_->masterAid_.SetProtocol(litebus::BUS_TCP);
    future = litebus::Async(dstActor_->GetAID(), &LocalSchedSrvActor::QueryMasterIP);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get(), expectAddress);
}

}  // namespace functionsystem::test