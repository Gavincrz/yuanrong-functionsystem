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

#include "common/constants/actor_name.h"
#include "common/constants/metastore_keys.h"
#include "common/resource_view/view_utils.h"
#include "common/resource_view/resource_view_mgr.h"
#include "mocks/mock_domain_underlayer_sched_mgr.h"
#include "mocks/mock_resource_view.h"
#include "mocks/mock_scheduler.h"
#include "utils/future_test_helper.h"
#include "domain_scheduler/domain_group_control/domain_group_ctrl_actor.h"

namespace functionsystem::test {
using namespace testing;
using namespace domain_scheduler;
const int INSTANCE_NUM = 3;
class LocalSchedSrvStub : public litebus::ActorBase {
public:
    LocalSchedSrvStub() : litebus::ActorBase("LocalSchedSrvStub")
    {
    }
    ~LocalSchedSrvStub() = default;

    litebus::Future<messages::GroupResponse> ForwardGroupSchedule(const litebus::AID &dst,
                                                                  const std::shared_ptr<messages::GroupInfo> &groupInfo)
    {
        Send(dst, "ForwardGroupSchedule", groupInfo->SerializeAsString());
        promises_[groupInfo->requestid()] = std::make_shared<litebus::Promise<messages::GroupResponse>>();
        return promises_[groupInfo->requestid()]->GetFuture();
    }

    void OnForwardGroupSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockForwardGroupSchedule();
        messages::GroupResponse resp;
        resp.ParseFromString(msg);
        if (promises_.find(resp.requestid()) != promises_.end()) {
            (void)promises_[resp.requestid()]->SetValue(resp);
            (void)promises_.erase(resp.requestid());
        }
    }

    MOCK_METHOD(void, MockForwardGroupSchedule, ());

    void Init() override
    {
        Receive("OnForwardGroupSchedule", &LocalSchedSrvStub::OnForwardGroupSchedule);
    }

private:
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::GroupResponse>>> promises_;
};

class DomainGroupCtrlTest : public ::testing::Test {
public:
    void SetUp() override
    {
        mockScheduler_ = std::make_shared<MockScheduler>();
        mockUnderlayerSchedMgr_ = std::make_shared<MockDomainUnderlayerSchedMgr>();
        domainGroupCtrlActor_ = std::make_shared<DomainGroupCtrlActor>(DOMAIN_GROUP_CTRL_ACTOR_NAME);
        domainGroupCtrlActor_->BindScheduler(mockScheduler_);
        domainGroupCtrlActor_->BindUnderlayerMgr(mockUnderlayerSchedMgr_);
        domainGroupCtrlActor_->BindScheduleRecorder(schedule_decision::ScheduleRecorder::CreateScheduleRecorder());
        localSchedSrvStub_ = std::make_shared<LocalSchedSrvStub>();
        litebus::Spawn(domainGroupCtrlActor_);
        litebus::Spawn(localSchedSrvStub_);
    }

    void TearDown() override
    {
        litebus::Terminate(domainGroupCtrlActor_->GetAID());
        litebus::Terminate(localSchedSrvStub_->GetAID());
        litebus::Await(domainGroupCtrlActor_);
        litebus::Await(localSchedSrvStub_);
        mockUnderlayerSchedMgr_ = nullptr;
        mockScheduler_ = nullptr;
        domainGroupCtrlActor_ = nullptr;
        localSchedSrvStub_ = nullptr;
    }

