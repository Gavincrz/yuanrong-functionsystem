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

#ifndef OBSERVABILITY_METRICS_TEST_MOCK_EXPORTER_H
#define OBSERVABILITY_METRICS_TEST_MOCK_EXPORTER_H

#include "metrics/exporters/exporter.h"

#include "gmock/gmock.h"

namespace observability::test {
namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;

class MockExporter : public MetricsExporter::Exporter {
public:
    MockExporter() {};
   ~MockExporter() = default;
   MOCK_METHOD(MetricsExporter::ExportResult, Export, (const std::vector<MetricsSdk::MetricData> &metricData), (override, noexcept));
   MOCK_METHOD(MetricsSdk::AggregationTemporality, GetAggregationTemporality, (MetricsSdk::InstrumentType instrumentType), (override, const, noexcept));
   MOCK_METHOD(bool, ForceFlush, (std::chrono::microseconds timeout), (override, noexcept));
   MOCK_METHOD(bool, Shutdown, (std::chrono::microseconds timeout), (override, noexcept));
   MOCK_METHOD(void, RegisterOnHealthChangeCb, (const std::function<void (bool)> &onChange), (override, noexcept));
};

}
#endif // OBSERVABILITY_METRICS_TEST_MOCK_EXPORTER_H