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

#include "common/schedule_decision/scheduler/priority_scheduler.h"
#include "common/resource_view/view_utils.h"
#include "common/schedule_decision/queue/queue_item.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"
#include "mocks/mock_schedule_performer.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace schedule_decision;

class PrioritySchedulerTest : public Test {
public:
    void SetUp() override
    {
        mockInstancePerformer_ = std::make_shared<MockInstanceSchedulePerformer>();
        mockGroupPerformer_ = std::make_shared<MockGroupSchedulePerformer>();
        mockAggregatedSchedulePerformer_ = std::make_shared<MockAggregatedSchedulePerformer>();
        recorder_ = schedule_decision::ScheduleRecorder::CreateScheduleRecorder();
    }

    void TearDown() override
    {
        mockInstancePerformer_ = nullptr;
        mockGroupPerformer_ = nullptr;
        mockAggregatedSchedulePerformer_ = nullptr;
    }

    static std::shared_ptr<InstanceItem> CreateAggregatedInstanceItem(const std::string &reqId, int priority,double cpu, double memory) {
        auto ins = InstanceItem::CreateInstanceItem(reqId, priority);
        auto instanceInfo1 = view_utils::GetInstanceWithResourceAndPriority(priority, cpu, memory);
        *ins->scheduleReq->mutable_instance() = instanceInfo1;
        return ins;
    }

protected:
    std::shared_ptr<MockInstanceSchedulePerformer> mockInstancePerformer_;
    std::shared_ptr<MockGroupSchedulePerformer> mockGroupPerformer_;
    std::shared_ptr<MockAggregatedSchedulePerformer> mockAggregatedSchedulePerformer_;
    std::shared_ptr<ScheduleRecorder> recorder_;
};

void SetAffinity(const std::shared_ptr<InstanceItem> &instance,
                 affinity::Selector selector = Selector(true, { { Exist("key1") } }))
{
    instance->scheduleReq->mutable_instance()->mutable_scheduleoption()
        ->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(selector);
}

void SetAffinity(const std::shared_ptr<GroupItem> &group,
                 affinity::Selector selector = Selector(true, { { Exist("key1") } }))
{
    for (auto &instanceItem : group->groupReqs) {
        auto instance = std::dynamic_pointer_cast<InstanceItem>(instanceItem);
        SetAffinity(instance, selector);
    }
}

//  FIFO and Fairness policy exhibit consistent behavior
TEST_F(PrioritySchedulerTest, ConsumeCompleteTest)
{
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FAIRNESS);
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    auto ins1 = InstanceItem::CreateInstanceItem("ins1");
    auto ins2 = InstanceItem::CreateInstanceItem("ins2");
    auto group1 = GroupItem::CreateGroupItem("group1");
    auto group2 = GroupItem::CreateGroupItem("group2");
    scheduler->Enqueue(ins1);
    scheduler->Enqueue(ins2);

    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    EXPECT_CALL(*mockInstancePerformer_, DoSchedule).WillOnce(Return(ScheduleResult{ "", 0, "" }))
        .WillOnce(Return(ScheduleResult{ "", StatusCode::INVALID_RESOURCE_PARAMETER, "" }));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule).WillOnce(Return(GroupScheduleResult{ 0, "", {} }))
        .WillOnce(Return(GroupScheduleResult{ StatusCode::INVALID_RESOURCE_PARAMETER, "", {} }));
    EXPECT_CALL(*mockGroupPerformer_, RollBack).WillOnce(Return(Status::OK()));

    scheduler->ConsumeRunningQueue();
    EXPECT_EQ(ins1->schedulePromise->GetFuture().Get().code, 0);
    EXPECT_EQ(ins2->schedulePromise->GetFuture().Get().code, StatusCode::INVALID_RESOURCE_PARAMETER);

    scheduler->Enqueue(group1);
    scheduler->Enqueue(group2);
    scheduler->ConsumeRunningQueue();
    EXPECT_EQ(group1->groupPromise->GetFuture().Get().code, 0);
    EXPECT_EQ(group2->groupPromise->GetFuture().Get().code, StatusCode::INVALID_RESOURCE_PARAMETER);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());
}