    std::shared_ptr<messages::GroupInfo> NewGroupInfo(int32_t timeout, int32_t scheduleTimeout = 0)
    {
        auto groupInfo = std::make_shared<messages::GroupInfo>();
        groupInfo->set_requestid("request-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        groupInfo->set_traceid("traceID");
        groupInfo->set_groupid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        groupInfo->mutable_groupopts()->set_timeout(timeout);
        for (int i = 0; i < INSTANCE_NUM; i++) {
            auto request = groupInfo->add_requests();
            request->set_requestid(groupInfo->requestid() + "-" + std::to_string(i));
            request->set_traceid(groupInfo->traceid());
            request->mutable_instance()->set_instanceid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
            request->mutable_instance()->set_groupid(groupInfo->groupid());
            request->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(scheduleTimeout);
        }
        return groupInfo;
    }
    std::shared_ptr<messages::GroupInfo> NewSfmdGroupInfo(int32_t timeout)
    {
        auto groupInfo = NewGroupInfo(timeout);
        for (auto &req : *groupInfo->mutable_requests()) {
            *req.mutable_instance() = view_utils::Get1DInstanceWithNpuResource(6, 20, 1);
            req.mutable_instance()->set_groupid(groupInfo->groupid());
        }
        return groupInfo;
    }

    std::shared_ptr<messages::ScheduleResponse> NewScheduleResponse(std::string selectedNodeId)
    {
        auto response = std::make_shared<messages::ScheduleResponse>();
        response->set_code(StatusCode::SUCCESS);
        messages::ScheduleResult scheduleResult;
        scheduleResult.set_nodeid(selectedNodeId);
        *(response->mutable_scheduleresult()) = scheduleResult;
        return response;
    }

    common::HeteroDeviceInfo NewHeteroDeviceInfo(int deviceId, std::string deviceIp)
    {
        common::HeteroDeviceInfo device;
        device.set_deviceid(deviceId);
        device.set_deviceip(deviceIp);
        return device;
    }

    std::shared_ptr<messages::GroupInfo> NewRangeInstanceScheduleGroupInfo(int32_t timeout, int32_t max, int32_t min, int32_t step)
    {
        auto groupInfo = std::make_shared<messages::GroupInfo>();
        groupInfo->set_requestid("request-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        groupInfo->set_traceid("traceID");
        groupInfo->set_groupid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        groupInfo->mutable_groupopts()->set_timeout(timeout);
        auto request = groupInfo->add_requests();
        request->set_requestid(groupInfo->requestid() + "-r");
        request->set_traceid(groupInfo->traceid());
        request->mutable_instance()->set_groupid(groupInfo->groupid());
        request->set_isinsrangescheduler(true);
        request->mutable_rangeopts()->mutable_range()->set_max(max);
        request->mutable_rangeopts()->mutable_range()->set_min(min);
        request->mutable_rangeopts()->mutable_range()->set_step(step);
        request->mutable_rangeopts()->set_currangeinstancenum(max);
        groupInfo->set_insrangescheduler(true);
        *groupInfo->mutable_insrange() = request->rangeopts().range();
        return groupInfo;
    }

protected:
    std::shared_ptr<DomainGroupCtrlActor> domainGroupCtrlActor_;
    std::shared_ptr<MockScheduler> mockScheduler_;
    std::shared_ptr<MockDomainUnderlayerSchedMgr> mockUnderlayerSchedMgr_;
    std::shared_ptr<LocalSchedSrvStub> localSchedSrvStub_;
};

// invalid msg
TEST_F(DomainGroupCtrlTest, InvalidMsg)
{
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).Times(0);
    domainGroupCtrlActor_->ForwardGroupSchedule(litebus::AID(), "ForwardGroupSchedule", "XXXXX");
}

// resource not enough timeout
TEST_F(DomainGroupCtrlTest, ResourceNotEnoughTimeout)
{
    auto groupInfo = NewGroupInfo(1);
    schedule_decision::GroupScheduleResult result;
    result.code = StatusCode::RESOURCE_NOT_ENOUGH;
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillRepeatedly(Return(AsyncReturn(result)));
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::RESOURCE_NOT_ENOUGH);
}

// reserve failed & rollback & retry decision failed
TEST_F(DomainGroupCtrlTest, ScheduleFailedAfterReserveFailure)
{
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < INSTANCE_NUM; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    litebus::Promise<schedule_decision::GroupScheduleResult> promise;
    promise.SetFailed(StatusCode::ERR_GROUP_SCHEDULE_FAILED);
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result))).WillOnce(Return(AsyncReturn(promise.GetFuture())));

    auto response = std::make_shared<messages::ScheduleResponse>();
    response->set_code(StatusCode::SUCCESS);
    auto response1 = std::make_shared<messages::ScheduleResponse>();
    response1->set_code(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve)
        .WillOnce(Return(AsyncReturn(response)))
        .WillOnce(Return(AsyncReturn(response1)))
        .WillOnce(Return(AsyncReturn(response1)));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve)
        .WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto groupInfo = NewGroupInfo(100);
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::ERR_GROUP_SCHEDULE_FAILED);
}

// reserve failed & rollback & retry decision failed
TEST_F(DomainGroupCtrlTest, ReserveRollback)
{
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < INSTANCE_NUM; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    schedule_decision::GroupScheduleResult failure;
    failure.code = StatusCode::ERR_GROUP_SCHEDULE_FAILED;
    schedule_decision::GroupScheduleResult noEnough;
    noEnough.code = StatusCode::RESOURCE_NOT_ENOUGH;
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_))
        .WillOnce(Return(AsyncReturn(result)))
        .WillOnce(Return(AsyncReturn(result)))
        .WillOnce(Return(AsyncReturn(noEnough)))
        .WillRepeatedly(Return(AsyncReturn(failure)));

    auto response = std::make_shared<messages::ScheduleResponse>();
    response->set_code(StatusCode::SUCCESS);
    auto response1 = std::make_shared<messages::ScheduleResponse>();
    response1->set_code(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve)
        // first round to reserve
        .WillOnce(Return(AsyncReturn(response)))
        .WillOnce(Return(AsyncReturn(response1)))
        .WillOnce(Return(AsyncReturn(response1)))
        // second round to reserve
        .WillOnce(Return(AsyncReturn(response)))
        .WillOnce(Return(AsyncReturn(response)))
        .WillOnce(Return(AsyncReturn(response1)));

    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve)
        .WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto groupInfo = NewGroupInfo(1);
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::RESOURCE_NOT_ENOUGH);
}

