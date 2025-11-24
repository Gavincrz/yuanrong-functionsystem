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

#include "common/resource_view/resource_poller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>

#include "async/asyncafter.hpp"
#include "common/logs/logging.h"
#include "litebus.hpp"
#include "utils/future_test_helper.h"

using namespace functionsystem::resource_view;

namespace functionsystem::test {
using namespace ::testing;
class ResourcePollerTest : public ::testing::Test {};

class FakeResourceActor : public litebus::ActorBase {
public:
    FakeResourceActor() : litebus::ActorBase("FakeResourceActor")
    {
    }
    ~FakeResourceActor() override = default;

    void SetPoller(const std::shared_ptr<ResourcePoller> &poller)
    {
        poller_ = poller;
    }

    void Add(const std::string &id)
    {
        poller_->Add(id);
    }

    bool Del(const std::string &id)
    {
        poller_->Del(id);
        return true;
    }

    void TimeOutReset(const std::string &id)
    {
        MockReset(id);
    }

    void Reset(const std::string &id)
    {
        poller_->Reset(id);
    }

    void SendPull(const std::string &id)
    {
        MockSendPull(id);
        poller_->Reset(id);
    }

    void Deffer(uint64_t duration)
    {
        MockDeffer(duration);
        (void)litebus::TimerTools::Cancel(tryPullTimer_);
        tryPullTimer_ = litebus::AsyncAfter(duration, GetAID(), &FakeResourceActor::TryPull);
    }

    void TryPull()
    {
        MockResetBeforePull();
        poller_->TryPullResource();
    }

    bool IsPulling(const std::string &id)
    {
        return poller_->pulling_.find(id) != poller_->pulling_.end();
    }

    MOCK_METHOD(void, MockResetBeforePull, ());
    MOCK_METHOD(void, MockSendPull, (const std::string &id));
    MOCK_METHOD(void, MockReset, (const std::string &id));
    MOCK_METHOD(void, MockDeffer, (uint64_t duration));

protected:
    void Init() override
    {
    }
    void Finalize() override
    {
        (void)litebus::TimerTools::Cancel(tryPullTimer_);
        poller_->Stop();
        poller_ = nullptr;
    }

private:
    std::shared_ptr<ResourcePoller> poller_;
    litebus::Timer tryPullTimer_;
};

// 添加下游poller，周期性pull资源, 删除后停止pull
TEST_F(ResourcePollerTest, AddWithPollPeriod)
{
    std::string id1 = "id1";
    std::string id2 = "id2";
    auto actor = std::make_shared<FakeResourceActor>();
    auto sendPull = [aid(actor->GetAID())](const std::string &id) {
        litebus::Async(aid, &FakeResourceActor::SendPull, id);
    };
    auto delegateReset = [aid(actor->GetAID())](const std::string &id) {
        litebus::Async(aid, &FakeResourceActor::TimeOutReset, id);
    };
    auto defer = [aid(actor->GetAID())](uint64_t duration) {
        litebus::Async(aid, &FakeResourceActor::Deffer, duration);
    };
    auto poller = std::make_shared<ResourcePoller>(sendPull, delegateReset, defer);
    actor->SetPoller(poller);
    litebus::Spawn(actor);
    ResourcePoller::SetInterval(200);
    auto begin = litebus::TimeWatch::Now();
    poller->Add(id1);
    poller->Add(id2);
    EXPECT_CALL(*actor, MockDeffer(200)).WillRepeatedly(Return());
    EXPECT_CALL(*actor, MockResetBeforePull()).WillRepeatedly(Return());
    EXPECT_CALL(*actor, MockSendPull(id1)).WillRepeatedly(Return());
    EXPECT_CALL(*actor, MockSendPull(id2)).WillRepeatedly(Return());
    poller->TryPullResource();
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    (void)litebus::Async(actor->GetAID(), &FakeResourceActor::Del, id1);
    auto ret = litebus::Async(actor->GetAID(), &FakeResourceActor::Del, id2);
    ASSERT_AWAIT_READY(ret);
    begin = litebus::TimeWatch::Now();
    EXPECT_CALL(*actor, MockSendPull(id1)).Times(0);
    EXPECT_CALL(*actor, MockSendPull(id2)).Times(0);
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    ret = litebus::Async(actor->GetAID(), &FakeResourceActor::IsPulling, id1);
    ASSERT_AWAIT_READY(ret);
    EXPECT_EQ(ret.Get(), false);
    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
}

// 添加下游poller，周期性pull资源，在发送拉取资源前被reset
TEST_F(ResourcePollerTest, ResetBeforePull)
{
    std::string id1 = "id1";
    std::string id2 = "id2";
    auto actor = std::make_shared<FakeResourceActor>();
    auto sendPull = [aid(actor->GetAID())](const std::string &id) {
        litebus::Async(aid, &FakeResourceActor::SendPull, id);
    };
    auto delegateReset = [aid(actor->GetAID())](const std::string &id) {
        litebus::Async(aid, &FakeResourceActor::TimeOutReset, id);
    };
    auto defer = [aid(actor->GetAID())](uint64_t duration) {
        litebus::Async(aid, &FakeResourceActor::Deffer, duration);
    };
    auto poller = std::make_shared<ResourcePoller>(sendPull, delegateReset, defer);
    actor->SetPoller(poller);
    litebus::Spawn(actor);
    ResourcePoller::SetInterval(200);
    auto begin = litebus::TimeWatch::Now();
    poller->Add(id1);
    poller->Add(id2);
    EXPECT_CALL(*actor, MockDeffer(200)).WillRepeatedly(Return());
    EXPECT_CALL(*actor, MockResetBeforePull()).WillRepeatedly(Invoke([&poller, id1]() { poller->Reset(id1); }));
    EXPECT_CALL(*actor, MockSendPull(id1)).Times(0);
    EXPECT_CALL(*actor, MockSendPull(id2)).WillRepeatedly(Return());
    poller->TryPullResource();
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    EXPECT_CALL(*actor, MockReset(id1)).WillRepeatedly(Return());
    EXPECT_CALL(*actor, MockReset(id2)).WillRepeatedly(Return());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 400; });
    (void)litebus::Async(actor->GetAID(), &FakeResourceActor::Del, id1);
    auto ret = litebus::Async(actor->GetAID(), &FakeResourceActor::Del, id2);
    ASSERT_AWAIT_READY(ret);
    begin = litebus::TimeWatch::Now();
    EXPECT_CALL(*actor, MockSendPull(id2)).Times(0);
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
}

// 重复添加
TEST_F(ResourcePollerTest, DuplicateAdd)
{
    std::string id1 = "id1";
    auto sendPull = [](const std::string &id) {};
    auto delegateReset = [](const std::string &id) {};
    auto defer = [](uint64_t duration) {};
    auto poller = std::make_shared<ResourcePoller>(sendPull, delegateReset, defer);
    poller->Add(id1);
    ASSERT_NO_THROW(poller->Add(id1));
}

// 重复删除
TEST_F(ResourcePollerTest, DuplicateDel)
{
    std::string id1 = "id1";
    auto sendPull = [](const std::string &id) {};
    auto delegateReset = [](const std::string &id) {};
    auto defer = [](uint64_t duration) {};
    auto poller = std::make_shared<ResourcePoller>(sendPull, delegateReset, defer);
    poller->Add(id1);
    poller->Del(id1);
    ASSERT_NO_THROW(poller->Del(id1));
}

}  // namespace functionsystem::test