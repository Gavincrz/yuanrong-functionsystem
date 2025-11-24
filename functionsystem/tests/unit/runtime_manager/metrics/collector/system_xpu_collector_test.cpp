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

#include "runtime_manager/metrics/collector/system_xpu_collector.h"

#include <gmock/gmock-actions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks/mock_cmdtool.h"
#include "common/utils/files.h"
#include "utils/future_test_helper.h"
#include "runtime_manager/metrics/collector/heterogeneous_collector/gpu_probe.h"
#include "runtime_manager/metrics/collector/heterogeneous_collector/npu_probe.h"
#include "runtime_manager/metrics/collector/heterogeneous_collector/topo_info.h"
#include "common/utils/path.h"

using ::testing::MatchesRegex;

namespace functionsystem::test {

class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

const std::string json = "{\"co200\": {\"nodeName\": \"co200\", \"number\": 6, \"vDeviceIDs\": [1, 2, 3, 4, 5, 6], \"vDevicePartition\": [\"npu1\", \"npu2\", \"npu3\", \"null\", \"null\", \"npu4\", \"npu5\", \"npu6\"]}, \"co150\": {\"nodeName\": \"co150\", \"number\": 5, \"vDeviceIDs\": [1, 4, 5, 6, 7], \"vDevicePartition\": [\"npu1\", \"null\", \"null\", \"npu4\", \"npu5\", \"npu6\", \"npu7\"]}}";
const std::string fourCards = "{\"co201\": {\"nodeName\": \"co201\", \"number\": 4, \"vDeviceIDs\": [0, 1, 2, 3], \"vDevicePartition\": [\"npu0\", \"npu1\", \"npu2\", \"npu3\"]}}";

const std::vector<std::string> topoInfo{
    "        NPU0    NPU1    NPU2    NPU3  CPU Affinity",
    "NPU0     X      PXB     SYS     PXB   144-167",
    "NPU1    PXB      X      PXB     SYS   96-119",
    "NPU2    SYS     PXB      X      PXB   48-71",
    "NPU3    PXB     SYS     PXB      X    0-23",
    "",
    "Legend:",
    "",
    "  X    = Self"
};

const std::string npuSminTopoInfo = R"(	   NPU0       NPU1       NPU2       NPU3       NPU4       NPU5       NPU6       NPU7       CPU Affinity
NPU0       X          HCCS       HCCS       HCCS       HCCS       HCCS       HCCS       HCCS       144-167
NPU1       HCCS       X          HCCS       HCCS       HCCS       HCCS       HCCS       HCCS       0-23
NPU2       HCCS       HCCS       X          HCCS       HCCS       HCCS       HCCS       HCCS       144-167
NPU3       HCCS       HCCS       HCCS       X          HCCS       HCCS       HCCS       HCCS       0-23
NPU4       HCCS       HCCS       HCCS       HCCS       X          HCCS       HCCS       HCCS       96-119
NPU5       HCCS       HCCS       HCCS       HCCS       HCCS       X          HCCS       HCCS       48-71
NPU6       HCCS       HCCS       HCCS       HCCS       HCCS       HCCS       X          HCCS       96-119
NPU7       HCCS       HCCS       HCCS       HCCS       HCCS       HCCS       HCCS       X          48-71

Legend:

  X    = Self
  SYS  = Path traversing PCIe and NUMA nodes. Nodes are connected through SMP, such as QPI, UPI.
  PHB  = Path traversing PCIe and the PCIe host bridge of a CPU.
  PIX  = Path traversing a single PCIe switch
  PXB  = Path traversing multipul PCIe switches
  HCCS = Connection traversing HCCS.
  NA   = Unknown relationship.)";


const std::vector<std::string> topoInfoNotSupport{
    "This device does not support querying topo."
};

const std::vector<std::string> PIP_LIST_INFO {
    "bash docker_build.sh -m runtime-manager -u 1003 -s 1002 -v 041801",
    "Package                                Version",
    "backoff                                2.2.1",
};

const std::vector<std::string> NPU_SMI_INFO{
    "+-------------------------------------------------------------------------------------------+",
    "| npu-smi 23.0.rc1.b070            Version: 23.0.rc1.b070                                   |",
    "+----------------------+---------------+----------------------------------------------------+",
    "| NPU   Name           | Health        | Power(W)    Temp(C)           Hugepages-Usage(page)|",
    "| Chip                 | Bus-Id        | AICore(%)   Memory-Usage(MB)  HBM-Usage(MB)        |",
    "+======================+===============+====================================================+",
    "| 0     910A           | Warning       | 72.0        39                0    / 0             |",
    "| 0                    | 0000:C1:00.0  | 0           938  / 15137      3    / 32768         |",
    "+======================+===============+====================================================+",
    "| 1     910A           | Warning       | 70.2        37                0    / 0             |",
    "| 0                    | 0000:81:00.0  | 0           1820 / 15137      3    / 32768         |",
    "+======================+===============+====================================================+",
    "| 2     910A           | Warning       | 70.8        37                0    / 0             |",
    "| 0                    | 0000:41:00.0  | 0           1667 / 15137      30750/ 32768         |",
    "+======================+===============+====================================================+",
    "| 3     910A           | Warning       | 68.8        39                0    / 0             |",
    "| 0                    | 0000:01:00.0  | 0           2777 / 15039      0    / 32768         |",
    "+======================+===============+====================================================+",
    "+----------------------+---------------+----------------------------------------------------+",
    "| NPU     Chip         | Process id    | Process name             | Process memory(B)       |",
    "+======================+===============+====================================================+",
    "| No running processes found in NPU 0                                                       |",
    "+======================+===============+====================================================+"
};

const std::vector<std::string> WRONG_NPU_SMI_INFO{
    "+-------------------------------------------------------------------------------------------+",
    "| npu-smi 23.0.rc1.b070            Version: 23.0.rc1.b070                                   |",
    "+----------------------+---------------+----------------------------------------------------+",
    "| NPU   Name           | Health        | Power(W)    Temp(C)           Hugepages-Usage(page)|",
    "| Chip                 | Bus-Id        | AICore(%)   Memory-Usage(MB)  HBM-Usage(MB)        |",
    "+======================+===============+====================================================+",
    "| 0     910A           | Warning       | 72.0        39                0    / 0             |",
    "| 0                    | 0000:C1:00.0  | 0           938  / 15137      3    / 32768         |",
    "+======================+===============+====================================================+",
    "+----------------------+---------------+----------------------------------------------------+",
    "| NPU     Chip         | Process id    | Process name             | Process memory(B)       |",
    "+======================+===============+====================================================+",
    "| No running processes found in NPU 0                                                       |",
    "+======================+===============+====================================================+"
};

const std::vector<std::string> wrongNpuMem{
    "+-------------------------------------------------------------------------------------------+",
    "| npu-smi 23.0.rc1.b070            Version: 23.0.rc1.b070                                   |",
    "+----------------------+---------------+----------------------------------------------------+",
    "| NPU   Name           | Health        | Power(W)    Temp(C)           Hugepages-Usage(page)|",
    "| Chip                 | Bus-Id        | AICore(%)   Memory-Usage(MB)  HBM-Usage(MB)        |",
    "+======================+===============+====================================================+",
    "| 0     910A           | Warning       | 72.0",
    "| 0                    | 0000:C1:00.0  | 0",
    "+======================+===============+====================================================+",
    "| 1     910A           | Warning       | 70.2",
    "| 0                    | 0000:81:00.0  | 0",
    "+======================+===============+====================================================+",
    "| 2     910A           | Warning       | 70.8",
    "| 0                    | 0000:41:00.0  | 0",
    "+======================+===============+====================================================+",
    "| 3     910A           | Warning       | 68.8",
    "| 0                    | 0000:01:00.0  | 0",
    "+======================+===============+====================================================+",
    "+----------------------+---------------+----------------------------------------------------+",
    "| NPU     Chip         | Process id    | Process name             | Process memory(B)       |",
    "+======================+===============+====================================================+",
    "| No running processes found in NPU 0                                                       |",
    "+======================+===============+====================================================+"
};