// bind failed & rollback & retry decision failed
TEST_F(DomainGroupCtrlTest, BindRollback)
{
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < INSTANCE_NUM; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    litebus::Promise<schedule_decision::GroupScheduleResult> promise;
    promise.SetFailed(StatusCode::ERR_GROUP_SCHEDULE_FAILED);
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result))).WillOnce(Return(AsyncReturn(promise.GetFuture())));

    auto response = std::make_shared<messages::ScheduleResponse>();
    response->set_code(StatusCode::SUCCESS);
    auto response1 = std::make_shared<messages::ScheduleResponse>();
    response1->set_code(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve).Times(3)
        .WillRepeatedly(Return(AsyncReturn(response)));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);

    EXPECT_CALL(*mockUnderlayerSchedMgr_, Bind)
        .WillOnce(Return(AsyncReturn(Status::OK())))
        .WillOnce(Return(AsyncReturn(Status(StatusCode::ERR_INNER_COMMUNICATION))))
        .WillOnce(Return(AsyncReturn(Status(StatusCode::ERR_INNER_COMMUNICATION))));

    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnBind)
        .WillOnce(Return(AsyncReturn(Status::OK())))
        .WillOnce(Return(AsyncReturn(Status(StatusCode::ERR_INNER_COMMUNICATION))))
        .WillOnce(Return(AsyncReturn(Status(StatusCode::ERR_INNER_COMMUNICATION))));

    auto groupInfo = NewGroupInfo(100);
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::ERR_GROUP_SCHEDULE_FAILED);
}

// bind failed because of local failed & rollback & retry decision failed
TEST_F(DomainGroupCtrlTest, LocalAbnormalBindRollback)
{
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < INSTANCE_NUM; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));

    auto response = std::make_shared<messages::ScheduleResponse>();
    response->set_code(StatusCode::SUCCESS);
    EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve).Times(3)
        .WillRepeatedly(Return(AsyncReturn(response)));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);

    EXPECT_CALL(*mockUnderlayerSchedMgr_, Bind)
        .WillOnce(Return(AsyncReturn(Status::OK())))
        .WillOnce(Return(AsyncReturn(Status(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER))))
        .WillOnce(Return(AsyncReturn(Status(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER))));

    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnBind)
        .WillOnce(Return(AsyncReturn(Status::OK())))
        .WillOnce(Return(AsyncReturn(Status(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER))))
        .WillOnce(Return(AsyncReturn(Status(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER))));

    auto groupInfo = NewGroupInfo(100);
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::ERR_GROUP_SCHEDULE_FAILED);
}

// group schedule successful
TEST_F(DomainGroupCtrlTest, GroupScheduleSuccessful)
{
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < INSTANCE_NUM; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));

    auto response = std::make_shared<messages::ScheduleResponse>();
    response->set_code(StatusCode::SUCCESS);
    EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve).Times(3)
        .WillRepeatedly(Return(AsyncReturn(response)));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);

    EXPECT_CALL(*mockUnderlayerSchedMgr_, Bind)
        .WillRepeatedly(Return(AsyncReturn(Status::OK())));

    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnBind).Times(0);

    auto groupInfo = NewGroupInfo(100);
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);
}

TEST_F(DomainGroupCtrlTest, GroupScheduleRangeInstanceSuccessful)
{
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < INSTANCE_NUM; ++i) {
        result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
    auto response = std::make_shared<messages::ScheduleResponse>();
    response->set_code(StatusCode::SUCCESS);
    EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve).Times(3)
        .WillRepeatedly(Return(AsyncReturn(response)));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);

    EXPECT_CALL(*mockUnderlayerSchedMgr_, Bind)
        .WillRepeatedly(Return(AsyncReturn(Status::OK())));

    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnBind).Times(0);
    auto groupInfo = NewRangeInstanceScheduleGroupInfo(100, 3, 1, 1);
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);
}

TEST_F(DomainGroupCtrlTest, GroupScheduleRangeInstanceFailed_ResourceNotEnoughTimeout)
{
    auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, 3, 1, 1);
    schedule_decision::GroupScheduleResult result;
    result.code = StatusCode::RESOURCE_NOT_ENOUGH;
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillRepeatedly(Return(AsyncReturn(result)));
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::RESOURCE_NOT_ENOUGH);
}

