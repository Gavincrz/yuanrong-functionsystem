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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <httpd/http_connect.hpp>

#include "common/http/api_router_register.h"
#include "common/http/http_server.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

using namespace std;
using namespace litebus::http;
using namespace functionsystem;

const string HTTP_SERVER_NAME = "serverless";
const string TCP_IP = "tcp://127.0.0.1";
const string SUCCESS_API = "/v1/posix/instance/success";
const string FAILED_API = "/v1/posix/instance/failed";
const string PREFIX_API = "/v1/posix/instance";
const string REQ_DATA = string(1024 * 1024 * 10, 'a');
const string CONTENT_TYPE = "text/html";

class MockApiRouterRegister : public ApiRouterRegister {
public:
    MOCK_CONST_METHOD0(GetHandlers, std::shared_ptr<HandlerMap>());

    void RegisterHandler(const std::string &url, const HttpHandler &handler) const override
    {
        ApiRouterRegister::RegisterHandler(url, handler);
    };
};

class HttpTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        httpServer_->RegisterRoute(testActor_);
        litebus::Spawn(testActor_);
        litebus::Spawn(httpServer_);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        litebus::Terminate(httpServer_->GetAID());
        litebus::Await(httpServer_->GetAID());

        litebus::Terminate(testActor_->GetAID());
        litebus::Await(testActor_->GetAID());

        httpServer_ = nullptr;
        testActor_ = nullptr;
    }

private:
    // mock actor, resister url-handler into http server
    class TestActor : public ApiRouterRegister, public litebus::ActorBase {
    public:
        explicit TestActor(const string &name) : ApiRouterRegister(), ActorBase(name)
        {
            ApiRouterRegister::RegisterHandler(SUCCESS_API, [aid(GetAID())](const HttpRequest &req) {
                return litebus::Async(aid, &TestActor::TestHandlerSuccess, req);
            });

            ApiRouterRegister::RegisterHandler(FAILED_API, [aid(GetAID())](const HttpRequest &req) {
                return litebus::Async(aid, &TestActor::TestHandlerFailed, req);
            });

            ApiRouterRegister::RegisterHandler(PREFIX_API, [aid(GetAID())](const HttpRequest &req) {
                return litebus::Async(aid, &TestActor::TestHandlerPrefix, req);
            });

            // If register "/" as a URL, all requests will be accepted.
            ApiRouterRegister::RegisterHandler("/", [aid(GetAID())](const HttpRequest &req) {
                return litebus::Async(aid, &TestActor::TestHandlerSuccess, req);
            });
        }

        litebus::Future<HttpResponse> TestHandlerSuccess(HttpRequest request)
        {
            return HttpResponse(ResponseCode::OK);
        }

        litebus::Future<HttpResponse> TestHandlerFailed(HttpRequest request)
        {
            return HttpResponse(ResponseCode::SERVICE_UNAVAILABLE);
        }

        litebus::Future<HttpResponse> TestHandlerPrefix(HttpRequest request)
        {
            return HttpResponse(ResponseCode::ACCEPTED);
        }
    };

    inline static shared_ptr<TestActor> testActor_ = make_shared<TestActor>("test");
    inline static shared_ptr<HttpServer> httpServer_ = make_shared<HttpServer>(HTTP_SERVER_NAME);
};

/**
 * Feature: HttpTest HttpSend
 * Description: Send http request
 * Steps:
 * 1. Send http request to api /serverless/v1/posix/instance/success
 * 2. Send http request to api /serverless/v1/posix/instance/failed
 * 3. Send http request to a prefix api /serverless/v1/posix/instance
 * 4. Send http request to api /serverless/
 *
 * Expectation:
 * 1. ResponseCode::OK
 * 2. ResponseCode::SERVICE_UNAVAILABLE
 * 3. ResponseCode::ACCEPTED
 * 4. ResponseCode::OK
 */
TEST_F(HttpTest, HttpSend)
{
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    URL urlSuccess("http", TCP_IP, port, HTTP_SERVER_NAME + SUCCESS_API);
    litebus::Future<HttpResponse> response = Post(urlSuccess, litebus::None(), REQ_DATA, CONTENT_TYPE);
    response.Wait();
    ASSERT_EQ(response.Get().retCode, OK);

    URL urlFailed("http", TCP_IP, port, HTTP_SERVER_NAME + FAILED_API);
    response = Post(urlFailed, litebus::None(), REQ_DATA, CONTENT_TYPE);
    response.Wait();
    ASSERT_EQ(response.Get().retCode, SERVICE_UNAVAILABLE);

    URL urlPrefix("http", TCP_IP, port, HTTP_SERVER_NAME + PREFIX_API);
    response = Post(urlPrefix, litebus::None(), REQ_DATA, CONTENT_TYPE);
    response.Wait();
    ASSERT_EQ(response.Get().retCode, ACCEPTED);

    URL urlEmpty("http", TCP_IP, port, HTTP_SERVER_NAME + "/");
    response = Post(urlEmpty, litebus::None(), REQ_DATA, CONTENT_TYPE);
    response.Wait();
    ASSERT_EQ(response.Get().retCode, OK);
}

/**
 * Feature: HttpTest HttpFail
 * Description: Send http request failed
 * Steps:
 * 1. Send http request to wrong server name
 * 2. Send http request to wrong api
 *
 * Expectation:
 * 1. ResponseCode::NOT_FOUND
 * 2. ResponseCode::NOT_FOUND
 */
