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
#include "runtime_manager/metrics/collector/disk_collector.h"

namespace functionsystem::test {

using namespace testing;
using namespace functionsystem::runtime_manager;

struct ExpectedDisk {
    std::string name;
    int size;
    std::string mountpoints;
};

class DiskCollectorTest : public ::testing::Test {};

void ValidateDiskExtensions(const DevClusterMetrics &devMetrics, const std::vector<ExpectedDisk> &expectedDisks)
{

    EXPECT_EQ(devMetrics.extensionInfo.size(), expectedDisks.size());
    EXPECT_EQ(devMetrics.intsInfo.count(resource_view::DISK_RESOURCE_NAME) > 0, true);
    EXPECT_EQ(devMetrics.intsInfo.at(resource_view::DISK_RESOURCE_NAME).size(), expectedDisks.size());

    for (size_t i = 0; i < expectedDisks.size(); ++i) {
        const auto& diskExt = devMetrics.extensionInfo[i].disk();
        const auto& expected = expectedDisks[i];

        EXPECT_EQ(diskExt.name(), expected.name);
        EXPECT_EQ(diskExt.size(), expected.size);
        EXPECT_EQ(diskExt.mountpoints(), expected.mountpoints);

        if (devMetrics.intsInfo.count(resource_view::DISK_RESOURCE_NAME) > 0) {
            EXPECT_EQ(diskExt.size(), devMetrics.intsInfo.at(resource_view::DISK_RESOURCE_NAME)[i]);
        }
    }
}

/*
Test for parsing valid disk configurations from JSON input
1. JSON contains multiple disks with valid name, size and mount points -> should successfully parse all disks
2. Each parsed disk should have correct metrics including size conversion from "G" to numeric value
3. Mount paths should be accurately captured for each disk device
*/
TEST_F(DiskCollectorTest, ParseValidDisks) {
    std::string json = R"(
            [{"name": "disk1", "size": "100G", "mountPoints": "/tmp/disk1/"},
             {"name": "disk2", "size": "200G", "mountPoints": "/tmp/disk2a/"}]
        )";

    auto collector = std::make_shared<DiskCollector>(json);
    auto usage = collector->GetUsage().Get();

    auto devMetrics = usage.devClusterMetrics.Get();
    std::vector<ExpectedDisk> expectedDisks = {
        {"disk1", 100, "/tmp/disk1/"},
        {"disk2", 200, "/tmp/disk2a/"}
    };
    ValidateDiskExtensions(devMetrics, expectedDisks);
}

/*
 * Test for disk configs with mixed valid and invalid entries
 * 1. JSON array contains both properly formatted disk and malformed config
 * 2. Valid disk config should be parsed normally while invalid entry is silently skipped
 */
TEST_F(DiskCollectorTest, ParseMixedConfigWithPartialValidity) {
    std::string json = R"(
            [{"name": "invalid_disk1", "mountPoints": "/mnt/"},
            {"name": "invalid_disk2", "size": "100G", "mountPoints": "/mnt——a/"},
            {"name": "disk1", "size": "100G", "mountPoints": "/tmp/disk1/a_b-c.d/"}]
        )";

    auto collector = std::make_shared<DiskCollector>(json);
    auto usage = collector->GetUsage().Get();

    auto devMetrics = usage.devClusterMetrics.Get();
    std::vector<ExpectedDisk> expectedDisks = {
        {"disk1", 100, "/tmp/disk1/a_b-c.d/"}
    };
    ValidateDiskExtensions(devMetrics, expectedDisks);
}

/*
 * Test for disk config validation with missing mandatory fields
 * JSON disk config missing "size" field -> validation fails
 */
TEST_F(DiskCollectorTest, RejectConfigWithMissingSizeField) {
    std::string json = R"(
            [{"name": "invalid_disk", "mountPoints": "/mnt/"}]
        )";

    DiskCollector collector(json);
    EXPECT_FALSE(collector.GetUsage().Get().devClusterMetrics.IsSome());
}

/*
 * Test for disk size format validation
 * Invalid size value -> rejected by regex check
 */
TEST_F(DiskCollectorTest, RejectInvalidSizeFormat) {
    std::string json = R"(
            [{"name": "disk3", "size": "10K", "mountPoints": "/mnt/disk3/"},
             {"name": "disk3", "size": "10GB", "mountPoints": "/mnt/disk3/"},
             {"name": "disk3", "size": "10g", "mountPoints": "/mnt/disk3/"},
             {"name": "disk3", "size": "1_00g", "mountPoints": "/mnt/disk3/"},]
        )";

    DiskCollector collector(json);
    EXPECT_FALSE(collector.GetUsage().Get().devClusterMetrics.IsSome());
}

/*
 * Test for detecting path traversal attacks
 * Mount path containing '..' sequence -> blocked by security check
 */
TEST_F(DiskCollectorTest, BlockPathTraversalInMountPoint) {
    std::string json = R"(
            [{"name": "unsafe_disk", "size": "50G", "mountPoints": "/var/../unsafe/"}]
        )";

    DiskCollector collector(json);
    EXPECT_FALSE(collector.GetUsage().Get().devClusterMetrics.IsSome());
}

/*
 * Test for absolute path format enforcement
 * Mount point missing trailing slash -> violates path regex
 */
TEST_F(DiskCollectorTest, RequireTrailingSlashInPath) {
    std::string json = R"(
            [{"name": "bad_path", "size": "30G", "mountPoints": "/tmp"}]
        )";

    DiskCollector collector(json);
    EXPECT_FALSE(collector.GetUsage().Get().devClusterMetrics.IsSome());
}

/*
 * Test for special character filtering in paths
 * Path contains invalid characters '()' -> rejected by regex
 */
TEST_F(DiskCollectorTest, FilterSpecialCharsInMountPoint) {
    std::string json = R"(
            [{"name": "weird_path", "size": "40G", "mountPoints": "/tmp/()}"]
        )";

    DiskCollector collector(json);
    EXPECT_FALSE(collector.GetUsage().Get().devClusterMetrics.IsSome());
}

/*
 * Test for empty disk configuration JSON input handling
 * Provide empty JSON array as disk config -> config validation fails
 */
TEST_F(DiskCollectorTest, HandleEmptyJSONConfig) {
    DiskCollector collector("[]");
    EXPECT_FALSE(collector.GetUsage().Get().devClusterMetrics.IsSome());
}

/*
 * Test for JSON structure validation at root level
 * Provide JSON object instead of array as root element -> config validation fails
 */
TEST_F(DiskCollectorTest, RejectNonArrayJSONInput) {
    DiskCollector collector(R"({"name": "invalid_root"})");
    EXPECT_FALSE(collector.GetUsage().Get().devClusterMetrics.IsSome());
}

}