// reserve failed & rollback & retry decision failed
TEST_F(DomainGroupCtrlTest, GroupScheduleRangeInstanceReserveCallBackThenSuccessful)
{
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < INSTANCE_NUM; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    schedule_decision::GroupScheduleResult result2;
    result2.code = 0;
    for (int i = 0; i < 2; ++i) {
        (void)result2.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    schedule_decision::GroupScheduleResult failure;
    failure.code = StatusCode::ERR_GROUP_SCHEDULE_FAILED;
    schedule_decision::GroupScheduleResult noEnough;
    noEnough.code = StatusCode::RESOURCE_NOT_ENOUGH;
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_))
        .WillOnce(Return(AsyncReturn(result)))
        .WillOnce(Return(AsyncReturn(noEnough)));

    auto response = std::make_shared<messages::ScheduleResponse>();
    response->set_code(StatusCode::SUCCESS);
    auto response1 = std::make_shared<messages::ScheduleResponse>();
    response1->set_code(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
    EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve)
        // first round to reserve
        .WillOnce(Return(AsyncReturn(response)))
        .WillOnce(Return(AsyncReturn(response1)))
        .WillOnce(Return(AsyncReturn(response1)));

    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve)
        .WillRepeatedly(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, Bind)
        .WillRepeatedly(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnBind).Times(0);

    auto groupInfo = NewRangeInstanceScheduleGroupInfo(100, 3, 1, 1);
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::RESOURCE_NOT_ENOUGH);
}

// reserve failed & rollback all instance after successful
TEST_F(DomainGroupCtrlTest, RollBackInstanceAfterLastestSuccessfulReserved)
{
    auto genResult = [](std::shared_ptr<GroupScheduleContext> ctx, std::vector<bool> reserveResult) {
        for (int i = 0; i < reserveResult.size(); i++) {
            if (!reserveResult[i]) {
                ctx->failedReserve.emplace(ctx->requests[i]->requestid());
            }
            ctx->tryScheduleResults.emplace_back(schedule_decision::ScheduleResult{"agent", 0, ""});
        }
    };

    // group schedule
    {
        auto groupInfo = NewGroupInfo(1);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        // failed success success
        genResult(ctx, {false, true, true});
        auto rollbacked = domainGroupCtrlActor_->RollbackContext(ctx);
        EXPECT_EQ(ctx->lastReservedInd, -1);
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(3)
                .WillRepeatedly(Return(Status::OK()));
        auto future = domainGroupCtrlActor_->RollbackRangeReserve(rollbacked, ctx->tryScheduleResults, ctx);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(ctx->lastReservedInd, -1);
    }

    // strict pack group schedule
    {
        auto groupInfo = NewGroupInfo(1);
        groupInfo->mutable_groupopts()->set_grouppolicy(common::GroupPolicy::StrictPack);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        // failed success success
        genResult(ctx, {true, true, false});
        auto rollbacked = domainGroupCtrlActor_->RollbackContext(ctx);
        EXPECT_EQ(ctx->lastReservedInd, -1);
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(3)
            .WillRepeatedly(Return(Status::OK()));
        auto future = domainGroupCtrlActor_->RollbackRangeReserve(rollbacked, ctx->tryScheduleResults, ctx);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(ctx->lastReservedInd, -1);
    }

    // range schedule
    {
        auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, INSTANCE_NUM, 1, 1);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        // success success false
        genResult(ctx, {true, true, false});
        auto rollbacked = domainGroupCtrlActor_->RollbackContext(ctx);
        EXPECT_EQ(ctx->lastReservedInd, 1);
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(1)
                .WillRepeatedly(Return(AsyncReturn(Status::OK())));
        auto future = domainGroupCtrlActor_->RollbackRangeReserve(rollbacked, ctx->tryScheduleResults, ctx);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(ctx->lastReservedInd, 1);
    }
    {
        auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, INSTANCE_NUM, 1, 2);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        // success success false
        genResult(ctx, {true, true, false});
        auto rollbacked = domainGroupCtrlActor_->RollbackContext(ctx);
        EXPECT_EQ(ctx->lastReservedInd, 1);
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(1)
                .WillRepeatedly(Return(AsyncReturn(Status::OK())));
        auto future = domainGroupCtrlActor_->RollbackRangeReserve(rollbacked, ctx->tryScheduleResults, ctx);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(ctx->lastReservedInd, 1);
    }
    {
        auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, INSTANCE_NUM, 1, 2);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        // success success success
        genResult(ctx, {true, true, true});
        auto rollbacked = domainGroupCtrlActor_->RollbackContext(ctx);
        EXPECT_EQ(ctx->lastReservedInd, 2);
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);
        auto future = domainGroupCtrlActor_->RollbackRangeReserve(rollbacked, ctx->tryScheduleResults, ctx);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(ctx->lastReservedInd, 2);
    }
    {
        auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, 5, 3, 2);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        genResult(ctx, {true, false, true, false, true});
        auto rollbacked = domainGroupCtrlActor_->RollbackContext(ctx);
        EXPECT_EQ(ctx->lastReservedInd, 2);
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(3)
            .WillRepeatedly(Return(Status::OK()));
        auto future = domainGroupCtrlActor_->RollbackRangeReserve(rollbacked, ctx->tryScheduleResults, ctx);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(ctx->lastReservedInd, 2);
    }
    {
        auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, 5, 3, 2);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        genResult(ctx, {true, true, true, false, true});
        auto rollbacked = domainGroupCtrlActor_->RollbackContext(ctx);
        EXPECT_EQ(ctx->lastReservedInd, 2);
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(2)
            .WillRepeatedly(Return(Status::OK()));
        auto future = domainGroupCtrlActor_->RollbackRangeReserve(rollbacked, ctx->tryScheduleResults, ctx);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(ctx->lastReservedInd, 2);
    }
}

