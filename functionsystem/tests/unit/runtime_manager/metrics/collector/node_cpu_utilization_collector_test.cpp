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
#include "runtime_manager/metrics/collector/node_cpu_utilization_collector.h"

namespace functionsystem::test {
class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class NodeCPUUtilizationCollectorTest : public ::testing::Test {};


TEST_F(NodeCPUUtilizationCollectorTest, GetUsage)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{
            R"(cpu  75516619 39 2153191 297728831 6311 1721359 360091 0 0 0
cpu0 3384272 0 139385 19797097 846 121505 124819 0 0 0
cpu1 5836240 0 66362 17585075 313 81257 41791 0 0 0
cpu2 7357656 1 137150 15894497 332 150990 17509 0 0 0
cpu3 1889326 10 137052 21499020 355 67661 17826 0 0 0
cpu4 5454478 0 165505 17792773 502 141432 16498 0 0 0
cpu5 4092877 0 106571 19330296 253 78039 11926 0 0 0
cpu6 5298449 11 170382 17943279 389 142069 15950 0 0 0
cpu7 4249886 11 105856 19175645 210 77997 9827 0 0 0
cpu8 5217513 0 171257 18024660 327 141772 15927 0 0 0
cpu9 4332041 0 106459 19092948 211 78630 9521 0 0 0
cpu10 5223854 0 172906 18016599 431 140731 15875 0 0 0
cpu11 4275180 0 107497 19149022 195 78247 9279 0 0 0
cpu12 5060223 0 176147 18179345 345 138770 15940 0 0 0
cpu13 4445408 0 104966 18981743 145 77514 9138 0 0 0
cpu14 5260870 0 174289 17980399 338 134536 17333 0 0 0
cpu15 4138338 0 111400 19286425 1111 70202 10926 0 0 0
intr 1290728296 0 9 0 0 0 0 0 0 0 0 23784 31 15 0 0 0 0 0 0 0 0 0 0 0 0 306095 0 1 0 1908 0 744513 264294 534304 255984 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
ctxt 578202814
btime 1756458064
processes 2910938
procs_running 1
procs_blocked 0
softirq 453474464 2 21784575 1 4296442 291444 0 6370 180610881 0 246484749)"
        })).WillOnce(
        testing::Return(litebus::Option<std::string>{
            R"(cpu  75518250 39 2153238 297731563 6311 1721402 360098 0 0 0
cpu0 3384365 0 139387 19797282 846 121506 124820 0 0 0
cpu1 5836387 0 66363 17585198 313 81258 41792 0 0 0
cpu2 7357686 1 137153 15894746 332 150991 17510 0 0 0
cpu3 1889450 10 137055 21499160 355 67665 17827 0 0 0
cpu4 5454640 0 165506 17792878 502 141442 16498 0 0 0
cpu5 4092884 0 106579 19330557 253 78040 11926 0 0 0
cpu6 5298599 11 170383 17943400 389 142073 15950 0 0 0
cpu7 4250027 11 105862 19175778 210 77998 9827 0 0 0
cpu8 5217689 0 171258 18024765 327 141773 15927 0 0 0
cpu9 4332054 0 106460 19093212 211 78632 9521 0 0 0
cpu10 5224003 0 172909 18016718 431 140735 15876 0 0 0
cpu11 4275193 0 107501 19149285 195 78248 9279 0 0 0
cpu12 5060335 0 176150 18179508 345 138771 15940 0 0 0
cpu13 4445541 0 104967 18981888 145 77517 9139 0 0 0
cpu14 5261024 0 174292 17980517 338 134542 17334 0 0 0
cpu15 4138365 0 111405 19286664 1111 70203 10926 0 0 0
intr 1290752326 0 9 0 0 0 0 0 0 0 0 23784 31 15 0 0 0 0 0 0 0 0 0 0 0 0 306099 0 1 0 1908 0 744582 264388 534349 256064 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
ctxt 578209657
btime 1756458064
processes 2910988
procs_running 9
procs_blocked 0
softirq 453483073 2 21784959 1 4296888 291448 0 6383 180613657 0 246489735)"}));

    auto collector = std::make_shared<runtime_manager::NodeCPUUtilizationCollector>(tools);
    auto metric = collector->GetUsage().Get();
    YRLOG_INFO("result is {}", metric.value.Get());
    EXPECT_NEAR(metric.value.Get(), 38.74, 0.9); // Solving the precision issue in floating-point number comparison
}
}

