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
#include <litebus.hpp>

#include "common/constants/actor_name.h"
#include "common/resource_view/view_utils.h"
#include "common/status/status.h"
#include "domain_scheduler/underlayer_scheduler_manager/underlayer_sched_mgr.h"
#include "domain_scheduler/underlayer_scheduler_manager/underlayer_sched_mgr_actor.h"
#include "mocks/mock_domain_instance_ctrl.h"
#include "mocks/mock_domain_sched_srv.h"
#include "mocks/mock_resource_view.h"
#include "mocks/mock_scaler_actor.h"
#include "underlayer_stub.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using ::testing::_;
using ::testing::Return;
using namespace domain_scheduler;
class UnderlayerSchedMgrTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        auto address = litebus::GetLitebusAddress();
        address_ = address.ip + ":" + std::to_string(address.port);
    }

    void SetUp()
    {
        underlayerSchedMgrActor_ = std::make_shared<UnderlayerSchedMgrActor>("under_layer_test", 3, 100, 100);
        mockInstanceCtrl_ = std::make_shared<MockDomainInstanceCtrl>();
        mockDomainSrv_ = std::make_shared<MockDomainSchedSrv>();
        auto resourceViewMgr_ = std::make_shared<resource_view::ResourceViewMgr>();
        primary_ = MockResourceView::CreateMockResourceView();
        virtual_ = MockResourceView::CreateMockResourceView();
        resourceViewMgr_->primary_ = primary_;
        resourceViewMgr_->virtual_ = virtual_;
        underlayerSchedMgrActor_->BindInstanceCtrl(mockInstanceCtrl_);
        underlayerSchedMgrActor_->BindDomainService(mockDomainSrv_);
        underlayerSchedMgrActor_->BindResourceView(resourceViewMgr_);
        litebus::Spawn(underlayerSchedMgrActor_);
    }

    void TearDown()
    {
        litebus::Terminate(underlayerSchedMgrActor_->GetAID());
        litebus::Await(underlayerSchedMgrActor_);
    }

    void UnderlayerRegiter(std::shared_ptr<MockUnderlayer> &mockUnderlayerActor, UnderlayerSchedMgr &underlayer)
    {
        litebus::Future<std::string> msgName;
        litebus::Future<std::string> msgValue;
        EXPECT_CALL(*mockUnderlayerActor, MockRegistered(_, _, _))
            .WillOnce(testing::DoAll(FutureArg<1>(&msgName), FutureArg<2>(&msgValue)));
        EXPECT_CALL(*primary_, AddResourceUnitWithUrl(_, _)).WillOnce(Return(AsyncReturn(Status::OK())));
        EXPECT_CALL(*virtual_, AddResourceUnitWithUrl(_, _)).WillOnce(Return(AsyncReturn(Status::OK())));
        EXPECT_CALL(*primary_, DeleteLocalResourceView(_)).WillOnce(Return(AsyncReturn(Status::OK())));
        EXPECT_CALL(*virtual_, DeleteLocalResourceView(_)).WillOnce(Return(AsyncReturn(Status::OK())));
        EXPECT_CALL(*mockInstanceCtrl_, UpdateMaxSchedRetryTimes(1)).Times(1);
        messages::ScheduleTopology topo;
        auto member = topo.add_members();
        member->set_name(mockUnderlayerActor->GetAID().Name());
        member->set_address(address_);
        underlayer.UpdateUnderlayerTopo(topo);

        litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRegister,
                       underlayerSchedMgrActor_->GetAID());

        ASSERT_AWAIT_READY(msgName);
        EXPECT_TRUE(msgName.Get() == "Registered");
        ASSERT_AWAIT_READY(msgValue);
        messages::Registered rsp;
        EXPECT_TRUE(rsp.ParseFromString(msgValue.Get()));
        EXPECT_EQ(rsp.code(), StatusCode::SUCCESS);

        auto registered = underlayer.IsRegistered(mockUnderlayerActor->GetAID().Name());
        ASSERT_AWAIT_READY(registered);
        EXPECT_TRUE(registered.Get());
    }