TEST_F(DomainGroupCtrlTest, TestReleaseUnusedReserve)
{
    auto genResult = [](std::shared_ptr<GroupScheduleContext> ctx, std::vector<bool> reserveResult, int instanceNum) {
        for (int i = 0; i < instanceNum; i++) {
            if (!reserveResult[i]) {
                ctx->failedReserve.emplace(ctx->requests[i]->requestid());
                ctx->tryScheduleResults.emplace_back(schedule_decision::ScheduleResult{"agent",
                    StatusCode::RESOURCE_NOT_ENOUGH, ""});
                continue;
            }
            ctx->tryScheduleResults.emplace_back(schedule_decision::ScheduleResult{"agent", 0, ""});
        }
    };

    // 1.range schedule -- range scheduled instances(1) < reserved instances(2)
    {
        auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, INSTANCE_NUM, 1, 1);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        // Only one instance is scheduled successfully in the current scheduling round
        genResult(ctx, {true}, 1);
        // Previous scheduling round: [total instances=3][succeeded and reserved=2][rollback=1]
        ctx->lastReservedInd = 1;
        (*ctx->requests[0]->mutable_contexts())[GROUP_SCHEDULE_CONTEXT]
            .mutable_groupschedctx()->set_reserved("test1");
        (*ctx->requests[1]->mutable_contexts())[GROUP_SCHEDULE_CONTEXT]
            .mutable_groupschedctx()->set_reserved("test2");

        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(1)
            .WillOnce(Return(AsyncReturn(Status::OK())));
        domainGroupCtrlActor_->ReleaseUnusedReserve(ctx->tryScheduleResults, ctx);
        EXPECT_EQ(ctx->lastReservedInd, 0);
    }
    // 2.range schedule -- range scheduled instances(3) > reserved instances(2)
    {
        auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, INSTANCE_NUM, 1, 1);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        // three instances are scheduled successfully in the current scheduling round
        genResult(ctx, {true, true, true}, 3);
        // Previous scheduling round: [total instances=3][succeeded and reserved=2][rollback=1]
        ctx->lastReservedInd = 1;
        (*ctx->requests[0]->mutable_contexts())[GROUP_SCHEDULE_CONTEXT]
            .mutable_groupschedctx()->set_reserved("test1");
        (*ctx->requests[1]->mutable_contexts())[GROUP_SCHEDULE_CONTEXT]
            .mutable_groupschedctx()->set_reserved("test2");

        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);
        domainGroupCtrlActor_->ReleaseUnusedReserve(ctx->tryScheduleResults, ctx);
        EXPECT_EQ(ctx->lastReservedInd, 1);
    }
    // 3.range schedule -- range scheduled instances(2) == reserved instances(2)
    {
        auto groupInfo = NewRangeInstanceScheduleGroupInfo(1, INSTANCE_NUM, 1, 1);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);
        // two instances are scheduled successfully in the current scheduling round
        genResult(ctx, {true, true}, 2);
        // Previous scheduling round: [total instances=3][succeeded and reserved=2][rollback=1]
        ctx->lastReservedInd = 1;
        (*ctx->requests[0]->mutable_contexts())[GROUP_SCHEDULE_CONTEXT]
            .mutable_groupschedctx()->set_reserved("test1");
        (*ctx->requests[1]->mutable_contexts())[GROUP_SCHEDULE_CONTEXT]
            .mutable_groupschedctx()->set_reserved("test2");

        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);
        domainGroupCtrlActor_->ReleaseUnusedReserve(ctx->tryScheduleResults, ctx);
        EXPECT_EQ(ctx->lastReservedInd, 1);
    }
}

