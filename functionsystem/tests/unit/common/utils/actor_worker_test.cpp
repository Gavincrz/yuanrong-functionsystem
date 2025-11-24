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

#include "common/utils/actor_worker.h"

#include <gtest/gtest.h>

#include <litebus.hpp>

#include "utils/future_test_helper.h"

namespace functionsystem::test {
class ActorWorkerTest : public ::testing::Test {
};

TEST_F(ActorWorkerTest, WorkTest)
{
    auto worker = ActorWorker();
    int x = 0;
    auto handler = [&x]() mutable { x += 1; };
    auto future = worker.AsyncWork(handler);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    EXPECT_EQ(x, 1);
}

TEST_F(ActorWorkerTest, WorkFutureTest)
{
    auto worker = ActorWorker();
    auto x = std::make_shared<litebus::Promise<int>>();
    auto handler = [x]() mutable { x->SetValue(1); };
    (void)worker.AsyncWork(handler);
    ASSERT_AWAIT_READY(x->GetFuture());
    EXPECT_EQ(x->GetFuture().IsOK(), true);
    EXPECT_EQ(x->GetFuture().Get(), 1);
}

}  // namespace functionsystem::test
