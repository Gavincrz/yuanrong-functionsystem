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
#ifndef OBSERVABILITY_METRICS_JSON_PARSER_H
#define OBSERVABILITY_METRICS_JSON_PARSER_H

#include <nlohmann/json.hpp>

#include "sdk/include/metrics_data.h"

namespace observability {
namespace metrics {

class JsonParser {
public:
    const std::string Serialize(const MetricsData &metric) const
    {
        nlohmann::json metricJson;
        metricJson["name"] = metric.name;
        metricJson["description"] = metric.description;
        metricJson["unit"] = metric.unit;
        metricJson["type"] = metric.metricType;
        metricJson["value"] = ToString(metric.metricValue);
        auto duration = metric.collectTimeStamp.time_since_epoch();
        metricJson["timestamp_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        metricJson["labels"] = metric.labels;
        try {
            std::string metricString = metricJson.dump();
            return metricString;
        } catch (std::exception &e) {
            std::cerr << "dump metric json failed, error: " << e.what() << std::endl;
            return std::string();
        }
    }
};

}  // namespace metrics
}  // namespace observability

#endif  // OBSERVABILITY_METRICS_JSON_PARSER_H
