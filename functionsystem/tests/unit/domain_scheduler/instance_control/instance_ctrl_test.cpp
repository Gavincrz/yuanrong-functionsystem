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

#include "domain_scheduler/instance_control/instance_ctrl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/constants/constants.h"
#include "domain_scheduler/instance_control/instance_ctrl_actor.h"
#include "common/schedule_decision/schedule_recorder/schedule_recorder.h"
#include "mocks/mock_domain_sched_srv.h"
#include "mocks/mock_scheduler.h"
#include "mocks/mock_domain_underlayer_sched_mgr.h"
#include "mocks/mock_scaler_actor.h"
#include "mocks/mock_shared_client.h"
#include "mocks/mock_shared_client_manager_proxy.h"
#include "mocks/mock_resource_view.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using schedule_decision::ScheduleResult;
using ::testing::_;
using ::testing::Return;
class DomainInstanceCtrlTest : public ::testing::Test {
public:
    void SetUp() override
    {
        instanceCtrl_ = std::make_shared<domain_scheduler::InstanceCtrlActor>("DomainInstanceCtrlTest");
        mockScheduler_ = std::make_shared<MockScheduler>();
        mockUnderlayerScheMgr_ = std::make_shared<MockDomainUnderlayerSchedMgr>();
        auto resourceViewMgr_ = std::make_shared<resource_view::ResourceViewMgr>();
        primary_ = MockResourceView::CreateMockResourceView();
        virtual_ = MockResourceView::CreateMockResourceView();
        resourceViewMgr_->primary_ = primary_;
        resourceViewMgr_->virtual_ = virtual_;
        instanceCtrl_->BindScheduler(mockScheduler_);
        instanceCtrl_->BindResourceView(resourceViewMgr_);
        instanceCtrl_->BindUnderlayerMgr(mockUnderlayerScheMgr_);
        instanceCtrl_->BindScheduleRecorder(schedule_decision::ScheduleRecorder::CreateScheduleRecorder());
        litebus::Spawn(instanceCtrl_);
    }

    void TearDown() override
    {
        litebus::Terminate(instanceCtrl_->GetAID());
        litebus::Await(instanceCtrl_);
    }

protected:
    std::shared_ptr<domain_scheduler::InstanceCtrlActor> instanceCtrl_;
    std::shared_ptr<MockScheduler> mockScheduler_;
    std::shared_ptr<MockDomainUnderlayerSchedMgr> mockUnderlayerScheMgr_;
    std::shared_ptr<MockResourceView> primary_;
    std::shared_ptr<MockResourceView> virtual_;
};

/**
 * Description: Test the normal scheduling process.
 * Steps:
 * 1. mock Scheduler ScheduleDecision return success
 * 2. mock underlayer DispatchSchedule return success
 * Expectation:
 * Schedule return success
 */
TEST_F(DomainInstanceCtrlTest, ScheduleInstanceSuccessful)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).WillOnce(Return(AsyncReturn(result)));

    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _)).WillOnce(Return(AsyncReturn(mockSchedRsp)));
    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);
}

/**
 * Description: Test the scheduling failed because version is wrong.
 * Steps:
 * 1. mock Scheduler ScheduleDecision return version is wrong
 * 2. mock underlayer DispatchSchedule return success
 * Expectation:
 * Schedule return version is wrong
 */
TEST_F(DomainInstanceCtrlTest, ScheduleInstanceVersionWrong)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).WillOnce(Return(AsyncReturn(result)));

    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _)).WillOnce(Return(AsyncReturn(mockSchedRsp)));
    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
}

/**
 * Description: Test that local resources are insufficient.
 * Steps:
 * 1. mock Scheduler ScheduleDecision Failed to select a proper node due to insufficient resources.
 * 2. mock domainSrv ForwardSchedule
 *    - return no uplayer
 *    - return success/failed
 * Expectation:
 * 1. return failed
 * 2. return success/failed
 */