const std::vector<std::string> gpuOrUnitInfo{
    "==============NVSMI LOG==============",
    "",
    "Timestamp                                 : Mon Mar 31 10:11:18 2025",
    "Driver Version                            : 535.154.05",
    "CUDA Version                              : 12.2",
    "",
    "Attached GPUs                             : 1",
    "GPU 00000000:04:00.0",
    "    Product Name                          : NVIDIA GeForce RTX 3090",
    "    Product Brand                         : GeForce",
    "    Product Architecture                  : Ampere",
    "    Display Mode                          : Disabled",
    "    Display Active                        : Disabled",
    "    Persistence Mode                      : Disabled",
    "    Addressing Mode                       : None",
    "    MIG Mode",
    "        Current                           : N/A",
    "        Pending                           : N/A",
    "    Accounting Mode                       : Disabled",
    "    Accounting Mode Buffer Size           : 4000",
    "    Driver Model",
    "        Current                           : N/A",
    "        Pending                           : N/A",
    "    Serial Number                         : N/A",
    "    GPU UUID                              : GPU-6b1d0869-fb77-7f91-fcea-007340e02271",
    "    Minor Number                          : 0",
    "    VBIOS Version                         : 94.02.26.88.08",
    "    MultiGPU Board                        : No",
    "    Board ID                              : 0x400",
    "    Board Part Number                     : N/A",
    "    GPU Part Number                       : 2204-300-A1",
    "    FRU Part Number                       : N/A",
    "    Module ID                             : 1",
    "    Inforom Version",
    "        Image Version                     : G001.0000.03.03",
    "        OEM Object                        : 2.0",
    "        ECC Object                        : N/A",
    "        Power Management Object           : N/A",
    "    Inforom BBX Object Flush",
    "        Latest Timestamp                  : N/A",
    "        Latest Duration                   : N/A",
    "    GPU Operation Mode",
    "        Current                           : N/A",
    "        Pending                           : N/A",
    "    GSP Firmware Version                  : N/A",
    "    GPU Virtualization Mode",
    "        Virtualization Mode               : None",
    "        Host VGPU Mode                    : N/A",
    "    GPU Reset Status",
    "        Reset Required                    : No",
    "        Drain and Reset Recommended       : N/A",
    "    IBMNPU",
    "        Relaxed Ordering Mode             : N/A",
    "    PCI",
    "        Bus                               : 0x04",
    "        Device                            : 0x00",
    "        Domain                            : 0x0000",
    "        Device Id                         : 0x220410DE",
    "        Bus Id                            : 00000000:04:00.0",
    "        Sub System Id                     : 0x00007377",
    "        GPU Link Info",
    "            PCIe Generation",
    "                Max                       : 3",
    "                Current                   : 3",
    "                Device Current            : 3",
    "                Device Max                : 4",
    "                Host Max                  : 3",
    "            Link Width",
    "                Max                       : 16x",
    "                Current                   : 16x",
    "        Bridge Chip",
    "            Type                          : N/A",
    "            Firmware                      : N/A",
    "        Replays Since Reset               : 0",
    "        Replay Number Rollovers           : 0",
    "        Tx Throughput                     : 0 KB/s",
    "        Rx Throughput                     : 0 KB/s",
    "        Atomic Caps Inbound               : N/A",
    "        Atomic Caps Outbound              : N/A",
    "    Fan Speed                             : 30 %",
    "    Performance State                     : P0",
    "    Clocks Event Reasons",
    "        Idle                              : Active",
    "        Applications Clocks Setting       : Not Active",
    "        SW Power Cap                      : Not Active",
    "        HW Slowdown                       : Not Active",
    "            HW Thermal Slowdown           : Not Active",
    "            HW Power Brake Slowdown       : Not Active",
    "        Sync Boost                        : Not Active",
    "        SW Thermal Slowdown               : Not Active",
    "        Display Clock Setting             : Not Active",
    "    FB Memory Usage",
    "        Total                             : 24576 MiB",
    "        Reserved                          : 316 MiB",
    "        Used                              : 0 MiB",
    "        Free                              : 24259 MiB",
    "    BAR1 Memory Usage",
    "        Total                             : 256 MiB",
    "        Used                              : 1 MiB",
    "        Free                              : 255 MiB",
    "    Conf Compute Protected Memory Usage",
    "        Total                             : 0 MiB",
    "        Used                              : 0 MiB",
    "        Free                              : 0 MiB",
    "    Compute Mode                          : Default",
    "    Utilization",
    "        Gpu                               : 2 %",
    "        Memory                            : 0 %",
    "        Encoder                           : 0 %",
    "        Decoder                           : 0 %",
    "        JPEG                              : 0 %",
    "        OFA                               : 0 %",
    "    Encoder Stats",
    "        Active Sessions                   : 0",
    "        Average FPS                       : 0",
    "        Average Latency                   : 0",
    "    FBC Stats",
    "        Active Sessions                   : 0",
    "        Average FPS                       : 0",
    "        Average Latency                   : 0",
    "    ECC Mode",
    "        Current                           : N/A",
    "        Pending                           : N/A",
    "    ECC Errors",
    "        Volatile",
    "            SRAM Correctable              : N/A",
    "            SRAM Uncorrectable            : N/A",
    "            DRAM Correctable              : N/A",
    "            DRAM Uncorrectable            : N/A",
    "        Aggregate",
    "            SRAM Correctable              : N/A",
    "            SRAM Uncorrectable            : N/A",
    "            DRAM Correctable              : N/A",
    "            DRAM Uncorrectable            : N/A",
    "    Retired Pages",
    "        Single Bit ECC                    : N/A",
    "        Double Bit ECC                    : N/A",
    "        Pending Page Blacklist            : N/A",
    "    Remapped Rows                         : N/A",
    "    Temperature",
    "        GPU Current Temp                  : 33 C",
    "        GPU T.Limit Temp                  : N/A",
    "        GPU Shutdown Temp                 : 98 C",
    "        GPU Slowdown Temp                 : 95 C",
    "        GPU Max Operating Temp            : 93 C",
    "        GPU Target Temperature            : 83 C",
    "        Memory Current Temp               : N/A",
    "        Memory Max Operating Temp         : N/A",
    "    GPU Power Readings",
    "        Power Draw                        : 99.31 W",
    "        Current Power Limit               : 350.00 W",
    "        Requested Power Limit             : 350.00 W",
    "        Default Power Limit               : 350.00 W",
    "        Min Power Limit                   : 100.00 W",
    "        Max Power Limit                   : 350.00 W",
    "    Module Power Readings",
    "        Power Draw                        : N/A",
    "        Current Power Limit               : N/A",
    "        Requested Power Limit             : N/A",
    "        Default Power Limit               : N/A",
    "        Min Power Limit                   : N/A",
    "        Max Power Limit                   : N/A",
    "    Clocks",
    "        Graphics                          : 1695 MHz",
    "        SM                                : 1695 MHz",
    "        Memory                            : 9751 MHz",
    "        Video                             : 1515 MHz",
    "    Applications Clocks",
    "        Graphics                          : N/A",
    "        Memory                            : N/A",
    "    Default Applications Clocks",
    "        Graphics                          : N/A",
    "        Memory                            : N/A",
    "    Deferred Clocks",
    "        Memory                            : N/A",
    "    Max Clocks",
    "        Graphics                          : 2100 MHz",
    "        SM                                : 2100 MHz",
    "        Memory                            : 9751 MHz",
    "        Video                             : 1950 MHz",
    "    Max Customer Boost Clocks",
    "        Graphics                          : N/A",
    "    Clock Policy",
    "        Auto Boost                        : N/A",
    "        Auto Boost Default                : N/A",
    "    Voltage",
    "        Graphics                          : 812.500 mV",
    "    Fabric",
    "        State                             : N/A",
    "        Status                            : N/A",
    "    Processes                             : None",
};

const std::vector<std::string> gpuInfo {
    "GPU 0: Tesla V100-PCIE-16GB (UUID: GPU-70051dd3-070d-24b9-366f-111f5ef475bc)",
};

const std::vector<std::string> gpuTopoInfo {
    "GPU0    CPU Affinity    NUMA Affinity",
    "GPU0     X      0-5,12-17       0",
    "",
    "Legend:",
    "",
    "X    = Self"
};

// nvidia-smi -q
const std::vector<std::string> gpuMemoryInfo{
    "Wed Aug  9 15:02:09 2023",
    "|-----------------------------------------+----------------------+----------------------+",
    "| NVIDIA-SMI 535.154.05             Driver Version: 535.154.05   CUDA Version: 12.2     |",
    "|-----------------------------------------+----------------------+----------------------+",
    "| GPU  Name                 Persistence-M | Bus-Id        Disp.A | Volatile Uncorr. ECC |",
    "| Fan  Temp   Perf          Pwr:Usage/Cap |         Memory-Usage | GPU-Util  Compute M. |",
    "|                                         |                      |               MIG M. |",
    "|=========================================+======================+======================|",
    "|   0  NVIDIA GeForce RTX 3090        Off | 00000000:04:00.0 Off |                  N/A |",
    "| 30%   32C    P0              91W / 350W |      20MiB / 24576MiB |      0%      Default |",
    "|                                |                      |                  N/A |",
    "+--------------------------------+----------------------+----------------------+",
    "",
    "+-----------------------------------------------------------------------------+",
    "| Processes:                                                                  |",
    "|  GPU   GI   CI        PID   Type   Process name                  GPU Memory |",
    "|        ID   ID                                                   Usage      |"
};

