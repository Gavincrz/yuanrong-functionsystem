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

#include "common/schedule_decision/queue/schedule_queue.h"
#include "common/resource_view/resource_type.h"

namespace functionsystem::test {
using namespace schedule_decision;
class ScheduleQueueTest : public ::testing::Test {
public:
    static std::shared_ptr<ScheduleQueue> CreatePriorityQueue() {
        auto priorityQueue = std::make_shared<ScheduleQueue>(3);
        priorityQueue->Enqueue(InstanceItem::CreateInstanceItem("test", 2));
        priorityQueue->Enqueue(GroupItem::CreateGroupItem("testGroup", 3, 2));
        EXPECT_EQ(priorityQueue->reqIndex_.size(), size_t(2));
        return priorityQueue;
    }
};

TEST_F(ScheduleQueueTest, EnqueueTest)
{
    auto priorityQueue = CreatePriorityQueue();

    auto req = std::make_shared<messages::ScheduleRequest>();
    auto item = std::make_shared<InstanceItem>(req, std::make_shared<litebus::Promise<ScheduleResult>>(), litebus::Future<std::string>());
    auto res = priorityQueue->Enqueue(item).Get();
    EXPECT_EQ(res.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(res.GetMessage(), "[get instance requestId failed]");

    req->set_requestid("123");
    req->mutable_instance()->mutable_scheduleoption()->set_priority(4);
    res = priorityQueue->Enqueue(item).Get();
    EXPECT_EQ(res.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(res.GetMessage(), "[instance priority is greater than maxPriority]");

    req->mutable_instance()->mutable_scheduleoption()->set_priority(1);
    res = priorityQueue->Enqueue(item).Get();
    EXPECT_EQ(res.StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(priorityQueue->reqIndex_.size(), 3);
    EXPECT_TRUE(priorityQueue->reqIndex_.find("123") != priorityQueue->reqIndex_.end());
}

TEST_F(ScheduleQueueTest, FrontAndDequeueTest)
{
    auto priorityQueue = std::make_shared<ScheduleQueue>();
    auto res = priorityQueue->Dequeue().Get();
    EXPECT_EQ(res.StatusCode(), StatusCode::FAILED);
    EXPECT_EQ(res.GetMessage(), "[queue is empty]");

    priorityQueue = CreatePriorityQueue();
    EXPECT_EQ(priorityQueue->Front()->GetPriority(), 3);
    EXPECT_EQ(priorityQueue->Front()->GetRequestId(), "testGroup");
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(priorityQueue->reqIndex_.size(), 1);
    EXPECT_EQ(priorityQueue->Front()->GetPriority(), 2);
    EXPECT_EQ(priorityQueue->Front()->GetRequestId(), "test");
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_TRUE(priorityQueue->reqIndex_.empty());
    EXPECT_EQ(priorityQueue->Front(), nullptr);
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::FAILED);
}

TEST_F(ScheduleQueueTest, SwapTest)
{
    auto scheduleQueue = std::make_shared<ScheduleQueue>();
    scheduleQueue->Enqueue(InstanceItem::CreateInstanceItem("req1"));
    auto targetQueue = std::make_shared<ScheduleQueue>();
    scheduleQueue->Swap(targetQueue);
    EXPECT_EQ(scheduleQueue->reqIndex_.size(), 0);
    EXPECT_TRUE(targetQueue->reqIndex_.find("req1") != targetQueue->reqIndex_.end());

    scheduleQueue = std::make_shared<ScheduleQueue>();
    targetQueue = std::make_shared<ScheduleQueue>();
    targetQueue->Enqueue(InstanceItem::CreateInstanceItem("req1"));
    scheduleQueue->Swap(targetQueue);
    EXPECT_TRUE(scheduleQueue->reqIndex_.find("req1") != scheduleQueue->reqIndex_.end());
    EXPECT_EQ(targetQueue->reqIndex_.size(), 0);

    scheduleQueue = std::make_shared<ScheduleQueue>();
    scheduleQueue->Enqueue(InstanceItem::CreateInstanceItem("req1"));
    targetQueue = std::make_shared<ScheduleQueue>();
    targetQueue->Enqueue(InstanceItem::CreateInstanceItem("req2"));
    targetQueue->Enqueue(InstanceItem::CreateInstanceItem("req3"));
    scheduleQueue->Swap(targetQueue);
    EXPECT_TRUE(scheduleQueue->reqIndex_.find("req2") != scheduleQueue->reqIndex_.end());
    EXPECT_TRUE(scheduleQueue->reqIndex_.find("req3") != scheduleQueue->reqIndex_.end());
    EXPECT_TRUE(scheduleQueue->reqIndex_.size() == 2);
    EXPECT_TRUE(targetQueue->reqIndex_.find("req1") != targetQueue->reqIndex_.end());
}
}