TEST_F(DomainInstanceCtrlTest, InsufficientResource)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    instanceCtrl_->SetRetryScheduleIntervals({ 100, 100, 100 });

    ScheduleResult resourceNotEnough{ "", RESOURCE_NOT_ENOUGH, "resources not enough" };
    ScheduleResult invalidParam{ "", PARAMETER_ERROR, "parameter error" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillRepeatedly(Return(AsyncReturn(resourceNotEnough)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::ERR_RESOURCE_NOT_ENOUGH);
    EXPECT_EQ(rsp->requestid(), requestID);
    YRLOG_INFO("err msg: {}", rsp->message());
    EXPECT_THAT(rsp->message(), testing::HasSubstr("resources not enough"));

    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillRepeatedly(Return(AsyncReturn(invalidParam)));
    future = instanceCtrl.Schedule(req);
    ASSERT_AWAIT_READY_FOR(future, 1000);
    rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::ERR_RESOURCE_CONFIG_ERROR);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_THAT(
        rsp->message(),
        testing::HasSubstr("invalid resource parameter, request resource is greater than each node's max resource"));
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
}

/**
 * Description: The request is successfully delivered after two retries.
 * Steps:
 * 1. mock Scheduler ScheduleDecision return success
 * 2. mock underlayer DispatchSchedule  times out for two times. then success
 * Expectation:
 * return success
 */
TEST_F(DomainInstanceCtrlTest, SuccessfullyAfterRetries)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).WillOnce(Return(AsyncReturn(result)));

    auto pro = litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>();
    pro.SetFailed(StatusCode::REQUEST_TIME_OUT);
    auto failedFuture = pro.GetFuture();
    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _))
        .WillOnce(Return(AsyncReturn(failedFuture)))
        .WillOnce(Return(AsyncReturn(failedFuture)))
        .WillOnce(Return(AsyncReturn(mockSchedRsp)));
    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).Times(3).WillRepeatedly(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));


    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
}

/**
 * Description: Rescheduling is triggered after three retries fail.
 * Steps:
 * 1. mock Scheduler ScheduleDecision return success
 * 2. mock underlayer DispatchSchedule times out for three times
 * 4. Rescheduling success
 * Expectation:
 * 1. return success
 */
TEST_F(DomainInstanceCtrlTest, ReschedulingTriggeredByRetriesFailed)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).WillRepeatedly(Return(AsyncReturn(result)));

    auto pro = litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>();
    pro.SetFailed(StatusCode::REQUEST_TIME_OUT);
    auto failedFuture = pro.GetFuture();
    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _))
        .WillOnce(Return(AsyncReturn(failedFuture)))
        .WillOnce(Return(AsyncReturn(failedFuture)))
        .WillOnce(Return(AsyncReturn(failedFuture)))
        .WillOnce(Return(AsyncReturn(mockSchedRsp)));

    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    resource_view::PullResourceRequest snapshot2;
    snapshot2.set_localviewinittime("INITTIME1");
    snapshot2.set_version(5);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}))
        .WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}))
        .WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}))
        .WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot2}));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 5);
}

/**
 * Description: ReScheduling after conflict
 * Steps:
 * 1. mock Scheduler ScheduleDecision return success
 * 2. mock underlayer DispatchSchedule return failed
 *  - ReScheduling success
 * Expectation:
 * 2. return success
 */
TEST_F(DomainInstanceCtrlTest, ReSchedulingAfterConflict)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).WillRepeatedly(Return(AsyncReturn(result)));

    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(StatusCode::RESOURCE_NOT_ENOUGH);
    mockSchedRsp->set_requestid(requestID);
    auto mockSuccessRsp = std::make_shared<messages::ScheduleResponse>();
    mockSuccessRsp->set_code(0);
    mockSuccessRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _))
        .WillOnce(Return(AsyncReturn(mockSchedRsp)))
        .WillOnce(Return(AsyncReturn(mockSuccessRsp)));
    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    resource_view::PullResourceRequest snapshot2;
    snapshot2.set_localviewinittime("INITTIME1");
    snapshot2.set_version(5);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}))
        .WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot2}));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 5);
}

