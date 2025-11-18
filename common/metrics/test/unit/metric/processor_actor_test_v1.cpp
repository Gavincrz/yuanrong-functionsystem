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
#include <utils/os_utils.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include "metrics/api/provider.h"
#include "metrics/plugin/dynamic_load.h"
#include "metrics/exporters/prometheus_push_exporter/prometheus_push_exporter.h"
#include "metrics/sdk/metric_data.h"
#include "../mocks/mock_exporter.h"

#define private public
#include "sdk/include/processor_actor.h"

namespace observability::test {
namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;

const std::string IMMEDIATELY_EXPORTER = "immediatelyExporter";
const std::string BATCH_EXPORTER = "BatchExporter";
const std::string FAILURE_FILE_NAME = "Failure.metrics";
const sdk::metrics::InstrumentDescriptor instrumentDescriptor1 = sdk::metrics::InstrumentDescriptor {
    .name = "test_metric1",
    .description = "test metric1 desc",
    .unit = "ms",
    .type = sdk::metrics::InstrumentType::COUNTER,
    .valueType = sdk::metrics::InstrumentValueType::DOUBLE };
const sdk::metrics::InstrumentDescriptor instrumentDescriptor2 = sdk::metrics::InstrumentDescriptor {
    .name = "test_metric2",
    .description = "test metric2 desc",
    .unit = "ms",
    .type = sdk::metrics::InstrumentType::COUNTER,
    .valueType = sdk::metrics::InstrumentValueType::DOUBLE };
const std::list<std::pair<std::string, std::string>> pointLabels1 = { std::pair {"instance_id", "ins001"},
                                                                      std::pair {"job_id", "job001"} };
const std::list<std::pair<std::string, std::string>> pointLabels2 = { std::pair {"instance_id", "ins002"},
                                                                      std::pair {"job_id", "job002"} };
const std::vector<MetricsSdk::PointData> pointData = { { .labels = pointLabels1, .value = (double)10 } };
const std::vector<MetricsSdk::PointData> pointData2 = { { .labels = pointLabels2, .value = (double)20 } };
const MetricsSdk::MetricData metricData = { .instrumentDescriptor = instrumentDescriptor1,
                                            .aggregationTemporality = sdk::metrics::AggregationTemporality::UNSPECIFIED,
                                            .collectionTs = std::chrono::system_clock::now(), .pointData = pointData };
const MetricsSdk::MetricData metricData2 = { .instrumentDescriptor = instrumentDescriptor2,
                                             .aggregationTemporality = sdk::metrics::AggregationTemporality::UNSPECIFIED,
                                             .collectionTs = std::chrono::system_clock::now(),
                                             .pointData = pointData2 };
const std::string MetricsStr = "{\"aggregationTemporality\":\"DELTA\",\"instrumentDescriptor\":\"{\\\"description\\\":\\\"\\\",\\\"name\\\":\\\"\\\",\\\"type\\\":\\\"GAUGE\\\",\\\"unit\\\":\\\"\\\",\\\"valueType\\\":\\\"DOUBLE\\\"}\",\"pointData\":\"{\\\"labels\\\":[[\\\"DELEGATE_DIRECTORY_QUOTA\\\",\\\"512\\\"],[\\\"cpu_type\\\",\\\"Intel(R) Xeon(R) Gold 6161 CPU @ 2.20GHz\\\"],[\\\"end_ms\\\",\\\"1721394795346\\\"],[\\\"export_sub_url\\\",\\\"/instanceId/540c0000-0000-4000-9509-39dff5dc9819/requestId/12600855a41aed2105\\\"],[\\\"function_name\\\",\\\"12345678901234561234567890123456/0@fasa001@hello/latest\\\"],[\\\"interval_ms\\\",\\\"4\\\"],[\\\"pool_label\\\",\\\"[\\\\\\\"HOST_IP:10.244.176.93\\\\\\\",\\\\\\\"NODE_ID:dggphis35946\\\\\\\",\\\\\\\"app:function-agent-pool24-600-512-fusion\\\\\\\",\\\\\\\"pod-template-hash:67dfd5f795\\\\\\\",\\\\\\\"resource.owner:default\\\\\\\",\\\\\\\"reuse:false\\\\\\\"]\\\"],[\\\"request_id\\\",\\\"12600855a41aed2105\\\"],[\\\"schedule_policy\\\",\\\"monopoly\\\"],[\\\"start_ms\\\",\\\"1721394795342\\\"],[\\\"status_code\\\",\\\"0\\\"]],\\\"value\\\":\\\"4\\\"}\",\"pointTimeStamp\":\"1721392554332\"}";
const std::string FilePath = "/metrics-test";

void GenDir(const std::string &path)
{
    if (!litebus::os::ExistPath(path)) {
        litebus::os::Mkdir(path);
    }
}

void GenFile(const std::string &path, const std::string &content)
{
    if (!litebus::os::ExistPath(path)) {
        litebus::os::Mkdir(path);
    }
    auto fileName = path + FAILURE_FILE_NAME;

    std::ofstream outfile;
    outfile.open(fileName.c_str());
    outfile << content << std::endl;
    outfile.close();
}

class ProcessorActorV1Test : public ::testing::Test {
protected:
    inline static std::shared_ptr<MockExporter> mockExporter_;
    std::shared_ptr<MetricsSdk::ProcessorActor> processorActor_;