TEST_F(HttpTest, HttpFail)
{
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    URL urlWrongServer("http", TCP_IP, port, "WRONG_SERVER_NAME" + SUCCESS_API);
    litebus::Future<Response> response = Post(urlWrongServer, litebus::None(), REQ_DATA, CONTENT_TYPE);
    response.Wait();
    ASSERT_EQ(response.Get().retCode, NOT_FOUND);

    URL urlEmpty("http", TCP_IP, port, HTTP_SERVER_NAME + SUCCESS_API + "/123");
    response = Post(urlEmpty, litebus::None(), REQ_DATA, CONTENT_TYPE);
    response.Wait();
    ASSERT_EQ(response.Get().retCode, NOT_FOUND);
}

/**
 * Feature: DefaultHealthyRouter.
 * Description: To set DefaultHealthyRoutern.
 * Steps:
 * 1. Set DefaultHealthyRouter and check handlermap.
 *
 */
TEST_F(HttpTest, DefaultHealthyRouter)
{
    DefaultHealthyRouter defaultHealthyRouter("test_nodeid");
    std::shared_ptr<HandlerMap> handlerMapPtr = defaultHealthyRouter.GetHandlers();
    EXPECT_FALSE(handlerMapPtr->empty());
    EXPECT_EQ("/healthy", handlerMapPtr->begin()->first);
}

/**
 * Feature: HttpServer.
 * Description: To set HttpServer.
 * Steps:
 * 1. Set HttpServer and register router.
 *
 */
TEST_F(HttpTest, RegisterRoute)
{
    functionsystem::HttpServer httpServer("test");
    std::shared_ptr<ApiRouterRegister> router = std::make_shared<ApiRouterRegister>();
    Status statusSuccess = httpServer.RegisterRoute(router);
    EXPECT_EQ(statusSuccess.StatusCode(), StatusCode::SUCCESS);

    std::shared_ptr<MockApiRouterRegister> router1 = std::make_shared<MockApiRouterRegister>();
    EXPECT_CALL(*router1, GetHandlers()).WillOnce(testing::Return(nullptr));
    Status statusNull = httpServer.RegisterRoute(router1);

    EXPECT_EQ(statusNull.StatusCode(), StatusCode::FA_HTTP_REGISTER_HANDLER_NULL_ERROR);

    std::shared_ptr<MockApiRouterRegister> router2 = std::make_shared<MockApiRouterRegister>();
    std::string nodeID = "node_test";
    auto defaultHealthy = [nodeID](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        return litebus::http::Ok();
    };

    router2->RegisterHandler("", defaultHealthy);
    // repeat insert
    router2->RegisterHandler("", defaultHealthy);
    Status statusKeyEmpty = httpServer.RegisterRoute(router2);
    EXPECT_EQ(statusKeyEmpty.StatusCode(), StatusCode::FA_HTTP_REGISTER_HANDLER_NULL_ERROR);
}

class Probe : public litebus::ActorBase {
public:
    explicit Probe(const std::string &name) : litebus::ActorBase(name) {}
    ~Probe() = default;

    Status ToProbe()
    {
        return Status::OK();
    }

    Status ToSleepProbe()
    {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return Status::OK();
    }
};

/**
 * Feature: HealthyApiRouter.
 * Description: To set HealthyRouter. and test different probe
 */
TEST_F(HttpTest, HealthyApiRouter)
{
    auto probe = std::make_shared<Probe>("Probe");
    litebus::Spawn(probe);

    HttpRequest request;
    request.headers.insert({ "Node-ID", "test_nodeid" });
    request.headers.insert({ "PID", std::to_string(getpid()) });
    // empty handler
    {
        litebus::Duration timeout = 5000;
        HealthyApiRouter healthyApiRouter("test_nodeid", timeout);
        healthyApiRouter.Register();
        std::shared_ptr<HandlerMap> handlerMapPtr = healthyApiRouter.GetHandlers();
        EXPECT_FALSE(handlerMapPtr->empty());
        EXPECT_EQ("/healthy", handlerMapPtr->begin()->first);
        auto response = handlerMapPtr->begin()->second(request);
        ASSERT_AWAIT_READY(response);
        EXPECT_EQ(response.Get().retCode, 200);
    }
    {
        litebus::Duration timeout = 5000;
        HealthyApiRouter healthyApiRouter("test_nodeid", timeout);
        healthyApiRouter.AddProbe([aid(probe->GetAID())]() {
            return litebus::Async(aid, &Probe::ToProbe);
        });
        healthyApiRouter.Register();
        std::shared_ptr<HandlerMap> handlerMapPtr = healthyApiRouter.GetHandlers();
        auto response = handlerMapPtr->begin()->second(request);
        ASSERT_AWAIT_READY(response);
        EXPECT_EQ(response.Get().retCode, 200);
    }
    // timeout handler
    {
        litebus::Duration timeout = 10;
        HealthyApiRouter healthyApiRouter("test_nodeid", timeout);
        healthyApiRouter.AddProbe([aid(probe->GetAID())]() {
            return litebus::Async(aid, &Probe::ToSleepProbe);
        });
        healthyApiRouter.Register();
        std::shared_ptr<HandlerMap> handlerMapPtr = healthyApiRouter.GetHandlers();
        auto response = handlerMapPtr->begin()->second(request);
        ASSERT_AWAIT_READY(response);
        EXPECT_EQ(response.Get().retCode, BAD_REQUEST);
    }
}

} // namespace functionsystem::test