protected:
    std::shared_ptr<UnderlayerSchedMgrActor> underlayerSchedMgrActor_;
    std::shared_ptr<MockDomainInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockDomainSchedSrv> mockDomainSrv_;
    std::shared_ptr<MockResourceView> primary_;
    std::shared_ptr<MockResourceView> virtual_;
    inline static std::string address_;
};

TEST_F(UnderlayerSchedMgrTest, UnderlayerRegister)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);
    UnderlayerRegiter(mockUnderlayerActor, underlayer);
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, UnderlayerRegisterAlreadyRegistered)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);

    litebus::Future<std::string> msgName;
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*mockUnderlayerActor, MockRegistered(_, _, _))
        .WillRepeatedly(testing::DoAll(FutureArg<1>(&msgName), FutureArg<2>(&msgValue)));
    EXPECT_CALL(*primary_, AddResourceUnitWithUrl(_, _)).WillOnce(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*virtual_, AddResourceUnitWithUrl(_, _)).WillOnce(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*primary_, DeleteLocalResourceView(_)).WillOnce(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*virtual_, DeleteLocalResourceView(_)).WillOnce(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*mockInstanceCtrl_, UpdateMaxSchedRetryTimes(1)).Times(1);

    messages::ScheduleTopology topo;
    auto member = topo.add_members();
    member->set_name(mockUnderlayerActor->GetAID().Name());
    member->set_address(address_);
    underlayer.UpdateUnderlayerTopo(topo);

    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRegister, underlayerSchedMgrActor_->GetAID());
    ASSERT_AWAIT_READY(msgName);
    ASSERT_AWAIT_READY(msgValue);
    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRegister, underlayerSchedMgrActor_->GetAID());

    ASSERT_AWAIT_READY(msgName);
    EXPECT_TRUE(msgName.Get() == "Registered");
    ASSERT_AWAIT_READY(msgValue);
    messages::Registered rsp;
    EXPECT_TRUE(rsp.ParseFromString(msgValue.Get()));
    EXPECT_EQ(rsp.code(), StatusCode::SUCCESS);

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, UnderlayerRegisterNotFound)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);

    litebus::Future<std::string> msgName;
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*mockUnderlayerActor, MockRegistered(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&msgName), FutureArg<2>(&msgValue)));

    messages::ScheduleTopology topo;
    auto member = topo.add_members();
    member->set_name("test");
    member->set_address(address_);
    underlayer.UpdateUnderlayerTopo(topo);

    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRegister, underlayerSchedMgrActor_->GetAID());

    ASSERT_AWAIT_READY(msgName);
    EXPECT_TRUE(msgName.Get() == "Registered");
    ASSERT_AWAIT_READY(msgValue);
    messages::Registered rsp;
    EXPECT_TRUE(rsp.ParseFromString(msgValue.Get()));
    EXPECT_EQ(rsp.code(), StatusCode::FAILED);
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, UnderlayerRegisterFailWhenParseReq)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);

    litebus::Future<std::string> msgName;
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*mockUnderlayerActor, MockRegistered(_, _, _))
        .WillRepeatedly(testing::DoAll(FutureArg<1>(&msgName), FutureArg<2>(&msgValue)));

    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "Register", "test");

    ASSERT_AWAIT_NO_SET_FOR(msgName, 1000);
    EXPECT_FALSE(msgName.IsOK());

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, ForwardScheduleSuccessful)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("Forwarder");
    litebus::Spawn(mockUnderlayerActor);

    auto successRsp = std::make_shared<messages::ScheduleResponse>();
    successRsp->set_code(0);
    successRsp->set_requestid("request");

    EXPECT_CALL(*mockInstanceCtrl_, Schedule(_)).WillOnce(Return(AsyncReturn(successRsp)));

    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseForwardSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg)));

    messages::ScheduleRequest req;
    req.set_requestid("request");
    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "ForwardSchedule", req.SerializeAsString());

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.requestid(), "request");

    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, ForwardScheduleFailWhenScheduleFail)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("Forwarder");
    litebus::Spawn(mockUnderlayerActor);

    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> failRsp;
    failRsp.SetFailed(100);

    EXPECT_CALL(*mockInstanceCtrl_, Schedule(_)).WillOnce(Return(AsyncReturn(failRsp)));

    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseForwardSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg)));

    messages::ScheduleRequest req;
    req.set_requestid("request");
    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "ForwardSchedule", req.SerializeAsString());

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.requestid(), "request");
    EXPECT_EQ(rsp.code(), 100);

    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

