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
#include "common/file/file_sink.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iostream>

#include <utils/os_utils.hpp>

#include "common/file/file_utils.h"
#include "spdlog/async.h"

namespace observability::test {
using namespace observability::metrics::common;

TEST(CommonFileSinkTest, GetFileNameByIndexTest)
{
    std::remove("/tmp/metrics/file_sink/get_file_name_test.txt");
    auto sink = std::make_shared<FileSink>("/tmp/metrics/file_sink/get_file_name_test.txt", 1024, 3);

    auto fileName = sink->GetFileNameByIndex("get_file_name_test.txt", 3);
    EXPECT_EQ("get_file_name_test.3.txt", fileName);
    std::remove("/tmp/metrics/file_sink/get_file_name_test.txt");
}

int CountLines(const std::string &filename)
{
    std::ifstream ReadFile;
    int n = 0;
    std::string temp;
    ReadFile.open(filename, std::ios::in);
    if (ReadFile.fail()) {
        return 0;
    }

    while (getline(ReadFile, temp)) {
        n++;
    }
    return n;
}

/**
 * Feature: FileSink Flush records.
 * Description: test flush is or not usefully.
 * Steps:
 * 1. create a MetricLogger with FileSink.
 * 2. make the logger record something.
 * 3. invoke flush.
 * 4. check the lines in file with the record data.
 */
TEST(CommonFileSinkTest, FlushTest)
{
    const std::string filename = "/tmp/metrics/file_sink/get_file_name_test.txt";
    std::remove(filename.c_str());
    auto logger = yr_spdlog::create_async<FileSink>("CommonFlushTest", filename, 1024 * 1024, 3);

    for (int i = 0; i < 9; i++) {
        auto str = std::to_string(i) + "-get_file_name_test";
        logger->info(str);
    }
    logger->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    auto lines = CountLines(filename);
    EXPECT_EQ(lines, 9);
    std::remove(filename.c_str());
}

/**
 * Feature: FileSink Rotate without Compress Test.
 * Description: test Rotate without Compress is or not valid.
 * Steps:
 * 1. create a MetricLogger with FileSink.
 * 2. make the logger record something which more than max file size.
 * 3. find some files with pattern the name we wanted.
 * 4. check the file size.
 */
TEST(CommonFileSinkTest, RotateWithoutCompressTest)
{
    const std::string dir = "/tmp/metrics/file_sink";
    litebus::os::Rmdir(dir);
    litebus::os::Mkdir(dir);
    const std::string filename = dir + "/rotate_compress_test.txt";
    std::remove(filename.c_str());

    auto sink = std::make_shared<observability::metrics::common::FileSink>(filename, 300, 3, true, false);
    auto logger = std::make_shared<yr_spdlog::logger>("rotate_without_compress", sink);
    logger->set_pattern("%v");

    std::ostringstream oss;
    for (int i = 0; i < 20; i++) {
        oss << "rotate compress ssssssssssssssssssssssssssssssssssssssssss:" << i;
        logger->info(oss.str());
        oss.str("");
        oss.clear();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::vector<std::string> files;
    std::stringstream ss;
    ss << "/tmp/metrics/file_sink/rotate_compress_test"
       << "\\."
       << "[0-9]*\\.txt"
       << "\\.gz";
    std::string pattern = ss.str();
    Glob(pattern, files);
    // no tar file
    ASSERT_EQ(files.size(), static_cast<uint64_t>(0));

    ss.str("");
    ss.clear();
    ss << "/tmp/metrics/file_sink/rotate_compress_test"
       << "\\."
       << "[0-9]*\\.txt";
    pattern = ss.str();
    std::vector<std::string> files1;
    Glob(pattern, files1);
    // rolled files
    ASSERT_EQ(files1.size(), static_cast<uint64_t>(2));
    for (auto &file : files1) {
        std::remove(file.c_str());
    }

    std::vector<std::string> files2;
    Glob(filename, files2);
    // data file
    ASSERT_EQ(files2.size(), static_cast<uint64_t>(1));
    std::remove(filename.c_str());
}

TEST(CommonFileSinkTest, GlobTest)
{
    std::vector<std::string> files;
    std::string pattern = "/tmp/metrics/file_sink/rotate_compress_test.[0-9]*.txt";
    Glob(pattern, files);
}

/**
 * Feature: FileSink Rotate and Compress Test.
 * Description: test Rotate and Compress is or not valid.
 * Steps:
 * 1. create a MetricLogger with FileSink.
 * 2. make the logger record something which more than max file size.
 * 3. find some gz files with pattern the name we wanted.
 * 4. check the gz file size.
 */
TEST(CommonFileSinkTest, RotateCompressTest)
{
    const std::string dir = "/tmp/metrics/file_sink";
    litebus::os::Rmdir(dir);
    litebus::os::Mkdir(dir);
    const std::string filename = dir + "/rotate_compress_test.txt";
    std::remove(filename.c_str());

    auto sink = std::make_shared<observability::metrics::common::FileSink>(filename, 300, 3, true, true);
    auto logger = std::make_shared<yr_spdlog::logger>("rotate_with_compress", sink);
    logger->set_pattern("%v");

    std::ostringstream oss;
    for (int i = 0; i < 20; i++) {
        oss << "rotate compress ssssssssssssssssssssssssssssssssssssssssss:" << i;
        logger->info(oss.str());
        oss.str("");
        oss.clear();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::vector<std::string> files;
    std::stringstream ss;
    ss << "/tmp/metrics/file_sink/rotate_compress_test"
       << "\\."
       << "[0-9]*\\.txt"
       << "\\.gz";
    std::string pattern = ss.str();
    Glob(pattern, files);
    ASSERT_EQ(files.size(), static_cast<uint64_t>(2));
    for (auto &file : files) {
        std::remove(file.c_str());
    }

    std::vector<std::string> files1;
    Glob(filename, files1);
    // data file
    ASSERT_EQ(files1.size(), static_cast<uint64_t>(1));
    std::remove(filename.c_str());
}
}  // namespace observability::test
