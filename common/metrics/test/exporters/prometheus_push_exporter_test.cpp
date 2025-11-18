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

#include "metrics/api/provider.h"
#include "metrics/plugin/dynamic_load.h"
#include "metrics/sdk/immediately_export_processor.h"
#include "metrics/sdk/meter_provider.h"
#include "metrics/sdk/metric_exporter.h"

#define private public

#include "metrics/exporters/prometheus_push_exporter/prometheus_push_exporter.h"

using namespace observability::sdk::metrics;
namespace MetricsApi = observability::api::metrics;
namespace MetricsExporter = observability::exporters::metrics;
namespace MetricsSdk = observability::sdk::metrics;

const observability:: sdk::metrics::InstrumentDescriptor instrumentDescriptor =
    observability::sdk::metrics::InstrumentDescriptor{ .name = "test_metric",
                                        .description = "test metric desc",
                                        .unit = "ms",
                                        .type = observability::sdk::metrics::InstrumentType::COUNTER,
                                        .valueType = observability::sdk::metrics::InstrumentValueType::DOUBLE };
const std::list<std::pair<std::string, std::string>> pointLabels1 = { std::pair{ "instance_id", "ins001" },
                                                                      std::pair{ "job_id", "job001" } };
const std::vector<MetricsSdk::PointData> pointData = { { .labels = pointLabels1, .value = (double)10 } };
const MetricsSdk::MetricData metricData = { .instrumentDescriptor = instrumentDescriptor,
                                            .aggregationTemporality = observability::sdk::metrics::AggregationTemporality::UNSPECIFIED,
                                            .collectionTs = std::chrono::system_clock::now(),
                                            .pointData = pointData };
namespace observability::test::exporter {

class PrometheusPushExporterTest : public ::testing::Test {};

static std::string GetLibraryPath()
{
    char path[1024];
    std::string filePath;
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        char *directoryPath = path;
        char *fileName = strrchr(path, '/');
        if (fileName) {
            *fileName = '\0';
        }
        filePath = std::string(directoryPath) + "/../lib/libobservability-prometheus-push-exporter.so";
        std::cout << "filePath: " << filePath << std::endl;
    }
    return filePath;
}

TEST_F(PrometheusPushExporterTest, simpleExport)
{
    auto mp = std::make_shared<MeterProvider>();

    nlohmann::json jsonConfig;
    jsonConfig["endpoint"] = "127.0.0.1:9091";
    jsonConfig["jobName"] = "prometheusPushExportTest";
    jsonConfig["heartbeatInterval"] = 10;
    jsonConfig["heartbeatUrl"] = "/healthcheck";
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    std::vector<MetricData> vec = { metricData };
    EXPECT_NE(exporter, nullptr);
    exporter->Export(vec);
}

TEST_F(PrometheusPushExporterTest, GetAggregationTemporality)
{
    nlohmann::json jsonConfig;
    jsonConfig["endpoint"] = "127.0.0.1:9091";
    jsonConfig["jobName"] = "prometheusPushExportTest";
    jsonConfig["isSSLEnable"] = true;
    jsonConfig["rootCertFile"] = "rootCertFile";
    jsonConfig["certFile"] = "certFile";
    jsonConfig["keyFile"] = "keyFile";
    jsonConfig["passphrase"] = "passphrase";
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto res = exporter->GetAggregationTemporality(InstrumentType::GAUGE);
    EXPECT_EQ(res, AggregationTemporality::DELTA);
    res = exporter->GetAggregationTemporality(InstrumentType::COUNTER);
    EXPECT_EQ(res, AggregationTemporality::DELTA);
    res = exporter->GetAggregationTemporality(InstrumentType::HISTOGRAM);
    EXPECT_EQ(res, AggregationTemporality::CUMULATIVE);

    auto resFlush = exporter->ForceFlush(std::chrono::microseconds());
    EXPECT_TRUE(resFlush);
    auto resShutdown = exporter->Shutdown(std::chrono::microseconds());
    EXPECT_TRUE(resShutdown);
}

}  // namespace observability::test::exporter