// npu-smi info 910B
const std::string npuSmiInfo910B = R"(
+------------------------------------------------------------------------------------------------+
| npu-smi 24.1.rc3                 Version: 24.1.rc3                                             |
+---------------------------+---------------+----------------------------------------------------+
| NPU   Name                | Health        | Power(W)    Temp(C)           Hugepages-Usage(page)|
| Chip                      | Bus-Id        | AICore(%)   Memory-Usage(MB)  HBM-Usage(MB)        |
+===========================+===============+====================================================+
| 0     910B4               | OK            | 85.0        36                0    / 0             |
| 0                         | 0000:C1:00.0  | 0           0    / 0          22283/ 32768         |
+===========================+===============+====================================================+
| 1     910B4               | OK            | 84.2        36                0    / 0             |
| 0                         | 0000:01:00.0  | 0           0    / 0          22267/ 32768         |
+===========================+===============+====================================================+
| 2     910B4               | OK            | 81.8        35                0    / 0             |
| 0                         | 0000:C2:00.0  | 0           0    / 0          2818 / 32768         |
+===========================+===============+====================================================+
| 3     910B4               | OK            | 83.9        36                0    / 0             |
| 0                         | 0000:02:00.0  | 0           0    / 0          2819 / 32768         |
+===========================+===============+====================================================+
| 4     910B4               | OK            | 81.4        35                0    / 0             |
| 0                         | 0000:81:00.0  | 0           0    / 0          2829 / 32768         |
+===========================+===============+====================================================+
| 5     910B4               | OK            | 81.5        37                0    / 0             |
| 0                         | 0000:41:00.0  | 0           0    / 0          2829 / 32768         |
+===========================+===============+====================================================+
| 6     910B4               | OK            | 263.2       46                0    / 0             |
| 0                         | 0000:82:00.0  | 82          0    / 0          30759/ 32768         |
+===========================+===============+====================================================+
| 7     910B4               | OK            | 250.5       47                0    / 0             |
| 0                         | 0000:42:00.0  | 68          0    / 0          30760/ 32768         |
+===========================+===============+====================================================+
+---------------------------+---------------+----------------------------------------------------+
| NPU     Chip              | Process id    | Process name             | Process memory(MB)      |
+===========================+===============+====================================================+
| 0       0                 | 582939        |                          | 19502                   |
+===========================+===============+====================================================+
| 1       0                 | 695171        |                          | 19498                   |
+===========================+===============+====================================================+
| No running processes found in NPU 2                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 3                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 4                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 5                                                            |
+===========================+===============+====================================================+
| 6       0                 | 99910         |                          | 27977                   |
+===========================+===============+====================================================+
| 7       0                 | 99976         |                          | 27977                   |
+===========================+===============+====================================================+
)";

// npu-smi info 910C
const std::string npuSmiInfo910C = R"(
+------------------------------------------------------------------------------------------------+
| npu-smi 24.1.rc3.3               Version: 24.1.rc3.3                                           |
+---------------------------+---------------+----------------------------------------------------+
| NPU   Name                | Health        | Power(W)    Temp(C)           Hugepages-Usage(page)|
| Chip  Phy-ID              | Bus-Id        | AICore(%)   Memory-Usage(MB)  HBM-Usage(MB)        |
+===========================+===============+====================================================+
| 0     Ascend910           | OK            | 182.0       36                0    / 0             |
| 0     0                   | 0000:9D:00.0  | 0           0    / 0          3402 / 65536         |
+------------------------------------------------------------------------------------------------+
| 0     Ascend910           | OK            | -           35                0    / 0             |
| 1     1                   | 0000:9F:00.0  | 0           0    / 0          3200 / 65536         |
+===========================+===============+====================================================+
| 1     Ascend910           | OK            | 181.0       35                0    / 0             |
| 0     2                   | 0000:99:00.0  | 0           0    / 0          3396 / 65536         |
+------------------------------------------------------------------------------------------------+
| 1     Ascend910           | OK            | -           36                0    / 0             |
| 1     3                   | 0000:9B:00.0  | 0           0    / 0          3205 / 65536         |
+===========================+===============+====================================================+
| 2     Ascend910           | OK            | 176.9       34                0    / 0             |
| 0     4                   | 0000:95:00.0  | 0           0    / 0          3395 / 65536         |
+------------------------------------------------------------------------------------------------+
| 2     Ascend910           | OK            | -           34                0    / 0             |
| 1     5                   | 0000:97:00.0  | 0           0    / 0          3203 / 65536         |
+===========================+===============+====================================================+
| 3     Ascend910           | OK            | 181.2       36                0    / 0             |
| 0     6                   | 0000:91:00.0  | 0           0    / 0          3395 / 65536         |
+------------------------------------------------------------------------------------------------+
| 3     Ascend910           | OK            | -           35                0    / 0             |
| 1     7                   | 0000:93:00.0  | 0           0    / 0          3203 / 65536         |
+===========================+===============+====================================================+
| 4     Ascend910           | OK            | 179.8       36                0    / 0             |
| 0     8                   | 0000:8D:00.0  | 0           0    / 0          52553/ 65536         |
+------------------------------------------------------------------------------------------------+
| 4     Ascend910           | OK            | -           37                0    / 0             |
| 1     9                   | 0000:8F:00.0  | 0           0    / 0          52358/ 65536         |
+===========================+===============+====================================================+
| 5     Ascend910           | OK            | 183.8       37                0    / 0             |
| 0     10                  | 0000:89:00.0  | 0           0    / 0          52567/ 65536         |
+------------------------------------------------------------------------------------------------+
| 5     Ascend910           | OK            | -           36                0    / 0             |
| 1     11                  | 0000:8B:00.0  | 0           0    / 0          52345/ 65536         |
+===========================+===============+====================================================+
| 6     Ascend910           | OK            | 185.9       37                0    / 0             |
| 0     12                  | 0000:85:00.0  | 0           0    / 0          52552/ 65536         |
+------------------------------------------------------------------------------------------------+
| 6     Ascend910           | OK            | -           36                0    / 0             |
| 1     13                  | 0000:87:00.0  | 0           0    / 0          52358/ 65536         |
+===========================+===============+====================================================+
| 7     Ascend910           | OK            | 182.1       34                0    / 0             |
| 0     14                  | 0000:81:00.0  | 0           0    / 0          52554/ 65536         |
+------------------------------------------------------------------------------------------------+
| 7     Ascend910           | OK            | -           36                0    / 0             |
| 1     15                  | 0000:83:00.0  | 0           0    / 0          52358/ 65536         |
+===========================+===============+====================================================+
+---------------------------+---------------+----------------------------------------------------+
| NPU     Chip              | Process id    | Process name             | Process memory(MB)      |
+===========================+===============+====================================================+
| No running processes found in NPU 0                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 1                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 2                                                            |
+===========================+===============+====================================================+
| No running processes found in NPU 3                                                            |
+===========================+===============+====================================================+
| 4       0                 | 957687        | pt_main_thread           | 49207                   |
| 4       1                 | 957770        | pt_main_thread           | 49207                   |
+===========================+===============+====================================================+
| 5       0                 | 957790        | pt_main_thread           | 49207                   |
| 5       1                 | 957852        | pt_main_thread           | 49207                   |
+===========================+===============+====================================================+
| 6       0                 | 957876        | pt_main_thread           | 49207                   |
| 6       1                 | 957912        | pt_main_thread           | 49207                   |
+===========================+===============+====================================================+
| 7       0                 | 957957        | pt_main_thread           | 49207                   |
| 7       1                 | 958011        | pt_main_thread           | 49207                   |
+===========================+===============+====================================================+
)";

