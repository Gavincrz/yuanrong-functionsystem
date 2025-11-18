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
#include "exporters/file_exporter/include/file_exporter.h"

#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <exception>
#include <mutex>
#include <nlohmann/json.hpp>

#include "common/file/file_sink.h"
#include "common/include/utils.h"
#include "metrics/api/alarm_data.h"
#include "metrics/sdk/instruments.h"
#include "metrics/exporters/file_exporter/file_exporter.h"

namespace observability {
namespace metrics {

FileExporter::FileExporter(const FileParam &fileParam)
{
    InitLogger(fileParam);
    InitJsonParser();
}

bool FileExporter::Export(const std::vector<MetricsData> &data)
{
    for (const auto &metric : data) {
        auto metricString = MetricSerialize(metric);

        metricLogger->Record(metricString);
    }
    return true;
}

bool FileExporter::ForceFlush()
{
    metricLogger->Flush();
    return true;
}

bool FileExporter::Finalize()
{
    return ForceFlush();
}

void FileExporter::InitLogger(const FileParam &fileParam)
{
    metricLogger = std::make_shared<MetricLogger>(fileParam);
}

void FileExporter::InitJsonParser()
{
    jsonParser = std::make_shared<JsonParser>();
}

std::string FileExporter::MetricSerialize(const MetricsData &metrics)
{
    return jsonParser->Serialize(metrics);
}

}  // namespace metrics

namespace exporters::metrics {
namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsApi = observability::api::metrics;
namespace MetricsExporter = observability::exporters::metrics;
const yr_spdlog::level::level_enum LOGGER_LEVEL = yr_spdlog::level::info;
const uint32_t MIN_FILE_CAPACITY = 1;
const uint32_t MAX_FILE_CAPACITY = 1024;
const uint32_t MIN_FILE_CNT = 1;
const uint32_t MAX_FILE_CNT = 100;

static std::string ToString(const MetricsSdk::InstrumentType type)
{
    switch (type) {
        case MetricsSdk::InstrumentType::COUNTER:
            return "Counter";
        case MetricsSdk::InstrumentType::GAUGE:
            return "Gauge";
        case MetricsSdk::InstrumentType::HISTOGRAM:
            return "Histogram";
        default:
            return "Unknown";
    }
}

static std::string ToString(const MetricsSdk::PointValue value)
{
    std::stringstream ss;
    std::visit([&ss](const auto &arg) { ss << std::boolalpha << arg; }, value);
    return ss.str();
}

std::string AlarmLevel2Str(MetricsApi::AlarmSeverity severity)
{
    std::unordered_map<MetricsApi::AlarmSeverity, std::string> m = {
        {MetricsApi::AlarmSeverity::CRITICAL, "critical"},
        {MetricsApi::AlarmSeverity::MAJOR, "major"},
        {MetricsApi::AlarmSeverity::MINOR, "minor"},
        {MetricsApi::AlarmSeverity::NOTICE, "notice"},
    };
    if (const auto &it = m.find(severity); it != m.end()) {
        return it->second;
    }
    return "";
}

void ParseRollingOptions(const nlohmann::json &rollingJson, FileExporterOptions &options)
{
    if (rollingJson.find("enable") != rollingJson.end() && rollingJson.at("enable").get<bool>()) {
        options.rolling = true;
        if (rollingJson.find("maxFiles") != rollingJson.end()) {
            uint64_t fileCnt = rollingJson.at("maxFiles").get<uint64_t>();
            if (fileCnt <= MAX_FILE_CNT && fileCnt >= MIN_FILE_CNT) {
                options.maxFiles = fileCnt;
            }
        }
        if (rollingJson.find("maxSize") != rollingJson.end()) {
            uint64_t fileSize = rollingJson.at("maxSize").get<uint64_t>();
            if (fileSize <= MAX_FILE_CAPACITY && fileSize >= MIN_FILE_CAPACITY) {
                options.maxSize = fileSize * SIZE_MEGA_BYTES;
            }
        }
        if (rollingJson.find("compress") != rollingJson.end()) {
            options.compress = rollingJson.at("compress").get<bool>();
        }
    }
}

void ParseFileExporterOptions(const std::string &config, FileExporterOptions &options)
{
    auto configJson = nlohmann::json::parse(config);
    if (configJson.find("fileDir") != configJson.end()) {
        options.fileDir = configJson.at("fileDir");
    }
    if (configJson.find("fileName") != configJson.end()) {
        options.fileName = configJson.at("fileName");
    }
    if (configJson.find("rolling") != configJson.end()) {
        auto rollingJson = configJson.at("rolling");
        ParseRollingOptions(rollingJson, options);
    }

    if (configJson.find("contentType") != configJson.end()) {
        if (configJson.at("contentType") == "STANDARD") {
            options.contentType = FileContentType::STANDARD;
        } else if (configJson.at("contentType") == "LABELS") {
            options.contentType = FileContentType::LABELS;
        }
    }
}

FileExporter::FileExporter(const std::string &config)
{
    FileExporterOptions options;
    try {
        ParseFileExporterOptions(config, options);
    } catch (std::exception &e) {
        std::cerr << "failed to parse FileExporterOptions" << std::endl;
        return;
    }
    options_ = options;
    InitExporter();
}

FileExporter::FileExporter(const FileExporterOptions &options) : options_(options)
{
    InitExporter();
}

FileExporter::~FileExporter()
{
    logExporter_ = nullptr;
}

void FileExporter::InitExporter() noexcept
{
    try {
        if (options_.fileDir.empty() || options_.fileName.empty()) {
            std::cerr << "failed to init file exporter, error: file dir or file name is invalid" << std::endl;
            return;
        }
        char realPath[PATH_MAX] = { 0 };
        if (realpath(options_.fileDir.c_str(), realPath) == nullptr) {
            std::cerr << "failed to init file exporter, error: realpath of FileExporterOptions.fileDir is invalid"
                      << std::endl;
            return;
        }
        std::string filePath = realPath;
        if (filePath.back() != '/') {
            filePath += '/';
        }
        if (options_.rolling) {
            logExporter_ = CreateRotatingLogger("FileExporterRotating", filePath + options_.fileName, options_.maxSize,
                                                options_.maxFiles, options_.compress);
        } else {
            logExporter_ = CreateBasicLogger("FileExporterBasic", filePath + options_.fileName, LOGGER_LEVEL);
        }
    } catch (const std::exception &e) {
        std::cerr << "failed to init file exporter, error: " << e.what() << std::endl;
    }
}

void FileExporter::RegisterOnHealthChangeCb(const std::function<void(bool)>& /* onChange */) noexcept
{
}

FileExporter::LogExporter FileExporter::CreateRotatingLogger(const std::string &loggerName, const std::string &filename,
                                                             size_t maxFileSize, size_t maxFiles, bool compress) const
{
    auto sink = std::make_shared<observability::metrics::common::FileSink>(filename, maxFileSize, maxFiles, true,
                                                                           compress);
    auto rotatingLogger = std::make_shared<yr_spdlog::logger>(loggerName, sink);
    rotatingLogger->set_level(LOGGER_LEVEL);
    auto f = std::make_unique<yr_spdlog::pattern_formatter>("%v", yr_spdlog::pattern_time_type::local, std::string(""));
    rotatingLogger->set_formatter(std::move(f));
    rotatingLogger->flush_on(LOGGER_LEVEL);
    return rotatingLogger;
}

FileExporter::LogExporter FileExporter::CreateBasicLogger(const std::string &loggerName, const std::string &filename,
                                                          yr_spdlog::level::level_enum logLevel) const
{
    auto sink = std::make_shared<yr_spdlog::sinks::basic_file_sink_st>(filename);
    auto basicLogger = std::make_shared<yr_spdlog::logger>(loggerName, sink);
    basicLogger->set_level(logLevel);
    auto f = std::make_unique<yr_spdlog::pattern_formatter>("%v", yr_spdlog::pattern_time_type::local, std::string(""));
    basicLogger->set_formatter(std::move(f));
    basicLogger->flush_on(logLevel);
    return basicLogger;
}

MetricsExporter::ExportResult FileExporter::Export(const std::vector<MetricsSdk::MetricData> &metricDataVec) noexcept
{
    if (logExporter_ == nullptr) {
        return MetricsExporter::ExportResult::FAILURE;
    }
    std::stringstream ss;
    for (auto &data : metricDataVec) {
        for (auto &d : data.pointData) {
            switch (options_.contentType) {
                case FileContentType::STANDARD:
                    SerializeMetricStandard(ss, data.instrumentDescriptor, data.collectionTs, d);
                    continue;
                case FileContentType::LABELS:
                    SerializeMetricLabels(ss, d.labels);
                    continue;
                default:
                    continue;
            }
        }
    }

    return LogToFile(ss);
}

// {"name":"memory_usage","description":"","type":"Gauge","unit":"KB","value":11000000,"timestamp_ms":1691056024621,
// "labels":{"job_id":"","instance_id":""}}
void FileExporter::SerializeMetricStandard(std::ostream &ss,
    const observability::sdk::metrics::InstrumentDescriptor &descriptor,
    const observability::sdk::metrics::PointTimeStamp &timestamp,
    const observability::sdk::metrics::PointData &data) const
{
    ss << "{";
    ss << R"("name":")" << descriptor.name << R"(",)";
    ss << R"("description":")" << descriptor.description << R"(",)";
    ss << R"("type":")" << ToString(descriptor.type) << R"(",)";
    ss << R"("unit":")" << descriptor.unit << R"(",)";
    ss << R"("value":")" << ToString(data.value) << R"(",)";
    ss << R"("timestamp_ms":)"
       << std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count())
       << ",";
    ss << R"("labels":{)";
    for (auto &l : data.labels) {
        ss << R"(")" << l.first << R"(":")" << l.second << R"(")";
        if (l != data.labels.back()) {
            ss << ",";
        }
    }
    ss << "}}" << "\n";
}

// {"job_id":"","instance_id":""}
void FileExporter::SerializeMetricLabels(std::ostream &ss,
                                         const observability::sdk::metrics::PointLabels &labels) const
{
    ss << "{";
    for (auto &l : labels) {
        ss << R"(")" << l.first << R"(":")" << l.second << R"(")";
        if (l != labels.back()) {
            ss << ",";
        }
    }
    ss << "}" << "\n";
}

MetricsExporter::ExportResult FileExporter::LogToFile(const std::stringstream &ss) const
{
    if (logExporter_ == nullptr) {
        std::cerr << "The log exporter did not initialize successfully. " << std::endl;
        return MetricsExporter::ExportResult::FAILURE;
    }
    try {
        GetLogFunc(LOGGER_LEVEL)(ss.str());
    } catch (const std::exception &e) {
        std::cerr << "failed to write metrics to file, error: " << e.what() << std::endl;
        return MetricsExporter::ExportResult::FAILURE;
    }
    return MetricsExporter::ExportResult::SUCCESS;
}

std::function<void(std::string)> FileExporter::GetLogFunc(const yr_spdlog::level::level_enum level) const
{
    switch (level) {
        case yr_spdlog::level::info:
            return [&](std::string content) { logExporter_->info(content); };
        case yr_spdlog::level::level_enum::trace:
        case yr_spdlog::level::level_enum::debug:
        case yr_spdlog::level::level_enum::warn:
        case yr_spdlog::level::level_enum::err:
        case yr_spdlog::level::level_enum::critical:
        case yr_spdlog::level::level_enum::off:
        case yr_spdlog::level::level_enum::n_levels:
        default:
            return [&](std::string content) { logExporter_->info(content); };
    }
}

MetricsSdk::AggregationTemporality FileExporter::GetAggregationTemporality(
    MetricsSdk::InstrumentType /* instrumentType */) const noexcept
{
    return MetricsSdk::AggregationTemporality::DELTA;
}

bool FileExporter::ForceFlush(std::chrono::microseconds /* timeout */) noexcept
{
    return true;
}

bool FileExporter::Shutdown(std::chrono::microseconds /* timeout */) noexcept
{
    return true;
}

}  // namespace exporters::metrics

}  // namespace observability