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

#include "common/metrics/metrics_adapter.h"
#include "common/metrics/metrics_context.h"

namespace functionsystem::test {
using namespace functionsystem::metrics;
class MetricsMetricsTest : public ::testing::Test {};

TEST_F(MetricsMetricsTest, IsSystemFunc)
{
    EXPECT_FALSE(
        MetricsAdapter::GetInstance().GetMetricsContext().IsSystemFunc("function-agent-pool08012-58f488c974-ldgwr"));

    EXPECT_TRUE(MetricsAdapter::GetInstance().GetMetricsContext().IsSystemFunc(
        "function-agent-3570bc2a2afa-500m-500mi-faasmanager-55470025008f"));

    EXPECT_TRUE(MetricsAdapter::GetInstance().GetMetricsContext().IsSystemFunc(
        "function-agent-18a9078842ae-500m-500mi-faasscheduler-1f0000002f"));

    EXPECT_TRUE(MetricsAdapter::GetInstance().GetMetricsContext().IsSystemFunc(
        "function-agent-97ffe67a0963-500m-2048mi-faasfrontend-b655564e00"));

    EXPECT_TRUE(MetricsAdapter::GetInstance().GetMetricsContext().IsSystemFunc(
        "function-agent-0-500m-500mi-faascontroller-4d62528400000000001f"));
}
}  // namespace functionsystem::test