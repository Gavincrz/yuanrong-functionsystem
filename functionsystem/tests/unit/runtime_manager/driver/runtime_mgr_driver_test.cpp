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

#include "runtime_manager/driver/runtime_manager_driver.h"

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

class RuntimeMgrDriverTest : public ::testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(RuntimeMgrDriverTest, DriverTest)
{
    functionsystem::runtime_manager::Flags flags;
    const char *argv[] = {
        "/runtime_manager",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--port=32233",
        "--runtime_initial_port=500",
        "--port_num=2000",
        "--runtime_dir=/tmp",
        "--agent_address=127.0.0.1:1234",
        "--runtime_ld_library_path=/tmp:/home/disk",
        "--proc_metrics_cpu=2000",
        "--proc_metrics_memory=2000",
        "--nodejs_entry=/home/runtime/node.js",
        "--resource_label_path=/tmp/labels",
        "--runtime_ds_connect_timeout=10",
        "--kill_process_timeout_seconds=2",
        R"(--log_config={"filepath": "/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})"
    };
    flags.ParseFlags(std::size(argv), argv);
    EXPECT_EQ(flags.GetRuntimeHomeDir(), litebus::os::GetEnv("HOME").Get());
    std::cout << "flags.GetRuntimeHomeDir(): " << flags.GetRuntimeHomeDir() << std::endl;
    EXPECT_EQ(flags.GetNodeJsEntryPath(), "/home/runtime/node.js");
    EXPECT_EQ(flags.GetResourceLabelPath(), "/tmp/labels");
    EXPECT_EQ(flags.GetNpuDeviceInfoPath(), "/home/sn/config/topology-info.json");
    EXPECT_EQ(flags.GetRuntimeDsConnectTimeout(), static_cast<uint32_t>(10));
	EXPECT_EQ(flags.GetRuntimeLdLibraryPath(), "/tmp:/home/disk");
    RuntimeManagerDriver driver(flags, "runtime_manager");
    EXPECT_EQ(driver.Start(), Status::OK());
    EXPECT_EQ(driver.Stop(), Status::OK());
    driver.Await();
}

TEST_F(RuntimeMgrDriverTest, DriverParseFailTest)
{
    functionsystem::runtime_manager::Flags flags;
    const char *argv[] = {
        "/runtime_manager",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--port=32233",
        "--runtime_initial_port=500",
        "--port_num=2000",
        "--runtime_dir=/tmp",
        "--agent_address=127.0.0.1:1234",
        "--runtime_ld_library_path=/tmp:;",
        "--proc_metrics_cpu=2000",
        "--proc_metrics_memory=2000",
        "--nodejs_entry=/home/runtime/node.js",
        "--resource_label_path=/tmp/labels",
        "--runtime_ds_connect_timeout=10",
        "--kill_process_timeout_seconds=2",
        R"(--log_config={"filepath": "/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})"
    };
    auto result = flags.ParseFlags(std::size(argv), argv);
    EXPECT_TRUE(result.IsSome());
    EXPECT_EQ(result.Get(), "Failed to parse value for: runtime_ld_library_path");
}

}  // namespace functionsystem::test