/**
 * ForwardScheduleFailWhenVersionWrong
 * Test is transition version is incorrect, and Forward response directly
 * Expectations:
 * scheduleRep code is version wrong
 */
TEST_F(UnderlayerSchedMgrTest, ForwardScheduleFailWhenVersionWrong)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("Forwarder");
    litebus::Spawn(mockUnderlayerActor);

    auto versionWrongRsp = std::make_shared<messages::ScheduleResponse>();
    versionWrongRsp->set_code(StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);
    versionWrongRsp->set_requestid("request");

    EXPECT_CALL(*mockInstanceCtrl_, Schedule(_)).WillOnce(Return(AsyncReturn(versionWrongRsp)));

    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseForwardSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg)));

    messages::ScheduleRequest req;
    req.set_requestid("request");
    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "ForwardSchedule", req.SerializeAsString());

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.requestid(), "request");
    EXPECT_EQ(rsp.code(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);

    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, ForwardScheduleFailToParseRequestFail)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("Forwarder");
    litebus::Spawn(mockUnderlayerActor);

    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseForwardSchedule(_, _, _))
        .WillRepeatedly(testing::DoAll(FutureArg<2>(&msg)));

    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "ForwardSchedule", "test");

    ASSERT_AWAIT_NO_SET_FOR(msg, 1000);
    EXPECT_FALSE(msg.IsOK());

    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, ForwardScheduleWithUpdateResourceSuccessful)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("Forwarder");
    litebus::Spawn(mockUnderlayerActor);

    auto successRsp = std::make_shared<messages::ScheduleResponse>();
    successRsp->set_code(0);
    successRsp->set_requestid("request");

    EXPECT_CALL(*mockInstanceCtrl_, Schedule(_)).WillOnce(Return(AsyncReturn(successRsp)));

    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseForwardSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg)));

    messages::ScheduleRequest req;
    req.set_requestid("request");
    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "ForwardSchedule", req.SerializeAsString());

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.requestid(), "request");

    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, ForwardScheduleFailedToForwardUplayerFail)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("Forwarder");
    litebus::Spawn(mockUnderlayerActor);

    auto failedRsp = std::make_shared<messages::ScheduleResponse>();
    failedRsp->set_code(2);
    failedRsp->set_requestid("request1");
    EXPECT_CALL(*mockInstanceCtrl_, Schedule(_)).WillOnce(Return(AsyncReturn(failedRsp)));

    auto upfailRsp = std::make_shared<messages::ScheduleResponse>();
    upfailRsp->set_code(StatusCode::DOMAIN_SCHEDULER_FORWARD_ERR);
    upfailRsp->set_requestid("request1");
    EXPECT_CALL(*mockDomainSrv_, ForwardSchedule(_)).WillOnce(Return(AsyncReturn(upfailRsp)));

    messages::ScheduleRequest req;
    req.set_requestid("request1");

    litebus::Future<std::string> failedMsg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseForwardSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&failedMsg)));

    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "ForwardSchedule", req.SerializeAsString());

    ASSERT_AWAIT_READY_FOR(failedMsg, 1000);
    messages::ScheduleResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(failedMsg.Get()));
    EXPECT_EQ(rsp.requestid(), "request1");
    EXPECT_EQ(rsp.code(), 2);

    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, ForwardScheduleFailedToForwardUplayerSuccess)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("Forwarder");
    litebus::Spawn(mockUnderlayerActor);

    auto failedRsp = std::make_shared<messages::ScheduleResponse>();
    failedRsp->set_code(2);
    failedRsp->set_requestid("request1");
    EXPECT_CALL(*mockInstanceCtrl_, Schedule(_)).WillOnce(Return(AsyncReturn(failedRsp)));

    auto successRsp = std::make_shared<messages::ScheduleResponse>();
    successRsp->set_code(0);
    successRsp->set_requestid("request1");
    EXPECT_CALL(*mockDomainSrv_, ForwardSchedule(_)).WillOnce(Return(AsyncReturn(successRsp)));

    messages::ScheduleRequest req;
    req.set_requestid("request1");

    litebus::Future<std::string> failedMsg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseForwardSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&failedMsg)));

    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "ForwardSchedule", req.SerializeAsString());

    ASSERT_AWAIT_READY_FOR(failedMsg, 1000);
    messages::ScheduleResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(failedMsg.Get()));
    EXPECT_EQ(rsp.requestid(), "request1");
    EXPECT_EQ(rsp.code(), 0);

    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, DispatchScheduleWithNoUnderLayer)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto req = std::make_shared<messages::ScheduleRequest>();
    auto future = underlayer.DispatchSchedule("invalid", req);
    EXPECT_AWAIT_READY_FOR(future, 1000);
    EXPECT_EQ(future.Get()->code(), StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
}

