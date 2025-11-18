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
#include <nlohmann/json.hpp>

#define private public
#define protected public
#include "metrics/exporters/file_exporter/file_exporter.h"
#include "metrics/exporters/prometheus_push_exporter/prometheus_push_exporter.h"
#include "common/include/utils.h"
#include "utils/os_utils.hpp"


namespace observability::test {

namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;

class UtilsTest : public ::testing::Test {};

TEST_F(UtilsTest, ValidateExportConfig)
{
    sdk::metrics::ExportConfigs exportConfigs;
    exportConfigs.batchSize = 1000;
    exportConfigs.batchIntervalSec = 0;
    exportConfigs.failureQueueMaxSize = 1000;
    exportConfigs.failureDataFileMaxCapacity = 2000 * MetricsSdk::SIZE_MEGA_BYTES;

    observability::metrics::ValidateExportConfigs(exportConfigs);

    EXPECT_EQ(exportConfigs.batchSize, MetricsSdk::DEFAULT_EXPORT_BATCH_SIZE);
    EXPECT_EQ(exportConfigs.batchIntervalSec, MetricsSdk::DEFAULT_EXPORT_BATCH_INTERVAL_SEC);
    EXPECT_EQ(exportConfigs.failureQueueMaxSize, MetricsSdk::DEFAULT_FAILURE_QUEUE_MAX_SIZE);
    EXPECT_EQ(exportConfigs.failureDataFileMaxCapacity, MetricsSdk::DEFAULT_FAILURE_FILE_MAX_CAPACITY);
}

TEST_F(UtilsTest, FileExporterConfig)
{
    const std::string dir = "/metrics_test";
    litebus::os::Rmdir(dir);
    nlohmann::json jsonConfig;
    jsonConfig["fileDir"] = dir;
    jsonConfig["fileName"] = "file_exporter_without_write.data";
    jsonConfig["rolling"]["enable"] = true;
    jsonConfig["rolling"]["maxFiles"] = 3;
    jsonConfig["rolling"]["maxSize"] = 0;

    auto exporter = std::make_unique<MetricsExporter::FileExporter>(jsonConfig.dump());
    EXPECT_EQ(exporter->options_.maxSize, observability::exporters::metrics::DEFAULT_MAX_SIZE);

    jsonConfig["rolling"]["maxSize"] = 1025;
    exporter = std::make_unique<MetricsExporter::FileExporter>(jsonConfig.dump());
    EXPECT_EQ(exporter->options_.maxSize, observability::exporters::metrics::DEFAULT_MAX_SIZE);

    jsonConfig["rolling"]["maxSize"] = 10;
    exporter = std::make_unique<MetricsExporter::FileExporter>(jsonConfig.dump());
    EXPECT_EQ(exporter->options_.maxSize, 10 * observability::exporters::metrics::SIZE_MEGA_BYTES);

    uint64_t defaultMaxFiles = 3;
    jsonConfig["rolling"]["maxFiles"] = 0;
    exporter = std::make_unique<MetricsExporter::FileExporter>(jsonConfig.dump());
    EXPECT_EQ(exporter->options_.maxFiles, defaultMaxFiles);

    jsonConfig["rolling"]["maxFiles"] = 101;
    exporter = std::make_unique<MetricsExporter::FileExporter>(jsonConfig.dump());
    EXPECT_EQ(exporter->options_.maxFiles, defaultMaxFiles);

    uint64_t normalMaxFiles = 10;
    jsonConfig["rolling"]["maxFiles"] = normalMaxFiles;
    exporter = std::make_unique<MetricsExporter::FileExporter>(jsonConfig.dump());
    EXPECT_EQ(exporter->options_.maxFiles, normalMaxFiles);
}

TEST_F(UtilsTest, RegisterOnHealthChangeCb)
{
    exporters::metrics::PrometheusPushExportOptions options;
    options.endpoint = "127.0.0.1:9091";
    options.jobName = "prometheusPushExportTest";
    options.heartbeatInterval = 10;
    options.heartbeatUrl = "/healthcheck";

    auto exporter = exporters::metrics::PrometheusPushExporter(options);
    exporter.RegisterOnHealthChangeCb([](bool newStatus) {
        (void)newStatus;
    });
    EXPECT_TRUE(exporter.httpHeartBeatClient_ != nullptr);
    EXPECT_EQ(exporter.heartbeatParam_.heartbeatInterval, 5000);
}
}