const std::string npuSmiInfo310P3 = R"(
+--------------------------------------------------------------------------------------------------------+
| npu-smi 24.1.0.1                                 Version: 24.1.0.1                                     |
+-------------------------------+-----------------+------------------------------------------------------+
| NPU     Name                  | Health          | Power(W)     Temp(C)           Hugepages-Usage(page) |
| Chip    Device                | Bus-Id          | AICore(%)    Memory-Usage(MB)                        |
+===============================+=================+======================================================+
| 1536    310P3                 | OK              | NA           51                0     / 0             |
| 0       0                     | 0000:06:00.0    | 0            1596 / 44280                            |
+-------------------------------+-----------------+------------------------------------------------------+
| 1536    310P3                 | OK              | NA           50                0     / 0             |
| 1       1                     | 0000:06:00.0    | 0            1325 / 43693                            |
+===============================+=================+======================================================+
| 1792    310P3                 | OK              | NA           51                0     / 0             |
| 0       2                     | 0000:07:00.0    | 0            1452 / 44280                            |
+-------------------------------+-----------------+------------------------------------------------------+
| 1792    310P3                 | OK              | NA           51                0     / 0             |
| 1       3                     | 0000:07:00.0    | 0            1444 / 43693                            |
+===============================+=================+======================================================+
| 2048    310P3                 | OK              | NA           50                0     / 0             |
| 0       4                     | 0000:08:00.0    | 0            1353 / 44280                            |
+-------------------------------+-----------------+------------------------------------------------------+
| 2048    310P3                 | OK              | NA           49                0     / 0             |
| 1       5                     | 0000:08:00.0    | 0            1529 / 43693                            |
+===============================+=================+======================================================+
| 2304    310P3                 | OK              | NA           52                0     / 0             |
| 0       6                     | 0000:09:00.0    | 0            1656 / 44280                            |
+-------------------------------+-----------------+------------------------------------------------------+
| 2304    310P3                 | OK              | NA           50                0     / 0             |
| 1       7                     | 0000:09:00.0    | 0            1278 / 43693                            |
+===============================+=================+======================================================+
+-------------------------------+-----------------+------------------------------------------------------+
| NPU     Chip                  | Process id      | Process name             | Process memory(MB)        |
+===============================+=================+======================================================+
| No running processes found in NPU 1536                                                                 |
+===============================+=================+======================================================+
| No running processes found in NPU 1792                                                                 |
+===============================+=================+======================================================+
| No running processes found in NPU 2048                                                                 |
+===============================+=================+======================================================+
| No running processes found in NPU 2304                                                                 |
+===============================+=================+======================================================+
)";

// npu-smi info
const std::string wrongNpuSmiInfo1 = R"(
+===========================+===============+====================================================+
| 0     910B4               | OK            | 85.0        36                0    / 0             |
| 0                         | 0000:C1:00.0  | 0           0    / 0          22283/ 32768         |
+===========================+===============+====================================================+
| 0     910B4               | OK            | 85.0        36                0    / 0             |
)";

const std::string wrongNpuSmiInfo2 = R"(
| 0     Ascend910           | OK            | 182.0       36                0    / 0             |
| 0     0                   | 0000:9D:00.0  | 0           0    / 0          3402 / 65536         |
+------------------------------------------------------------------------------------------------+
| 0     Ascend910           | OK            | -           35                0    / 0             |
)";

const std::string wrongNpuSmiInfo3 = R"(
+===========================+===============+====================================================+
| 0     910B4               | OK            | 85.0        36                0    / 0             |
| 0                         | 0000:C1:00.0  | 0           0    / 0          22283/ 32768         |
+===========================+===============+====================================================+
| 0           910B4         | OK            | 85.0        36                0    / 0             |
| 0                         | 0000:C1:00.0  | 0           0    / 0                               |
+===========================+===============+====================================================+
)";


const std::string wrongNpuSmiInfo4 = R"(
+===========================+===============+====================================================+
| 0     910B4               | OK            | 85.0        36                0    / 0             |
| 0                         | 0000:C1:00.0  | 0           0    / 0          22283/ 32768         |
+===========================+===============+====================================================+
| 0     910B4               | OK            | 85.0        36                0    / 0             |
| 0                         | 0000:C1:00.0  | 0            4    / 5.s             30759/ 32768      |
+===========================+===============+====================================================+
)";

const std::string wrongNpuSmiInfo5 = R"(
| 1536    310P3                 | OK              | NA           51                0     / 0             |
| 0       0                     | 0000:06:00.0    | 0            1596 / 44280                            |
+-------------------------------+-----------------+------------------------------------------------------+
| 1536    310P3                 | OK              | NA           50                0     / 0             |
)";

// mock ls /dev | grep davinci  910C 1Chip2Npu
const std::string devDavinciInfo = R"(davinci0
davinci1
davinci10
davinci11
davinci12
davinci13
davinci14
davinci15
davinci2
davinci3
davinci4
davinci5
davinci6
davinci7
davinci8
davinci9
davinci_manager
)";

// /etc/hccn.conf
const std::string hccnConf = R"(address_0=127.0.0.123
netmask_0=255.255.0.0
netdetect_0=127.0.0.1
gateway_0=127.0.0.1
address_1=127.0.0.182
netmask_1=255.255.0.0
netdetect_1=127.0.0.1
gateway_1=127.0.0.1
address_2=127.0.0.116
netmask_2=255.255.0.0
netdetect_2=127.0.0.1
gateway_2=127.0.0.1
address_3=127.0.0.67
)";

// 8 NPU /etc/hccn.conf
const std::string hccn8NpuConf = R"(address_0=127.0.0.45
netmask_0=255.255.0.0
netdetect_0=127.0.0.1
gateway_0=127.0.0.1
send_arp_status_0=1
tls_enable_0=0
address_1=127.0.0.226
netmask_1=255.255.0.0
netdetect_1=127.0.0.1
gateway_1=127.0.0.1
send_arp_status_1=1
tls_enable_1=0
address_2=127.0.0.83
netmask_2=255.255.0.0
netdetect_2=127.0.0.1
gateway_2=127.0.0.1
send_arp_status_2=1
tls_enable_2=0
address_3=127.0.0.190
netmask_3=255.255.0.0
netdetect_3=127.0.0.1
gateway_3=127.0.0.1
send_arp_status_3=1
tls_enable_3=0
address_4=127.0.0.208
netmask_4=255.255.0.0
netdetect_4=127.0.0.1
gateway_4=127.0.0.1
send_arp_status_4=1
tls_enable_4=0
address_5=127.0.0.247
netmask_5=255.255.0.0
netdetect_5=127.0.0.1
gateway_5=127.0.0.1
send_arp_status_5=1
tls_enable_5=0
address_6=127.0.0.118
netmask_6=255.255.0.0
netdetect_6=127.0.0.1
gateway_6=127.0.0.1
send_arp_status_6=1
tls_enable_6=0
address_7=127.0.0.223
netmask_7=255.255.0.0
netdetect_7=127.0.0.1
gateway_7=127.0.0.1
send_arp_status_7=1
tls_enable_7=0
)";

