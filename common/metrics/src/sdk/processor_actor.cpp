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

#include <fstream>
#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <utils/string_utils.hpp>
#include <utils/os_utils.hpp>
#include <nlohmann/json.hpp>

#include "common/include/constant.h"
#include "common/logs/log.h"
#include "common/file/file_sink.h"
#include "sdk/include/processor_actor.h"

namespace observability::sdk::metrics {
using namespace std;

static std::string ToString(const PointData &pointData)
{
    nlohmann::json pointDataJson;
    pointDataJson["labels"] = pointData.labels;
    std::stringstream ss;
    std::visit([&ss](const auto &arg) { ss << std::boolalpha << arg; }, pointData.value);
    pointDataJson["value"] = ss.str();
    try {
        return pointDataJson.dump();
    } catch (std::exception &e) {
        METRICS_LOG_ERROR("dump metric json failed, error: {}", e.what());
        return std::string();
    }
}

static PointData ToPointData(const std::string &content, InstrumentValueType valueType)
{
    auto pointDataJson = nlohmann::json::parse(content);
    PointData pd;
    pd.labels = pointDataJson.at("labels");

    std::string valStr = pointDataJson.at("value");
    switch (valueType) {
        case InstrumentValueType::UINT64: {
            uint64_t val = std::stoull(valStr);
            pd.value = val;
            break;
        }
        case InstrumentValueType::INT64: {
            int64_t val = std::stoll(valStr);
            pd.value = val;
            break;
        }
        case InstrumentValueType::DOUBLE:
        default: {
            double val = std::stod(valStr);
            pd.value = val;
        }
    }
    return pd;
}

static std::string GetInstrumentTypeDesc(const InstrumentType instrumentType)
{
    const std::unordered_map<InstrumentType, std::string> instrumentType2StrMap = {
        { InstrumentType::COUNTER, "COUNTER" }, { InstrumentType::GAUGE, "GAUGE" },
        { InstrumentType::HISTOGRAM, "HISTOGRAM" }};
    if (instrumentType2StrMap.find(instrumentType) == instrumentType2StrMap.end()) {
        return instrumentType2StrMap.find(InstrumentType::GAUGE)->second;
    }
    return instrumentType2StrMap.find(instrumentType)->second;
}

static InstrumentType GetInstrumentType(const std::string instrumentType)
{
    const std::unordered_map<std::string, InstrumentType> str2InstrumentTypeMap = {
        { "COUNTER", InstrumentType::COUNTER }, { "GAUGE", InstrumentType::GAUGE },
        { "HISTOGRAM", InstrumentType::HISTOGRAM }};
    if (str2InstrumentTypeMap.find(instrumentType) == str2InstrumentTypeMap.end()) {
        return str2InstrumentTypeMap.find("GAUGE")->second;
    }
    return str2InstrumentTypeMap.find(instrumentType)->second;
}

static std::string GetInstrumentValueTypeDesc(const InstrumentValueType instrumentValueType)
{
    const std::unordered_map<InstrumentValueType, std::string> instrumentValueType2StrMap = {
        { InstrumentValueType::UINT64, "UINT64" }, { InstrumentValueType::INT64, "INT64" },
        { InstrumentValueType::DOUBLE, "DOUBLE" }};
    if (instrumentValueType2StrMap.find(instrumentValueType) == instrumentValueType2StrMap.end()) {
        return instrumentValueType2StrMap.find(InstrumentValueType::DOUBLE)->second;
    }
    return instrumentValueType2StrMap.find(instrumentValueType)->second;
}

static InstrumentValueType GetInstrumentValueType(const std::string instrumentValueType)
{
    const std::unordered_map<std::string, InstrumentValueType> str2InstrumentValueTypeMap = {
        { "UINT64", InstrumentValueType::UINT64 }, { "DOUBLE", InstrumentValueType::DOUBLE },
        { "INT64", InstrumentValueType::INT64 }};
    if (str2InstrumentValueTypeMap.find(instrumentValueType) == str2InstrumentValueTypeMap.end()) {
        return str2InstrumentValueTypeMap.find("DOUBLE")->second;
    }
    return str2InstrumentValueTypeMap.find(instrumentValueType)->second;
}

static std::string GetAggregationTemporalityDesc(const AggregationTemporality aggregationTemporality)
{
    const std::unordered_map<AggregationTemporality, std::string> aggregationTemporality2StrMap = {
        { AggregationTemporality::UNSPECIFIED, "UNSPECIFIED" },
        { AggregationTemporality::CUMULATIVE, "CUMULATIVE" },
        { AggregationTemporality::DELTA, "DELTA" }};
    if (aggregationTemporality2StrMap.find(aggregationTemporality) == aggregationTemporality2StrMap.end()) {
        return aggregationTemporality2StrMap.find(AggregationTemporality::UNSPECIFIED)->second;
    }
    return aggregationTemporality2StrMap.find(aggregationTemporality)->second;
}

static AggregationTemporality GetAggregationTemporalityEnum(const std::string aggregationTemporality)
{
    const std::unordered_map<std::string, AggregationTemporality> str2AggregationTemporalityMap = {
        { "UNSPECIFIED", AggregationTemporality::UNSPECIFIED },
        { "CUMULATIVE", AggregationTemporality::CUMULATIVE },
        { "DELTA", AggregationTemporality::DELTA }};
    if (str2AggregationTemporalityMap.find(aggregationTemporality) == str2AggregationTemporalityMap.end()) {
        return str2AggregationTemporalityMap.find("UNSPECIFIED")->second;
    }
    return str2AggregationTemporalityMap.find(aggregationTemporality)->second;
}

static std::string GetFailureFileName()
{
    return "Failure.metrics";
}

ProcessorActor::ProcessorActor(std::shared_ptr<MetricsExporter::Exporter> &&exporter,
                               const ExportConfigs &exportConfigs)
    : litebus::ActorBase(exportConfigs.exporterName  + litebus::uuid_generator::UUID::GetRandomUUID().ToString()),
    exporter_(std::move(exporter)), exportConfigs_(exportConfigs)
{
}

void ProcessorActor::Finalize()
{
    METRICS_LOG_INFO("{} processor begins to destruct", exportConfigs_.exporterName);
    ExportFailureQueueData();
    ExportMetricDataFromFile(exportConfigs_.failureDataDir);
}

void ProcessorActor::Start()
{
    METRICS_LOG_INFO("Exporter {}, mode {}", exportConfigs_.exporterName,
        static_cast<std::underlying_type_t<ExportMode>>(exportConfigs_.exportMode));
    if (exporter_ == nullptr) {
        METRICS_LOG_ERROR("Exporter {} is nullptr", exportConfigs_.exporterName);
        return;
    }
    exporter_->RegisterOnHealthChangeCb([aid(GetAID())](bool newStatus) {
        litebus::Async(aid, &ProcessorActor::OnBackendHealthChangeHandler, newStatus);
    });
    InitMetricLogger();
    ExportMetricDataFromFile(exportConfigs_.failureDataDir);
    if (exportConfigs_.exportMode == ExportMode::BATCH) {
        StartBatchExportTimer(exportConfigs_.batchIntervalSec);
    }
}

void ProcessorActor::Export(const MetricData &data)
{
    if (!exportConfigs_.enabledInstruments.empty()) {
        if (exportConfigs_.enabledInstruments.count(data.instrumentDescriptor.name) <= 0) {
            METRICS_LOG_DEBUG("metric {} is not enabled in {}", data.instrumentDescriptor.name,
                              exportConfigs_.exporterName);
            return;
        }
    }
    metricDataQueue_.push_back(data);
    METRICS_LOG_DEBUG("{} metric queue push {}, count {}", exportConfigs_.exporterName, data.instrumentDescriptor.name,
                      metricDataQueue_.size());
    // if number of stored metric data exceeds batchSize_, export them
    if (metricDataQueue_.size() >= exportConfigs_.batchSize) {
        METRICS_LOG_DEBUG("{} metric queue {} exceeds exportConfigs batchSize {}", exportConfigs_.exporterName,
                          metricDataQueue_.size(), exportConfigs_.batchSize);
        ExportMetricQueueData();
    }
}

litebus::Future<AggregationTemporality> ProcessorActor::GetAggregationTemporality(
    MetricsSdk::InstrumentType instrumentType) noexcept
{
    return exporter_->GetAggregationTemporality(instrumentType);
}

void ProcessorActor::OnBackendHealthChangeHandler(bool healthy)
{
    healthyExporter_.store(healthy);
    // unhealthy -> healthy
    if (healthy) {
        METRICS_LOG_INFO("{} status changes to {}, try to send failure data again", exportConfigs_.exporterName,
                         healthyExporter_.load());
        ExportFailureQueueData();
        ExportMetricDataFromFile(exportConfigs_.failureDataDir);
    }
}

MetricsExporter::ExportResult ProcessorActor::SendData(const std::vector<MetricData> &vec)
{
    if (vec.size() == 0) {
        return MetricsExporter::ExportResult::EMPTY_DATA;
    }
    auto res = exporter_->Export(vec);
    METRICS_LOG_DEBUG("{} sends res: {}", exportConfigs_.exporterName,
                      static_cast<std::underlying_type_t<MetricsExporter::ExportResult>>(res));
    return res;
}

void ProcessorActor::ExportMetricQueueData()
{
    METRICS_LOG_DEBUG("{} begins to export metric queue, queue size: {}, exporter health is {}",
                      exportConfigs_.exporterName, metricDataQueue_.size(), healthyExporter_.load());
    auto vec = std::move(metricDataQueue_);
    MetricsExporter::ExportResult res = SendData(vec);
    // no data send, dont update health status
    if (res == MetricsExporter::ExportResult::EMPTY_DATA) {
        return;
    }
    // if current report is successful and previous report is failed, try to report data in failure queue and file
    if (res == MetricsExporter::ExportResult::SUCCESS && !healthyExporter_) {
        OnBackendHealthChangeHandler(true);
    }
    // failed export, write data into failure queue
    if (res != MetricsExporter::ExportResult::SUCCESS) {
        METRICS_LOG_ERROR("Failed to export metric queue data {}, exporter status {}", vec.size(),
                          healthyExporter_.load());
        healthyExporter_.store(false);
        WriteIntoFailureQueue(std::move(vec));
        return;
    }
    healthyExporter_.store(true);
}

void ProcessorActor::ExportFailureQueueData()
{
    METRICS_LOG_DEBUG("{} begins to export failure queue, queue size: {}, exporter health is {}",
                      exportConfigs_.exporterName, failureMetricDataQueue_.size(), healthyExporter_.load());
    MetricsExporter::ExportResult res = SendData(failureMetricDataQueue_);
    if (res != MetricsExporter::ExportResult::SUCCESS) {
        WriteFailureQueueDataIntoFile();
    } else {
        failureMetricDataQueue_.clear();
    };
}

void ProcessorActor::WriteIntoFailureQueue(const std::vector<MetricData> &vec)
{
    failureMetricDataQueue_.insert(failureMetricDataQueue_.end(), vec.begin(), vec.end());
    METRICS_LOG_DEBUG("{} writes data into failure metric data queue, num is: {}, threshold is {}",
                      exportConfigs_.exporterName, failureMetricDataQueue_.size(), exportConfigs_.failureQueueMaxSize);
    // failure queue exceeds threshold, write it into file
    if (failureMetricDataQueue_.size() >= exportConfigs_.failureQueueMaxSize) {
        WriteFailureQueueDataIntoFile();
    }
}

void ProcessorActor::WriteFailureQueueDataIntoFile()
{
    if (failureMetricDataQueue_.size() == 0) {
        METRICS_LOG_INFO("Failure queue is empty");
        return;
    }
    if (metricLogger_ == nullptr) {
        METRICS_LOG_INFO("Metrics logger is null");
        failureMetricDataQueue_.clear();
        return;
    }
    std::stringstream ss;
    for (auto &data : failureMetricDataQueue_) {
        ss << Serialize(data) << "\n";
    }
    try {
        metricLogger_->info(ss.str());
    } catch (const std::exception &ex) {
        METRICS_LOG_ERROR("Failed to write metrics to file, error: {}", ex.what());
        return;
    }
    METRICS_LOG_DEBUG("{} writes {} metrics into file", exportConfigs_.exporterName, failureMetricDataQueue_.size());
    failureMetricDataQueue_.clear();
}

void ProcessorActor::ExportMetricDataFromFile(const std::string &path)
{
    std::string fileName = path;
    if (fileName.back() != '/') {
        fileName += '/';
    }
    fileName += exportConfigs_.exporterName + GetFailureFileName();
    if (!litebus::os::ExistPath(fileName)) {
        METRICS_LOG_INFO("No failure metric file");
        return;
    }
    auto content = ReadFailureDataFromFile(fileName);
    if (content.empty()) {
        METRICS_LOG_INFO("No content in failure metric file");
        return;
    }

    std::vector<MetricData> vec;
    for (auto &it : litebus::strings::Split(content, "\n")) {
        if (it.empty()) {
            continue;
        }
        vec.push_back(std::move(Deserialize(it)));
    }
    METRICS_LOG_DEBUG("{} reads {} metrics", exportConfigs_.exporterName, vec.size());
    auto res = SendData(std::move(vec));
    if (res == MetricsExporter::ExportResult::SUCCESS) {
        if (truncate(fileName.c_str(), 0) != 0) {
            METRICS_LOG_ERROR("Failed to clear {}", fileName);
        }
    }
}

std::string ProcessorActor::ReadFailureDataFromFile(const std::string &path) const
{
    char realPath[PATH_MAX] = { 0 };
    if (realpath(path.c_str(), realPath) == nullptr) {
        METRICS_LOG_INFO("No real path of failure data file, error: {} is invalid", path);
        return "";
    }
    std::string filePath = std::string(realPath);
    std::ifstream in(filePath);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string contents(buffer.str());
    in.close();
    return contents;
}

void ProcessorActor::StartBatchExportTimer(const int interval)
{
    METRICS_LOG_DEBUG("Start batch export timer, interval is {}", interval);
    (void)litebus::Async(GetAID(), &ProcessorActor::ExportMetricQueueData);
    batchExportTimer_ = litebus::AsyncAfter(interval * ::observability::metrics::SEC2MS, GetAID(),
                                            &ProcessorActor::StartBatchExportTimer, interval);
}

void ProcessorActor::InitMetricLogger()
{
    if (metricLogger_ != nullptr) {
        return;
    }
    METRICS_LOG_INFO("{} starts init metric logger dir {}", exportConfigs_.exporterName, exportConfigs_.failureDataDir);
    char realPath[PATH_MAX] = { 0 };
    if (realpath(exportConfigs_.failureDataDir.c_str(), realPath) == nullptr) {
        METRICS_LOG_INFO("{} No metric data logger inited, because {} is invalid", exportConfigs_.exporterName,
                         exportConfigs_.failureDataDir);
        return;
    }
    std::string filePath = realPath;
    if (filePath.back() != '/') {
        filePath += '/';
    }
    auto sink = std::make_shared<::observability::metrics::common::FileSink>(
             filePath + exportConfigs_.exporterName + GetFailureFileName(),
             exportConfigs_.failureDataFileMaxCapacity * SIZE_MEGA_BYTES, 1, false);
    metricLogger_ = std::make_shared<yr_spdlog::logger>(exportConfigs_.exporterName + "FailureFileLogger", sink);
    metricLogger_->set_level(LOGGER_LEVEL);
    metricLogger_->set_pattern("%v");
    metricLogger_->flush_on(LOGGER_LEVEL);
    METRICS_LOG_INFO("Metric logger inited");
}

std::string ProcessorActor::Serialize(const MetricData &metricData) const
{
    nlohmann::json instrumentDescriptorJson;
    InstrumentDescriptor instrumentDescriptor = metricData.instrumentDescriptor;
    instrumentDescriptorJson["name"] = instrumentDescriptor.name;
    instrumentDescriptorJson["description"] = instrumentDescriptor.description;
    instrumentDescriptorJson["unit"] = instrumentDescriptor.unit;
    instrumentDescriptorJson["type"] = GetInstrumentTypeDesc(instrumentDescriptor.type);
    instrumentDescriptorJson["valueType"] = GetInstrumentValueTypeDesc(instrumentDescriptor.valueType);

    nlohmann::json metricDataJson;
    try {
        metricDataJson["instrumentDescriptor"] = instrumentDescriptorJson.dump();
    } catch (std::exception &e) {
        METRICS_LOG_ERROR("dump metric instrumentDescriptorJson failed, error: {}", e.what());
    }
    metricDataJson["aggregationTemporality"] = GetAggregationTemporalityDesc(metricData.aggregationTemporality);
    metricDataJson["collectionTs"] =std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        metricData.collectionTs.time_since_epoch()).count());

    std::stringstream ss;
    ss << ToString(metricData.pointData[0]);
    metricDataJson["pointData"] = ss.str();
    try {
        return metricDataJson.dump();
    } catch (std::exception &e) {
        METRICS_LOG_ERROR("dump metricDataJson failed, error: {}", e.what());
        return std::string();
    }
}