TEST_F(PrioritySchedulerTest, AggregatedConsumeCompleteTest)
{
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FAIRNESS, "relaxed");
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    auto inst1 = CreateAggregatedInstanceItem("inst1",3,10,20);
    auto inst2 = CreateAggregatedInstanceItem("inst2",3,10,20);
    auto group1 = GroupItem::CreateGroupItem("group1");
    auto group2 = GroupItem::CreateGroupItem("group2");
    scheduler->Enqueue(inst1);
    scheduler->Enqueue(group1);
    scheduler->Enqueue(inst2);
    scheduler->Enqueue(group2);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_CALL(*mockAggregatedSchedulePerformer_, DoSchedule)
    .WillOnce(Return(
        std::make_shared<std::deque<ScheduleResult>>
        (std::initializer_list<ScheduleResult>{ScheduleResult{"", 0, ""}})))
    .WillOnce(Return(
    std::make_shared<std::deque<ScheduleResult>>
    (std::initializer_list<ScheduleResult>{ScheduleResult{ "", StatusCode::INVALID_RESOURCE_PARAMETER, "" }})));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule).WillOnce(Return(GroupScheduleResult{ 0, "", {} }))
        .WillOnce(Return(GroupScheduleResult{ StatusCode::INVALID_RESOURCE_PARAMETER, "", {} }));
    EXPECT_CALL(*mockGroupPerformer_, RollBack).WillOnce(Return(Status::OK()));
    scheduler->ConsumeRunningQueue();
    EXPECT_EQ(inst1->schedulePromise->GetFuture().Get().code, 0);
    EXPECT_EQ(inst2->schedulePromise->GetFuture().Get().code, StatusCode::INVALID_RESOURCE_PARAMETER);
    EXPECT_EQ(group1->groupPromise->GetFuture().Get().code, 0);
    EXPECT_EQ(group2->groupPromise->GetFuture().Get().code, StatusCode::INVALID_RESOURCE_PARAMETER);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());
}


//  FIFO and Fairness policy exhibit consistent behavior
TEST_F(PrioritySchedulerTest, ConsumeCancelTest)
{
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FAIRNESS);
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);
    // ins1 cancel before schedule  ins2 cancel on schedule
    auto ins1 = InstanceItem::CreateInstanceItem("ins1");
    ins1->cancelTag.SetValue("cancel");
    scheduler->Enqueue(ins1);
    auto ins2 = InstanceItem::CreateInstanceItem("ins2");
    scheduler->Enqueue(ins2);
    auto group1 = GroupItem::CreateGroupItem("group1");
    group1->cancelTag.SetValue("cancel");
    scheduler->Enqueue(group1);
    auto group2 = GroupItem::CreateGroupItem("group2");
    scheduler->Enqueue(group2);

    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Invoke([&ins2](const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                 const resource_view::ResourceViewInfo &resource,
                                 const std::shared_ptr<schedule_decision::InstanceItem> &instanceItem) {
            ins2->cancelTag.SetValue("cancel");
            return ScheduleResult{ "", 0, "" };
        }));
    EXPECT_CALL(*mockInstancePerformer_, RollBack).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule)
        .WillOnce(
            Invoke([&group2](const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                             const resource_view::ResourceViewInfo &resource, const std::shared_ptr<GroupItem> &_1) {
                group2->cancelTag.SetValue("cancel");
                return GroupScheduleResult{ 0, "", {} };
            }));
    EXPECT_CALL(*mockGroupPerformer_, RollBack).WillOnce(Return(Status::OK()));

    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsOK());
    EXPECT_TRUE(group1->groupPromise->GetFuture().IsOK());
}