// 16 NPU /etc/hccn.conf
const std::string hccn16NpuConf = R"(
address_0=127.0.0.24
netmask_0=255.255.128.0
gateway_0=127.0.0.1
arp_0=-i eth0 -s 127.0.0.114 bc:1e:85:d8:ca:dd
netdetect_0=127.0.0.1
send_arp_status_0=1
tls_enable_0=0
address_1=127.0.0.114
netmask_1=255.255.128.0
gateway_1=127.0.0.1
arp_1=-i eth1 -s 127.0.0.24 bc:1e:85:d8:ca:dc
netdetect_1=127.0.0.1
send_arp_status_1=1
tls_enable_1=0
address_2=127.0.0.217
netmask_2=255.255.128.0
gateway_2=127.0.0.1
arp_2=-i eth2 -s 127.0.0.70 d8:76:ae:d9:ba:ed
netdetect_2=127.0.0.1
send_arp_status_2=1
tls_enable_2=0
address_3=127.0.0.70
netmask_3=255.255.128.0
gateway_3=127.0.0.1
arp_3=-i eth3 -s 127.0.0.217 d8:76:ae:d9:ba:ec
netdetect_3=127.0.0.1
send_arp_status_3=1
tls_enable_3=0
address_4=127.0.0.136
netmask_4=255.255.128.0
gateway_4=127.0.0.1
arp_4=-i eth4 -s 127.0.0.93 78:dd:33:71:6e:df
netdetect_4=127.0.0.1
send_arp_status_4=1
tls_enable_4=0
address_5=127.0.0.93
netmask_5=255.255.128.0
gateway_5=127.0.0.1
arp_5=-i eth5 -s 127.0.0.136 78:dd:33:71:6e:de
netdetect_5=127.0.0.1
send_arp_status_5=1
tls_enable_5=0
address_6=127.0.0.131
netmask_6=255.255.128.0
gateway_6=127.0.0.1
arp_6=-i eth6 -s 127.0.0.179 d8:76:ae:76:bd:e9
netdetect_6=127.0.0.1
send_arp_status_6=1
tls_enable_6=0
address_7=127.0.0.179
netmask_7=255.255.128.0
gateway_7=127.0.0.1
arp_7=-i eth7 -s 127.0.0.131 d8:76:ae:76:bd:e8
netdetect_7=127.0.0.1
send_arp_status_7=1
tls_enable_7=0
address_8=127.0.0.180
netmask_8=255.255.128.0
gateway_8=127.0.0.1
arp_8=-i eth8 -s 127.0.0.168 bc:1e:85:d8:ca:a7
netdetect_8=127.0.0.1
send_arp_status_8=1
tls_enable_8=0
address_9=127.0.0.168
netmask_9=255.255.128.0
gateway_9=127.0.0.1
arp_9=-i eth9 -s 127.0.0.180 bc:1e:85:d8:ca:a6
netdetect_9=127.0.0.1
send_arp_status_9=1
tls_enable_9=0
address_10=127.0.0.82
netmask_10=255.255.128.0
gateway_10=127.0.0.1
arp_10=-i eth10 -s 127.0.0.128 d8:76:ae:d9:bb:39
netdetect_10=127.0.0.1
send_arp_status_10=1
tls_enable_10=0
address_11=127.0.0.128
netmask_11=255.255.128.0
gateway_11=127.0.0.1
arp_11=-i eth11 -s 127.0.0.82 d8:76:ae:d9:bb:38
netdetect_11=127.0.0.1
send_arp_status_11=1
tls_enable_11=0
address_12=127.0.0.250
netmask_12=255.255.128.0
gateway_12=127.0.0.1
arp_12=-i eth12 -s 127.0.0.43 78:dd:33:71:6f:79
netdetect_12=127.0.0.1
send_arp_status_12=1
tls_enable_12=0
address_13=127.0.0.43
netmask_13=255.255.128.0
gateway_13=127.0.0.1
arp_13=-i eth13 -s 127.0.0.250 78:dd:33:71:6f:78
netdetect_13=127.0.0.1
send_arp_status_13=1
tls_enable_13=0
address_14=127.0.0.214
netmask_14=255.255.128.0
gateway_14=127.0.0.1
arp_14=-i eth14 -s 127.0.0.94 d8:76:ae:76:bd:d9
netdetect_14=127.0.0.1
send_arp_status_14=1
tls_enable_14=0
address_15=127.0.0.94
netmask_15=255.255.128.0
gateway_15=127.0.0.1
arp_15=-i eth15 -s 127.0.0.214 d8:76:ae:76:bd:d8
netdetect_15=127.0.0.1
send_arp_status_15=1
tls_enable_15=0
)";

std::vector<std::string> hccnIps = std::vector<std::string> {"127.0.0.123", "127.0.0.182", "127.0.0.116", "127.0.0.67"};

std::vector<std::string> stringToVector(const std::string &input)
{
    std::vector<std::string> vec;
    std::istringstream stream(input);
    std::string line;
    while (std::getline(stream, line)) {
        vec.push_back(line);
    }
    return vec;
}

const std::string emptyLdLibraryPath = "";

std::vector<int> expectID16 = std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
std::vector<int> expectID8 = std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 };
std::vector<int> expectUseMemory = std::vector<int>{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
std::vector<int> expectTotalMemory = std::vector<int>{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
std::vector<int> expectUseHBM = std::vector<int>{ 22283, 22267, 2818, 2819, 2829, 2829, 30759, 30760 };
std::vector<int> expectLimitHBM = std::vector<int>{ 32768, 32768, 32768, 32768, 32768, 32768, 32768, 32768 };
std::vector<int> expectUseHBM16 = std::vector<int>{ 3402,  3200,  3396,  3205,  3395,  3203,  3395,  3203,
                                                    52553, 52358, 52567, 52345, 52552, 52358, 52554, 52358 };

std::vector<int> expectUseMemory310 = std::vector<int>{ 1596, 1325, 1452, 1444, 1353, 1529, 1656, 1278};
std::vector<int> expectTotalMemory310 = std::vector<int>{ 44280, 43693, 44280, 43693, 44280, 43693, 44280, 43693};


const int expectLimitHBM16 = 65536;

class XpuCollectorTest : public ::testing::Test {};


// case 1: test for count scene
TEST_F(XpuCollectorTest, TestNpuProbeOnGetCountNPUInfo)
{
    auto tool = std::make_shared<MockProcFSTools>();
    auto cmdTool = std::make_shared<MockCmdTools>();
    auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
    params->ldLibraryPath = emptyLdLibraryPath;
    params->deviceInfoPath = "/home/sn/config/topology-info.json";
    params->collectMode = runtime_manager::NPU_COLLECT_COUNT;
    auto probe = std::make_shared<runtime_manager::NpuProbe>("co200", tool, cmdTool, params);

    // case 1.1 get from /dev successfully
    {
        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(devDavinciInfo)));
        auto status = probe->OnGetNPUInfo(true);
        EXPECT_TRUE(status.IsOk());
        auto devInfo = probe->GetClusterInfo();
        ASSERT_EQ(devInfo->devIDs.size(), expectID16.size());
        for (size_t i = 0; i < expectID16.size(); i++) {
            EXPECT_EQ(expectID16[i], devInfo->devIDs[i]);
            EXPECT_EQ(0, devInfo->health[i]);
        }
    }

    // case 1.2 get from /dev failed but get success from npu-smi info
    {
        probe = std::make_shared<runtime_manager::NpuProbe>("co200", tool, cmdTool, params);
        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(""))).WillOnce(testing::Return(stringToVector(npuSmiInfo910B)));
        auto status = probe->OnGetNPUInfo(true);
        EXPECT_TRUE(status.IsOk());
        auto devInfo = probe->GetClusterInfo();
        ASSERT_EQ(devInfo->devIDs.size(), expectID8.size());
        for (size_t i = 0; i < expectID8.size(); i++) {
            EXPECT_EQ(expectID8[i], devInfo->devIDs[i]);
            EXPECT_EQ(expectUseMemory[i], devInfo->devUsedMemory[i]);
            EXPECT_EQ(expectTotalMemory[i], devInfo->devTotalMemory[i]);
            EXPECT_EQ(expectUseHBM[i], devInfo->devUsedHBM[i]);
            EXPECT_EQ(expectLimitHBM[i], devInfo->devLimitHBMs[i]);
            EXPECT_EQ(0, devInfo->health[i]);
        }
        EXPECT_EQ("910B4", devInfo->devProductModel);
    }

    // case 1.3 failed get from /dev and from npu-smi info
    {
        auto devInfo = probe->GetClusterInfo();
        devInfo = std::make_shared<runtime_manager::DevCluster>();
        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(""))).WillOnce(testing::Return(stringToVector("")));
        auto status = probe->OnGetNPUInfo(true);
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.RawMessage(), "can not get npu from npu-smi info, make sure npu-smi is exist!");

        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(""))).WillOnce(testing::Return(stringToVector("AAAAA")));
        EXPECT_CALL(*tool.get(), Read).WillRepeatedly(testing::Return(litebus::Option<std::string>{}));
        status = probe->OnGetNPUInfo(true);
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.RawMessage(), "failed to parse npu smi info!");
    }
}