// test cancelled by job & parent & function
TEST_F(DomainGroupCtrlTest, TryCancelScheduleTest)
{
    // cancel by job
    {
        auto groupInfo = NewGroupInfo(1);
        groupInfo->set_traceid("job-123-X");
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);

        auto cancel = std::make_shared<messages::CancelSchedule>();
        cancel->set_id("job-123");
        cancel->set_reason("job finalized");
        cancel->set_type(messages::CancelType::JOB);
        domainGroupCtrlActor_->TryCancelSchedule(cancel);
        EXPECT_EQ(ctx->cancelPromise.GetFuture().IsOK(), true);
    }
    // cancel by parent
    {
        auto groupInfo = NewGroupInfo(1);
        groupInfo->set_parentid("parent");
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);

        auto cancel = std::make_shared<messages::CancelSchedule>();
        cancel->set_id("parent");
        cancel->set_reason("parent terminated");
        cancel->set_type(messages::CancelType::PARENT);
        domainGroupCtrlActor_->TryCancelSchedule(cancel);
        EXPECT_EQ(ctx->cancelPromise.GetFuture().IsOK(), true);
    }
    // cancel by function
    {
        auto groupInfo = NewGroupInfo(1);
        groupInfo->mutable_requests(0)->mutable_instance()->set_function("function");
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);

        auto cancel = std::make_shared<messages::CancelSchedule>();
        cancel->set_id("function");
        cancel->set_reason("function deleted");
        cancel->set_type(messages::CancelType::FUNCTION);
        domainGroupCtrlActor_->TryCancelSchedule(cancel);
        EXPECT_EQ(ctx->cancelPromise.GetFuture().IsOK(), true);
    }
    // cancel by request
    {
        auto groupInfo = NewGroupInfo(1);
        auto ctx = domainGroupCtrlActor_->NewGroupContext(groupInfo);

        auto cancel = std::make_shared<messages::CancelSchedule>();
        cancel->set_id(groupInfo->requestid());
        cancel->set_reason("user");
        cancel->set_type(messages::CancelType::REQUEST);
        domainGroupCtrlActor_->TryCancelSchedule(cancel);
        EXPECT_EQ(ctx->cancelPromise.GetFuture().IsOK(), true);
    }
}