//  FIFO and Fairness policy exhibit consistent behavior
TEST_F(PrioritySchedulerTest, AggregatedConsumeCancelTest)
{
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FAIRNESS, "relaxed");
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);
    // ins1 cancel before schedule  ins2 cancel on schedule
    auto ins1 = CreateAggregatedInstanceItem("ins1",3,10,20);
    ins1->cancelTag.SetValue("cancel");
    auto ins2 = CreateAggregatedInstanceItem("ins2",4,10,20);
    scheduler->Enqueue(ins1);
    scheduler->Enqueue(ins2);
    auto group1 = GroupItem::CreateGroupItem("group1");
    group1->cancelTag.SetValue("cancel");
    scheduler->Enqueue(group1);
    auto group2 = GroupItem::CreateGroupItem("group2");
    scheduler->Enqueue(group2);
    std::shared_ptr<InstanceItem> captureIns = nullptr;
    EXPECT_CALL(*mockAggregatedSchedulePerformer_, DoSchedule)
                .WillOnce(Invoke([&captureIns](
                const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                const resource_view::ResourceViewInfo &resourceInfo,
                const std::shared_ptr<AggregatedItem> &aggregatedItem) {
                        captureIns = aggregatedItem->reqQueue->front();
                        captureIns->cancelTag.SetValue("cancel");
                        return std::make_shared<std::deque<ScheduleResult> >
                        (std::initializer_list<ScheduleResult>{
                            ScheduleResult{"", 0, ""}
                        });
                    }));
    EXPECT_CALL(*mockInstancePerformer_, RollBack).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule)
            .WillOnce(
                Invoke([&group2](const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                 const resource_view::ResourceViewInfo &resource, const std::shared_ptr<GroupItem> &_1) {
                    group2->cancelTag.SetValue("cancel");
                    return GroupScheduleResult{ 0, "", {} };
                }));
    EXPECT_CALL(*mockGroupPerformer_, RollBack).WillOnce(Return(Status::OK()));

    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(captureIns->schedulePromise->GetFuture().IsOK());
    EXPECT_TRUE(group2->groupPromise->GetFuture().IsOK());
}

//  FIFO and Fairness policy exhibit consistent behavior
TEST_F(PrioritySchedulerTest, ConsumeOnResourceUpdateTest)
{
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FAIRNESS);
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);
    // ins1 cancel before schedule  ins2 cancel on schedule
    auto ins1 = InstanceItem::CreateInstanceItem("ins1");
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    SetAffinity(ins1, Selector(true, { { Exist("ins1") } }));
    auto ins2 = InstanceItem::CreateInstanceItem("ins2");
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    SetAffinity(ins2, Selector(true, { { Exist("ins2") } }));
    auto group1 = GroupItem::CreateGroupItem("group1");
    SetAffinity(group1, Selector(true, { { Exist("group1") } }));

    scheduler->Enqueue(ins1);
    scheduler->Enqueue(ins2);
    scheduler->Enqueue(group1);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .Times(2)
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }))
        .WillOnce(Return(ScheduleResult{ "", 0, "" }));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule)
        .WillOnce(Return(GroupScheduleResult{ StatusCode::AFFINITY_SCHEDULE_FAILED, "", {} }));

    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(ins2->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(group1->groupPromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->runningQueue_->CheckIsQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    auto ins3 = InstanceItem::CreateInstanceItem("ins3");
    scheduler->Enqueue(ins3);

    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .Times(2)
        .WillRepeatedly(Return(ScheduleResult{ "", 0, "" }));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule)
        .WillOnce(Return(GroupScheduleResult{ 0, "", {} }));

    scheduler->ActivatePendingRequests();
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(group1->groupPromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());
}