MetricData ProcessorActor::Deserialize(const std::string &content) const
{
    MetricData metricData;
    try {
        auto metricDataJson = nlohmann::json::parse(content);
        if (metricDataJson.contains("aggregationTemporality")) {
            metricData.aggregationTemporality = GetAggregationTemporalityEnum(
                metricDataJson.at("aggregationTemporality"));
        }
        if (metricDataJson.contains("collectionTs")) {
            std::string timeStr = metricDataJson.at("collectionTs");
        auto milliseconds = std::chrono::milliseconds{std::stoll(timeStr)};
        metricData.collectionTs =
            std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(milliseconds);
    }
    if (metricDataJson.contains("instrumentDescriptor")) {
        std::string descriptorStr = metricDataJson.at("instrumentDescriptor");
        auto instrumentDescriptorJson = nlohmann::json::parse(descriptorStr);
        InstrumentDescriptor descriptor;
        descriptor.description = instrumentDescriptorJson.at("description");
        descriptor.name = instrumentDescriptorJson.at("name");
        descriptor.type = GetInstrumentType(instrumentDescriptorJson.at("type"));
        descriptor.unit = instrumentDescriptorJson.at("unit");
        descriptor.valueType = GetInstrumentValueType(instrumentDescriptorJson.at("valueType"));
        metricData.instrumentDescriptor = descriptor;
    }
    if (metricDataJson.contains("pointData")) {
            metricData.pointData.push_back(ToPointData(metricDataJson.at("pointData"),
                                                       metricData.instrumentDescriptor.valueType));
        }
    } catch (std::exception &e) {
        METRICS_LOG_ERROR("parse metric data json failed, error: {}", e.what());
    }
    return metricData;
}

} // namespace observability::sdk::metrics
