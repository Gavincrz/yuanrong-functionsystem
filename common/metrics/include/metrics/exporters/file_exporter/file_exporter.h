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

#ifndef OBSERVABILITY_EXPORTERS_METRICS_FILE_EXPORTER_H
#define OBSERVABILITY_EXPORTERS_METRICS_FILE_EXPORTER_H

#include <spdlog/spdlog.h>

#include <functional>
#include <string>

#include "metrics/exporters/exporter.h"

namespace observability::exporters::metrics {

enum class FileContentType { STANDARD, LABELS };

const uint64_t DEFAULT_MAX_FILE_NUM = 3;
const uint32_t SIZE_MEGA_BYTES = 1024 * 1024;             // 1 MB
const uint64_t DEFAULT_MAX_SIZE = 100 * SIZE_MEGA_BYTES;  // 100 MB

struct FileExporterOptions {
    std::string fileDir;
    std::string fileName;
    bool rolling = false;
    bool compress = false;
    uint64_t maxFiles = DEFAULT_MAX_FILE_NUM;
    uint64_t maxSize = DEFAULT_MAX_SIZE;
    FileContentType contentType = FileContentType::STANDARD;
};

class FileExporter : public observability::exporters::metrics::Exporter {
public:
    using LogExporter = std::shared_ptr<yr_spdlog::logger>;
    explicit FileExporter(const std::string &config);
    explicit FileExporter(const FileExporterOptions &options);
    ~FileExporter() override;

    ExportResult Export(const std::vector<observability::sdk::metrics::MetricData> &metricDataVec) noexcept override;

    observability::sdk::metrics::AggregationTemporality GetAggregationTemporality(
        observability::sdk::metrics::InstrumentType instrumentType) const noexcept override;

    bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

    bool Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds(0)) noexcept override;

    void RegisterOnHealthChangeCb(const std::function<void(bool)> &onChange) noexcept override;

private:
    void InitExporter() noexcept;
    LogExporter CreateRotatingLogger(const std::string &loggerName, const std::string &filename, size_t maxFileSize,
                                     size_t maxFiles, bool compress) const;
    LogExporter CreateBasicLogger(const std::string &loggerName, const std::string &filename,
                                  yr_spdlog::level::level_enum logLevel) const;
    void SerializeMetricStandard(std::ostream &ss,
                                 const observability::sdk::metrics::InstrumentDescriptor &descriptor,
                                 const observability::sdk::metrics::PointTimeStamp &timestamp,
                                 const observability::sdk::metrics::PointData &data) const;

    void SerializeMetricLabels(std::ostream &ss, const observability::sdk::metrics::PointLabels &labels) const;

    ExportResult LogToFile(const std::stringstream &ss) const;
    std::function<void(std::string)> GetLogFunc(const yr_spdlog::level::level_enum level) const;
    FileExporterOptions options_;

    LogExporter logExporter_{ nullptr };
};

}  // namespace observability::exporters::metrics

#endif