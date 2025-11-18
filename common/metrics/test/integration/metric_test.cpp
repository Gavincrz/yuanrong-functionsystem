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

#include <fstream>
#include <nlohmann/json.hpp>
#include <utils/string_utils.hpp>

#include "common/include/exporter_loader.h"
#include "exporters/file_exporter/include/file_exporter.h"
#include "sdk/include/meter_provider.h"

namespace observability::test {
const int INTERVAL_1000MS = 1000;
const int TOTAL_TIME_MS = 2500;
const int TIMER_COLLECT_SEC[] = { 0, 1, 2, 3, 4, 5 };
const double MOCK_DISK_USAGE_VALUES[] = { 0, 1, 2, 3, 4, 5 };
const int COLLECT_TIMES = 4;

class MetricTest : public ::testing::Test {
protected:
    void SetUp()
    {
        std::remove("./test_file_exporter.data");

        metrics::MeterProvider::GetInstance().Init();
        metrics::FileParam fileParam = { "./", "test_file_exporter" };
        std::unique_ptr<metrics::BasicExporter> exporter = std::make_unique<metrics::FileExporter>(fileParam);
        metrics::MeterProvider::GetInstance().SetExporter(exporter);
    }
    void TearDown()
    {
    }
};

double MockGetDiskUsage()
{
    static int callTimes = 0;
    return MOCK_DISK_USAGE_VALUES[++callTimes];
}

void IncreaseMemoryUsage(std::shared_ptr<observability::metrics::Gauge<double>> gauge)
{
    for (uint32_t i = 0; i < TOTAL_TIME_MS / INTERVAL_1000MS; ++i) {
        gauge->Set(gauge->Value() + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(INTERVAL_1000MS));
    }
}

std::string ReadDataFromFile()
{
    nlohmann::json jsonContent;
    std::ifstream dataFile("./test_file_exporter.data");
    if (!dataFile.is_open()) {
        std::cerr << "Failed to open file!" << std::endl;
        dataFile.close();
        return "";
    }
    std::stringstream buffer;
    buffer << dataFile.rdbuf();
    dataFile.close();
    std::string content = buffer.str();
    std::cout << "content: " << content << std::endl;
    return content;
}

/**
 * Feature: Integration Metrics initialization framework。
 * Description: Test CreateDoubleGauge Usage.
 * Steps:
 * 1. Initialize MeterProvider and set exporter.
 * 2. Create a Meter with the Collection Interval of 1s.
 * 3. Create double gauge to mock collect memory and Create Thread mock to set value
 * 4. Sleep for a while, Check metric value
 * 5. Finalize MeterProvider
 */
TEST_F(MetricTest, TestCreateDoubleGauge)
{
    auto meter = observability::metrics::MeterProvider::GetInstance().GetMeter();
    auto interval = 2;
    // Obtain the value in other threads without calling back.
    auto memoryGauge = meter->CreateGauge<double>(
        metrics::TitleOptions{ "interval_2_memory_usage", "memory test", "memory size" }, interval);
    std::thread IncreaseMemoryUsageThread{ &IncreaseMemoryUsage, memoryGauge };
    IncreaseMemoryUsageThread.join();

    EXPECT_GE(memoryGauge->Value(), TOTAL_TIME_MS / INTERVAL_1000MS / interval);

    metrics::MeterProvider::GetInstance().Finalize();
    // Check the contents of the written file
    int retryCount = 0;
    std::vector<std::string> lines;
    const int maxRetryCount = TOTAL_TIME_MS / INTERVAL_1000MS * interval * interval;
    while (retryCount < maxRetryCount) {
        auto strContent = ReadDataFromFile();
        lines = litebus::strings::Split(strContent, "\n");
        if (lines.size() > 1) {
            break;
        }
        retryCount++;
        if (retryCount >= maxRetryCount) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(INTERVAL_1000MS / interval));
    }
    EXPECT_GE(lines.size(), static_cast<uint64_t>(3));
    nlohmann::json j;
    for (uint64_t i = 0; i < lines.size() - 1; ++i) {
        j = nlohmann::json::parse(lines[i]);
        int value = std::stoi(j.at("value").get<std::string>());
        EXPECT_LE(static_cast<uint64_t>(value), i + 1);
    }
}

/**
 * Feature: Integration Metrics initialization framework。
 * Description:  Test Create Gauge With Callback Usage.
 * Steps:
 * 1. Initialize MeterProvider and set exporter.
 * 2. Create a Meter with the Collection Interval of 2s.
 * 3. Create double gauge with mock call back, metric value wil get from callback.
 * 4. Sleep for a while, Check Collect metric value
 * 5. Finalize MeterProvider
 */