    static void SetUpTestCase()
    {
        mockExporter_ = std::make_shared<MockExporter>();
    }

    static void TearDownTestCase()
    {
        mockExporter_ = nullptr;
    }

    void TearDown() override
    {
        if (processorActor_ != nullptr) {
            litebus::Terminate(processorActor_->GetAID());
            litebus::Await(processorActor_->GetAID());
            processorActor_ = nullptr;
        }
    }
};

TEST_F(ProcessorActorV1Test, ImmediatelyExportSuccess)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = IMMEDIATELY_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::IMMEDIATELY;
    exportConfigs.batchSize = 1;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    litebus::Spawn(processorActor_);
    litebus::Async(processorActor_->GetAID(), &MetricsSdk::ProcessorActor::Start);

    EXPECT_CALL(*mockExporter_, Export).WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS));
    processorActor_->Export(metricData);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
    auto failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(0));
}

TEST_F(ProcessorActorV1Test, ImmediatelyExportFail)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = IMMEDIATELY_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::IMMEDIATELY;
    exportConfigs.batchSize = 1;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    litebus::Spawn(processorActor_);
    litebus::Async(processorActor_->GetAID(), &MetricsSdk::ProcessorActor::Start);

    EXPECT_CALL(*mockExporter_, Export)
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS));
    processorActor_->Export(metricData);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
    auto failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(1));
    EXPECT_EQ(failureMetricDataQueue[0].pointData.size(), static_cast<uint64_t>(1));
}

TEST_F(ProcessorActorV1Test, ExportSuccessWhenDestruct)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = IMMEDIATELY_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::IMMEDIATELY;
    exportConfigs.batchSize = 1;
    exportConfigs.failureQueueMaxSize = 2;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    litebus::Spawn(processorActor_);
    litebus::Async(processorActor_->GetAID(), &MetricsSdk::ProcessorActor::Start);

    // 2 data in file, 1 in failure queue. When destruct, call export two times(export data in file and queue)
    EXPECT_CALL(*mockExporter_, Export)
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS));
    litebus::Async(processorActor_->GetAID(), &MetricsSdk::ProcessorActor::Export, metricData);
    litebus::Async(processorActor_->GetAID(), &MetricsSdk::ProcessorActor::Export, metricData);
    litebus::Async(processorActor_->GetAID(), &MetricsSdk::ProcessorActor::Export, metricData);
}

TEST_F(ProcessorActorV1Test, ExportFailWhenDestruct)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);

    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = IMMEDIATELY_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::IMMEDIATELY;
    exportConfigs.batchSize = 1;
    exportConfigs.failureQueueMaxSize = 2;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    litebus::Spawn(processorActor_);
    processorActor_->Start();

    // 2 data in file, one in failure queue. When destruct, call export two times(export data in file and queue)
    EXPECT_CALL(*mockExporter_, Export)
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE));
    processorActor_->Export(metricData);
    processorActor_->Export(metricData);
    auto failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(0));
    processorActor_->Export(metricData);
    EXPECT_TRUE(litebus::os::ExistPath(FilePath + "/" + exportConfigs.exporterName + FAILURE_FILE_NAME));
    auto content = litebus::os::Read(FilePath + "/" + exportConfigs.exporterName + FAILURE_FILE_NAME);
    std::cerr << "content" << content.Get() << std::endl;
    int cnt = 0;
    for (auto &it : litebus::strings::Split(content.Get(), "\n")) {
        if (!it.empty()) cnt++;
    }
    EXPECT_EQ(cnt, 2);
    if (processorActor_ != nullptr) {
        litebus::Terminate(processorActor_->GetAID());
        litebus::Await(processorActor_->GetAID());
    }
}

