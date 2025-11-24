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

#include <gmock/gmock-actions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_manager/metrics/collector/node_disk_collector.h"

namespace functionsystem::test {
class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class NodeDiskCollectorTest : public ::testing::Test {};

TEST_F(NodeDiskCollectorTest, GetUsage)
{
    auto tools = std::make_shared<MockProcFSTools>();

    auto collector = std::make_shared<runtime_manager::NodeDiskCollector>(tools);

    auto usage = collector->GetUsage();
    EXPECT_GT(usage.Get().value.Get(), 0);
}
}  // namespace functionsystem::test