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

#ifndef FUNCTION_AGENT_MOCK_CLOUD_API_GATEWAY_H
#define FUNCTION_AGENT_MOCK_CLOUD_API_GATEWAY_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <httpd/http_connect.hpp>

#include "common/http/api_router_register.h"
#include "common/http/http_server.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {

using namespace std;
using namespace litebus::http;
using namespace functionsystem;

// URI path
const std::string TOKENS_PATH = "/OS-AUTH/id-token/tokens";
const std::string SECURITY_TOKENS_PATH = "/OS-CREDENTIAL/securitytokens";

// mock actor, resister url-handler into http server
class MockCloudApiGateway : public ApiRouterRegister, public litebus::ActorBase {
public:
    explicit MockCloudApiGateway(const string &name) : ApiRouterRegister(), ActorBase(name)
    {
        ApiRouterRegister::RegisterHandler(TOKENS_PATH, [aid(GetAID())](const HttpRequest &req) {
            return litebus::Async(aid, &MockCloudApiGateway::TestIdTokenHandler, req);
        });

        ApiRouterRegister::RegisterHandler(SECURITY_TOKENS_PATH, [aid(GetAID())](const HttpRequest &req) {
            return litebus::Async(aid, &MockCloudApiGateway::TestSecurityTokensHandler, req);
        });
    }

    MOCK_METHOD(litebus::Future<HttpResponse>, TestIdTokenHandler, (HttpRequest request), ());
    MOCK_METHOD(litebus::Future<HttpResponse>, TestSecurityTokensHandler, (HttpRequest request), ());
};

} // namespace functionsystem::test

#endif // FUNCTION_AGENT_MOCK_CLOUD_API_GATEWAY_H