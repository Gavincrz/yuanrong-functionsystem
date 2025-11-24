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

#include "mocks/mock_cmdtool.h"
#include "common/utils/files.h"
#include "utils/future_test_helper.h"
#include "runtime_manager/metrics/collector/heterogeneous_collector/gpu_probe.h"
#include "runtime_manager/metrics/collector/heterogeneous_collector/npu_probe.h"
#include "runtime_manager/metrics/collector/heterogeneous_collector/topo_info.h"

#define private public
#include "runtime_manager/metrics/collector/instance_xpu_collector.h"

using ::testing::MatchesRegex;

namespace functionsystem::test {
class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

// npu-smi info 910B
const std::string npuSmiInfo910B = R"(
+------------------------------------------------------------------------------------------------+
| npu-smi 25.2.0                   Version: 25.2.0                                               |
+---------------------------+---------------+----------------------------------------------------+
| NPU   Name                | Health        | Power(W)    Temp(C)           Hugepages-Usage(page)|
| Chip                      | Bus-Id        | AICore(%)   Memory-Usage(MB)  HBM-Usage(MB)        |
+===========================+===============+====================================================+
| 0     910B4               | OK            | 84.7        38                0    / 0             |
| 0                         | 0000:C1:00.0  | 0           0    / 0          2894 / 32768         |
+===========================+===============+====================================================+
| 1     910B4               | OK            | 88.9        39                0    / 0             |
| 0                         | 0000:01:00.0  | 0           0    / 0          2653 / 32768         |
+===========================+===============+====================================================+
| 2     910B4               | OK            | 85.3        38                0    / 0             |
| 0                         | 0000:C2:00.0  | 0           0    / 0          2836 / 32768         |
+===========================+===============+====================================================+
| 3     910B4               | OK            | 88.7        40                0    / 0             |
| 0                         | 0000:02:00.0  | 0           0    / 0          2842 / 32768         |
+===========================+===============+====================================================+
| 4     910B4               | OK            | 86.0        39                0    / 0             |
| 0                         | 0000:81:00.0  | 0           0    / 0          2653 / 32768         |
+===========================+===============+====================================================+
| 5     910B4               | OK            | 90.0        40                0    / 0             |
| 0                         | 0000:41:00.0  | 0           0    / 0          2836 / 32768         |
+===========================+===============+====================================================+
| 6     910B4               | OK            | 88.6        39                0    / 0             |
| 0                         | 0000:82:00.0  | 0           0    / 0          2659 / 32768         |
+===========================+===============+====================================================+
| 7     910B4               | OK            | 84.9        40                0    / 0             |
| 0                         | 0000:42:00.0  | 0           0    / 0          2659 / 32768         |
+===========================+===============+====================================================+
+---------------------------+---------------+----------------------------------------------------+
| NPU     Chip              | Process id    | Process name             | Process memory(MB)      |
+===========================+===============+====================================================+
| 0       0                 | 472206        | python3.9                | 113                     |
+===========================+===============+====================================================+
| No running processes found in NPU 1                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 2                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 3                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 4                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 5                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 6                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 7                                                            |
+===========================+===============+====================================================+
)";

std::vector<std::string> npuinfoStrToVector(const std::string &input)
{
    std::vector<std::string> vec;
    std::istringstream stream(input);
    std::string line;
    while (std::getline(stream, line)) {
        vec.push_back(line);
    }
    return vec;
}

class InstanceXpuCollectorTest : public ::testing::Test {};

TEST_F(InstanceXpuCollectorTest, InstanceNPUCollectorGetUsage)
{
    auto tool = std::make_shared<MockProcFSTools>();
    auto cmdTool = std::make_shared<MockCmdTools>();
    auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
    params->ldLibraryPath = "";
    params->deviceInfoPath = "/home/sn/config/topology-info.json";
    params->collectMode = runtime_manager::NPU_COLLECT_HBM;
    auto probe = std::make_shared<runtime_manager::NpuProbe>("co200", tool, cmdTool, params);
    EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillRepeatedly(testing::Return(npuinfoStrToVector(npuSmiInfo910B)));
    EXPECT_CALL(*cmdTool.get(), GetCmdResult("pip3 list")).WillRepeatedly(testing::Return(npuinfoStrToVector("")));

    auto deployDir = "/home/sn/function/package/xxxz";
    auto startReq = std::make_shared<messages::StartInstanceRequest>();
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_userenvs()->insert({"func-NPU-DEVICE-IDS", "0"});
    runtime_manager::InstInfoWithXPU info = {472206, "instanceID1", deployDir, startReq->runtimeinstanceinfo(), "co200"};

    auto instNpuCollector =
            std::make_shared<runtime_manager::InstanceXPUCollector>(info, runtime_manager::metrics_type::NPU, tool, params);

    instNpuCollector->probe_ = probe;
    ASSERT_EQ(instNpuCollector->logicIDs_.size(), 1);
    litebus::Future<runtime_manager::Metric> future = instNpuCollector->GetUsage();
    runtime_manager::Metric metric = future.Get();
    ASSERT_EQ(metric.value, 8.0);
    runtime_manager::DevClusterMetrics devClusterMetrics = metric.devClusterMetrics.Get();

    std::vector<int> expectUseIDs = std::vector<int>{ 0 };
    std::vector<int> expectRealIDs = std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 };
    std::vector<int> expectUseHBM = std::vector<int>{ 2894, 2653, 2836, 2842, 2653, 2836, 2659, 2659 };

    auto usedIDs = devClusterMetrics.intsInfo.at(resource_view::USED_IDS_KEY);
    auto ids = devClusterMetrics.intsInfo.at(resource_view::IDS_KEY);
    auto usedHBM = devClusterMetrics.intsInfo.at(runtime_manager::dev_metrics_type::USED_HBM_KEY);

    for (size_t i = 0; i < expectRealIDs.size(); i++) {
        ASSERT_EQ(expectRealIDs[i], ids[i]);
        ASSERT_EQ(expectUseHBM[i], usedHBM[i]);
    }
    ASSERT_EQ(expectUseIDs[0], usedIDs[0]);
}

}
