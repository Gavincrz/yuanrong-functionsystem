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

#ifndef OBSERVABILITY_SDK_METRICS_PROCESSOR_ACTOR_H
#define OBSERVABILITY_SDK_METRICS_PROCESSOR_ACTOR_H

#include <actor/actor.hpp>
#include <async/asyncafter.hpp>
#include <async/defer.hpp>

#include "common/include/constant.h"
#include "metrics/exporters/exporter.h"
#include "metrics/sdk/metric_processor.h"

namespace observability::sdk::metrics {
namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;

const yr_spdlog::level::level_enum LOGGER_LEVEL = yr_spdlog::level::info;

class ProcessorActor : public litebus::ActorBase {
public:
    using MetricLogger = std::shared_ptr<yr_spdlog::logger>;
    ProcessorActor(std::shared_ptr<MetricsExporter::Exporter> &&exporter, const ExportConfigs &exportConfigs);
    ~ProcessorActor() override = default;

    void Start();
    void Export(const MetricData &data);

    litebus::Future<AggregationTemporality> GetAggregationTemporality(MetricsSdk::InstrumentType instrumentType)
        noexcept;

    // for test
    [[maybe_unused]] std::vector<MetricData> GetMetricDataQueue()
    {
        return metricDataQueue_;
    }

    // for test
    [[maybe_unused]] std::vector<MetricData> GetFailureMetricDataQueue()
    {
        return failureMetricDataQueue_;
    }

    // for test
    [[maybe_unused]] void SetHealthyExporter(bool status)
    {
        healthyExporter_ = status;
    }

    // for test
    [[maybe_unused]] bool GetHealthyExporter()
    {
        return healthyExporter_;
    }

protected:
    void Finalize() override;

private:
    std::shared_ptr<MetricsExporter::Exporter> exporter_;
    ExportConfigs exportConfigs_;
    litebus::Timer batchExportTimer_;
    std::vector<MetricData> metricDataQueue_;
    std::vector<MetricData> failureMetricDataQueue_;
    std::atomic<bool> healthyExporter_ = true;

    MetricLogger metricLogger_{ nullptr };

    void OnBackendHealthChangeHandler(bool healthy);
    MetricsExporter::ExportResult SendData(const std::vector<MetricData> &vec);
    void ExportMetricQueueData();
    void ExportFailureQueueData();
    void ExportMetricDataFromFile(const std::string &path);

    void WriteIntoFailureQueue(const std::vector<MetricData> &vec);
    void WriteFailureQueueDataIntoFile();
    std::string ReadFailureDataFromFile(const std::string &path) const;

    void StartBatchExportTimer(const int interval);
    void InitMetricLogger();

    std::string Serialize(const MetricData &metricData) const;
    MetricData Deserialize(const std::string &content) const;
};

}
#endif // OBSERVABILITY_SDK_METRICS_PROCESSOR_ACTOR_H
