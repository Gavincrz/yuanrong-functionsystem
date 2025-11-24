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

#include "gtest/gtest.h"
#define private public
#include "common/schedule_decision/queue/time_sorted_queue.h"
#undef private

namespace functionsystem::test {
using namespace ::testing;
using namespace schedule_decision;

class TimeSortedQueueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }
    void TearDown() override
    {
    }
};

/**
 * @tc.name  : Enqueue_ShouldReturnFailed_WhenQueueItemIsNull
 * @tc.desc  : Test scenario where the queue item is nullptr.
 */
TEST_F(TimeSortedQueueTest, Enqueue_ShouldReturnFailed_WhenQueueItemIsNull)
{
    TimeSortedQueue timeSortedQueue;
    auto result = timeSortedQueue.Enqueue(nullptr);
    EXPECT_EQ(result.Get().StatusCode(), StatusCode::FAILED);
}

/**
 * @tc.name  : Enqueue_ShouldReturnErrParamInvalid_WhenRequestIdIsEmpty
 * @tc.desc  : Test scenario where the request id is empty.
 */
TEST_F(TimeSortedQueueTest, Enqueue_ShouldReturnErrParamInvalid_WhenRequestIdIsEmpty)
{
    TimeSortedQueue timeSortedQueue;
    auto queueItem = InstanceItem::CreateInstanceItem("");
    auto result = timeSortedQueue.Enqueue(queueItem);
    EXPECT_EQ(result.Get().StatusCode(), StatusCode::ERR_PARAM_INVALID);
}

/**
 * @tc.name  : Enqueue_ShouldReturnErrParamInvalid_WhenPriorityIsGreaterThanMaxPriority
 * @tc.desc  : Test scenario where the priority is greater than maxPriority.
 */
TEST_F(TimeSortedQueueTest, Enqueue_ShouldReturnErrParamInvalid_WhenPriorityIsGreaterThanMaxPriority)
{
    TimeSortedQueue timeSortedQueue;
    auto queueItem = InstanceItem::CreateInstanceItem("123", 101);
    timeSortedQueue.maxPriority_ = 100;
    auto result = timeSortedQueue.Enqueue(queueItem);
    EXPECT_EQ(result.Get().StatusCode(), StatusCode::ERR_PARAM_INVALID);
}

/**
 * @tc.name  : Enqueue_ShouldReturnOk_WhenParametersAreValid
 * @tc.desc  : Test scenario where all parameters are valid.
 */
TEST_F(TimeSortedQueueTest, Enqueue_ShouldReturnOk_WhenParametersAreValid)
{
    TimeSortedQueue timeSortedQueue;
    auto queueItem = InstanceItem::CreateInstanceItem("123", 50);
    timeSortedQueue.maxPriority_ = 100;
    auto result = timeSortedQueue.Enqueue(queueItem);
    EXPECT_EQ(result.Get().StatusCode(), StatusCode::SUCCESS);
}

/**
 * @tc.name  : Front_ShouldReturnNull_WhenQueueIsEmpty
 * @tc.desc  : Test Front method when the queue is empty.
 */
TEST_F(TimeSortedQueueTest, Front_ShouldReturnNull_WhenQueueIsEmpty)
{
    TimeSortedQueue queue;
    auto result = queue.Front();
    EXPECT_EQ(result, nullptr);
}

/**
 * @tc.name  : Front_ShouldReturnTopElement_WhenQueueIsNotEmpty
 * @tc.desc  : Test Front method when the queue is not empty.
 */
TEST_F(TimeSortedQueueTest, Front_ShouldReturnTopElement_WhenQueueIsNotEmpty)
{
    TimeSortedQueue queue;
    auto queueItem = InstanceItem::CreateInstanceItem("1233456");
    queue.Enqueue(queueItem);
    auto result = queue.Front();
    EXPECT_EQ(result != nullptr, true);
    EXPECT_EQ(result->GetRequestId(), queueItem->GetRequestId());
}

/**
 * @tc.name  : Front_ShouldReturnTopElement_WhenQueueHasMultiplePriorities
 * @tc.desc  : Test Front method when the queue has multiple priorities.
 */