//  FIFO and Fairness policy exhibit consistent behavior
TEST_F(PrioritySchedulerTest, AggregatedConsumeOnResourceUpdateTest)
{
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FAIRNESS, "relaxed");
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);
    auto ins1 = CreateAggregatedInstanceItem("ins1",3,10,20);
    // set_scheduletimeoutms is used to enter the suspended queue
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    SetAffinity(ins1, Selector(true, { { Exist("ins1") } }));
    auto ins2 = CreateAggregatedInstanceItem("ins2",3,10,20);
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    SetAffinity(ins2, Selector(true, { { Exist("ins2") } }));
    auto group1 = GroupItem::CreateGroupItem("group1");
    SetAffinity(group1, Selector(true, { { Exist("group1") } }));
    scheduler->Enqueue(ins1);
    scheduler->Enqueue(ins2);
    scheduler->Enqueue(group1);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    EXPECT_CALL(*mockAggregatedSchedulePerformer_, DoSchedule)
    .WillOnce(Return(
        std::make_shared<std::deque<ScheduleResult>>
        (std::initializer_list<ScheduleResult>{ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" },
            ScheduleResult{"", 0, ""}})));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule)
        .WillOnce(Return(GroupScheduleResult{ StatusCode::AFFINITY_SCHEDULE_FAILED, "", {} }));

    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(ins2->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(group1->groupPromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->runningQueue_->CheckIsQueueEmpty());
    auto aggregatedQueue = std::dynamic_pointer_cast<AggregatedQueue>(scheduler->pendingQueue_);
    EXPECT_TRUE(aggregatedQueue->queueSize_ == 2);
    auto ins3 = CreateAggregatedInstanceItem("ins3",3,10,20);
    scheduler->Enqueue(ins3);
    EXPECT_CALL(*mockAggregatedSchedulePerformer_, DoSchedule)
    .WillOnce(Return(
        std::make_shared<std::deque<ScheduleResult>>
        (std::initializer_list<ScheduleResult>{ScheduleResult{ "", 0, "" },
            ScheduleResult{"", 0, ""}})));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule)
            .WillOnce(Return(GroupScheduleResult{ 0, "", {} }));
    scheduler->ActivatePendingRequests();
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(ins3->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(group1->groupPromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());
}

/*
 * Test for handling similar pending requests in the priority scheduler(fairness)(All instances have the same priority)
 * 1. Create and enqueue ins1 with affinity1
 * 2. Create and enqueue ins2 with affinity2
 * 3. Simulate scheduling (failure)
 * 4. Create and enqueue ins3 with the same affinity as ins1 --> enqueue the pending queue
 * 5. Create and enqueue ins4 with the same affinity as ins2 --> enqueue the pending queue
 * 6. Create ins5 with different affinity                    --> enqueue the running queue
 */
TEST_F(PrioritySchedulerTest, FairnessWithSamePriorityTest) {
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FAIRNESS);
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);

    // 1. Create and enqueue ins1 with affinity
    auto ins1 = InstanceItem::CreateInstanceItem("ins1");
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    auto affinity1 = Selector(true, { { Exist("key1") }, { In("key1", { "value1" }) } });
    SetAffinity(ins1, affinity1);

    scheduler->Enqueue(ins1);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 2. Create and enqueue ins2 with affinity2
    auto ins2 = InstanceItem::CreateInstanceItem("ins2");
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    auto affinity2 = Selector(true, { { NotExist("key2") } });
    SetAffinity(ins2, affinity2);
    scheduler->Enqueue(ins2);
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 3. Simulate scheduling (failure)
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }))
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(ins2->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    // 4. Create and enqueue ins3 with the same affinity as ins1 --> enqueue the pending queue
    auto ins3 = InstanceItem::CreateInstanceItem("ins3");
    SetAffinity(ins3, affinity1);
    scheduler->Enqueue(ins3);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 3);

    // 5. Create and enqueue ins4 with the same affinity as ins2 --> enqueue the pending queue
    auto ins4 = InstanceItem::CreateInstanceItem("ins4");
    SetAffinity(ins4, affinity2);
    scheduler->Enqueue(ins4);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 4);

    // 6.Create ins5 with different affinity --> enqueue the running queue
    auto ins5 = InstanceItem::CreateInstanceItem("ins5");
    auto affinity3 = Selector(true, { { In("key3", { "value3" }) }});
    SetAffinity(ins5, affinity3);
    scheduler->Enqueue(ins5);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 4);

    // 7. verify pending affinity
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", 0, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins5->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(ins5->scheduleReq->instance().scheduleoption().affinity().inner().has_pending());
    auto &pendingAffinity = ins5->scheduleReq->instance().scheduleoption().affinity().inner().pending();
    EXPECT_TRUE(pendingAffinity.resources().size() == 2);
    auto &condition1 = pendingAffinity.resources()[0].requiredaffinity().condition();
    EXPECT_TRUE(condition1.subconditions()[0].expressions()[0].op().has_exists());
    EXPECT_TRUE(condition1.subconditions()[1].expressions()[0].op().has_in());
    auto &condition2 = pendingAffinity.resources()[1].requiredaffinity().condition();
    EXPECT_TRUE(condition2.subconditions()[0].expressions()[0].op().has_notexist());
}

