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

#ifndef FUNCTIONSYSTEM_TEST_UNIT_KUBE_API_SERVER_H
#define FUNCTIONSYSTEM_TEST_UNIT_KUBE_API_SERVER_H

#include <queue>
#include <string>

#include "async/future.hpp"
#include "async/async.hpp"
#include "litebus.hpp"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "httpd/http_actor.hpp"


namespace functionsystem::kube_client::test {
using namespace litebus;
using namespace litebus::http;

using litebus::http::HeaderMap;
using litebus::http::HttpConnect;
using litebus::http::Request;
using litebus::http::Response;
using litebus::http::ResponseCode;
using litebus::http::URL;

class KubeApiServer : public litebus::http::HttpActor {
public:
    explicit KubeApiServer(const std::string &name) : HttpActor(name)
    {
    }

    void SetResponseQueue(const std::queue<std::pair<ResponseCode, std::string>> &responseQueue)
    {
        responseQueue_ = responseQueue;
    }

    void SetResponse(const Response &response)
    {
        responseOkQueue_.push(response);
    }

    std::queue<std::pair<ResponseCode, std::string>> GetResponseQueue()
    {
        return responseQueue_;
    }

    void SetTimeout(uint32_t timeout)
    {
        timeout_ = timeout;
    }
protected:
    virtual void Init() override
    {
        AddRoute("/api/v1/namespaces/default/pods", &KubeApiServer::HandlePodRequest);
        AddRoute("/api/v1/nodes", &KubeApiServer::HandleNodeRequest);
        AddRoute("/apis/apps/v1/namespaces/default/deployments", &KubeApiServer::HandleDeploymentRequest);
        AddRoute("/apis/coordination.k8s.io/v1/namespaces/default/leases", &KubeApiServer::HandleLeaseRequest);
        AddRoute("/apis/autoscaling/v2beta2/namespaces/default/horizontalpodautoscalers", &KubeApiServer::HandleHorizontalPodAutoscalerRequest);
        AddRoute("/nuwa-runtime", &KubeApiServer::HandleNuwaRequest);
        AddRoute("/apis/ray.io/v1/namespaces", &KubeApiServer::HandleRayRequest);
    }

    Future<Response> HandlePodRequest(const Request &request);

    Future<Response> HandleNodeRequest(const Request &request);

    Future<Response> HandleDeploymentRequest(const Request &request);

    Future<Response> HandleLeaseRequest(const Request &request);

    Future<Response> HandleHorizontalPodAutoscalerRequest(const Request &request);

    Future<Response> HandleNuwaRequest(const Request &request);

    Future<Response> HandleRayRequest(const Request &request);

    std::queue<std::pair<ResponseCode, std::string>> responseQueue_;

    std::queue<Response> responseOkQueue_;

    uint32_t timeout_{0};
};
}  // namespace functionsystem::kube_client::test

#endif //FUNCTIONSYSTEM_TEST_UNIT_KUBE_API_SERVER_H