TEST_F(TimeSortedQueueTest, Front_ShouldReturnTopElement_WhenQueueHasMultiplePriorities)
{
    TimeSortedQueue queue;
    queue.maxPriority_ = 4;
    auto queueItem1 = InstanceItem::CreateInstanceItem("1233456_1", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem2 = InstanceItem::CreateInstanceItem("1233456_2", 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem3 = InstanceItem::CreateInstanceItem("1233456_3", 3);
    queue.Enqueue(queueItem1);
    queue.Enqueue(queueItem2);
    queue.Enqueue(queueItem3);
    auto result = queue.Front();
    EXPECT_EQ(result->GetRequestId(), queueItem3->GetRequestId());
    EXPECT_EQ(queue.Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = queue.Front();
    EXPECT_EQ(result->GetRequestId(), queueItem2->GetRequestId());
    EXPECT_EQ(queue.Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = queue.Front();
    EXPECT_EQ(result->GetRequestId(), queueItem1->GetRequestId());
    EXPECT_EQ(queue.Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(queue.CheckIsQueueEmpty(), true);
}

/**
 * @tc.name  : Front_ShouldReturnTopElement_WhenQueueHasMultiplePrioritiesAndSomeAreEmpty
 * @tc.desc  : Test Front method when the queue has multiple timestamp.
 */
TEST_F(TimeSortedQueueTest, Front_ShouldReturnTopElement_WhenQueueHasMutlipulTimestamp)
{
    TimeSortedQueue queue;
    queue.maxPriority_ = 4;
    auto queueItem1 = InstanceItem::CreateInstanceItem("1233456_1", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem2 = InstanceItem::CreateInstanceItem("1233456_2", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem3 = GroupItem::CreateGroupItem("1233456_3", 1);
    queue.Enqueue(queueItem2);
    queue.Enqueue(queueItem1);
    queue.Enqueue(queueItem3);
    auto result = queue.Front();
    EXPECT_EQ(result->GetRequestId(), queueItem1->GetRequestId());
    EXPECT_EQ(queue.Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = queue.Front();
    EXPECT_EQ(result->GetRequestId(), queueItem2->GetRequestId());
    EXPECT_EQ(queue.Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = queue.Front();
    EXPECT_EQ(result->GetRequestId(), queueItem3->GetRequestId());
    EXPECT_EQ(queue.Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(queue.CheckIsQueueEmpty(), true);
}

/**
 * @tc.name  : TimeSortedQueue_Swap_ShouldSwap_WhenTargetIsNotNull
 * @tc.desc  : Test Swap method when target is not null
 */
TEST_F(TimeSortedQueueTest, TimeSortedQueue_Swap_ShouldSwap_WhenTargetIsNotNull)
{
    auto queue1 = std::make_shared<TimeSortedQueue>();
    auto queue2 = std::make_shared<TimeSortedQueue>();
    queue1->maxPriority_ = 4;
    auto queueItem1 = InstanceItem::CreateInstanceItem("1233456_1", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem2 = InstanceItem::CreateInstanceItem("1233456_2", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem3 = GroupItem::CreateGroupItem("1233456_3", 1);
    queue1->Enqueue(queueItem2);
    queue1->Enqueue(queueItem1);
    queue1->Enqueue(queueItem3);

    // Swap the queues
    queue2->Swap(queue1);
    EXPECT_EQ(queue1->CheckIsQueueEmpty(), true);
    auto result = queue2->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem1->GetRequestId());
    EXPECT_EQ(queue2->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = queue2->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem2->GetRequestId());
    EXPECT_EQ(queue2->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = queue2->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem3->GetRequestId());
    EXPECT_EQ(queue2->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(queue2->CheckIsQueueEmpty(), true);
}

/**
 * @tc.name  : TimeSortedQueue_Swap_ShouldNotSwap_WhenTargetIsNull
 * @tc.desc  : Test Swap method when target is null
 */
TEST_F(TimeSortedQueueTest, TimeSortedQueue_Swap_ShouldNotSwap_WhenTargetIsNull)
{
    auto queue1 = std::make_shared<TimeSortedQueue>();
    auto queueItem = InstanceItem::CreateInstanceItem("1233456");
    queue1->Enqueue(queueItem);
    std::shared_ptr<ScheduleQueue> queue2 = nullptr;
    // Swap the queues
    queue1->Swap(queue2);
    // Check if the swap was not successful
    EXPECT_EQ(queue1->queueMap_.size(), 1);
    auto result = queue1->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem->GetRequestId());
}

/**
 * @tc.name  : TimeSortedQueue_Extend_ShouldHandleNullTargetQueue
 * @tc.desc  : Test when targetQueue is nullptr then Extend method should return immediately
 */
TEST_F(TimeSortedQueueTest, TimeSortedQueue_Extend_ShouldHandleNullTargetQueue)
{
    std::shared_ptr<TimeSortedQueue> timeSortedQueue = std::make_shared<TimeSortedQueue>();
    auto queueItem = InstanceItem::CreateInstanceItem("1233456");
    timeSortedQueue->Enqueue(queueItem);
    timeSortedQueue->Extend(nullptr);
    EXPECT_EQ(timeSortedQueue->queueMap_.size(), 1);
    EXPECT_EQ(timeSortedQueue->queueMap_[0].size(), 1);
}

/**
 * @tc.name  : TimeSortedQueue_Extend_ShouldHandleNonTimeSortedQueue
 * @tc.desc  : Test when targetQueue is not a TimeSortedQueue then Extend method should return immediately
 */
TEST_F(TimeSortedQueueTest, TimeSortedQueue_Extend_ShouldHandleNonTimeSortedQueue)
{
    std::shared_ptr<TimeSortedQueue> timeSortedQueue = std::make_shared<TimeSortedQueue>();
    auto queueItem = InstanceItem::CreateInstanceItem("1233456");
    timeSortedQueue->Enqueue(queueItem);
    std::shared_ptr<TimeSortedQueue> nonTimeSortedQueue = std::make_shared<TimeSortedQueue>();
    timeSortedQueue->Extend(nonTimeSortedQueue);
    EXPECT_EQ(timeSortedQueue->queueMap_.size(), 1);
    EXPECT_EQ(timeSortedQueue->queueMap_[0].size(), 1);
}

/**
 * @tc.name  : TimeSortedQueue_Extend_ShouldHandleValidTimeSortedQueue
 * @tc.desc  : Test when targetQueue is a valid TimeSortedQueue then Extend method should merge the queues
 */
TEST_F(TimeSortedQueueTest, TimeSortedQueue_Extend_ShouldHandleValidTimeSortedQueue)
{
    std::shared_ptr<TimeSortedQueue> timeSortedQueue = std::make_shared<TimeSortedQueue>(3);
    std::shared_ptr<TimeSortedQueue> targetQueue = std::make_shared<TimeSortedQueue>(3);
    auto queueItem1 = InstanceItem::CreateInstanceItem("1233456_1", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem2 = InstanceItem::CreateInstanceItem("1233456_2", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem3 = GroupItem::CreateGroupItem("1233456_3", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem4 = GroupItem::CreateGroupItem("1233456_4", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto queueItem5 = InstanceItem::CreateInstanceItem("1233456_5", 2);
    targetQueue->Enqueue(queueItem2);
    timeSortedQueue->Enqueue(queueItem1);
    targetQueue->Enqueue(queueItem3);
    timeSortedQueue->Enqueue(queueItem4);
    targetQueue->Enqueue(queueItem5);

    // Populate targetQueue with some data
    // Call the method under test
    timeSortedQueue->Extend(targetQueue);
    EXPECT_EQ(timeSortedQueue->Size(), (size_t)5);
    auto result = timeSortedQueue->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem5->GetRequestId());
    EXPECT_EQ(timeSortedQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = timeSortedQueue->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem1->GetRequestId());
    EXPECT_EQ(timeSortedQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = timeSortedQueue->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem2->GetRequestId());
    EXPECT_EQ(timeSortedQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = timeSortedQueue->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem3->GetRequestId());
    EXPECT_EQ(timeSortedQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    result = timeSortedQueue->Front();
    EXPECT_EQ(result->GetRequestId(), queueItem4->GetRequestId());
    EXPECT_EQ(timeSortedQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(timeSortedQueue->CheckIsQueueEmpty(), true);
}
}