TEST_F(UnderlayerSchedMgrTest, DispatchScheduleSuccess)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);

    UnderlayerRegiter(mockUnderlayerActor, underlayer);

    messages::ScheduleResponse successfulRsp;
    successfulRsp.set_code(0);
    successfulRsp.set_requestid("request");
    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg), Return(successfulRsp.SerializeAsString())));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    auto future = litebus::Async(underlayerSchedMgrActor_->GetAID(), &UnderlayerSchedMgrActor::DispatchSchedule,
                                 mockUnderlayerActor->GetAID().Name(), req);

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleRequest ret;
    EXPECT_TRUE(ret.ParseFromString(msg.Get()));
    EXPECT_EQ(ret.requestid(), "request");

    EXPECT_AWAIT_READY_FOR(future, 1000);
    EXPECT_EQ(future.Get()->code(), 0);
    EXPECT_EQ(future.Get()->requestid(), "request");

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, DispatchScheduleWithResponseParseFail)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);

    UnderlayerRegiter(mockUnderlayerActor, underlayer);

    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg), Return("test")));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    auto future = litebus::Async(underlayerSchedMgrActor_->GetAID(), &UnderlayerSchedMgrActor::DispatchSchedule,
                                 mockUnderlayerActor->GetAID().Name(), req);

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleRequest ret;
    EXPECT_TRUE(ret.ParseFromString(msg.Get()));
    EXPECT_EQ(ret.requestid(), "request");

    ASSERT_AWAIT_NO_SET_FOR(future, 1000);
    EXPECT_FALSE(future.IsOK());

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, DispatchScheduleWithResponseMatchFail)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);

    UnderlayerRegiter(mockUnderlayerActor, underlayer);

    messages::ScheduleResponse successfulRsp;
    successfulRsp.set_code(0);
    successfulRsp.set_requestid("response");
    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg), Return(successfulRsp.SerializeAsString())));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    auto future = litebus::Async(underlayerSchedMgrActor_->GetAID(), &UnderlayerSchedMgrActor::DispatchSchedule,
                                 mockUnderlayerActor->GetAID().Name(), req);

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleRequest ret;
    EXPECT_TRUE(ret.ParseFromString(msg.Get()));
    EXPECT_EQ(ret.requestid(), "request");

    ASSERT_AWAIT_NO_SET_FOR(future, 1000);
    EXPECT_FALSE(future.IsOK());

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, NotifyAbnormalSuccess)
{
    litebus::Future<messages::NotifySchedAbnormalRequest> msg;
    litebus::Future<messages::NotifySchedAbnormalRequest> msg1;
    litebus::Future<Status> failStatus;
    failStatus.SetFailed(100);
    litebus::Future<messages::NotifySchedAbnormalRequest> msg2;
    EXPECT_CALL(*mockDomainSrv_, NotifySchedAbnormal(_))
        .WillOnce(testing::DoAll(FutureArg<0>(&msg), Return(AsyncReturn(Status::OK()))))
        .WillOnce(testing::DoAll(FutureArg<0>(&msg1), Return(AsyncReturn(failStatus))))
        .WillRepeatedly(testing::DoAll(FutureArg<0>(&msg2), Return(AsyncReturn(Status::OK()))));

    messages::NotifySchedAbnormalRequest req;
    req.set_schedname("request");
    litebus::Async(underlayerSchedMgrActor_->GetAID(), &UnderlayerSchedMgrActor::NotifyAbnormal, req);

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    ASSERT_TRUE(msg.IsOK());
    EXPECT_EQ(msg.Get().schedname(), "request");

    litebus::Async(underlayerSchedMgrActor_->GetAID(), &UnderlayerSchedMgrActor::NotifyAbnormal, req);
    EXPECT_AWAIT_READY_FOR(msg1, 1000);
    ASSERT_TRUE(msg1.IsOK());
    EXPECT_EQ(msg1.Get().schedname(), "request");

    EXPECT_AWAIT_READY_FOR(msg2, 1000);
    ASSERT_TRUE(msg2.IsOK());
    EXPECT_EQ(msg2.Get().schedname(), "request");
}