TEST_F(MetricTest, TestCreateGaugeWithCallBack)
{
    auto meter = observability::metrics::MeterProvider::GetInstance().GetMeter();
    auto const interval = 1;
    // Callback mode. The callback value is written to the corresponding metric every time.
    auto diskGauge = meter->CreateGauge<double>(
        metrics::TitleOptions{ "interval_2_disk_usage" }, interval,
        [](observability::metrics::ObserveResult ob_res, observability::metrics::MetricValue refState) {
            (void)refState;
            if (std::holds_alternative<std::shared_ptr<observability::metrics::ObserverResultT<double>>>(ob_res)) {
                double value = MockGetDiskUsage();
                std::get<std::shared_ptr<observability::metrics::ObserverResultT<double>>>(ob_res)->Observe(value);
            }
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(TOTAL_TIME_MS));
    EXPECT_GE(diskGauge->Value(), MOCK_DISK_USAGE_VALUES[TOTAL_TIME_MS / INTERVAL_1000MS / interval]);

    metrics::MeterProvider::GetInstance().Finalize();
    // Check the contents of the written file
    int retryCount = 0;
    std::vector<std::string> lines;
    const int maxRetryCount = TOTAL_TIME_MS / INTERVAL_1000MS * interval * interval;
    while (retryCount < maxRetryCount) {
        auto strContent = ReadDataFromFile();
        lines = litebus::strings::Split(strContent, "\n");
        if (lines.size() > 1) {
            break;
        }
        retryCount++;
        if (retryCount >= maxRetryCount) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(INTERVAL_1000MS / interval));
    }
    EXPECT_GE(lines.size(), static_cast<uint64_t>(3));

    nlohmann::json j;
    for (uint64_t i = 0; i < lines.size() - 1; ++i) {
        j = nlohmann::json::parse(lines[i]);
        int value = std::stoi(j.at("value").get<std::string>());
        EXPECT_EQ(static_cast<uint64_t>(value), i + 1);
    }
}

/**
 * Feature: Meter
 * Description: Test Create Gauge And Report Temporarily Metric
 * Steps:
 * 1. Initialize MeterProvider and set exporter.
 * 2. Create a Meter with the Temporarily Data(interval = 0)
 * 3. Report TemporarilyMetrics
 * 4. Finalize MeterProvider
 */
TEST_F(MetricTest, TestReportTemporarilyMetric)
{
    auto meter = metrics::MeterProvider::GetInstance().GetMeter();
    auto cpuGauge = meter->CreateGauge<double>(metrics::TitleOptions{ "cpu_usage" });
    auto value = 0.44;
    cpuGauge->Set(value);
    cpuGauge->SetLabels({
        { "node_id", "127.0.0.1" },
    });
    cpuGauge->SetTimestamp(std::chrono::system_clock::now());

    meter->Collect(cpuGauge);

    EXPECT_EQ(cpuGauge->Value(), value);
    auto labels = cpuGauge->GetLabels();
    EXPECT_TRUE(!labels.empty());
    auto it = labels.find("node_id");
    EXPECT_EQ(it->second, "127.0.0.1");
    std::this_thread::sleep_for(std::chrono::milliseconds(TOTAL_TIME_MS));

    metrics::MeterProvider::GetInstance().Finalize();
    // Check the contents of the written file
    int retryCount = 0;
    std::vector<std::string> lines;
    const int maxRetryCount = TOTAL_TIME_MS / INTERVAL_1000MS * 4;
    while (retryCount < maxRetryCount) {
        auto strContent = ReadDataFromFile();
        lines = litebus::strings::Split(strContent, "\n");
        if (lines.size() > 1) {
            break;
        }
        retryCount++;
        if (retryCount >= maxRetryCount) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(INTERVAL_1000MS / 4));
    }
    EXPECT_GE(lines.size(), static_cast<uint64_t>(2));

    nlohmann::json j;
    for (uint64_t i = 0; i < lines.size() - 1; ++i) {
        j = nlohmann::json::parse(lines[i]);
        double val = std::stod(j.at("value").get<std::string>());
        EXPECT_EQ(val, 0.44);

        std::string name = j.at("name").get<std::string>();
        EXPECT_EQ(name, "cpu_usage");
    }
}

void RecordMemoryUsage(const std::shared_ptr<metrics::Gauge<uint64_t>> &vmSizeGauge,
                       const std::shared_ptr<metrics::Gauge<uint64_t>> &vmRssGauge,
                       const std::shared_ptr<metrics::Gauge<uint64_t>> &rssAnonGauge)
{
    uint64_t value = 0;
    while (value < COLLECT_TIMES) {
        vmSizeGauge->Set(value);
        vmRssGauge->Set(value);
        rssAnonGauge->Set(value);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++value;
    }
}

/**
 * Feature: Meter
 * Description: Create Gauge And Mock Report Memory Usgae
 * Steps:
 * 1. Initialize MeterProvider and set exporter.
 * 2. Create Meters and Set Labels
 * 3. Change values periodically in other threads
 * 4. Report TemporarilyMetrics
 * 5. Finalize MeterProvider
 */
TEST_F(MetricTest, TestMonitorMemUsage)
{
    metrics::TitleOptions titleVmSize = { "runtime_memory_usage_vm_size", "", "KB" };
    metrics::TitleOptions titleVmRss = { "runtime_memory_usage_vm_rss", "", "KB" };
    metrics::TitleOptions titleRssAnon = { "runtime_memory_usage_rss_anon", "", "KB" };

    std::map<std::string, std::string> labels;
    labels["job_id"] = "yr-job-id";
    labels["instance_id"] = "yr-instance-id";

    auto meter = metrics::MeterProvider::GetInstance().GetMeter();
    int interval = 1;
    auto vmSizeGauge = meter->CreateGauge<uint64_t>(titleVmSize, interval);
    auto vmRssGauge = meter->CreateGauge<uint64_t>(titleVmRss, interval);
    auto rssAnonGauge = meter->CreateGauge<uint64_t>(titleRssAnon, interval);

    vmSizeGauge->SetLabels(labels);
    rssAnonGauge->SetLabels(labels);

    std::thread memoryMonitorThread{ RecordMemoryUsage, vmSizeGauge, vmRssGauge, rssAnonGauge };
    memoryMonitorThread.join();

    metrics::MeterProvider::GetInstance().Finalize();
}
}  // namespace observability::test