TEST_F(ProcessorActorV1Test, BatchExportWriteIntoMetricDataQueue)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);

    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.failureDataDir = FilePath;
    exportConfigs.batchSize = 10;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);

    processorActor_->Export(metricData);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(1));
    EXPECT_EQ(metricDataQueue[0].pointData.size(), static_cast<uint64_t>(1));

    processorActor_->Export(metricData2);
    metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(2));
}

TEST_F(ProcessorActorV1Test, TimerReachedThenExportSuccess)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 10;
    exportConfigs.batchIntervalSec = 2;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    litebus::Spawn(processorActor_);
    litebus::Async(processorActor_->GetAID(), &MetricsSdk::ProcessorActor::Start);

    EXPECT_CALL(*mockExporter_, Export)
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS));
    processorActor_->Export(metricData);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
    processorActor_->Export(metricData);
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
}

TEST_F(ProcessorActorV1Test, TimerReachedThenNoDataExport)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 10;
    exportConfigs.batchIntervalSec = 2;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    processorActor_->SetHealthyExporter(false);
    litebus::Spawn(processorActor_);
    litebus::Async(processorActor_->GetAID(), &MetricsSdk::ProcessorActor::Start);

    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    EXPECT_FALSE(processorActor_->GetHealthyExporter());
}

TEST_F(ProcessorActorV1Test, ExcceedBatchSizeThenExportSuccess)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 2;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    litebus::Spawn(processorActor_);

    EXPECT_CALL(*mockExporter_, Export).WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS));
    processorActor_->Export(metricData);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(1));

    processorActor_->Export(metricData);
    metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
    auto failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(0));
}

TEST_F(ProcessorActorV1Test, ExcceedBatchSizeThenExportFail)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 2;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    litebus::Spawn(processorActor_);

    EXPECT_CALL(*mockExporter_, Export)
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS));
    processorActor_->Export(metricData);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(1));

    processorActor_->Export(metricData);
    metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
    auto failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(2));
    EXPECT_TRUE(!litebus::os::ExistPath(FilePath + "/" + exportConfigs.exporterName + FAILURE_FILE_NAME));
}

TEST_F(ProcessorActorV1Test, ExportFailThenExportSuccess)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 1;
    exportConfigs.batchIntervalSec = 30;
    exportConfigs.failureQueueMaxSize = 2;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    processorActor_->Start();

    EXPECT_CALL(*mockExporter_, Export)
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS))
        .WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS));

    processorActor_->Export(metricData);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
    auto failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(1));

    processorActor_->Export(metricData);
    metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
    failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(0));

    processorActor_->Export(metricData);
    failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(1));
    auto content = litebus::os::Read(FilePath + "/" + exportConfigs.exporterName + FAILURE_FILE_NAME);
    std::cerr << "content" << content.Get() << std::endl;
    int cnt = 0;
    for (auto &it : litebus::strings::Split(content.Get(), "\n")) {
        if (!it.empty()) cnt++;
    }
    EXPECT_EQ(cnt, 2);
    // until here, two metricData in file, one metricData in failureMetricQueue

    processorActor_->Export(metricData);
    failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(0));
    litebus::os::ExistPath(FilePath + "/" + exportConfigs.exporterName + FAILURE_FILE_NAME);
}