/*
 * Test for handling similar pending requests in the priority scheduler(fairness)(All instances have different priority)
 * 1. Create and enqueue ins1 with priority 10
 * 2. Create and enqueue ins2 with priority 8
 * 3. Simulate scheduling (failure)
 * 4. Create and enqueue ins3 with priority 9 and the same affinity as ins1    --> enqueue the pending queue
 * 5. Create and enqueue ins4 with priority 10 and the same affinity as ins1   --> enqueue the pending queue
 * 6. Create and enqueue ins5 with priority 11 and the same affinity as ins1   --> enqueue the running queue
 */
TEST_F(PrioritySchedulerTest, FairnessWithDifferentPriorityTest) {
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 20, PriorityPolicyType::FAIRNESS);
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);

    // 1. Create and enqueue ins1 with priority 10
    auto ins1 = InstanceItem::CreateInstanceItem("ins1");
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(10);
    auto affinity1 = Selector(true, { { Exist("key1") } });
    SetAffinity(ins1, affinity1);

    scheduler->Enqueue(ins1);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 2. Create and enqueue ins2 with priority 8
    auto ins2 = InstanceItem::CreateInstanceItem("ins2");
    auto affinity2 = Selector(true, { { In("key2", { "value1" }) } });
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(8);
    SetAffinity(ins2, affinity2);
    scheduler->Enqueue(ins2);
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 3. Simulate scheduling (failure)
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }))
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(ins2->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    EXPECT_TRUE(ins2->scheduleReq->instance().scheduleoption().affinity().inner().has_pending());
    auto &pendingAffinity2 = ins2->scheduleReq->instance().scheduleoption().affinity().inner().pending();
    EXPECT_TRUE(pendingAffinity2.resources().size() == 1);
    auto &condition = pendingAffinity2.resources()[0].requiredaffinity().condition();
    EXPECT_TRUE(condition.subconditions_size() == 1);
    EXPECT_TRUE(condition.subconditions()[0].expressions()[0].op().has_exists());

    // 4. Create and enqueue ins3 with priority 9 and the same affinity as ins1    --> enqueue the pending queue
    auto ins3 = InstanceItem::CreateInstanceItem("ins3");
    SetAffinity(ins3, affinity1);
    ins3->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(9);
    scheduler->Enqueue(ins3);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 3);

    // 5. Create and enqueue ins4 with priority 10 and the same affinity as ins1   --> enqueue the pending queue
    auto ins4 = InstanceItem::CreateInstanceItem("ins4");
    SetAffinity(ins4, affinity1);
    ins4->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(10);
    scheduler->Enqueue(ins4);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 4);

    // 6.Create and enqueue ins5 with priority 11 and the same affinity as ins1   --> enqueue the running queue
    auto ins5 = InstanceItem::CreateInstanceItem("ins5");
    SetAffinity(ins5, affinity1);
    ins5->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(11);
    scheduler->Enqueue(ins5);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 4);

    // 7. verify pending affinity
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", 0, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins5->schedulePromise->GetFuture().Get().code == 0);
    auto &pendingAffinity5 = ins5->scheduleReq->instance().scheduleoption().affinity().inner().pending();
    EXPECT_TRUE(pendingAffinity5.resources().empty());
}