// case 2: test for hbm scene
TEST_F(XpuCollectorTest, TestNpuProbeOnGetNPUSmiInfo)
{
    auto tool = std::make_shared<MockProcFSTools>();
    auto cmdTool = std::make_shared<MockCmdTools>();
    auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
    params->ldLibraryPath = emptyLdLibraryPath;
    params->deviceInfoPath = "/home/sn/config/topology-info.json";
    params->collectMode = runtime_manager::NPU_COLLECT_COUNT;
    auto probe = std::make_shared<runtime_manager::NpuProbe>("co200", tool, cmdTool, params);

    // case 2.1 success get from npu-smi info
    {
        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(npuSmiInfo910C)));
        auto status = probe->OnGetNPUInfo(false);
        EXPECT_TRUE(status.IsOk());
        auto devInfo = probe->GetClusterInfo();
        ASSERT_EQ(devInfo->devIDs.size(), expectID16.size());
        for (size_t i = 0; i < expectID16.size(); i++) {
            EXPECT_EQ(expectID16[i], devInfo->devIDs[i]);
            EXPECT_EQ(expectUseMemory[i], devInfo->devUsedMemory[i]);
            EXPECT_EQ(expectTotalMemory[i], devInfo->devTotalMemory[i]);
            EXPECT_EQ(expectUseHBM16[i], devInfo->devUsedHBM[i]);
            EXPECT_EQ(expectLimitHBM16, devInfo->devLimitHBMs[i]);
            EXPECT_EQ(0, devInfo->health[i]);
        }
        EXPECT_EQ("Ascend910", devInfo->devProductModel);
    }

    // case 2.2 success get from npu-smi info for 310P3
    {
        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(npuSmiInfo310P3)));
        auto status = probe->OnGetNPUInfo(false);
        EXPECT_TRUE(status.IsOk());
        auto devInfo = probe->GetClusterInfo();
        ASSERT_EQ(devInfo->devIDs.size(), expectID8.size());
        for (size_t i = 0; i < expectID8.size(); i++) {
            EXPECT_EQ(expectID8[i], devInfo->devIDs[i]);
            EXPECT_EQ(expectUseMemory310[i], devInfo->devUsedMemory[i]);
            EXPECT_EQ(expectTotalMemory310[i], devInfo->devTotalMemory[i]);
            EXPECT_EQ(expectUseMemory310[i], devInfo->devUsedHBM[i]);
            EXPECT_EQ(expectTotalMemory310[i], devInfo->devLimitHBMs[i]);
            EXPECT_EQ(0, devInfo->health[i]);
        }
        EXPECT_EQ("310P3", devInfo->devProductModel);
    }
}


// case 2.1: test for get wrong npu smi info
TEST_F(XpuCollectorTest, TestNpuProbeOnGetNPUSmiInfoFailed)
{
    auto tool = std::make_shared<MockProcFSTools>();
    auto cmdTool = std::make_shared<MockCmdTools>();
    auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
    params->ldLibraryPath = emptyLdLibraryPath;
    params->deviceInfoPath = "/home/sn/config/topology-info.json";
    params->collectMode = runtime_manager::NPU_COLLECT_COUNT;
    auto probe = std::make_shared<runtime_manager::NpuProbe>("co200", tool, cmdTool, params);

    // case 2.2 failed get from npu-smi info and get from json
    {
        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector("AAAAA")));
        EXPECT_CALL(*tool.get(), Read).WillRepeatedly(testing::Return(litebus::Option<std::string>{json}));
        EXPECT_CALL(*cmdTool.get(), GetCmdResult("pip3 list")).WillRepeatedly(testing::Return(PIP_LIST_INFO));
        auto status = probe->OnGetNPUInfo(false);
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.RawMessage(), "failed to parse npu smi info!");
        auto devInfo = probe->GetClusterInfo();
        EXPECT_EQ(devInfo->devIDs.size(), 6);

        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(wrongNpuSmiInfo1)));
        status = probe->OnGetNPUInfo(false);
        EXPECT_EQ(status.RawMessage(), "parse npu chip info failed.");

        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(wrongNpuSmiInfo2)));
        status = probe->OnGetNPUInfo(false);
        EXPECT_EQ(status.RawMessage(), "parse npu chip info failed.");

        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(wrongNpuSmiInfo3)));
        status = probe->OnGetNPUInfo(false);
        EXPECT_EQ(status.RawMessage(), "parse npu chip info failed.");

        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(wrongNpuSmiInfo4)));
        status = probe->OnGetNPUInfo(false);
        EXPECT_EQ(status.RawMessage(), "parse npu chip info failed.");

        EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).WillOnce(testing::Return(stringToVector(wrongNpuSmiInfo5)));
        status = probe->OnGetNPUInfo(false);
        EXPECT_EQ(status.RawMessage(), "parse npu chip info failed.");
    }
}

TEST_F(XpuCollectorTest, TestGetNPUIPInfo)
{
    auto tool = std::make_shared<MockProcFSTools>();
    std::string nodeID = "co200";
    auto cmdTools = std::make_shared<MockCmdTools>();
    auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
    params->ldLibraryPath = emptyLdLibraryPath;
    params->deviceInfoPath = "/home/sn/config/topology-info.json";
    auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTools, params);
    // case1: get ips from hccn_conf file successfully
    {
        EXPECT_CALL(*tool.get(), Read).WillOnce(testing::Return(litebus::Option<std::string>{hccnConf}));
        probe->npuNum_ = hccnIps.size();
        auto devInfo = probe->GetClusterInfo();
        devInfo->devIDs = {0,1,2,3};
        auto status = probe->GetNPUIPInfo();
        EXPECT_EQ(status, Status::OK());
        ASSERT_EQ(devInfo->devIPs.size(), hccnIps.size());
        for (size_t i =0; i < hccnIps.size(); i++){
            EXPECT_EQ(devInfo->devIPs[i], hccnIps[i]);
        }
    }

    // case2: failed to get ips from hccn_conf file and get from hccn_tools successfully
    {
        EXPECT_CALL(*cmdTools.get(),GetCmdResult("hccn_tool -i 0 -ip -g | grep ipaddr: | grep -o [0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*"))
            .WillRepeatedly(testing::Return(std::vector<std::string>{ "127.0.0.123" }));
        EXPECT_CALL(*cmdTools.get(), GetCmdResult("hccn_tool -i 1 -ip -g | grep ipaddr: | grep -o [0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*"))
            .WillRepeatedly(testing::Return(std::vector<std::string>{ "127.0.0.182" }));
        EXPECT_CALL(*cmdTools.get(), GetCmdResult("hccn_tool -i 2 -ip -g | grep ipaddr: | grep -o [0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*"))
            .WillRepeatedly(testing::Return(std::vector<std::string>{ "127.0.0.116" }));
        EXPECT_CALL(*cmdTools.get(), GetCmdResult("hccn_tool -i 3 -ip -g | grep ipaddr: | grep -o [0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*"))
            .WillRepeatedly(testing::Return(std::vector<std::string>{ "127.0.0.67" }));
        EXPECT_CALL(*tool.get(), Read).WillOnce(testing::Return(litebus::Option<std::string>{}));
        auto devInfo = probe->GetClusterInfo();
        devInfo->devIDs = {0,1,2,3};
        auto status = probe->GetNPUIPInfo();
        EXPECT_TRUE(status.IsOk());
        ASSERT_EQ(devInfo->devIPs.size(), hccnIps.size());
        for (size_t i =0; i < hccnIps.size(); i++){
            EXPECT_EQ(devInfo->devIPs[i], hccnIps[i]);
        }
    }

    // case3: failed to get ips from hccn_conf file and get from hccn_tools
    {
        EXPECT_CALL(*tool.get(), Read).WillOnce(testing::Return(litebus::Option<std::string>{"testString"}));
        EXPECT_CALL(*cmdTools.get(), GetCmdResult("hccn_tool -i 3 -ip -g | grep ipaddr: | grep -o [0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*"))
            .WillRepeatedly(testing::Return(std::vector<std::string>{}));
        auto status = probe->GetNPUIPInfo();
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.RawMessage(), "failed to get all ip with hccn_tool");
    }

    // case4: get from hccn_tools while id is start from 2, 3
    {
        EXPECT_CALL(*tool.get(), Read).WillOnce(testing::Return(litebus::Option<std::string>{"testString"}));
        EXPECT_CALL(*cmdTools.get(), GetCmdResult("hccn_tool -i 2 -ip -g | grep ipaddr: | grep -o [0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*"))
            .WillOnce(testing::Return(std::vector<std::string>{"127.0.0.116"}));
        EXPECT_CALL(*cmdTools.get(), GetCmdResult("hccn_tool -i 3 -ip -g | grep ipaddr: | grep -o [0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*"))
            .WillOnce(testing::Return(std::vector<std::string>{"127.0.0.117"}));
        probe->npuNum_ = 2;
        auto devInfo = probe->GetClusterInfo();
        devInfo->devIDs = {2,3};
        auto status = probe->GetNPUIPInfo();
        EXPECT_TRUE(status.IsOk());
        ASSERT_EQ(devInfo->devIPs.size(), 2);
        EXPECT_EQ(devInfo->devIPs[0], "127.0.0.116");
        EXPECT_EQ(devInfo->devIPs[1], "127.0.0.117");
    }

   // case5: get from hccn conf while id is start from 2, 3
   {
       EXPECT_CALL(*tool.get(), Read).WillOnce(testing::Return(litebus::Option<std::string>{hccn16NpuConf}));
       probe->npuNum_ = 2;
       auto devInfo = probe->GetClusterInfo();
       devInfo->devIDs = {2,3};
       auto status = probe->GetNPUIPInfo();
       EXPECT_TRUE(status.IsOk());
       ASSERT_EQ(devInfo->devIPs.size(), 2);
       EXPECT_EQ(devInfo->devIPs[0], "127.0.0.217");
       EXPECT_EQ(devInfo->devIPs[1], "127.0.0.70");
   }
}

