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

#ifndef OBSERVABILITY_BASIC_EXPORTER_H
#define OBSERVABILITY_BASIC_EXPORTER_H

#include <vector>

#include "common/include/constant.h"
#include "sdk/include/metrics_data.h"

namespace observability {
namespace metrics {
/**
 * @brief Exporter Options, for Set Export Mode
 * @param mode
 *        Simple: Export the data immediately after it is collected
 *        Batch: The data is exported when the number of stored data reaches a specified value
 * @param batchSize metric number reaches a specified value
 */
struct ExporterOptions {
    enum class Mode {
        SIMPLE,
        BATCH,
    };
    Mode mode = Mode::SIMPLE;
    uint32_t batchSize = DEFAULT_EXPORT_BATCH_SIZE;
    uint32_t batchIntervalSec = DEFAULT_EXPORT_BATCH_INTERVAL_SEC;
};

class BasicExporter {
public:
    BasicExporter() = default;
    virtual ~BasicExporter() = default;

    /**
     * @brief Export the collected data, inherited exports should implement it
     * @param data vector of collected data
     */
    virtual bool Export(const std::vector<MetricsData> &data) = 0;

    /**
     * @brief Force flush action, inherited exports should implement it
     */
    virtual bool ForceFlush() = 0;

    /**
     * @brief Finalize action, inherited exports should implement it
     */
    virtual bool Finalize() = 0;

    /**
     * @brief Get Exporter options
     */
    const ExporterOptions GetExporterOptions()
    {
        return exporterOptions_;
    }

    void SetExporterOptions(const ExporterOptions &options)
    {
        exporterOptions_ = options;
    }

protected:
    ExporterOptions exporterOptions_;
};
// dlsym type
typedef BasicExporter *(*ExporterCreateFunc)(const ExporterOptions options);
typedef void (*ExporterDelFunc)(BasicExporter *);
}  // namespace metrics
}  // namespace observability
#endif  // OBSERVABILITY_BASIC_EXPORTER_H