/*
 * Test for handling similar pending requests priority scheduler(fifo)
 * 1. Create and enqueue ins1 with affinity
 * 2. Create and enqueue ins2 with empty affinity
 * 3. Simulate scheduling (failure)
 * 4. Create and enqueue ins3 with the same affinity as ins1 --> enqueue the running queue
 * 5. Create and enqueue ins4 with empty affinity            --> enqueue the running queue
 * 6. Create ins5 with different affinity                    --> enqueue the running queue
 */
TEST_F(PrioritySchedulerTest, HasSimilarPendingRequestFifoTest) {
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FIFO);
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);

    // 1. Create and enqueue ins1 with affinity
    auto ins1 = InstanceItem::CreateInstanceItem("ins1");
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    auto affinity1 = Selector(true, { { Exist("key1") }, { In("key2", { "value1" }) } });

    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()
    ->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(affinity1);
    scheduler->Enqueue(ins1);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 2. Create and enqueue ins2 with empty affinity
    auto ins2 = InstanceItem::CreateInstanceItem("ins2");
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    scheduler->Enqueue(ins2);
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 3. Simulate scheduling (failure)
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }))
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(ins2->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    // 4. Create and enqueue ins3 with the same affinity as ins1 --> enqueue the running queue
    auto ins3 = InstanceItem::CreateInstanceItem("ins3");
    ins3->scheduleReq->mutable_instance()->mutable_scheduleoption()
        ->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(affinity1);
    scheduler->Enqueue(ins3);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->runningQueue_->Size() == 1);
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    // 5. Create and enqueue ins4 with empty affinity --> enqueue the running queue
    auto ins4 = InstanceItem::CreateInstanceItem("ins4");
    scheduler->Enqueue(ins4);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->runningQueue_->Size() == 2);
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    // 6.Create ins5 with different affinity --> enqueue the running queue
    auto ins5 = InstanceItem::CreateInstanceItem("ins5");
    auto affinity2 = Selector(true, { { In("key2", { "value1" }) }, { Exist("key1") } });
    ins5->scheduleReq->mutable_instance()->mutable_scheduleoption()
        ->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(affinity2);
    scheduler->Enqueue(ins5);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->runningQueue_->Size() == 3);
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    // 7. verify pending affinity
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", 0, "" }))
        .WillOnce(Return(ScheduleResult{ "", 0, "" }))
        .WillOnce(Return(ScheduleResult{ "", 0, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins3 ->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(ins4->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(ins5->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_FALSE(ins3->scheduleReq->instance().scheduleoption().affinity().inner().has_pending());
    EXPECT_FALSE(ins4->scheduleReq->instance().scheduleoption().affinity().inner().has_pending());
    EXPECT_FALSE(ins5->scheduleReq->instance().scheduleoption().affinity().inner().has_pending());
}

/*
 * Test for handling similar pending requests in the priority scheduler
 * (FIFO and Fairness policy exhibit consistent behavior)
 * 1. Create and enqueue ins1 with affinity
 * 2. Create and enqueue ins2 with the same affinity as ins1
 * 3. Simulate scheduling (failure)
 * 4. Resource update: Resources are sufficient for scheduling one instance -->
 *    ins1 was scheduled successfully, but the scheduling of isn2 failed
 */
TEST_F(PrioritySchedulerTest, RequestOrderTest) {
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 10, PriorityPolicyType::FAIRNESS);
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);

    // 1. Create and enqueue ins1 with affinity
    auto ins1 = InstanceItem::CreateInstanceItem("ins1");
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    auto affinity1 = Selector(true, { { Exist("key1") }, { In("key2", { "value1" }) } });

    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()
        ->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(affinity1);
    scheduler->Enqueue(ins1);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 2. Create and enqueue ins2 with the same affinity as ins1
    auto ins2 = InstanceItem::CreateInstanceItem("ins2");
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);

    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()
        ->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(affinity1);
    scheduler->Enqueue(ins2);
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 3. Simulate scheduling (failure)
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(ins2->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    // 4. Resource update: Resources are sufficient for scheduling one instance
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", 0, "" }))
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }));
    scheduler->ActivatePendingRequests();
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().Get().code == 0);
    EXPECT_TRUE(ins2->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 1);
}

