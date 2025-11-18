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

#include "http_heartbeat_observer.h"

#include "common/logs/log.h"

namespace observability::exporters::metrics {

const int32_t CODE_OK = 200;

HttpHeartbeatObserver::HttpHeartbeatObserver(const HeartbeatParam &heartbeatParam)
    : litebus::ActorBase("HttpHeartbeatActor"  + litebus::uuid_generator::UUID::GetRandomUUID().ToString()),
    pingCycleMs_(heartbeatParam.heartbeatInterval), url_(heartbeatParam.heartbeatUrl), method_(heartbeatParam.method)
{
    curlHelper_ = std::make_shared<CurlHelper>();
    if (curlHelper_) {
        curlHelper_->SetHttpHeader(heartbeatParam.httpHeader.c_str());
        curlHelper_->SetSSLConfig(heartbeatParam.sslConfig);
    }
}

void HttpHeartbeatObserver::RegisterOnHealthChangeCb(const std::function<void(bool)> &onChange)
{
    onChange_ = onChange;
}

void HttpHeartbeatObserver::Start()
{
    if (!healthy_.load() || url_.empty()) {
        METRICS_LOG_INFO("Can not start http heartbeat, health status is {}, url is {}", healthy_.load(), url_);
        return;
    }
    Ping();
}

void HttpHeartbeatObserver::Stop()
{
    if (!timer_.GetTimeWatch().Expired()) {
        METRICS_LOG_INFO("heartbeat({}) cancel send ping", std::string(GetAID()));
        (void)litebus::TimerTools::Cancel(timer_);
    }
}

void HttpHeartbeatObserver::Ping()
{
    std::ostringstream oss;
    auto responseCode = curlHelper_->SendRequest(method_, url_, oss);
    if (responseCode != CODE_OK) {
        METRICS_LOG_WARN("{} metrics export backend health check res is {}", GetAID().Name(), responseCode);
        healthy_.store(false);
        if (onChange_ != nullptr) {
            onChange_(false);
        }
        timer_ = litebus::AsyncAfter(pingCycleMs_, GetAID(), &HttpHeartbeatObserver::Ping);
    } else {
        METRICS_LOG_INFO("{} metrics export backend health check finishes, exporter is healthy", GetAID().Name());
        healthy_.store(true);
        if (onChange_ != nullptr) {
            onChange_(true);
        }
        Stop();
    }
}

void HttpHeartbeatObserver::Finalize()
{
    (void)Stop();
}
}
