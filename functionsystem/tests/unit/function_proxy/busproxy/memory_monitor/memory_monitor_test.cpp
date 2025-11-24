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
#include <string>
#include "utils/future_test_helper.h"
#include "function_proxy/common/flags/flags.h"
#include "common/utils/files.h"
#include "busproxy/memory_monitor/memory_monitor.h"

namespace functionsystem::test {
const std::string FILE_PATH = "/home/sn/";
const std::string FILE_NAME = "memory.stat";
const float DEFAULT_LOW_MEMORY_THRESHOLD = 0.6;
const float DEFAULT_HIGH_MEMORY_THRESHOLD = 0.8;
const uint64_t DEFAULT_MESSAGE_SIZE_THRESHOLD = 20 * 1024;

class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class MemoryMonitorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        config_.enable = true;
        config_.highMemoryThreshold = DEFAULT_HIGH_MEMORY_THRESHOLD;
        config_.lowMemoryThreshold = DEFAULT_LOW_MEMORY_THRESHOLD;
        config_.msgSizeThreshold = DEFAULT_MESSAGE_SIZE_THRESHOLD;
        tools_ = std::make_shared<MockProcFSTools>();
        monitor_ = std::make_shared<functionsystem::MemoryMonitor>(config_);
    }
    void TearDown() override
    {
        litebus::Terminate(monitor_->GetCollector()->GetAID());
        litebus::Await(monitor_->GetCollector());
        tools_ = nullptr;
        monitor_ = nullptr;
    }
protected:
    MemoryControlConfig config_;
    std::shared_ptr<MockProcFSTools> tools_;
    std::shared_ptr<functionsystem::MemoryMonitor> monitor_;
};

std::string MockStatFileWithRss(const std::string &path, const std::string &fileName, int64_t rss)
{
    std::string content = "cache 201535488\nrss " + std::to_string(rss) + "\nrss_huge 262144000\nshmem 0\n";
    if (!litebus::os::ExistPath(path)) {
        litebus::os::Mkdir(path);
    }
    auto file = path + "/" + fileName;
    Write(file, content);
    return file;
}
/**
 * Feature: MemoryMonitor
 * Description: test Allow function at high-threshold
 * Steps:
 * 1. mock MockProcFSTools to return current system memory usage
 * 2. call Allow with different input
 * Expectation: return false
 */
TEST_F(MemoryMonitorTest, AllowAtHighThreshold)
{
    monitor_->GetCollector()->SetProcFSTools(tools_);
    auto rssFile = MockStatFileWithRss(FILE_PATH, FILE_NAME, 435200000);
    monitor_->GetCollector()->rssPath_ = rssFile;
    // Condition: above high threshold
    EXPECT_CALL(*tools_.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{"512000000"}));
    monitor_->GetCollector()->SetLimit();
    monitor_->GetCollector()->SetCurrent();

    // Result: reject request smaller than 20 Bytes
    EXPECT_FALSE(monitor_->Allow("instance_1", "request_1", 10000));
    // Result: reject request larger than 20 Bytes
    EXPECT_FALSE(monitor_->Allow("instance_2", "request_2", 30000));
    litebus::os::Rmdir(rssFile);
}

/**
 * Feature: MemoryMonitor
 * Description: test Allow function at low-threshold
 * Steps:
 * 1. mock MockProcFSTools to return current system memory usage
 * 2. call Allow with different input
 * Expectation:
 * 1. allow new request to pass
 * 2. reject request estimated usage larger than average
 * 3. allow request estimated usage smaller than average
 */
