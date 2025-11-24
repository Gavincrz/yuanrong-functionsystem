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

#ifndef LLT_UTILS_FUTURE_TEST_HELPER_H
#define LLT_UTILS_FUTURE_TEST_HELPER_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "async/future.hpp"
#include "common/utils/actor_worker.h"

namespace functionsystem::test {

#define TEST_AWAIT_TIMEOUT 15000

template <typename T>
::testing::AssertionResult AwaitAssertReady(const char *expr,
                                            const char *,  // Unused string representation of 'duration'.
                                            const litebus::Future<T> &actual, const uint32_t &duration)
{
    litebus::Status s = actual.WaitFor(duration);
    if (!s.IsOK()) {
        return ::testing::AssertionFailure() << "Failed to wait " << duration << "ms for " << expr;
    } else if (actual.IsError()) {
        return ::testing::AssertionFailure() << "(" << expr << ").failure(): " << actual.GetErrorCode();
    }
    return ::testing::AssertionSuccess();
}

template <typename T>
::testing::AssertionResult AwaitAssertSet(const char *expr,
                                          const char *,  // Unused string representation of 'duration'.
                                          const litebus::Future<T> &actual, const uint32_t &duration)
{
    litebus::Status s = actual.WaitFor(duration);
    if (!s.IsOK()) {
        return ::testing::AssertionFailure() << "Failed to wait " << duration << "ms for " << expr;
    }
    return ::testing::AssertionSuccess();
}

template <typename T>
::testing::AssertionResult AwaitAssertNoSet(const char *expr,
                                            const char *,  // Unused string representation of 'duration'.
                                            const litebus::Future<T> &actual, const uint32_t &duration)
{
    litebus::Status s = actual.WaitFor(duration);
    if (!s.IsOK()) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "the " << expr << " shouldn't set when waiting for " << duration << "ms";
}

#define EXPECT_AWAIT_READY_FOR(actual, duration) EXPECT_PRED_FORMAT2(AwaitAssertReady, actual, duration)
#define EXPECT_AWAIT_READY(actual) EXPECT_AWAIT_READY_FOR(actual, TEST_AWAIT_TIMEOUT)
#define ASSERT_AWAIT_READY_FOR(actual, duration) ASSERT_PRED_FORMAT2(AwaitAssertReady, actual, duration)
#define ASSERT_AWAIT_READY(actual) ASSERT_AWAIT_READY_FOR(actual, TEST_AWAIT_TIMEOUT)
#define ASSERT_AWAIT_SET_FOR(actual, duration) ASSERT_PRED_FORMAT2(AwaitAssertSet, actual, duration)
#define ASSERT_AWAIT_SET(actual) ASSERT_AWAIT_SET_FOR(actual, TEST_AWAIT_TIMEOUT)
#define ASSERT_AWAIT_NO_SET_FOR(actual, duration) ASSERT_PRED_FORMAT2(AwaitAssertNoSet, actual, duration)

// cycle check
#define AWAIT_ASSERT_TRUE_USLEEP_TIME 1000
template <typename T = bool>
::testing::AssertionResult AwaitAssertTrue(const char *expr, const char *, const std::function<bool(void)> &fn,
                                           uint32_t duration)
{
    uint32_t cycle = (TEST_AWAIT_TIMEOUT * 1000) / AWAIT_ASSERT_TRUE_USLEEP_TIME;
    uint32_t i = 0;

    do {
        if (fn()) {
            return ::testing::AssertionSuccess();
        }
        i++;
        usleep(AWAIT_ASSERT_TRUE_USLEEP_TIME);
    } while (i <= cycle);

    return ::testing::AssertionFailure() << "Failed to wait " << duration << "ms for true"
                                         << ", actual value: " << fn();
}

#define EXPECT_AWAIT_TRUE_FOR(actual, duration) EXPECT_PRED_FORMAT2(AwaitAssertTrue, actual, duration)
#define EXPECT_AWAIT_TRUE(actual) EXPECT_AWAIT_TRUE_FOR(actual, TEST_AWAIT_TIMEOUT)
#define ASSERT_AWAIT_TRUE_FOR(actual, duration) ASSERT_PRED_FORMAT2(AwaitAssertTrue, actual, duration)
#define ASSERT_AWAIT_TRUE(actual) ASSERT_AWAIT_TRUE_FOR(actual, TEST_AWAIT_TIMEOUT)

ACTION_TEMPLATE(PromiseArg, HAS_1_TEMPLATE_PARAMS(int, k), AND_1_VALUE_PARAMS(promise))
{
    promise->SetValue(std::get<k>(args));
}

template <int index, typename T>
PromiseArgActionP<index, std::shared_ptr<litebus::Promise<T>>> FutureArg(litebus::Future<T> *future)
{
    auto promise = std::make_shared<litebus::Promise<T>>();
    *future = promise->GetFuture();
    return PromiseArg<index>(promise);
}

template <typename T>
litebus::Future<T> AsyncReturn(T val)
{
    auto promise = std::make_shared<litebus::Promise<T>>();
    auto actor = std::make_shared<ActorWorker>();
    (void)actor->AsyncWork([promise, val]() { promise->SetValue(val); })
        .OnComplete([actor](const litebus::Future<Status> &) { actor->Terminate(); });
    return promise->GetFuture();
}

template <typename T>
litebus::Future<T> AsyncReturn(litebus::Future<T> val)
{
    auto promise = std::make_shared<litebus::Promise<T>>();
    auto actor = std::make_shared<ActorWorker>();
    (void)actor->AsyncWork([promise, val]() { promise->SetValue(val); })
        .OnComplete([actor](const litebus::Future<Status> &) { actor->Terminate(); });
    return promise->GetFuture();
}

}  // namespace functionsystem::test

#endif  // LLT_UTILS_FUTURE_TEST_HELPER_H