/*
 * Test for handling empty affinity requests in the priority scheduler(fairness)(All instances have different priority)
 * 1. Create and enqueue ins1 with priority 10 and empty affinity
 * 2. Simulate scheduling (failure)
 * 3. Create and enqueue ins2 with priority 10 and empty affinity   --> enqueue the pending queue
 * 4. Create and enqueue ins3 with priority 11 and empty affinity   --> enqueue the running queue
 * 5. Create and enqueue ins4 with priority 9 and affinity1         --> enqueue the pending queue
 * 6. verify pending affinity of ins4
 * 7. Resource update triggers ins1,2,4 into running queue.
 *    Failed scheduling of ins1 forces ins2/4 into pending queue because of empty affinity.
 */
TEST_F(PrioritySchedulerTest, FairnessWithEmptyAffinityTest) {
    auto scheduler = std::make_shared<PriorityScheduler>(recorder_, 20, PriorityPolicyType::FAIRNESS);
    scheduler->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);

    // 1. Create and enqueue ins1 with priority 10
    auto ins1 = InstanceItem::CreateInstanceItem("ins1");
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    ins1->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(10);

    scheduler->Enqueue(ins1);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->CheckIsPendingQueueEmpty());

    // 2. Simulate scheduling (failure)
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 1);

    // 3. Create and enqueue ins2 with priority 10 and empty affinity --> enqueue the pending queue
    auto ins2 = InstanceItem::CreateInstanceItem("ins2");
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    ins2->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(10);
    scheduler->Enqueue(ins2);
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    // 4. Create and enqueue ins3 with priority 11 and empty affinity --> enqueue the running queue
    auto ins3 = InstanceItem::CreateInstanceItem("ins3");
    ins3->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(11);
    scheduler->Enqueue(ins3);
    EXPECT_FALSE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 2);

    // 5. Create and enqueue ins4 with priority 9 and affinity1 --> enqueue the pending queue
    auto ins4 = InstanceItem::CreateInstanceItem("ins4");
    SetAffinity(ins4);
    ins4->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(9);
    scheduler->Enqueue(ins4);
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 3);

    // 6. verify pending affinity
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", 0, "" }));
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins3->schedulePromise->GetFuture().Get().code == 0);
    auto &pendingAffinity = ins3->scheduleReq->instance().scheduleoption().affinity().inner().pending();
    EXPECT_TRUE(pendingAffinity.resources().empty());

    // 7.Resource update triggers ins1,2,4 into running queue
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" }));
    scheduler->ActivatePendingRequests();
    scheduler->ConsumeRunningQueue();
    EXPECT_TRUE(ins1->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(ins2->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(ins4->schedulePromise->GetFuture().IsInit());
    EXPECT_TRUE(scheduler->CheckIsRunningQueueEmpty());
    EXPECT_FALSE(scheduler->CheckIsPendingQueueEmpty());
    EXPECT_TRUE(scheduler->pendingQueue_->Size() == 3);
}

}  // namespace functionsystem::test