TEST_F(UnderlayerSchedMgrTest, NotifySchedAbnormalSuccess)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("NotifySchedAbnormal");
    litebus::Spawn(mockUnderlayerActor);

    EXPECT_CALL(*mockDomainSrv_, NotifySchedAbnormal(_)).WillOnce(Return(AsyncReturn(Status::OK())));

    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseNotifySchedAbnormal(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg)));

    messages::NotifySchedAbnormalRequest req;
    req.set_schedname("request");
    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "NotifySchedAbnormal", req.SerializeAsString());

    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::NotifySchedAbnormalResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.schedname(), "request");

    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, NotifyWorkerStatusSuccess)
{
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("NotifySchedAbnormal");
    litebus::Spawn(mockUnderlayerActor);
    EXPECT_CALL(*mockDomainSrv_, NotifyWorkerStatus(_)).WillOnce(Return(AsyncReturn(Status::OK())));
    litebus::Future<std::string> msg;
    EXPECT_CALL(*mockUnderlayerActor, MockResponseNotifyWorkerStatus(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<2>(&msg)));
    messages::NotifyWorkerStatusRequest req;
    req.set_workerip("127.0.0.1");
    req.set_healthy(true);
    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "NotifyWorkerStatus", req.SerializeAsString());
    EXPECT_AWAIT_READY_FOR(msg, 1000);
    messages::NotifyWorkerStatusResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.workerip(), "127.0.0.1");
    mockUnderlayerActor->ClosePingPong();
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, GetAddressTest)
{
    auto underlayerScheduler = std::make_shared<UnderlayerScheduler>("under_layer_test", "192.0.0.1", 3, 100);
    EXPECT_STREQ(underlayerScheduler->GetAddress().c_str(), "192.0.0.1");
}

