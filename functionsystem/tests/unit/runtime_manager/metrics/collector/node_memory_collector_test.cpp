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
#include <gmock/gmock-actions.h>
#include <gmock/gmock.h>
#include "runtime_manager/metrics/collector/node_memory_collector.h"

namespace functionsystem::test {

class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class NodeMemoryCollectorTest : public ::testing::Test {};

/**
 * Feature: NodeMemoryCollector
 * Description: Generate filter
 * Steps:
 * Expectation:
 * node-cpu
 */
TEST_F(NodeMemoryCollectorTest, GenFilter)
{
    auto collector = std::make_shared<runtime_manager::NodeMemoryCollector>();
    EXPECT_EQ(collector->GenFilter(), "node-Memory");
}

/**
 * Feature: NodeMemoryCollector
 * Description: Get Limit
 * Steps:
 * Expectation:
 */
TEST_F(NodeMemoryCollectorTest, GetLimitAndGetUsage)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillRepeatedly(testing::Return(litebus::Option<std::string>{
            R"(MemTotal:       197675280 kB
MemFree:        29990092 kB
MemAvailable:   135189944 kB
Buffers:         5727740 kB
Cached:         93357532 kB
SwapCached:          176 kB
Active:         115705316 kB
Inactive:       43054508 kB
Active(anon):   58886872 kB
Inactive(anon):   734380 kB
Active(file):   56818444 kB
Inactive(file): 42320128 kB
Unevictable:           0 kB
Mlocked:               0 kB
SwapTotal:       3998716 kB
SwapFree:        3982332 kB
Dirty:           2041752 kB
Writeback:         80012 kB
AnonPages:      59589348 kB
Mapped:           830260 kB
Shmem:             43704 kB
Slab:            8252304 kB
SReclaimable:    7516760 kB
SUnreclaim:       735544 kB
KernelStack:       28864 kB
PageTables:       171360 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    102836356 kB
Committed_AS:   71522528 kB
VmallocTotal:   34359738367 kB
VmallocUsed:           0 kB
VmallocChunk:          0 kB
HardwareCorrupted:     0 kB
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
CmaTotal:              0 kB
CmaFree:               0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
DirectMap4k:     2393984 kB
DirectMap2M:    115683328 kB
DirectMap1G:    84934656 kB)"
        }));

    auto collector = std::make_shared<runtime_manager::NodeMemoryCollector>(tools, 3000.0);
    auto limit = collector->GetLimit();
    EXPECT_LT(limit.value.Get(), 190042.3);
    EXPECT_GT(limit.value.Get(), 190042.2);
    EXPECT_EQ(limit.instanceID.IsNone(), true);
    auto usage = collector->GetUsage();
    EXPECT_LT(usage.Get().value.Get(), 61020.9);
    EXPECT_GT(usage.Get().value.Get(), 61020.1);
}
}