TEST_F(MemoryMonitorTest, AllowAtLowThreshold)
{
    monitor_->GetCollector()->SetProcFSTools(tools_);
    auto rssFile = MockStatFileWithRss(FILE_PATH, FILE_NAME, 33280000000);
    monitor_->GetCollector()->rssPath_ = rssFile;
    // Condition: above low threshold
    EXPECT_CALL(*tools_.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{"51200000000"}));
    monitor_->GetCollector()->SetLimit();
    monitor_->GetCollector()->SetCurrent();

    // Result: allow request smaller than 20 Bytes
    EXPECT_TRUE(monitor_->Allow("instance_1", "request_1", 10000));

    // Result: allow new request
    EXPECT_TRUE(monitor_->Allow("instance_2", "request_2", 80000));
    EXPECT_TRUE(monitor_->Allow("instance_3", "request_3", 25000));
    EXPECT_EQ(monitor_->GetEstimateUsage(), static_cast<uint64_t>(105000));
    // Result: reject request estimated usage larger than average
    EXPECT_FALSE(monitor_->Allow("instance_2", "request_4", 25000));

    // Result: allow request estimated usage smaller than average
    EXPECT_TRUE(monitor_->Allow("instance_3", "request_5", 25000));
    litebus::os::Rmdir(rssFile);
}

/**
 * Feature: MemoryMonitor
 * Description: test AllocateEstimateMemory and ReleaseEstimateMemory function at low-threshold
 * Steps:
 * 1. mock MockProcFSTools to return current system memory usage
 * 2. call Allow with different input
 * Expectation: value of estimateUsage and instanceUsage are correct
 */
TEST_F(MemoryMonitorTest, UpdateEstimateUsage)
{
    monitor_->GetCollector()->SetProcFSTools(tools_);
    auto rssFile = MockStatFileWithRss(FILE_PATH, FILE_NAME, 33280000000);
    monitor_->GetCollector()->rssPath_ = rssFile;
    EXPECT_CALL(*tools_.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{"51200000000"}));
    monitor_->GetCollector()->SetLimit();
    monitor_->GetCollector()->SetCurrent();

    EXPECT_TRUE(monitor_->Allow("instance_1", "request_1", 75000));
    EXPECT_TRUE(monitor_->Allow("instance_2", "request_2", 25000));
    EXPECT_TRUE(monitor_->Allow("instance_2", "request_3", 25000));

    // check estimate usage (Allocate memory)
    EXPECT_EQ(monitor_->GetFunctionMemMap()["instance_1"], static_cast<uint64_t>(75000));
    EXPECT_EQ(monitor_->GetFunctionMemMap()["instance_2"], static_cast<uint64_t>(50000));
    EXPECT_EQ(monitor_->GetEstimateUsage(), static_cast<uint64_t>(125000));

    // check estimate usage (Release memory)
    monitor_->ReleaseEstimateMemory("instance_2", "request_3");
    EXPECT_EQ(monitor_->GetFunctionMemMap()["instance_1"], static_cast<uint64_t>(75000));
    EXPECT_EQ(monitor_->GetFunctionMemMap()["instance_2"], static_cast<uint64_t>(25000));
    EXPECT_EQ(monitor_->GetEstimateUsage(), static_cast<uint64_t>(100000));
    monitor_->ReleaseEstimateMemory("instance_1", "request_1");
    monitor_->ReleaseEstimateMemory("instance_2", "request_2");
    EXPECT_EQ(monitor_->GetEstimateUsage(), static_cast<uint64_t>(0));
    litebus::os::Rmdir(rssFile);
}

TEST_F(MemoryMonitorTest, RefreshActualMemoryUsageTest)
{
    monitor_->GetCollector()->SetProcFSTools(tools_);
    auto rssFile = MockStatFileWithRss(FILE_PATH, FILE_NAME, 51200000000);
    monitor_->GetCollector()->rssPath_ = rssFile;
    EXPECT_CALL(*tools_.get(), Read)
        .WillRepeatedly(testing::Return(litebus::Option<std::string>{"51200000000"}));
    monitor_->RefreshActualMemoryUsage();
    monitor_->StopRefreshActualMemoryUsage();
    litebus::os::Rmdir(rssFile);
}

}  // namespace functionsystem::test
