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

#include "metrics/exporters/http_exporter/http_exporter.h"

#include <functional>
#include <future>
#include <iostream>

#include "common/logs/log.h"
#include "metrics/exporters/http_exporter/curl_helper.h"
#include "http_heartbeat_client.h"

namespace observability::exporters::metrics {
namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;
const int32_t CODE_100 = 100;
const int32_t CODE_400 = 400;

MetricsExporter::ExportResult HttpExporter::Export(
    const std::vector<MetricsSdk::MetricData> &metricDataVec) noexcept
{
    std::unordered_map<std::string, MetricsSdk::MetricData> metricDataMap;
    for (auto &data : metricDataVec) {
        if (data.pointData.empty()) {
            continue;
        }
        if (const auto &it = metricDataMap.find(data.instrumentDescriptor.name); it != metricDataMap.end()) {
            it->second.pointData.push_back(data.pointData[0]);
        } else {
            metricDataMap[data.instrumentDescriptor.name] = data;
        }
    }
    std::ostringstream oss;
    for (auto &it : metricDataMap) {
        if (serializer_ == nullptr) {
            METRICS_LOG_ERROR("Failed to push metrics, serializer is nullptr, url({})", jobUrl_);
            std::cerr << "Failed to push metrics, curlHelper is nullptr, url: " << jobUrl_ << std::endl;
            return MetricsExporter::ExportResult::FAILURE;
        }
        serializer_->Serialize(oss, it.second);
    }
    return PushMetrics(method_, oss);
}

MetricsExporter::ExportResult HttpExporter::PushMetrics(HttpRequestMethod method, const std::ostringstream &oss)
{
    if (httpHeartBeatClient_ != nullptr && !healthy_.load()) {
        METRICS_LOG_INFO("Backend is not healthy, jobUrl is {}", jobUrl_);
        return MetricsExporter::ExportResult::FAILURE;
    }
    std::string url = jobUrl_;
    if (curlHelper_ == nullptr) {
        METRICS_LOG_ERROR("Failed to push metrics, curlHelper is nullptr, url({})", url);
        std::cerr << "Failed to push metrics, curlHelper is nullptr, url: " << url << std::endl;
        return MetricsExporter::ExportResult::FAILURE;
    }
    auto responseCode = curlHelper_->SendRequest(method, url, oss);
    if (responseCode < CODE_100 || responseCode >= CODE_400) {
        METRICS_LOG_ERROR("Failed to push metrics, errCode {}, url {}, method {}", responseCode, url,
                          static_cast<int>(method));
        std::cerr << "<HttpExporter> push metrics error, code: " << responseCode << std::endl;
        StartMonitor();
        return MetricsExporter::ExportResult::FAILURE;
    }
    return MetricsExporter::ExportResult::SUCCESS;
}

void HttpExporter::StartMonitor()
{
    if (httpHeartBeatClient_ == nullptr) {
        METRICS_LOG_INFO("Heartbeat not initialized, which url is {}", heartbeatParam_.heartbeatUrl);
        return;
    }
    httpHeartBeatClient_->Start();
}

void HttpExporter::RegisterOnHealthChangeCb(const std::function<void(bool)> &onChange) noexcept
{
    if (heartbeatParam_.heartbeatUrl.empty()) {
        METRICS_LOG_INFO("Heartbeat url is empty, register onchange skipped");
        return;
    }
    httpHeartBeatClient_ = std::make_shared<HttpHeartBeatClient>(heartbeatParam_);
    httpHeartBeatClient_->RegisterOnHealthChangeCb([&healthy(healthy_), onChange](bool newStatus) {
        healthy.store(newStatus);
        onChange(newStatus);
    });
}

MetricsSdk::AggregationTemporality HttpExporter::GetAggregationTemporality(
    MetricsSdk::InstrumentType instrumentType) const noexcept
{
    switch (instrumentType) {
        case MetricsSdk::InstrumentType::GAUGE:
        case MetricsSdk::InstrumentType::COUNTER:
            return observability::sdk::metrics::AggregationTemporality::DELTA;
        case MetricsSdk::InstrumentType::HISTOGRAM:
        default:
            return observability::sdk::metrics::AggregationTemporality::CUMULATIVE;
    }
}

bool HttpExporter::ForceFlush(std::chrono::microseconds /* timeout */) noexcept
{
    return true;
}

bool HttpExporter::Shutdown(std::chrono::microseconds /* timeout */) noexcept
{
    return true;
}

}  // namespace observability::exporters::metrics