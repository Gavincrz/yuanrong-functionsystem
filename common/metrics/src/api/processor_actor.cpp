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

#include "api/include/processor_actor.h"
#include "common/include/transfer.h"

namespace observability {
namespace metrics {

void ProcessorActor::SetExportMode(const ExporterOptions &options)
{
    if (options.mode == ExporterOptions::Mode::SIMPLE) {
        processMethod_ = &ProcessorActor::CollectOnceThenExport;
    } else {
        processMethod_ = &ProcessorActor::CollectAndStore;
        exportBatchSize_ = options.batchSize;
        StartBatchExportTimer(static_cast<int>(options.batchIntervalSec));
    }
}

void ProcessorActor::Finalize()
{
    for (const auto &timerInfo : collectTimerInfos_) {
        auto timer = timerInfo.second;
        (void)litebus::TimerTools::Cancel(timer);
    }

    (void)litebus::TimerTools::Cancel(batchExportTimer_);
    collectTimerInfos_.clear();
    collectTimers_.clear();
}

void ProcessorActor::RegisterTimer(const int interval)
{
    auto it = collectTimers_.find(interval);
    if (it == collectTimers_.end()) {
        (void)collectTimers_.insert(interval);
        ReportData(interval);
    }
}

void ProcessorActor::RegisterCollectFunc(const std::function<CollectFunc> &collectFunc)
{
    collectFunc_ = collectFunc;
}

void ProcessorActor::RegisterExportFunc(const std::function<ExportFunc> &exportFunc)
{
    exportFunc_ = exportFunc;
}

void ProcessorActor::CollectOnceThenExport(const int interval)
{
    litebus::Async(GetAID(), &ProcessorActor::GetData, interval)
        .Then([exportFunc(exportFunc_)](const std::vector<MetricsData> &data) {
            return exportFunc(data);
        });
    if (interval > 0) {
        collectTimerInfos_[interval] =
            litebus::AsyncAfter(interval * SEC2MS, GetAID(), &ProcessorActor::CollectOnceThenExport, interval);
    }
}

void ProcessorActor::ReportData(const int interval)
{
    litebus::Async(GetAID(), processMethod_, interval);
}

void ProcessorActor::StartBatchExportTimer(const int interval)
{
    (void)litebus::Async(GetAID(), &ProcessorActor::ExportAllData);
    batchExportTimer_ =
        litebus::AsyncAfter(interval * SEC2MS, GetAID(), &ProcessorActor::StartBatchExportTimer, interval);
}

void ProcessorActor::CollectAndStore(const int interval)
{
    litebus::Async(GetAID(), &ProcessorActor::GetData, interval)
        .Then(litebus::Defer(GetAID(), &ProcessorActor::PutData, std::placeholders::_1))
        .Then(litebus::Defer(GetAID(), &ProcessorActor::ExportAllData));

    if (interval > 0) {
        collectTimerInfos_[interval] =
            litebus::AsyncAfter(interval * SEC2MS, GetAID(), &ProcessorActor::CollectAndStore, interval);
    }
}

litebus::Future<bool> ProcessorActor::PutData(const std::vector<MetricsData> &data)
{
    litebus::Promise<bool> promise;
    (void)buffer_.insert(buffer_.end(), data.begin(), data.end());
    if (buffer_.size() < exportBatchSize_) {
        promise.SetFailed(-1);
    } else {
        promise.SetValue(true);
    }
    return promise.GetFuture();
}

void ProcessorActor::ExportTemporarilyData(const std::shared_ptr<BasicMetric> &instrument)
{
    if (exportBatchSize_ == 0) {
        litebus::Async(GetAID(), &ProcessorActor::GetTemporarilyData, instrument)
            .Then([exportFunc(exportFunc_)](const std::vector<MetricsData> &data) {
                return exportFunc(data);
            });
        return;
    }
    litebus::Async(GetAID(), &ProcessorActor::GetTemporarilyData, instrument)
        .Then(litebus::Defer(GetAID(), &ProcessorActor::PutData, std::placeholders::_1))
        .Then(litebus::Defer(GetAID(), &ProcessorActor::ExportAllData));
}

std::vector<MetricsData> ProcessorActor::GetTemporarilyData(const std::shared_ptr<BasicMetric> &instrument)
{
    std::vector<MetricsData> metricDataList;
    auto timestamp = instrument->GetTimestamp().time_since_epoch().count() == 0 ? std::chrono::system_clock::now()
                                                                                : instrument->GetTimestamp();
    MetricsData metricData = {
        .labels = instrument->GetLabels(),
        .name = instrument->GetName(),
        .description = instrument->GetDescription(),
        .unit = instrument->GetUnit(),
        .metricType = GetMetricTypeStr(instrument->GetMetricType()),
        .collectTimeStamp = timestamp,
        .metricValue = GetInstrumentValue(instrument)
    };
    metricDataList.push_back(metricData);
    return metricDataList;
}

std::vector<MetricsData> ProcessorActor::GetData(const int interval)
{
    return collectFunc_(std::chrono::system_clock::now(), interval);
}

bool ProcessorActor::ExportAllData()
{
    if (!buffer_.empty()) {
        auto isOk = exportFunc_(buffer_);
        buffer_.clear();
        return isOk;
    }
    return true;
}

}  // namespace metrics
}  // namespace observability