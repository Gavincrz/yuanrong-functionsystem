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

#include <gtest/gtest.h>

#include "kube_api_server.h"

namespace functionsystem::kube_client::test {
using namespace litebus;
using namespace litebus::http;
using namespace std;

using litebus::http::HeaderMap;
using litebus::http::HttpConnect;
using litebus::http::Request;
using litebus::http::Response;
using litebus::http::ResponseCode;
using litebus::http::URL;


Future<Response> KubeApiServer::HandlePodRequest(const Request &request)
{
    if (!responseQueue_.empty()) {
        auto resp = Response(responseQueue_.front().first, responseQueue_.front().second, ResponseBodyType::JSON);
        responseQueue_.pop();
        return resp;
    }
    if (request.method == "POST" || request.method == "PATCH") {
        return Ok(request.body, ResponseBodyType::JSON);
    } else if(request.method == "DELETE") {
        string response = "{\"metadata\":{\"name\":\"function-agent-pod-0\"}}";
        return Ok(response, ResponseBodyType::JSON);
    } else {
        string response = "{\"kind\": \"list\" }";
        return Ok(response, ResponseBodyType::JSON);
    }
}

Future<Response> KubeApiServer::HandleNodeRequest(const Request &request)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_));
    if (!responseQueue_.empty()) {
        auto resp = Response(responseQueue_.front().first, responseQueue_.front().second, ResponseBodyType::JSON);
        responseQueue_.pop();
        return resp;
    }
    if (request.method == "POST" || request.method == "PATCH") {
        return Ok(request.body, ResponseBodyType::JSON);
    } else {
        string response = "{\"kind\": \"list\",\"items\":[],\"metadata\":{}}";
        return Ok(response, ResponseBodyType::JSON);
    }
}

Future<Response> KubeApiServer::HandleDeploymentRequest(const Request &request)
{
    if (!responseQueue_.empty()) {
        auto resp = Response(responseQueue_.front().first, responseQueue_.front().second, ResponseBodyType::JSON);
        responseQueue_.pop();
        return resp;
    }
    if (request.method == "POST" || request.method == "PATCH") {
        return Ok(request.body, ResponseBodyType::JSON);
    }  else if(request.method == "DELETE") {
        string response = "{\"metadata\":{\"name\":\"function-agent-pool1\"}}";
        return Ok(response, ResponseBodyType::JSON);
    } else {
        string response = "{\"kind\": \"list\" }";
        return Ok(response, ResponseBodyType::JSON);
    }
}

Future<Response> KubeApiServer::HandleLeaseRequest(const litebus::http::Request &request)
{
    if (!responseQueue_.empty()) {
        auto resp = Response(responseQueue_.front().first, responseQueue_.front().second, ResponseBodyType::JSON);
        responseQueue_.pop();
        return resp;
    }
    return Ok(request.body, ResponseBodyType::JSON);
}

Future<Response> KubeApiServer::HandleHorizontalPodAutoscalerRequest(const Request &request)
{
    if (!responseQueue_.empty()) {
        auto resp = Response(responseQueue_.front().first, responseQueue_.front().second, ResponseBodyType::JSON);
        responseQueue_.pop();
        return resp;
    }
    if (request.method == "POST" || request.method == "PATCH") {
        return Ok(request.body, ResponseBodyType::JSON);
    }  else if(request.method == "DELETE") {
        string response = "{\"metadata\":{\"name\":\"function-agent-pool1-hpa\"}}";
        return Ok(response, ResponseBodyType::JSON);
    } else {
        string response = "{\"kind\": \"list\" }";
        return Ok(response, ResponseBodyType::JSON);
    }
}

Future<Response> KubeApiServer::HandleNuwaRequest(const Request &request)
{
    if (!responseQueue_.empty()) {
        auto resp = Response(responseQueue_.front().first, responseQueue_.front().second, ResponseBodyType::JSON);
        responseQueue_.pop();
        return resp;
    }
    if (!responseOkQueue_.empty()) {
        auto resp = responseOkQueue_.front();
        responseOkQueue_.pop();
        return resp;
    }
    if (request.method == "GET") {
        string response = "{\"instanceId\": \"123\"}";
        return Ok(response, ResponseBodyType::JSON);
    }
    return Ok(request.body, ResponseBodyType::JSON);
}

Future<Response> KubeApiServer::HandleRayRequest(const Request &request)
{
    if (!responseOkQueue_.empty()) {
        auto resp = responseOkQueue_.front();
        responseOkQueue_.pop();
        return resp;
    }
    string response = R"({"spec": {"workerGroupSpecs": [ {"groupName": "group", "replicas": 1 }]}})";
    return Ok(response, ResponseBodyType::JSON);
}
}  // namespace functionsystem::kube_client::test
