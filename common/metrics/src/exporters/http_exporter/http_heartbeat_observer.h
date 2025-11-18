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

#ifndef OBSERVABILITY_METRICS_HTTP_HEARTBEAT_OBSERVER_H
#define OBSERVABILITY_METRICS_HTTP_HEARTBEAT_OBSERVER_H

#include "actor/actor.hpp"
#include "async/asyncafter.hpp"
#include "async/defer.hpp"

#include "metrics/exporters/http_exporter/curl_helper.h"
#include "metrics/exporters/http_exporter/http_exporter.h"

namespace observability::exporters::metrics {

class HttpHeartbeatObserver : public litebus::ActorBase {
public:
    explicit HttpHeartbeatObserver(const HeartbeatParam &heartbeatParam);
    ~HttpHeartbeatObserver() override = default;
    void Start();
    void Stop();
    void RegisterOnHealthChangeCb(const std::function<void(bool)> &onChange);

protected:
    void Finalize() override;

private:
    std::atomic<bool> healthy_ = true;
    uint32_t pingCycleMs_;  // millisecond
    std::string url_;
    HttpRequestMethod method_ = HttpRequestMethod::GET;
    std::shared_ptr<CurlHelper> curlHelper_;
    std::function<void(bool)> onChange_;
    litebus::Timer timer_;

    void Ping();
};
}
#endif // FUNCTIONSYSTEM_HTTP_HEARTBEAT_OBSERVER_H