/**
 * Description: ReScheduling after conflict
 * Steps:
 * 1. mock Scheduler ScheduleDecision return success
 * 2. mock underlayer DispatchSchedule return failed
 *  - ReScheduling failed
 * Expectation:
 * 2. return failed
 */
TEST_F(DomainInstanceCtrlTest, ReSchedulingFailedAfterConflict)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).WillRepeatedly(Return(AsyncReturn(result)));

    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(StatusCode::RESOURCE_NOT_ENOUGH);
    mockSchedRsp->set_requestid(requestID);
    int i = 0;
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _))
        .WillRepeatedly(testing::Invoke(
            [&i, rsp(mockSchedRsp)](const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest>
                &req)
                -> litebus::Future<std::shared_ptr<messages::ScheduleResponse>> {
                if (i >= 10) {
                    rsp->set_code(StatusCode::ERR_SCHEDULE_CANCELED);
                    return rsp;
                }
                i++;
                rsp->set_code(StatusCode::RESOURCE_NOT_ENOUGH);
                return rsp;
            }));
    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillRepeatedly(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    // After a scheduling conflict occurs, the error code indicating insufficient resources is returned after three
    // retry attempts.
    EXPECT_EQ(rsp->code(), StatusCode::ERR_SCHEDULE_CANCELED);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);
}

/**
 * Description: create agent success
 * Steps:
 * 1. schedule decision
 * 2. create agent
 * 3. schedule decision
 * Expectation:
 * 1. resources not enough
 * 2. create agent success
 * 3. success
 */
TEST_F(DomainInstanceCtrlTest, CreateAgentSuccess)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());
    instanceCtrl.SetDomainLevel(true);
    instanceCtrl.UpdateMaxSchedRetryTimes(1);

    auto mockScaler = std::make_shared<MockScalerActor>();
    litebus::Spawn(mockScaler);
    instanceCtrl.SetScalerAddress(mockScaler->GetAID().Url());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    ScheduleResult resourceNotEnough{ "", RESOURCE_NOT_ENOUGH, "resources not enough" };
    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(result)));

    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _)).WillOnce(Return(AsyncReturn(mockSchedRsp)));

    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    messages::CreateAgentResponse createAgentRsp;
    createAgentRsp.set_requestid(requestID);
    createAgentRsp.set_code(0);
    (*createAgentRsp.mutable_updatedcreateoptions())["123"] = "123";
    EXPECT_CALL(*mockScaler, GetCreateAgentResponse()).WillOnce(Return(createAgentRsp.SerializeAsString()));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    (*req->mutable_instance()->mutable_createoptions())[RESOURCE_OWNER_KEY] = SYSTEM_OWNER_VALUE;
    req->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);

    litebus::Terminate(mockScaler->GetAID());
    litebus::Await(mockScaler->GetAID());
}

TEST_F(DomainInstanceCtrlTest, AffinityCreateAgent)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());
    instanceCtrl.SetDomainLevel(true);
    instanceCtrl.UpdateMaxSchedRetryTimes(1);

    auto mockScaler = std::make_shared<MockScalerActor>();
    litebus::Spawn(mockScaler);
    instanceCtrl.SetScalerAddress(mockScaler->GetAID().Url());

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    ScheduleResult resourceNotEnough{ "", RESOURCE_NOT_ENOUGH, "resources not enough" };
    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(result)));

    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _)).WillOnce(Return(AsyncReturn(mockSchedRsp)));

    messages::CreateAgentResponse createAgentRsp;
    createAgentRsp.set_requestid(requestID);
    createAgentRsp.set_code(0);
    (*createAgentRsp.mutable_updatedcreateoptions())["123"] = "123";
    EXPECT_CALL(*mockScaler, GetCreateAgentResponse()).WillOnce(Return(createAgentRsp.SerializeAsString()));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    req->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    (*req->mutable_instance()
          ->mutable_scheduleoption()
          ->mutable_affinity()
          ->mutable_instanceaffinity()
          ->mutable_affinity())["label1"] = resource_view::AffinityType::RequiredAffinity;
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);

    litebus::Terminate(mockScaler->GetAID());
    litebus::Await(mockScaler->GetAID());
}