// SFMD group schedule successful
TEST_F(DomainGroupCtrlTest, SfmdGroupScheduleSuccessful)
{
    std::string selectedAgentId1 = "agent1";
    std::string selectedAgentId2 = "agent2";
    std::string selectedAgentId3 = "agent3";
    std::string selectedNodeId1 = "node1";
    std::string selectedNodeId2 = "node2";

    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId1, 0, "", {}, "NPU/310" });
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId2, 0, "", {}, "NPU/310"  });
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId3, 0, "", {}, "NPU/310"  });

    // single node
    {
        EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_))
            .WillOnce(Return(AsyncReturn(result)));
        auto groupInfo = NewSfmdGroupInfo(100);

        auto response1 = NewScheduleResponse(selectedNodeId1);
        (*response1->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(100, "0.0.0.0"));
        (*response1->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(102, "0.0.0.2"));
        auto response2 = NewScheduleResponse(selectedNodeId1);
        (*response2->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(106, "0.0.0.6"));
        (*response2->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(107, "0.0.0.7"));
        auto response3 = NewScheduleResponse(selectedNodeId1);
        (*response3->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(100, "0.0.0.0"));
        (*response3->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(103, "0.0.0.3"));

        EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve).Times(3)
            .WillOnce(Return(AsyncReturn(response1)))
            .WillOnce(Return(AsyncReturn(response2)))
            .WillOnce(Return(AsyncReturn(response3)));
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);

        std::vector<std::shared_ptr<messages::ScheduleRequest>> scheduleReqs;

        EXPECT_CALL(*mockUnderlayerSchedMgr_, Bind)
            .WillRepeatedly(Invoke([&scheduleReqs](std::string, std::shared_ptr<messages::ScheduleRequest> req) {
                scheduleReqs.push_back(req);
                return Status::OK();
        }));
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnBind).Times(0);

        auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                     domainGroupCtrlActor_->GetAID(), groupInfo);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);

        ASSERT_EQ(scheduleReqs.size(), 3);
        auto scheduleReq = scheduleReqs[0];

        // check functionGroupRunningInfo
        common::FunctionGroupRunningInfo functionGroupRunningInfo;
        if (!google::protobuf::util::JsonStringToMessage(scheduleReq->instance().createoptions().at(
            "FUNCTION_GROUP_RUNNING_INFO"), &functionGroupRunningInfo).ok()) {
            EXPECT_EQ(1, 0);
        }
        ASSERT_EQ(functionGroupRunningInfo.serverlist_size(), 1);
        EXPECT_EQ(functionGroupRunningInfo.worldsize(), 3);
        EXPECT_EQ(functionGroupRunningInfo.devicename(), "NPU/310");
        auto &serverList = functionGroupRunningInfo.serverlist(0);
        EXPECT_EQ(serverList.serverid(), selectedNodeId1);
        EXPECT_EQ(serverList.devices_size(), 5);

        // check rankId + device ip
        // key: device id, value: rank id
        std::map<int64_t, int64_t> expectedDeviceRanks = {
            {100, 0}, {102, 1},  {103, 2}, {106, 3}, {107, 4}
        };
        // key: device id, value: device ip
        std::map<int64_t, std::string> expectedDeviceIps = {
            {100, "0.0.0.0"}, {102, "0.0.0.2"},  {103, "0.0.0.3"}, {106, "0.0.0.6"}, {107, "0.0.0.7"}
        };

        for (const auto& device : serverList.devices()) {
            auto deviceId = device.deviceid();
            auto rankId = device.rankid();
            auto deviceIp = device.deviceip();
            EXPECT_EQ(expectedDeviceRanks[deviceId], rankId);
            EXPECT_EQ(expectedDeviceIps[deviceId], deviceIp);
        }

        // check instance rank id
        // key: instance id, value: instance rank id
        std::map<std::string, int64_t> expectedInsRankIds{};
        expectedInsRankIds[response1->instanceid()] = 0;
        expectedInsRankIds[response2->instanceid()] = 2;
        expectedInsRankIds[response3->instanceid()] = 1;

        for (const auto &req : scheduleReqs) {
            auto &instanceId = req->instance().instanceid();
            if (!google::protobuf::util::JsonStringToMessage(req->instance().createoptions().at(
                "FUNCTION_GROUP_RUNNING_INFO"), &functionGroupRunningInfo).ok()) {
                EXPECT_EQ(1, 0);
            }
            EXPECT_EQ(functionGroupRunningInfo.instancerankid(), expectedInsRankIds[instanceId]);
        }
    }

    // multi node
    {
        EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
        auto groupInfo = NewSfmdGroupInfo(100);

        auto response1 = NewScheduleResponse(selectedNodeId1);
        (*response1->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(100, "0.0.0.0"));
        (*response1->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(101, "0.0.0.1"));
        auto response2 = NewScheduleResponse(selectedNodeId1);
        (*response2->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(106, "0.0.0.6"));
        (*response2->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(107, "0.0.0.7"));
        auto response3 = NewScheduleResponse(selectedNodeId2);
        (*response3->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(100, "0.0.0.0"));
        (*response3->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(101, "0.0.0.1"));
        response1->set_instanceid(groupInfo->requests(0).instance().instanceid());
        response2->set_instanceid(groupInfo->requests(1).instance().instanceid());
        response3->set_instanceid(groupInfo->requests(2).instance().instanceid());

        EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve).Times(3)
            .WillOnce(Return(AsyncReturn(response1)))
            .WillOnce(Return(AsyncReturn(response2)))
            .WillOnce(Return(AsyncReturn(response3)));
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);

        std::vector<std::shared_ptr<messages::ScheduleRequest>> scheduleReqs;

        EXPECT_CALL(*mockUnderlayerSchedMgr_, Bind)
            .WillRepeatedly(Invoke([&scheduleReqs](std::string, std::shared_ptr<messages::ScheduleRequest> req) {
                scheduleReqs.push_back(req);
                return Status::OK();
            }));
        EXPECT_CALL(*mockUnderlayerSchedMgr_, UnBind).Times(0);

        auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                     domainGroupCtrlActor_->GetAID(), groupInfo);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);

        ASSERT_EQ(scheduleReqs.size(), 3);
        auto scheduleReq = scheduleReqs[0];

        // check functionGroupRunningInfo
        common::FunctionGroupRunningInfo functionGroupRunningInfo;
        if (!google::protobuf::util::JsonStringToMessage(scheduleReq->instance().createoptions().at(
            "FUNCTION_GROUP_RUNNING_INFO"), &functionGroupRunningInfo).ok()) {
            EXPECT_EQ(1, 0);
        }
        ASSERT_EQ(functionGroupRunningInfo.serverlist_size(), 2);
        EXPECT_EQ(functionGroupRunningInfo.worldsize(), 3);
        EXPECT_EQ(functionGroupRunningInfo.devicename(), "NPU/310");

        common::ServerInfo serverList1;
        for (int i = 0; i < functionGroupRunningInfo.serverlist_size(); ++i) {
            auto &server = functionGroupRunningInfo.serverlist(i);
            if (server.serverid() == selectedNodeId1) {
                serverList1 = server;
            }
        }
        EXPECT_EQ(serverList1.serverid(), selectedNodeId1);
        EXPECT_EQ(serverList1.devices_size(), 4);
        // key: device id, value: rank id
        std::map<int64_t, int64_t> expectedDeviceRanks = { {100, 2}, {101, 3},  {106, 4}, {107, 5} };
        // key: device id, value: device ip
        std::map<int64_t, std::string> expectedDeviceIps = {
            {100, "0.0.0.0"}, {101, "0.0.0.1"},  {106, "0.0.0.6"}, {107, "0.0.0.7"} };

        for (const auto& device : serverList1.devices()) {
            auto deviceId = device.deviceid();
            auto rankId = device.rankid();
            auto deviceIp = device.deviceip();
            EXPECT_EQ(expectedDeviceRanks[deviceId], rankId);
            EXPECT_EQ(expectedDeviceIps[deviceId], deviceIp);
        }

        common::ServerInfo serverList2;
        for (int i = 0; i < functionGroupRunningInfo.serverlist_size(); ++i) {
            auto &server = functionGroupRunningInfo.serverlist(i);
            if (server.serverid() == selectedNodeId2) {
                serverList2 = server;
            }
        }
        EXPECT_EQ(serverList2.devices_size(), 2);
        EXPECT_EQ(serverList2.serverid(), selectedNodeId2);
        // key: device id, value: rank id
        expectedDeviceRanks = { {100, 0}, {101, 1} };
        // key: device id, value: device ip
        expectedDeviceIps = { {100, "0.0.0.0"}, {101, "0.0.0.1"} };

        for (const auto& device : serverList2.devices()) {
            auto deviceId = device.deviceid();
            auto rankId = device.rankid();
            auto deviceIp = device.deviceip();
            EXPECT_EQ(expectedDeviceRanks[deviceId], rankId);
            EXPECT_EQ(expectedDeviceIps[deviceId], deviceIp);
        }

        // check instance rank id
        // key: instance id, value: instance rank id
        std::map<std::string, std::set<int64_t>> expectedInsRankIds{};
        expectedInsRankIds[response1->instanceid()] = { 1, 0 };
        expectedInsRankIds[response2->instanceid()] = { 2, 1 };
        expectedInsRankIds[response3->instanceid()] = { 0, 2 };

        for (const auto &req : scheduleReqs) {
        auto &instanceId = req->instance().instanceid();
        if (!google::protobuf::util::JsonStringToMessage(req->instance().createoptions().at(
                "FUNCTION_GROUP_RUNNING_INFO"), &functionGroupRunningInfo).ok()) {
                EXPECT_EQ(1, 0);
            }
            EXPECT_EQ(expectedInsRankIds[instanceId].find(functionGroupRunningInfo.instancerankid()) !=
                      expectedInsRankIds[instanceId].end(), true);
        }
    }
}