TEST_F(XpuCollectorTest, TestGetNPUTopoInfo)
{
    auto tool = std::make_shared<MockProcFSTools>();
    std::string nodeID = "co200";
    auto cmdTools = std::make_shared<MockCmdTools>();
    auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
    params->ldLibraryPath = emptyLdLibraryPath;
    params->deviceInfoPath = "/home/sn/config/topology-info.json";
    auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTools, params);

    // case1: get topo info successfully
    {
        probe->npuNum_ = 8;
        auto devInfo = probe->GetClusterInfo();
        devInfo->devIDs = std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7};
        EXPECT_CALL(*cmdTools.get(), GetCmdResultWithError("npu-smi info -t topo")).WillOnce(testing::Return(stringToVector(npuSminTopoInfo)));
        auto status = probe->GetNPUTopoInfo();
        EXPECT_TRUE(status.IsOk());
        int cnt = 0;
        for(auto part: devInfo->devPartition) {
            EXPECT_EQ(part, std::to_string(devInfo->devIDs[cnt]));
            cnt++;
        }
    }
    // case2: get topo info failed
    {
        probe->npuNum_ = 8;
        auto devInfo = probe->GetClusterInfo();
        devInfo->devIDs = std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7};
        EXPECT_CALL(*cmdTools.get(), GetCmdResultWithError("npu-smi info -t topo")).WillOnce(testing::Return(stringToVector("Failed to query \"topo\" info.")));
        auto status = probe->GetNPUTopoInfo();
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.RawMessage(), "node does not install npu driver");
    }

    // case3: not support get topo info
    {
        probe->npuNum_ = 1;
        auto devInfo = probe->GetClusterInfo();
        devInfo->devIDs = std::vector<int>{0};
        EXPECT_CALL(*cmdTools.get(), GetCmdResultWithError("npu-smi info -t topo")).WillOnce(testing::Return(stringToVector("This device does not support querying topo")));
        auto status = probe->GetNPUTopoInfo();
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.RawMessage(), "node does not install npu driver");

        EXPECT_CALL(*cmdTools.get(), GetCmdResultWithError("npu-smi info -t topo")).WillOnce(testing::Return(stringToVector("NPU can not query topo")));
        probe->npuNum_ = 2;
        devInfo->devIDs = std::vector<int>{0, 1};
        status = probe->GetNPUTopoInfo();
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.RawMessage(), "failed to get topo info");
    }

    // case4: get topo info
    {
        auto errorParam = R"(Error parameter of -t
        Usage: npu-smi info <watch|proc|-h|-m|-l|-t type> [Options...]

        Commands:
               watch          Show all device's status in scrolling format
               proc           Show device's matrix process status in scrolling format
               -h, --help     Show this help text and exit
               -m             Show all device's mapping information
               -l             Show all device's topology information
               -t type        Show information for type
                              type: board, flash, memory, usages, sensors, temp, power, volt, mac-addr,
                                    common, health, product, ecc, ip, sys-time, i2c_check, work-mode,
                                    ecc-enable, p2p-enable, ssh-enable, license, customized-info,
                                    device-share, nve-level, aicpu-config, pcie-err, mcu-monitor,
                                    err-count, boot-area, vnpu-mode, info-vnpu, vnpu-svm, cpu-num-cfg,
                                    first-power-on-date, proc-mem, phyid-remap, vnpu-cfg-recover, key-manage,
                                    template-info, pkcs-enable, p2p-mem-cfg, pwm-mode, pwm-duty-ratio,
                                    boot-select, topo, hccs, sio-info, spod-info, tls-csr-get, tls-cert,
                                    tls-cert-period, rootkey, hccs-bw.
        )";
        probe->npuNum_ = 4;
        auto devInfo = probe->GetClusterInfo();
        devInfo->devIDs = std::vector<int>{0,1,2,3};
        auto tmp = stringToVector(errorParam);
        EXPECT_CALL(*cmdTools.get(), GetCmdResultWithError("npu-smi info -t topo")).WillRepeatedly(testing::Return(tmp));
        auto status = probe->GetNPUTopoInfo();
        EXPECT_TRUE(status.IsError());

    }
}

TEST_F(XpuCollectorTest, TestRefreshTopoInfo)
{
   std::string nodeID = "co200";
   auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
   params->ldLibraryPath = emptyLdLibraryPath;
   params->deviceInfoPath = "/home/sn/config/topology-info.json";
   // case1: don't get any npu info
   {
       auto tool = std::make_shared<MockProcFSTools>();
       auto cmdTool = std::make_shared<MockCmdTools>();
       params->collectMode = "false";
       EXPECT_CALL(*cmdTool.get(), GetCmdResult(testing::_)).Times(0);
       EXPECT_CALL(*tool.get(), Read(testing::_)).Times(0);
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsError());
   }

   // case2: count scene, success get
   {
       auto tool = std::make_shared<MockProcFSTools>();
       auto cmdTool = std::make_shared<MockCmdTools>();
       params->collectMode = runtime_manager::NPU_COLLECT_COUNT;
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("ls /dev | grep davinci")).WillOnce(testing::Return(stringToVector(devDavinciInfo)));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsOk());
   }

   // case3: hbm scene, success get
   {
       auto tool = std::make_shared<MockProcFSTools>();
       auto cmdTool = std::make_shared<MockCmdTools>();
       params->collectMode = runtime_manager::NPU_COLLECT_HBM;
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillOnce(testing::Return(stringToVector(npuSmiInfo910B)));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsOk());
   }

   // case4: sfmd scene, success get
   {
       auto tool = std::make_shared<MockProcFSTools>();
       auto cmdTool = std::make_shared<MockCmdTools>();
       params->collectMode = runtime_manager::NPU_COLLECT_SFMD;
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillOnce(testing::Return(stringToVector(npuSmiInfo910C)));
       EXPECT_CALL(*tool.get(), Read("/etc/hccn.conf")).WillOnce(testing::Return(litebus::Option<std::string>{hccn16NpuConf}));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsOk());
   }

   // case5: topo scene, success get
   {
       auto tool = std::make_shared<MockProcFSTools>();
       auto cmdTool = std::make_shared<MockCmdTools>();
       params->collectMode = runtime_manager::NPU_COLLECT_TOPO;
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillOnce(testing::Return(stringToVector(npuSmiInfo910B)));
       EXPECT_CALL(*cmdTool.get(), GetCmdResultWithError("npu-smi info -t topo")).WillOnce(testing::Return(stringToVector(npuSminTopoInfo)));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsOk());
   }

   // case6: off scene or other scene, failed get
   {
       auto tool = std::make_shared<MockProcFSTools>();
       auto cmdTool = std::make_shared<MockCmdTools>();
       params->collectMode = "off";
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsError());
       params->collectMode = "other";
       probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsError());
   }
}

TEST_F(XpuCollectorTest, TestRefreshTopoInfoAllMode)
{
   auto tool = std::make_shared<MockProcFSTools>();
   std::string nodeID = "co200";
   auto cmdTool = std::make_shared<MockCmdTools>();
   auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
   params->ldLibraryPath = emptyLdLibraryPath;
   params->deviceInfoPath = "/home/sn/config/topology-info.json";
   params->collectMode = runtime_manager::NPU_COLLECT_ALL;

   // case1: success get all information
   {
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillOnce(testing::Return(stringToVector(npuSmiInfo910B)));
       EXPECT_CALL(*tool.get(), Read("/etc/hccn.conf")).WillOnce(testing::Return(litebus::Option<std::string>{hccn8NpuConf}));
       EXPECT_CALL(*cmdTool.get(), GetCmdResultWithError("npu-smi info -t topo")).WillOnce(testing::Return(stringToVector(npuSminTopoInfo)));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsOk());
   }

   // case2: failed get hbm information
   {
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillOnce(testing::Return(stringToVector(wrongNpuSmiInfo3)));
       EXPECT_CALL(*tool.get(), Read(params->deviceInfoPath)).WillOnce(testing::Return(litebus::Option<std::string>{}));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsError());
   }

   // case3: failed get ip information
   {
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillOnce(testing::Return(stringToVector(npuSmiInfo910B)));
       EXPECT_CALL(*tool.get(), Read("/etc/hccn.conf")).WillOnce(testing::Return(litebus::Option<std::string>{}));
       EXPECT_CALL(*cmdTool.get(), GetCmdResult(MatchesRegex("hccn_tool -i .*"))).WillRepeatedly(testing::Return(std::vector<std::string>{}));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsError());
   }

   // case4: failed get ip information
   {
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillOnce(testing::Return(stringToVector(npuSmiInfo910B)));
       EXPECT_CALL(*tool.get(), Read("/etc/hccn.conf")).WillOnce(testing::Return(litebus::Option<std::string>{hccn8NpuConf}));
       EXPECT_CALL(*cmdTool.get(), GetCmdResultWithError("npu-smi info -t topo")).WillOnce(testing::Return(std::vector<std::string>{}));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       auto status = probe->RefreshTopo();
       EXPECT_TRUE(status.IsError());
   }
}