TEST_F(UnderlayerSchedMgrTest, UnfinishedscheduleRequest)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);

    EXPECT_CALL(*mockDomainSrv_, NotifySchedAbnormal(_)).WillOnce(Return(AsyncReturn(Status::OK())));
    litebus::Promise<bool> pro;
    EXPECT_CALL(*mockUnderlayerActor, MockSchedule(_, _, _))
        .WillOnce(::testing::Invoke([pro](const litebus::AID &, std::string, std::string) {
            (void)pro.GetFuture().Get();
            return messages::ScheduleResponse().SerializeAsString();
        }));

    EXPECT_CALL(*mockInstanceCtrl_, UpdateMaxSchedRetryTimes(0)).Times(1);

    UnderlayerRegiter(mockUnderlayerActor, underlayer);
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    auto future = litebus::Async(underlayerSchedMgrActor_->GetAID(), &UnderlayerSchedMgrActor::DispatchSchedule,
                                 mockUnderlayerActor->GetAID().Name(), req);

    messages::NotifySchedAbnormalRequest abnormalReq;
    abnormalReq.set_schedname("WillRegister");
    litebus::Async(mockUnderlayerActor->GetAID(), &MockUnderlayer::SendRequest, underlayerSchedMgrActor_->GetAID(),
                   "NotifySchedAbnormal", abnormalReq.SerializeAsString());

    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get()->code(), DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    EXPECT_EQ(future.Get()->requestid(), "request");
    pro.SetValue(false);
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

TEST_F(UnderlayerSchedMgrTest, PreemptInstance)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("proxy-node1");
    litebus::Spawn(mockUnderlayerActor);
    UnderlayerRegiter(mockUnderlayerActor, underlayer);
    // init instance
    resource_view::InstanceInfo ins1;
    ins1.set_instanceid("ins1");
    ins1.set_functionproxyid("proxy-node1");
    resource_view::InstanceInfo ins2;
    ins2.set_instanceid("ins2");
    ins2.set_functionproxyid("proxy-node2");
    resource_view::InstanceInfo ins3;
    ins3.set_functionproxyid("proxy-node1");
    ins3.set_instanceid("ins3");
    // init preempt result
    std::vector<schedule_decision::PreemptResult> preemptResults;
    schedule_decision::PreemptResult preemptResult1;
    schedule_decision::PreemptResult preemptResult2;
    preemptResult2.unitID = "agentID1";
    schedule_decision::PreemptResult preemptResult3;
    preemptResult3.unitID = "agentID1";
    preemptResult3.preemptedInstances = {ins1};
    preemptResult3.ownerID = "proxy-node1";
    schedule_decision::PreemptResult preemptResult4;
    preemptResult4.unitID = "agentID2";
    preemptResult4.preemptedInstances = {ins2};
    preemptResult4.ownerID = "proxy-node2";
    schedule_decision::PreemptResult preemptResult5;
    preemptResult5.unitID = "agentID1";
    preemptResult5.preemptedInstances = {ins3};
    preemptResult5.ownerID = "proxy-node1";

    std::vector<schedule_decision::PreemptResult> preemptResults1{preemptResult1, preemptResult2};
    litebus::Async(underlayerSchedMgrActor_->GetAID(), &UnderlayerSchedMgrActor::PreemptInstance,
                                  preemptResults1);
    std::vector<schedule_decision::PreemptResult> preemptResults2{preemptResult3, preemptResult4, preemptResult5};
    litebus::Future<std::shared_ptr<messages::EvictAgentRequest>> futureResult;
    EXPECT_CALL(*mockUnderlayerActor, MockPreemptInstanceRequest(_)).WillOnce(DoAll(FutureArg<0>(&futureResult), Return()));
    litebus::Async(underlayerSchedMgrActor_->GetAID(), &UnderlayerSchedMgrActor::PreemptInstance,
                                  preemptResults2);
    ASSERT_AWAIT_READY_FOR(futureResult, 1000);
    auto preemptReq = futureResult.Get();
    EXPECT_TRUE(preemptReq->instances().size() == 2);
    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Await(mockUnderlayerActor);
}