TEST_F(DomainGroupCtrlTest, HeteroGroupSchedulerWithResourceGroup)
{
    std::string selectedAgentId1 = "agent1";
    std::string selectedAgentId2 = "agent2";
    std::string selectedAgentId3 = "agent3";
    std::string selectedNodeId1 = "node1";
    std::string selectedNodeId2 = "node2";

    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId1, 0, "", {}, "NPU/310" });
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId2, 0, "", {}, "NPU/310"  });
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId3, 0, "", {}, "NPU/310"  });


    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_))
        .WillOnce(Return(AsyncReturn(result)));
    auto groupInfo = NewSfmdGroupInfo(100);
    for (auto &req : *groupInfo->mutable_requests()) {
        req.mutable_instance()->mutable_scheduleoption()->set_target(resources::CreateTarget::RESOURCE_GROUP);
    }

    auto response1 = NewScheduleResponse(selectedNodeId1);
    (*response1->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(100, "0.0.0.0"));
    (*response1->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(102, "0.0.0.2"));
    auto response2 = NewScheduleResponse(selectedNodeId1);
    (*response2->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(106, "0.0.0.6"));
    (*response2->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(107, "0.0.0.7"));
    auto response3 = NewScheduleResponse(selectedNodeId1);
    (*response3->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(100, "0.0.0.0"));
    (*response3->mutable_scheduleresult()->add_devices()) = std::move(NewHeteroDeviceInfo(103, "0.0.0.3"));

    EXPECT_CALL(*mockUnderlayerSchedMgr_, Reserve).Times(3)
        .WillOnce(Return(AsyncReturn(response1)))
        .WillOnce(Return(AsyncReturn(response2)))
        .WillOnce(Return(AsyncReturn(response3)));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnReserve).Times(0);

    std::vector<std::shared_ptr<messages::ScheduleRequest>> scheduleReqs;

    EXPECT_CALL(*mockUnderlayerSchedMgr_, Bind)
        .WillRepeatedly(Invoke([&scheduleReqs](std::string, std::shared_ptr<messages::ScheduleRequest> req) {
            scheduleReqs.push_back(req);
            return Status::OK();
        }));
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UnBind).Times(0);

    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);

    ASSERT_EQ(scheduleReqs.size(), 3);
    auto scheduleReq = scheduleReqs[0];

    // check functionGroupRunningInfo
    common::FunctionGroupRunningInfo functionGroupRunningInfo;
    if (scheduleReq->instance().createoptions().contains("FUNCTION_GROUP_RUNNING_INFO")) {
        EXPECT_EQ(1, 0) << "FUNCTION_GROUP_RUNNING_INFO found in createoptions";
    }
}

TEST_F(DomainGroupCtrlTest, ScheduleTimeoutCancel)
{
    auto groupInfo = NewGroupInfo(1, 10);
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_))
        .WillRepeatedly(Return(litebus::Future<schedule_decision::GroupScheduleResult>()));
    auto future = litebus::Async(localSchedSrvStub_->GetAID(), &LocalSchedSrvStub::ForwardGroupSchedule,
                                 domainGroupCtrlActor_->GetAID(), groupInfo);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::ERR_SCHEDULE_CANCELED);
}

}  // namespace functionsystem::test