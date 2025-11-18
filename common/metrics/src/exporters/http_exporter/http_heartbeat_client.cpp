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

#include "http_heartbeat_client.h"

namespace observability::exporters::metrics {

HttpHeartBeatClient::HttpHeartBeatClient(const HeartbeatParam &heartbeatParam)
{
    observer_ = std::make_shared<HttpHeartbeatObserver>(heartbeatParam);
    litebus::Spawn(observer_);
}

HttpHeartBeatClient::~HttpHeartBeatClient()
{
    if (observer_ != nullptr) {
        litebus::Terminate(observer_->GetAID());
        litebus::Await(observer_->GetAID());
    }
}

void HttpHeartBeatClient::RegisterOnHealthChangeCb(const std::function<void(bool)> &onChange)
{
    litebus::Async(observer_->GetAID(), &HttpHeartbeatObserver::RegisterOnHealthChangeCb, onChange);
}

void HttpHeartBeatClient::Start()
{
    litebus::Async(observer_->GetAID(), &HttpHeartbeatObserver::Start);
}
}