// Reserve successful
// Reserve failed by underlayer lost
TEST_F(UnderlayerSchedMgrTest, Reserve)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);
    UnderlayerRegiter(mockUnderlayerActor, underlayer);

    auto mockLocalGroupCtrl = std::make_shared<MockLocalGroupCtrl>(LOCAL_GROUP_CTRL_ACTOR_NAME);
    litebus::Spawn(mockLocalGroupCtrl);

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    {
        messages::ScheduleResponse resp;
        resp.set_requestid(req->requestid());
        EXPECT_CALL(*mockLocalGroupCtrl, MockReserve).WillOnce(Return(resp.SerializeAsString()));
        auto future = underlayer.Reserve("WillRegister", req);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get()->code(), (int32_t)StatusCode::SUCCESS);
    }

    {
        EXPECT_CALL(*mockLocalGroupCtrl, MockReserve).WillRepeatedly(Return("xxxxx"));
        mockUnderlayerActor->ClosePingPong();
        EXPECT_CALL(*mockInstanceCtrl_, UpdateMaxSchedRetryTimes(0)).Times(1);
        EXPECT_CALL(*mockDomainSrv_, NotifySchedAbnormal(_)).WillOnce(Return(AsyncReturn(Status::OK())));
        auto future = underlayer.Reserve("WillRegister", req);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get()->code(), (int32_t)StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    }

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Terminate(mockLocalGroupCtrl->GetAID());
    litebus::Await(mockUnderlayerActor);
    litebus::Await(mockLocalGroupCtrl);
}

// UnReserve successful
// UnReserve failed by underlayer lost
TEST_F(UnderlayerSchedMgrTest, UnReserve)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);
    UnderlayerRegiter(mockUnderlayerActor, underlayer);

    auto mockLocalGroupCtrl = std::make_shared<MockLocalGroupCtrl>(LOCAL_GROUP_CTRL_ACTOR_NAME);
    litebus::Spawn(mockLocalGroupCtrl);

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    {
        messages::GroupResponse resp;
        resp.set_requestid(req->requestid());
        EXPECT_CALL(*mockLocalGroupCtrl, MockUnReserve).WillOnce(Return(resp.SerializeAsString()));
        auto future = underlayer.UnReserve("WillRegister", req);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);
    }

    {
        EXPECT_CALL(*mockLocalGroupCtrl, MockUnReserve).WillRepeatedly(Return("xxxxx"));
        mockUnderlayerActor->ClosePingPong();
        EXPECT_CALL(*mockInstanceCtrl_, UpdateMaxSchedRetryTimes(0)).Times(1);
        EXPECT_CALL(*mockDomainSrv_, NotifySchedAbnormal(_)).WillOnce(Return(AsyncReturn(Status::OK())));
        auto future = underlayer.UnReserve("WillRegister", req);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), (int32_t)StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    }

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Terminate(mockLocalGroupCtrl->GetAID());
    litebus::Await(mockUnderlayerActor);
    litebus::Await(mockLocalGroupCtrl);
}

// Bind successful
// Bind failed by underlayer lost
TEST_F(UnderlayerSchedMgrTest, Bind)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);
    UnderlayerRegiter(mockUnderlayerActor, underlayer);

    auto mockLocalGroupCtrl = std::make_shared<MockLocalGroupCtrl>(LOCAL_GROUP_CTRL_ACTOR_NAME);
    litebus::Spawn(mockLocalGroupCtrl);

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    {
        messages::GroupResponse resp;
        resp.set_requestid(req->requestid());
        EXPECT_CALL(*mockLocalGroupCtrl, MockBind).WillOnce(Return(resp.SerializeAsString()));
        auto future = underlayer.Bind("WillRegister", req);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);
    }

    {
        EXPECT_CALL(*mockLocalGroupCtrl, MockBind).WillRepeatedly(Return("xxxxx"));
        mockUnderlayerActor->ClosePingPong();
        EXPECT_CALL(*mockInstanceCtrl_, UpdateMaxSchedRetryTimes(0)).Times(1);
        EXPECT_CALL(*mockDomainSrv_, NotifySchedAbnormal(_)).WillOnce(Return(AsyncReturn(Status::OK())));
        auto future = underlayer.Bind("WillRegister", req);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), (int32_t)StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    }

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Terminate(mockLocalGroupCtrl->GetAID());
    litebus::Await(mockUnderlayerActor);
    litebus::Await(mockLocalGroupCtrl);
}