TEST_F(ProcessorActorV1Test, WriteFailureIntoFile)
{
    MetricsSdk::InstrumentDescriptor instrumentDescriptor3;
    instrumentDescriptor3.name = "test_metric1";
    instrumentDescriptor3.description = "test metric1 desc";
    instrumentDescriptor3.unit = "ms";
    MetricsSdk::MetricData metricData3;
    metricData3.instrumentDescriptor = instrumentDescriptor3;
    metricData3.collectionTs = std::chrono::system_clock::now();
    metricData3.pointData = pointData;

    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 2;
    exportConfigs.batchIntervalSec = 30;
    exportConfigs.failureQueueMaxSize = 2;
    exportConfigs.failureDataDir = FilePath;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    processorActor_->Start();

    EXPECT_CALL(*mockExporter_, Export)
        .WillOnce(testing::Return(MetricsExporter::ExportResult::FAILURE));
    processorActor_->Export(metricData3);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(1));

    processorActor_->Export(metricData3);
    metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), static_cast<uint64_t>(0));
    auto failureMetricDataQueue = processorActor_->GetFailureMetricDataQueue();
    EXPECT_EQ(failureMetricDataQueue.size(), static_cast<uint64_t>(0));
    auto content = litebus::os::Read(FilePath + "/" + exportConfigs.exporterName + FAILURE_FILE_NAME);
    EXPECT_NE(content, "");
    std::cerr << "content" << content.Get() << std::endl;
    std::vector<std::string> contents;
    for (auto &it : litebus::strings::Split(content.Get(), "\n")) {
        if (!it.empty()) {
            contents.push_back(it);
        }
    }
    EXPECT_EQ(contents.size(), 2);
    auto metricJson = nlohmann::json::parse(contents[0]);
    auto instrumentJson = nlohmann::json::parse(metricJson.at("instrumentDescriptor").get<std::string>());
    EXPECT_EQ(instrumentJson.at("type").get<std::string>(), "GAUGE");
    EXPECT_EQ(instrumentJson.at("valueType").get<std::string>(), "DOUBLE");
    EXPECT_EQ(metricJson.at("aggregationTemporality"), "UNSPECIFIED");
}

TEST_F(ProcessorActorV1Test, ReadFailureIntoFile)
{
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 2;
    exportConfigs.batchIntervalSec = 30;
    exportConfigs.failureQueueMaxSize = 2;
    exportConfigs.failureDataDir = FilePath;

    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    GenFile(FilePath + "/" + exportConfigs.exporterName, MetricsStr);
    EXPECT_CALL(*mockExporter_, Export).WillOnce(testing::Return(MetricsExporter::ExportResult::SUCCESS));

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    litebus::Spawn(processorActor_);
    processorActor_->Start();
    EXPECT_TRUE(!litebus::os::ExistPath(FilePath + "/" + FAILURE_FILE_NAME));
}

TEST_F(ProcessorActorV1Test, EnableAllInstruments)
{
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 10;

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    processorActor_->Start();

    processorActor_->Export(metricData);
    processorActor_->Export(metricData2);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), 2);
}

TEST_F(ProcessorActorV1Test, NoEnableInstrument)
{
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 10;
    exportConfigs.enabledInstruments = { "some-metric" };

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    processorActor_->Start();

    processorActor_->Export(metricData);
    processorActor_->Export(metricData2);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), 0);
}

TEST_F(ProcessorActorV1Test, EnableOneInstrument)
{
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 10;
    exportConfigs.enabledInstruments = { "test_metric1" };

    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    processorActor_->Start();

    processorActor_->Export(metricData);
    processorActor_->Export(metricData2);
    auto metricDataQueue = processorActor_->GetMetricDataQueue();
    EXPECT_EQ(metricDataQueue.size(), 1);
}

TEST_F(ProcessorActorV1Test, BackendStatusChange)
{
    litebus::os::Rmdir(FilePath);
    GenDir(FilePath);
    MetricsSdk::ExportConfigs exportConfigs;
    exportConfigs.exporterName = BATCH_EXPORTER;
    exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
    exportConfigs.batchSize = 1;
    exportConfigs.batchIntervalSec = 30;
    exportConfigs.failureQueueMaxSize = 2;
    exportConfigs.failureDataDir = FilePath;

    EXPECT_CALL(*mockExporter_, RegisterOnHealthChangeCb).Times(1);
    EXPECT_CALL(*mockExporter_, Export).WillRepeatedly(testing::Return(MetricsExporter::ExportResult::SUCCESS));
    processorActor_ = std::make_shared<MetricsSdk::ProcessorActor>(mockExporter_, exportConfigs);
    processorActor_->Start();
    processorActor_->Export(metricData);
    // backend becomes unhealthy
    processorActor_->failureMetricDataQueue_.push_back(metricData);
    processorActor_->OnBackendHealthChangeHandler(false);
    EXPECT_FALSE(processorActor_->healthyExporter_);
    EXPECT_EQ(processorActor_->failureMetricDataQueue_.size(), 1);

    // backend becomes healthy
    processorActor_->OnBackendHealthChangeHandler(true);
    EXPECT_TRUE(processorActor_->healthyExporter_);
    EXPECT_EQ(processorActor_->failureMetricDataQueue_.size(), 0);
}
}  // namespace observability::test::sdk