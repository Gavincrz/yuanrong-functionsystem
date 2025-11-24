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
#include <gmock/gmock.h>
#include <gmock/gmock-actions.h>
#include "runtime_manager/metrics/collector/instance_cpu_collector.h"

namespace functionsystem::test {

class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class InstanceCPUCollectorTest : public ::testing::Test {
public:
    void SetUp() override{};
    void TearDown() override{};
};

/**
 * Feature: InstanceCPUCollector
 * Description: Generate filter
 * Steps:
 * Expectation:
 * functionUrn-InstanceId-cpu
 */
TEST_F(InstanceCPUCollectorTest, GenFilter)
{
    auto collector = std::make_shared<runtime_manager::InstanceCPUCollector>(1, "id", 1000.0, "urn");
    EXPECT_EQ(collector->GenFilter(), "urn-id-CPU");
}

/**
 * Feature: InstanceCPUCollector
 * Description: Get Limit
 * Steps:
 * Expectation:
 * Get constructor parameter limit
 */
TEST_F(InstanceCPUCollectorTest, GetLimit)
{
    auto collector = std::make_shared<runtime_manager::InstanceCPUCollector>(1, "id", 1000.0, "urn");
    EXPECT_EQ(collector->GetLimit().value.Get(), 1000.0);
    EXPECT_EQ(collector->GetLimit().instanceID.Get(), "id");
}

/**
 * Feature: InstanceCPUCollector
 * Description: Get Usage
 * Steps:
 * Expectation:
 * Get constructor parameter usage
 */
TEST_F(InstanceCPUCollectorTest, GetUsage)
{
    auto tools = std::make_shared<MockProcFSTools>();
        EXPECT_CALL(*tools.get(), Read("/proc/stat"))
        .WillOnce(testing::Return(litebus::Option<std::string>{
            R"(cpu  258231303 589 11088715 8195531331 15244 2754827 848459 0 0 0
cpu0 11923702 41 1087644 515738274 2847 210938 279500 0 0 0
cpu1 20057109 15 291565 508745688 604 141915 104390 0 0 0
cpu2 6874749 48 1122999 520663754 1549 199736 75274 0 0 0
cpu3 24848059 22 188129 504152845 194 147783 12873 0 0 0
cpu4 18860652 37 1106445 509009860 982 229940 43436 0 0 0
cpu5 13826623 14 284263 515088686 176 114049 28281 0 0 0
cpu6 19189567 40 1127859 508655781 1034 232963 43336 0 0 0
cpu7 13439629 18 266736 515512787 199 110400 15170 0 0 0
cpu8 18788454 72 1130358 509053729 1080 232841 43926 0 0 0
cpu9 13702977 11 264410 515252647 182 111011 14882 0 0 0
cpu10 18793791 72 1149681 509029680 844 232674 44180 0 0 0
cpu11 13555453 8 248708 515425993 144 108081 12689 0 0 0
cpu12 18626769 78 1175226 509185741 893 233971 43553 0 0 0
cpu13 13681432 11 240006 515307595 137 107740 12482 0 0 0
cpu14 19112432 62 1147838 508693565 938 234448 56686 0 0 0
cpu15 12949897 33 256839 516014697 3433 106331 17795 0 0 0
intr 7392494251 0 9 0 0 0 0 0 0 0 0 529358 29 15 0 0 0 0 0 0 0 0 0 0 0 0 5659358 0 22 0 55474 0 9004793 4035290 8719258 4140521 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
ctxt 5334914153
btime 1755739399
processes 64962782
procs_running 5
procs_blocked 0
softirq 2896746553 1 202731347 1 59350651 5590608 0 135535 1076900957 1 1552037452)"
        })).WillOnce(testing::Return(litebus::Option<std::string>{
            R"(cpu  258231644 589 11088718 8195532805 15244 2754830 848460 0 0 0
cpu0 11923703 41 1087645 515738386 2847 210938 279500 0 0 0
cpu1 20057222 15 291565 508745688 604 141915 104390 0 0 0
cpu2 6874750 48 1123000 520663867 1549 199736 75274 0 0 0
cpu3 24848059 22 188129 504152959 194 147783 12873 0 0 0
cpu4 18860653 37 1106445 509009974 982 229940 43436 0 0 0
cpu5 13826650 14 284263 515088772 176 114049 28281 0 0 0
cpu6 19189568 40 1127859 508655894 1034 232963 43336 0 0 0
cpu7 13439630 18 266737 515512900 199 110400 15170 0 0 0
cpu8 18788455 72 1130359 509053843 1080 232841 43926 0 0 0
cpu9 13703016 11 264410 515252721 182 111011 14882 0 0 0
cpu10 18793791 72 1149681 509029792 844 232674 44180 0 0 0
cpu11 13555453 8 248708 515426107 144 108081 12689 0 0 0
cpu12 18626882 78 1175226 509185741 893 233971 43553 0 0 0
cpu13 13681432 11 240006 515307709 137 107740 12482 0 0 0
cpu14 19112477 62 1147839 508693634 938 234448 56686 0 0 0
cpu15 12949897 33 256839 516014810 3433 106331 17795 0 0 0
intr 7392500979 0 9 0 0 0 0 0 0 0 0 529359 29 15 0 0 0 0 0 0 0 0 0 0 0 0 5659358 0 22 0 55474 0 9004793 4035290 8719267 4140525 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
ctxt 5334917357
btime 1755739399
processes 64962801
procs_running 3
procs_blocked 0
softirq 2896749124 1 202731472 1 59350683 5590608 0 135535 1076902277 1 1552038546)"
        }));

    EXPECT_CALL(*tools.get(), Read("/proc/1/stat"))
    .WillOnce(testing::Return(litebus::Option<std::string>{
            R"(1 (monitor) S 3142022 3142022 2132682 34817 2132682 4194368 483 0 0 0 16072344 16340 0 0 20 0 2 0 470263721 109170688 2385 18446744073709551615 94672056987648 94672056987989 140733336937424 0 0 0 0 16781313 2 0 0 0 17 4 0 0 0 0 0 94672056999304 94672056999952 94672087425024 140733336942591 140733336942615 140733336942615 140733336944615 0)"
        }))
    .WillOnce(testing::Return(litebus::Option<std::string>{
            R"(1 (monitor) S 3142022 3142022 2132682 34817 2132682 4194368 483 0 0 0 16072345 16340 0 0 20 0 2 0 470263721 109170688 2385 18446744073709551615 94672056987648 94672056987989 140733336937424 0 0 0 0 16781313 2 0 0 0 17 4 0 0 0 0 0 94672056999304 94672056999952 94672087425024 140733336942591 140733336942615 140733336942615 140733336944615 0)"
        }));

    auto collector = std::make_shared<runtime_manager::InstanceCPUCollector>(1, "id", 1000.0, "urn", tools);
    auto usage = collector->GetUsage().Get();
    YRLOG_DEBUG("usage.value is {}", usage.value.Get());
    EXPECT_NEAR(usage.value.Get(), 0.05, 0.09);
    EXPECT_EQ(usage.instanceID.Get(), "id");
}

/**
 * Feature: InstanceCPUCollector
 * Description: Get Usage
 * Steps: read file is empty
 * Expectation:
 * {}
 */
#if 0
TEST_F(InstanceCPUCollectorTest, GetUsageWithEmptyContent)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(::testing::Return(litebus::Option<std::string>{}));

    auto collector = std::make_shared<runtime_manager::InstanceCPUCollector>(1, "id", 1000.0, "urn", tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.IsNone(), true);
    EXPECT_EQ(usage.instanceID.Get(), "id");
}

/**
 * Feature: InstanceCPUCollector
 * Description: Get Usage
 * Steps: read file content is not correct
 * Expectation:
 * {}
 */
TEST_F(InstanceCPUCollectorTest, GetUsageWithInvaldContent)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(::testing::Return(litebus::Option<std::string>{"1"}));

    auto collector = std::make_shared<runtime_manager::InstanceCPUCollector>(1, "id", 1000.0, "urn", tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.IsNone(), true);
    EXPECT_EQ(usage.instanceID.Get(), "id");
}
#endif
}