TEST_F(XpuCollectorTest, TestUpdateInfo)
{
   auto tool = std::make_shared<MockProcFSTools>();
   std::string nodeID = "co200";
   auto cmdTool = std::make_shared<MockCmdTools>();
   auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
   params->ldLibraryPath = emptyLdLibraryPath;
   params->deviceInfoPath = "/home/sn/config/topology-info.json";

   {
       params->collectMode = runtime_manager::NPU_COLLECT_HBM;
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillRepeatedly(testing::Return(stringToVector(npuSmiInfo910B)));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       // don't get any npu info, update healthy failed
       probe->UpdateHealth();
       auto devInfo = probe->GetClusterInfo();
       EXPECT_TRUE(devInfo->health.empty());

       // mock get npu info, update healthy successfully
       probe->npuNum_ = 8;
       probe->UpdateHealth();
       probe->UpdateHealth();
       EXPECT_EQ(devInfo->health.size(), 8);

       // mock get npu num not equal npu-smi info card num, update healthy failed
       probe->npuNum_ = 4;
       probe->UpdateHealth();
       EXPECT_EQ(devInfo->health.size(), 8);
   }
   // case2: update all info successfully
   {
       params->collectMode = runtime_manager::NPU_COLLECT_HBM;
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info")).WillRepeatedly(testing::Return(stringToVector(npuSmiInfo910B)));
       EXPECT_CALL(*tool.get(), Read("/etc/hccn.conf")).WillOnce(testing::Return(litebus::Option<std::string>{hccn8NpuConf}));
       auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
       probe->UpdateHBM();
       probe->UpdateMemory();
       probe->UpdateUsedMemory();
       probe->UpdateUsedHBM();
       probe->UpdateProductModel();
       probe->UpdateDeviceIDs();
       probe->UpdateDeviceIPs();
       probe->UpdateHealth();
       auto devInfo = probe->GetClusterInfo();
       EXPECT_EQ(probe->npuNum_, 8);
       EXPECT_EQ(devInfo->health.size(), 8);
       EXPECT_EQ(devInfo->devIDs.size(), 8);
       EXPECT_EQ(devInfo->devUsedMemory.size(), 8);
       EXPECT_EQ(devInfo->devTotalMemory.size(), 8);
       EXPECT_EQ(devInfo->devUsedHBM.size(), 8);
       EXPECT_EQ(devInfo->devLimitHBMs.size(), 8);
       EXPECT_EQ(devInfo->devIPs.size(), 8);
       probe->npuNum_ = 4;
       EXPECT_CALL(*cmdTool.get(), GetCmdResult("npu-smi info -t topo")).WillOnce(testing::Return(stringToVector(""))).WillRepeatedly(testing::Return(topoInfo));
       probe->UpdateDevTopo();
       probe->UpdateDevTopo();
       EXPECT_EQ(devInfo->devTopo.size(), 4);
   }
}

TEST_F(XpuCollectorTest, IsNpuTopoCommandValid_NewCmd_NotSupport)
{
   auto tool = std::make_shared<MockProcFSTools>();
   std::string nodeID = "co200";
   auto cmdTools = std::make_shared<MockCmdTools>();
   auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
   params->ldLibraryPath = emptyLdLibraryPath;
   params->deviceInfoPath = "/home/sn/config/topology-info.json";
   auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTools, params);
   EXPECT_FALSE(probe->IsNpuTopoCommandValid(topoInfoNotSupport));
   EXPECT_TRUE(probe->IsNpuTopoCommandValid(stringToVector(npuSminTopoInfo)));
}

TEST_F(XpuCollectorTest, TestGpuProbeSmiLFailed)
{
    auto cmdTools = std::make_shared<MockCmdTools>();
    auto probe = runtime_manager::GpuProbe(emptyLdLibraryPath, cmdTools);
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi -L")).WillRepeatedly(testing::Return(std::vector<std::string>{}));

    auto status = probe.RefreshTopo();
    EXPECT_EQ(status.StatusCode(), StatusCode::RUNTIME_MANAGER_GPU_NOTFOUND);
}

TEST_F(XpuCollectorTest, TestGpuProbeSmiInfoFailed)
{
    auto cmdTools = std::make_shared<MockCmdTools>();
    auto probe = runtime_manager::GpuProbe(emptyLdLibraryPath, cmdTools);
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi -q")).WillRepeatedly(testing::Return(gpuOrUnitInfo));
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi -L")).WillRepeatedly(testing::Return(gpuInfo));
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi")).WillRepeatedly(testing::Return(gpuMemoryInfo));
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi topo -m")).WillRepeatedly(testing::Return(std::vector<std::string>{}));

    auto status = probe.RefreshTopo();
    EXPECT_EQ(probe.GetPartition().size(), 0);
}

TEST_F(XpuCollectorTest, TestGpuProbe)
{
    auto cmdTools = std::make_shared<MockCmdTools>();
    auto probe = runtime_manager::GpuProbe(emptyLdLibraryPath, cmdTools);
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi -q")).WillRepeatedly(testing::Return(gpuOrUnitInfo));
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi -L")).WillRepeatedly(testing::Return(gpuInfo));
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi")).WillRepeatedly(testing::Return(gpuMemoryInfo));
    EXPECT_CALL(*cmdTools.get(), GetCmdResult("nvidia-smi topo -m")).WillRepeatedly(testing::Return(gpuTopoInfo));

    EXPECT_EQ(probe.devInfo_->health.size(), 0);
    auto status = probe.RefreshTopo();
    std::vector<int> expected = {0};
    EXPECT_EQ(probe.devInfo_->health.size(), 1);
    EXPECT_EQ(probe.devInfo_->health, expected);
    EXPECT_EQ(probe.GetDevClusterIDs().size(), 1);
    EXPECT_EQ(probe.GetLimit(), 1);
    EXPECT_EQ(probe.GetUsage(), 1);
    expected = {20};
    EXPECT_EQ(probe.devInfo_->devUsedHBM, expected);
    expected = {24576};
    EXPECT_EQ(probe.devInfo_->devLimitHBMs, expected);
    EXPECT_EQ(probe.devInfo_->devType, runtime_manager::DEV_TYPE_GPU);
    EXPECT_EQ(probe.devInfo_->devVendor, runtime_manager::DEV_VENDOR_NVIDIA);
    EXPECT_EQ(probe.devInfo_->devProductModel, "NVIDIA GeForce RTX 3090");
}

TEST_F(XpuCollectorTest, TestNpuCollectorByCmd)
{
    auto tool = std::make_shared<MockProcFSTools>();
    auto cmdTool = std::make_shared<MockCmdTools>();
    std::string nodeID = "co200";
    auto params = std::make_shared<runtime_manager::XPUCollectorParams>();
    params->ldLibraryPath = emptyLdLibraryPath;
    params->deviceInfoPath = "/home/sn/config/topology-info.json";
    params->collectMode = runtime_manager::NPU_COLLECT_COUNT;
    EXPECT_CALL(*cmdTool.get(), GetCmdResult("ls /dev | grep davinci")).WillRepeatedly(testing::Return(stringToVector(devDavinciInfo)));
    auto probe = std::make_shared<runtime_manager::NpuProbe>(nodeID, tool, cmdTool, params);
    auto npuCollector = runtime_manager::SystemXPUCollector(nodeID, runtime_manager::metrics_type::NPU, tool, params);
    npuCollector.probe_ = probe;
    auto deviceIDsUsage =
        npuCollector.GetUsage().Get().devClusterMetrics.Get().intsInfo
            .at(resource_view::IDS_KEY);
    EXPECT_EQ(deviceIDsUsage.size(), 16);
    auto deviceIDsLimit =
        npuCollector.GetLimit().devClusterMetrics.Get().intsInfo
            .at(resource_view::IDS_KEY);
    EXPECT_EQ(deviceIDsLimit.size(), 16);
}
}