/**
 * Description: create agent failed
 * Steps:
 * 1. schedule decision
 * 2. create agent
 * 3. schedule decision
 * Expectation:
 * 1. resources not enough
 * 2. create agent failed
 * 3. return init failed code
 */
TEST_F(DomainInstanceCtrlTest, CreateAgentFailed)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());
    instanceCtrl.SetDomainLevel(true);
    instanceCtrl_->SetCreateAgentRetryInterval(100);

    auto mockScaler = std::make_shared<MockScalerActor>();
    litebus::Spawn(mockScaler);

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    ScheduleResult resourceNotEnough{ "", RESOURCE_NOT_ENOUGH, "resources not enough" };
    ScheduleResult invalidParam{ "", PARAMETER_ERROR, "parameter error" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(invalidParam)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    req->mutable_instance()->set_requestid(requestID);
    (*req->mutable_instance()->mutable_createoptions())[RESOURCE_OWNER_KEY] = SYSTEM_OWNER_VALUE;
    req->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), -1);
    EXPECT_EQ(rsp->requestid(), requestID);
    YRLOG_INFO("err msg: {}", rsp->message());
    EXPECT_THAT(rsp->message(), testing::HasSubstr("scaler is not enabled"));

    messages::CreateAgentResponse createAgentRsp;
    createAgentRsp.set_requestid("test");
    createAgentRsp.set_code(-1);
    EXPECT_CALL(*mockScaler, GetCreateAgentResponse()).WillRepeatedly(Return(createAgentRsp.SerializeAsString()));

    instanceCtrl.SetScalerAddress(mockScaler->GetAID().Url());

    future = instanceCtrl.Schedule(req);
    ASSERT_AWAIT_READY_FOR(future, 1000);
    rsp = future.Get();
    EXPECT_EQ(rsp->code(), -1);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());

    litebus::Terminate(mockScaler->GetAID());
    litebus::Await(mockScaler->GetAID());
}

/**
 * Description: create agent success, then retry schedule success
 * Steps:
 * 1. schedule decision
 * 2. create agent
 * 3. schedule decision twice
 * Expectation:
 * 1. resources not enough
 * 2. create agent success
 * 3. failed, success
 */
TEST_F(DomainInstanceCtrlTest, CreateAgentRetrySuccess)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());
    instanceCtrl.SetDomainLevel(true);

    auto mockScaler = std::make_shared<MockScalerActor>();
    litebus::Spawn(mockScaler);
    instanceCtrl.SetScalerAddress(mockScaler->GetAID().Url());
    instanceCtrl_->SetCreateAgentAwaitRetryTimes(1);
    instanceCtrl_->SetCreateAgentAwaitRetryInterval(100);

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    ScheduleResult resourceNotEnough{ "", RESOURCE_NOT_ENOUGH, "resources not enough" };
    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(result)));

    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _)).WillOnce(Return(AsyncReturn(mockSchedRsp)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    (*req->mutable_instance()->mutable_createoptions())[RESOURCE_OWNER_KEY] = SYSTEM_OWNER_VALUE;
    req->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);

    messages::CreateAgentResponse createAgentRsp;
    createAgentRsp.set_requestid(requestID);
    createAgentRsp.set_code(0);
    (*createAgentRsp.mutable_updatedcreateoptions())[RESOURCE_OWNER_KEY] = SYSTEM_OWNER_VALUE;
    EXPECT_CALL(*mockScaler, GetCreateAgentResponse()).WillOnce(Return(createAgentRsp.SerializeAsString()));

    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);

    litebus::Terminate(mockScaler->GetAID());
    litebus::Await(mockScaler->GetAID());
}

