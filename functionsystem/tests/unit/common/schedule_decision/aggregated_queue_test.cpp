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
#include <gtest/gtest.h>

#include "common/schedule_decision/queue/aggregated_queue.h"

#include "common/schedule_decision/queue/schedule_queue.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/view_utils.h"

namespace functionsystem::test {
using namespace schedule_decision;
class AggregatedQueueTest : public ::testing::Test {
public:
    static std::shared_ptr<InstanceItem> CreateInstanceItem(const std::string &reqId, int priority,double cpu, double memory) {
        auto ins = InstanceItem::CreateInstanceItem(reqId, priority);
        auto instanceInfo1 = view_utils::GetInstanceWithResourceAndPriority(priority, cpu, memory);
        *ins->scheduleReq->mutable_instance() = instanceInfo1;
        return ins;
    }
};

TEST_F(AggregatedQueueTest, InvalidEnqueueTest)
{
    auto priorityQueue = std::make_shared<AggregatedQueue>(3,"strictly");
    auto req = std::make_shared<messages::ScheduleRequest>();

    auto res0 = priorityQueue->Enqueue(nullptr).Get();
    EXPECT_EQ(res0.StatusCode(), StatusCode::FAILED);
    EXPECT_EQ(res0.GetMessage(), "[queueItem is null]");
    EXPECT_EQ(priorityQueue->queueSize_, 0);


    auto ins1 = std::make_shared<InstanceItem>(req, std::make_shared<litebus::Promise<ScheduleResult>>(), litebus::Future<std::string>());
    auto res1 = priorityQueue->Enqueue(ins1).Get();
    EXPECT_EQ(res1.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(res1.GetMessage(), "[get instance requestId failed]");
    EXPECT_EQ(priorityQueue->queueSize_, 0);

    auto ins2 = InstanceItem::CreateInstanceItem("ins2",4);
    auto res2 = priorityQueue->Enqueue(ins2).Get();
    EXPECT_EQ(res2.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(res2.GetMessage(), "[instance priority is greater than maxPriority]");
    EXPECT_EQ(priorityQueue->queueSize_, 0);

    auto ins3 = CreateInstanceItem("ins3",1,10,10);
    auto res3 = priorityQueue->Enqueue(ins3).Get();
    EXPECT_EQ(res3.StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(priorityQueue->queueSize_, 1);

}

TEST_F(AggregatedQueueTest, StrictEnqueueTest) {
    auto priorityQueue = std::make_shared<AggregatedQueue>(3, "strictly");
    auto priorityQueue2 = std::make_shared<AggregatedQueue>(3, "strictly");

    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    priorityQueue->Enqueue(ins1);
    priorityQueue->Enqueue(ins2);
    priorityQueue->Enqueue(ins3);
    EXPECT_EQ(priorityQueue->aggregatedReqs[1].size(), 3);

    priorityQueue2->Enqueue(ins1);
    priorityQueue2->Enqueue(ins3);
    priorityQueue2->Enqueue(ins2);
    EXPECT_EQ(priorityQueue2->aggregatedReqs[1].size(), 2);

}

TEST_F(AggregatedQueueTest, RelaxEnqueueTest) {
    auto priorityQueue = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto priorityQueue2 = std::make_shared<AggregatedQueue>(3, "relaxed");

    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    priorityQueue->Enqueue(ins1);
    priorityQueue->Enqueue(ins2);
    priorityQueue->Enqueue(ins3);
    EXPECT_EQ(priorityQueue->aggregatedReqs[1].size(), 2);

    priorityQueue2->Enqueue(ins1);
    priorityQueue2->Enqueue(ins3);
    priorityQueue2->Enqueue(ins2);
    EXPECT_EQ(priorityQueue2->aggregatedReqs[1].size(), 2);
}



TEST_F(AggregatedQueueTest, FrontAndDequeueTest)
{
    auto priorityQueue = std::make_shared<AggregatedQueue>(3, "relaxed");

    auto res = priorityQueue->Dequeue().Get();
    EXPECT_EQ(res.StatusCode(), StatusCode::FAILED);
    EXPECT_EQ(res.GetMessage(), "[queue is empty]");

    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    priorityQueue->Enqueue(ins1);
    priorityQueue->Enqueue(ins2);
    priorityQueue->Enqueue(ins3);
    YRLOG_DEBUG("queue size:{}",priorityQueue->queueSize_);
    EXPECT_EQ(priorityQueue->Front()->GetPriority(), 1);
    EXPECT_EQ(priorityQueue->Front()->GetRequestId(), "ins1");
    auto queueItem = priorityQueue->Front();
    auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(queueItem);
    aggregatedItem->reqQueue->pop_front();
    auto result = priorityQueue->Dequeue().Get();
    EXPECT_EQ(result.StatusCode(), StatusCode::FAILED);
    EXPECT_EQ(result.GetMessage(), "[aggregateItem.reqQueue is not empty]");
    EXPECT_EQ(priorityQueue->queueSize_, 2);

    EXPECT_EQ(priorityQueue->Front()->GetPriority(), 1);
    EXPECT_EQ(priorityQueue->Front()->GetRequestId(), "ins3");
    queueItem = priorityQueue->Front();
    aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(queueItem);
    aggregatedItem->reqQueue->pop_front();
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(priorityQueue->queueSize_, 1);

    EXPECT_EQ(priorityQueue->Front()->GetPriority(), 1);
    EXPECT_EQ(priorityQueue->Front()->GetRequestId(), "ins2");
    queueItem = priorityQueue->Front();
    aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(queueItem);
    aggregatedItem->reqQueue->pop_front();
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(priorityQueue->queueSize_, 0);

    EXPECT_EQ(priorityQueue->Front(), nullptr);
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::FAILED);

}

TEST_F(AggregatedQueueTest, QueueSwapTest)
{
    auto runningQueue_ = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto pendingQueue_ = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    runningQueue_->Enqueue(ins1);
    runningQueue_->Enqueue(ins2);
    runningQueue_->Enqueue(ins3);

    auto ins4 = CreateInstanceItem("ins4", 1, 10, 10);
    pendingQueue_->Enqueue(ins4);
    runningQueue_->Swap(pendingQueue_);


    auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(pendingQueue_->Front());
    EXPECT_EQ(pendingQueue_->Front()->GetRequestId(), "ins1");
    aggregatedItem->reqQueue->pop_front();
    EXPECT_EQ(pendingQueue_->Front()->GetRequestId(), "ins3");
    aggregatedItem->reqQueue->pop_front();
    pendingQueue_->Dequeue();
    EXPECT_EQ(pendingQueue_->Front()->GetRequestId(), "ins2");
    pendingQueue_->Dequeue();
    EXPECT_EQ(runningQueue_->Front()->GetRequestId(), "ins4");
}

TEST_F(AggregatedQueueTest, QueueExtendTest)
{
    auto runningQueue_ = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto pendingQueue_ = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    runningQueue_->Enqueue(ins1);
    runningQueue_->Enqueue(ins2);
    pendingQueue_->Enqueue(ins3);

    runningQueue_->Extend(pendingQueue_);

    auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(runningQueue_->Front());
    EXPECT_EQ(runningQueue_->Front()->GetRequestId(), "ins1");
    aggregatedItem->reqQueue->pop_front();
    EXPECT_EQ(runningQueue_->Front()->GetRequestId(), "ins3");
    aggregatedItem->reqQueue->pop_front();
    runningQueue_->Dequeue();
    EXPECT_EQ(runningQueue_->Front()->GetRequestId(), "ins2");
    runningQueue_->Dequeue();
}

TEST_F(AggregatedQueueTest, AbnormalTest)
{
    // for LLT lcov
    auto runningQueue_ = std::make_shared<AggregatedQueue>(10, "relaxed");
    auto pendingQueue_ = std::make_shared<AggregatedQueue>(10, "relaxed");
    auto ins1 = CreateInstanceItem("ins1", 3, 10, 10);
    runningQueue_->Enqueue(ins1);
    auto result = runningQueue_->Dequeue();
    EXPECT_EQ(result.Get().StatusCode(), StatusCode::FAILED);

    runningQueue_->Extend(nullptr);

    auto runningQueue1 = std::make_shared<AggregatedQueue>(10, "relaxed");
    auto group1 = GroupItem::CreateGroupItem("group1");
    pendingQueue_->Enqueue(group1);
    EXPECT_EQ(runningQueue1->aggregatedReqs.size(), 0);
    runningQueue1->Extend(pendingQueue_);
    EXPECT_EQ(runningQueue1->aggregatedReqs.size(), 1);

    auto group2 = GroupItem::CreateGroupItem("group2");
    EXPECT_EQ(group2->GetItemType(), QueueItemType::GROUP);
    EXPECT_EQ(group2->GetRequestId(), "group2");
    EXPECT_EQ(group2->GetPriority(), 0);


    auto scheRunningQueue = std::make_shared<ScheduleQueue>(10);
    auto schePendingQueue = std::make_shared<ScheduleQueue>(10);
    auto ins3 = CreateInstanceItem("ins3", 3, 10, 10);
    schePendingQueue->Enqueue(ins3);
    scheRunningQueue->Extend(schePendingQueue);
    EXPECT_EQ(scheRunningQueue->queueMap_.size(), 1);

}





}