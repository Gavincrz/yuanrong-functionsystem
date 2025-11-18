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

#ifndef OBSERVABILITY_PROCESSOR_ACTOR_H
#define OBSERVABILITY_PROCESSOR_ACTOR_H

#include <actor/actor.hpp>
#include <async/asyncafter.hpp>
#include <async/defer.hpp>
#include <chrono>
#include <unordered_set>

#include "api/include/basic_metric.h"
#include "common/include/constant.h"
#include "sdk/include/basic_exporter.h"

namespace observability {
namespace metrics {
using CollectFunc = const std::vector<MetricsData>(const std::chrono::system_clock::time_point &timeStamp,
                                                   const int interval);
using ExportFunc = bool(const std::vector<MetricsData> &data);

class ProcessorActor : public litebus::ActorBase {
public:
    explicit ProcessorActor() : ActorBase(PROCESS_ACTOR_NAME)
    {
    }
    ~ProcessorActor() override = default;
    void RegisterTimer(const int interval);
    void RegisterCollectFunc(const std::function<CollectFunc> &collectFunc);
    void RegisterExportFunc(const std::function<ExportFunc> &exportFunc);
    void SetExportMode(const ExporterOptions &options);
    void ReportData(const int interval);
    bool ExportAllData();
    void ExportTemporarilyData(const std::shared_ptr<BasicMetric> &instrument);

protected:
    void Finalize() override;

private:
    void StartBatchExportTimer(const int interval);
    void CollectAndStore(const int interval);
    void CollectOnceThenExport(const int interval);
    std::vector<MetricsData> GetData(const int interval);
    litebus::Future<bool> PutData(const std::vector<MetricsData> &data);
    std::vector<MetricsData> GetTemporarilyData(const std::shared_ptr<BasicMetric> &instrument);
    std::vector<MetricsData> buffer_;
    std::map<int, litebus::Timer> collectTimerInfos_;
    void (ProcessorActor::*processMethod_)(const int){ nullptr };
    std::function<CollectFunc> collectFunc_{ nullptr };
    std::function<ExportFunc> exportFunc_{ nullptr };
    litebus::Timer batchExportTimer_;
    uint32_t exportBatchSize_ = 0;
    std::unordered_set<int> collectTimers_;
};
}  // namespace metrics
}  // namespace observability
#endif  // OBSERVABILITY_PROCESSOR_ACTOR_H