// UnBind successful
// UnBind failed by underlayer lost
TEST_F(UnderlayerSchedMgrTest, UnBind)
{
    UnderlayerSchedMgr underlayer(underlayerSchedMgrActor_->GetAID());
    auto mockUnderlayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderlayerActor);
    UnderlayerRegiter(mockUnderlayerActor, underlayer);

    auto mockLocalGroupCtrl = std::make_shared<MockLocalGroupCtrl>(LOCAL_GROUP_CTRL_ACTOR_NAME);
    litebus::Spawn(mockLocalGroupCtrl);

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    {
        messages::GroupResponse resp;
        resp.set_requestid(req->requestid());
        EXPECT_CALL(*mockLocalGroupCtrl, MockUnBind).WillOnce(Return(resp.SerializeAsString()));
        auto future = underlayer.UnBind("WillRegister", req);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);
    }

    {
        EXPECT_CALL(*mockLocalGroupCtrl, MockUnBind).WillRepeatedly(Return("xxxxx"));
        mockUnderlayerActor->ClosePingPong();
        EXPECT_CALL(*mockInstanceCtrl_, UpdateMaxSchedRetryTimes(0)).Times(1);
        EXPECT_CALL(*mockDomainSrv_, NotifySchedAbnormal(_)).WillOnce(Return(AsyncReturn(Status::OK())));
        auto future = underlayer.UnBind("WillRegister", req);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), (int32_t)StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    }

    litebus::Terminate(mockUnderlayerActor->GetAID());
    litebus::Terminate(mockLocalGroupCtrl->GetAID());
    litebus::Await(mockUnderlayerActor);
    litebus::Await(mockLocalGroupCtrl);
}

TEST_F(UnderlayerSchedMgrTest, DeletePodRequest)
{
    auto mockUnderLayerActor = std::make_shared<MockUnderlayer>("WillRegister");
    litebus::Spawn(mockUnderLayerActor);
    auto mockScalerActor = std::make_shared<MockScalerActor>();
    litebus::Spawn(mockScalerActor);
    auto deletePodRequest = std::make_shared<messages::DeletePodRequest>();
    deletePodRequest->set_requestid("req-123");
    deletePodRequest->set_functionagentid("function-agent-001");
    {
        litebus::Future<std::shared_ptr<messages::DeletePodResponse>> deletePodResponseArg;
        EXPECT_CALL(*mockUnderLayerActor.get(), MockDeletePodResponse).WillOnce(test::FutureArg<0>(&deletePodResponseArg));
        mockUnderLayerActor->SendRequest(underlayerSchedMgrActor_->GetAID(), "DeletePod", deletePodRequest->SerializeAsString());
        ASSERT_AWAIT_READY(deletePodResponseArg);
        EXPECT_EQ(deletePodResponseArg.Get()->code(), 0);
    }
    {
        underlayerSchedMgrActor_->SetScalerAddress(mockScalerActor->GetAID().Url());
        litebus::Future<std::shared_ptr<messages::DeletePodResponse>> deletePodResponseArg;
        EXPECT_CALL(*mockUnderLayerActor.get(), MockDeletePodResponse).WillOnce(test::FutureArg<0>(&deletePodResponseArg));
        EXPECT_CALL(*mockScalerActor.get(), MockDeletePodResponse).WillOnce(Return(111));
        mockUnderLayerActor->SendRequest(underlayerSchedMgrActor_->GetAID(), "DeletePod", deletePodRequest->SerializeAsString());
        ASSERT_AWAIT_READY(deletePodResponseArg);
        EXPECT_EQ(deletePodResponseArg.Get()->code(), 111);
    }
    litebus::Terminate(mockScalerActor->GetAID());
    litebus::Terminate(mockUnderLayerActor->GetAID());
    litebus::Await(mockScalerActor);
    litebus::Await(mockUnderLayerActor);
}
}  // namespace functionsystem::test
