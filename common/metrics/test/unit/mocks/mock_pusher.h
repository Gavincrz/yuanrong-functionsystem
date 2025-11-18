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

#ifndef OBSERVABILITY_METRICS_TEST_MOCK_PUSHER_H
#define OBSERVABILITY_METRICS_TEST_MOCK_PUSHER_H

#include "metrics/sdk/metric_pusher.h"

#include "gmock/gmock.h"

namespace observability::test {
namespace MetricsSdk = observability::sdk::metrics;

class MockPusher : public sdk::metrics::PusherHandle {
public:
    MockPusher() {};
    ~MockPusher() = default;
    MOCK_METHOD(void, Push, (const sdk::metrics::MetricData &metricData), (override, noexcept));
    MOCK_METHOD(MetricsSdk::AggregationTemporality, GetAggregationTemporality, (MetricsSdk::InstrumentType instrumentType), (override, noexcept));
};

}
#endif // OBSERVABILITY_METRICS_TEST_MOCK_PUSHER_H