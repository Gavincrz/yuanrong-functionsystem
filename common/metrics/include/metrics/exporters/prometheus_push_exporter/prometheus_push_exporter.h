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

#ifndef OBSERVIBILITY_EXPORTERS_PROMETHEUS_PUSH_EXPORTER_H
#define OBSERVIBILITY_EXPORTERS_PROMETHEUS_PUSH_EXPORTER_H

#include "metrics/exporters/http_exporter/http_exporter.h"
#include "metrics/sdk/metric_processor.h"

namespace observability::exporters::metrics {

namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;

struct PrometheusPushExportOptions {
    std::string endpoint;
    std::string jobName;
    SSLConfig sslConfig;
    uint32_t heartbeatInterval = MetricsSdk::DEFAULT_HEARTBEAT_INTERVAL;
    std::string heartbeatUrl = "";
};

class PrometheusPushExporter : public observability::exporters::metrics::HttpExporter {
public:
    explicit PrometheusPushExporter(const std::string &config);
    explicit PrometheusPushExporter(const PrometheusPushExportOptions &options);

private:
    void Init(const PrometheusPushExportOptions &options);
};

}  // namespace observability::exporters::metrics

#endif  // OBSERVIBILITY_EXPORTERS_PROMETHEUS_PUSH_EXPORTER_H