/**
 * Description: create agent success, then retry schedule failed
 * Steps:
 * 1. schedule decision
 * 2. create agent
 * 3. schedule decision twice
 * Expectation:
 * 1. resources not enough
 * 2. create agent success
 * 3. failed, failed, return inti failed code
 */
TEST_F(DomainInstanceCtrlTest, CreateAgentRetryFailed)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());
    instanceCtrl.SetDomainLevel(true);

    auto mockScaler = std::make_shared<MockScalerActor>();
    litebus::Spawn(mockScaler);
    instanceCtrl.SetScalerAddress(mockScaler->GetAID().Url());
    instanceCtrl_->SetCreateAgentAwaitRetryTimes(1);
    instanceCtrl_->SetCreateAgentAwaitRetryInterval(100);

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    ScheduleResult resourceNotEnough{ "", RESOURCE_NOT_ENOUGH, "resources not enough" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    req->mutable_instance()->set_requestid(requestID);
    (*req->mutable_instance()->mutable_createoptions())[RESOURCE_OWNER_KEY] = SYSTEM_OWNER_VALUE;
    req->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);

    messages::CreateAgentResponse createAgentRsp;
    createAgentRsp.set_requestid(requestID);
    createAgentRsp.set_code(0);
    (*createAgentRsp.mutable_updatedcreateoptions())[RESOURCE_OWNER_KEY] = SYSTEM_OWNER_VALUE;
    EXPECT_CALL(*mockScaler, GetCreateAgentResponse()).WillOnce(Return(createAgentRsp.SerializeAsString()));

    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::ERR_RESOURCE_NOT_ENOUGH);
    EXPECT_EQ(rsp->requestid(), requestID);
    YRLOG_INFO("err msg: {}", rsp->message());
    EXPECT_THAT(rsp->message(), testing::HasSubstr("resources not enough"));
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());

    litebus::Terminate(mockScaler->GetAID());
    litebus::Await(mockScaler->GetAID());
}

/**
 * Description: monopoly schedule retry
 * Steps:
 * 1. schedule decision
 * 2. retry three times
 * 3. schedule decision
 * 4. retry twice
 * Expectation:
 * 1. resources not enough
 * 2. schedule failed all three times, return failed code
 * 3. resources not enough
 * 4. failed, success
 */
TEST_F(DomainInstanceCtrlTest, MonopolyRetry)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());
    instanceCtrl.SetDomainLevel(true);

    instanceCtrl_->SetRetryScheduleIntervals({ 100, 100, 100 });

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    ScheduleResult resourceNotEnough{ "", RESOURCE_NOT_ENOUGH, "resources not enough" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    req->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::ERR_RESOURCE_NOT_ENOUGH);
    EXPECT_EQ(rsp->requestid(), requestID);
    YRLOG_INFO("err msg: {}", rsp->message());
    EXPECT_THAT(rsp->message(), testing::HasSubstr("resources not enough"));

    std::string mockSelectedName = "selected";
    ScheduleResult ok{ mockSelectedName, SUCCESS, "success" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(resourceNotEnough)))
        .WillOnce(Return(AsyncReturn(ok)));
    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _)).WillOnce(Return(AsyncReturn(mockSchedRsp)));

    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    future = instanceCtrl.Schedule(req);
    ASSERT_AWAIT_READY_FOR(future, 1000);
    rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::SUCCESS);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);
}

