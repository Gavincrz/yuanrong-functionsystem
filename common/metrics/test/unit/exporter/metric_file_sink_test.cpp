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
#include <cstdio>
#include <fstream>
#include <iostream>

#include <gtest/gtest.h>
#include "spdlog/async.h"

#include "exporters/file_exporter/include/file_utils.h"
#include "exporters/file_exporter/include/metric_file_sink.h"

namespace observability::test {
using namespace observability::metrics;

TEST(FileSinkTest, InValidFileSizeTest)
{
    EXPECT_THROW(MetricFileSink("/tmp/metrics/file_sink/test_invalid_file_size.txt", 0, 3), std::exception);

}

TEST(MetricFileSinkTest, GetFileNameByIndexTest)
{

    auto sink = std::make_shared<MetricFileSink>(
        "/tmp/metrics/file_sink/get_file_name_test.txt", 1024, 3);

    auto fileName = sink->GetFileNameByIndex("get_file_name_test.txt", 3);
    EXPECT_EQ("get_file_name_test.3.txt", fileName);
    std::remove("/tmp/metrics/file_sink/get_file_name_test.txt");

}

int CountLinesFileSink(const std::string &filename)
{
    std::ifstream ReadFile;
    int n = 0;
    std::string temp;
    ReadFile.open(filename, std::ios::in);
    if (ReadFile.fail())
    {
        return 0;
    }

    while (getline(ReadFile, temp)) {
        n++;
    }
    return n;
}

/**
 * Feature: MetricFileSink Flush records.
 * Description: test flush is or not usefully.
 * Steps:
 * 1. create a MetricLogger with MetricFileSink.
 * 2. make the logger record something.
 * 3. invoke flush.
 * 4. check the lines in file with the record data.
 */
TEST(MetricFileSinkTest, FlushTest)
{
   const std::string filename = "/tmp/metrics/file_sink/get_file_name_test.txt";
    auto logger = yr_spdlog::create_async<observability::metrics::MetricFileSink>(
        "FlusheTest", filename, 1024*1024, 3);

    for(int i =0; i < 9; i++){
        auto str = std::to_string(i) + "-get_file_name_test";
        logger->info(str);
    }
    logger->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    auto lines = CountLinesFileSink(filename);
    EXPECT_EQ(lines, 9);
    std::remove(filename.c_str());
}

/**
 * Feature: MetricFileSink Rotate and Compress Test.
 * Description: test Rotate and Compress is or not valid.
 * Steps:
 * 1. create a MetricLogger with MetricFileSink.
 * 2. make the logger record something which more than max file size.
 * 3. find some gz files with pattern the name we wanted.
 * 4. check the gz file size.
 */
TEST(MetricFileSinkTest, RotateCompressTest)
{
    const std::string filename = "/tmp/metrics/file_sink/rotate_compress_test.txt";

    auto logger = yr_spdlog::create_async<observability::metrics::MetricFileSink>(
        "rotate_compress_test", filename, 3, 3);

    std::ostringstream oss;
    for(int i = 0; i<10; i++) {
        oss << "rotate compress:" << i;
        logger->info(oss.str());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    std::vector<std::string> files;

    std::stringstream ss;
    ss << "/tmp/metrics/file_sink/rotate_compress_test" << "\\."
       << "*[0-9]\\.txt"
       << "\\.gz";
    std::string pattern = ss.str();
    Glob(pattern, files);
    ASSERT_NE(files.size(), static_cast<uint64_t>(0));
    for(auto &file : files) {
        std::remove(file.c_str());
    }
    std::remove(filename.c_str());
}
}  // namespace observability::test