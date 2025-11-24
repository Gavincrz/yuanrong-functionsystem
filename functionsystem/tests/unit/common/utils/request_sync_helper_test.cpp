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

#include "common/utils/request_sync_helper.h"

#include <gtest/gtest.h>

#include <litebus.hpp>

#include "utils/future_test_helper.h"

namespace functionsystem::test {

const int32_t TIMEOUT = 100;

class TestActor : public litebus::ActorBase {
public:
    TestActor() : ActorBase("sync_helper")
    {
    }
    ~TestActor() = default;

    litebus::Future<int> AddRequest(const std::string &key)
    {
        return sync_.AddSynchronizer(key);
    }

    Status FinishedRequest(const std::string &key, const int &rsp)
    {
        return sync_.Synchronized(key, rsp);
    }

private:
    REQUEST_SYNC_HELPER(TestActor, int, TIMEOUT, sync_);
};

class RequestSyncHelperTest : public ::testing::Test {
public:
    void SetUp() override
    {
        test_ = std::make_shared<TestActor>();
        litebus::Spawn(test_);
    }
    void TearDown() override
    {
        litebus::Terminate(test_->GetAID());
        litebus::Await(test_->GetAID());
        test_ = nullptr;
    }

protected:
    std::shared_ptr<TestActor> test_;
};

TEST_F(RequestSyncHelperTest, NormalResponse)
{
    std::string requestName = "request1";
    auto future = litebus::Async(test_->GetAID(), &TestActor::AddRequest, requestName);

    int rsp = 5;
    auto status = litebus::Async(test_->GetAID(), &TestActor::FinishedRequest, requestName, rsp);
    ASSERT_AWAIT_READY_FOR(status, 10);
    EXPECT_EQ(status.Get().IsOk(), true);
    ASSERT_AWAIT_READY_FOR(future, TIMEOUT);
    EXPECT_EQ(future.Get(), rsp);
}

TEST_F(RequestSyncHelperTest, ResponseTimeout)
{
    std::string requestName = "request2";
    auto future = litebus::Async(test_->GetAID(), &TestActor::AddRequest, requestName);
    auto ret = future.Get(TIMEOUT * 2);
    EXPECT_EQ(future.IsError(), true);
    EXPECT_EQ(future.GetErrorCode(), StatusCode::REQUEST_TIME_OUT);
}

TEST_F(RequestSyncHelperTest, InvalidResponse)
{
    std::string requestName = "request3";
    int rsp = 5;
    auto status = litebus::Async(test_->GetAID(), &TestActor::FinishedRequest, requestName, rsp);
    ASSERT_AWAIT_READY_FOR(status, 10);
    auto ret = status.Get();
    EXPECT_EQ(ret.IsError(), true);
    EXPECT_EQ(ret.StatusCode(), StatusCode::FAILED);
}
}  // namespace functionsystem::test