TEST_F(DomainInstanceCtrlTest, AffinityRetry)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());
    instanceCtrl.SetDomainLevel(true);

    instanceCtrl_->SetRetryScheduleIntervals({ 100, 100, 100 });

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    ScheduleResult labelNotFound{ "", AFFINITY_SCHEDULE_FAILED, "affinity schedule failed" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(labelNotFound)))
        .WillOnce(Return(AsyncReturn(labelNotFound)))
        .WillOnce(Return(AsyncReturn(labelNotFound)))
        .WillOnce(Return(AsyncReturn(labelNotFound)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    req->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    (*req->mutable_instance()
          ->mutable_scheduleoption()
          ->mutable_affinity()
          ->mutable_instanceaffinity()
          ->mutable_affinity())["label1"] = resource_view::AffinityType::RequiredAffinity;
    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 2000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::AFFINITY_SCHEDULE_FAILED);
    EXPECT_EQ(rsp->requestid(), requestID);
    YRLOG_INFO("err msg: {}", rsp->message());

    std::string mockSelectedName = "selected";
    ScheduleResult ok{ mockSelectedName, SUCCESS, "success" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(labelNotFound)))
        .WillOnce(Return(AsyncReturn(labelNotFound)))
        .WillOnce(Return(AsyncReturn(ok)));
    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _)).WillOnce(Return(AsyncReturn(mockSchedRsp)));

    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    future = instanceCtrl.Schedule(req);
    ASSERT_AWAIT_READY_FOR(future, 2000);
    rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::SUCCESS);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);
}

/**
 * Description: create agent with poolID success, then retry schedule success
 * Steps:
 * 1. schedule decision
 * 2. create agent
 * 3. schedule decision twice
 * Expectation:
 * 1. affinity failed
 * 2. create agent success
 * 3. failed, success
 */
TEST_F(DomainInstanceCtrlTest, CreateAgentByPoolIDAffinityFailed)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());
    instanceCtrl.SetDomainLevel(true);

    auto mockScaler = std::make_shared<MockScalerActor>();
    litebus::Spawn(mockScaler);
    instanceCtrl.SetScalerAddress(mockScaler->GetAID().Url());
    instanceCtrl_->SetCreateAgentAwaitRetryTimes(1);
    instanceCtrl_->SetCreateAgentAwaitRetryInterval(100);

    std::string requestID = "request-123";
    std::string traceID = "trace-123";

    ScheduleResult affinityFailed{ "", AFFINITY_SCHEDULE_FAILED, "affinity schedule failed"};
    std::string mockSelectedName = "selected";
    ScheduleResult result{ mockSelectedName, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(affinityFailed)))
        .WillOnce(Return(AsyncReturn(affinityFailed)))
        .WillOnce(Return(AsyncReturn(result)));

    auto mockSchedRsp = std::make_shared<messages::ScheduleResponse>();
    mockSchedRsp->set_code(0);
    mockSchedRsp->set_requestid(requestID);
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelectedName, _)).WillOnce(Return(AsyncReturn(mockSchedRsp)));

    resource_view::PullResourceRequest snapshot;
    snapshot.set_localviewinittime("INITTIME1");
    snapshot.set_version(2);
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid(traceID);
    (*req->mutable_instance()->mutable_createoptions())["AFFINITY_POOL_ID"] = "pool1";

    messages::CreateAgentResponse createAgentRsp;
    createAgentRsp.set_requestid(requestID);
    createAgentRsp.set_code(0);
    EXPECT_CALL(*mockScaler, GetCreateAgentResponse()).WillOnce(Return(createAgentRsp.SerializeAsString()));

    auto future = instanceCtrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 1000);
    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
    EXPECT_EQ(rsp->requestid(), requestID);
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
    EXPECT_EQ(req->unitsnapshot().localviewinittime(), "INITTIME1");
    EXPECT_EQ(req->unitsnapshot().version(), 2);

    litebus::Terminate(mockScaler->GetAID());
    litebus::Await(mockScaler->GetAID());
}

TEST_F(DomainInstanceCtrlTest, ScheduleTimeoutCancel)
{
    domain_scheduler::InstanceCtrl instanceCtrl(instanceCtrl_->GetAID());

    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).WillOnce(Return(litebus::Future<ScheduleResult>()));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("req");
    req->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(10);
    auto future = instanceCtrl.Schedule(req);

    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), StatusCode::ERR_SCHEDULE_CANCELED);
    EXPECT_EQ(rsp->requestid(), "req");
    EXPECT_TRUE(instanceCtrl_->cancelTag_.find(rsp->requestid())==instanceCtrl_->cancelTag_.end());
}
}  // namespace functionsystem::test