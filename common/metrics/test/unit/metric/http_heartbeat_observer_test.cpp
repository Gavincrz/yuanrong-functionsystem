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

#include "gtest/gtest.h"

#include "async/future.hpp"
#include "httpd/http_actor.hpp"
#include "httpd/http.hpp"

#include "common/logs/log.h"

#define private public
#include "exporters/http_exporter/http_heartbeat_observer.h"

namespace observability::test::exporter {

const std::string MOCK_COLLECTOR_NAME = "mockHealthCheckCollector";
const std::string HEALTH_CHECK_URL = "127.0.0.1:8080/"+ MOCK_COLLECTOR_NAME + "/healthcheck";

class MockHealthCheckCollector : public litebus::http::HttpActor {
public:
    explicit MockHealthCheckCollector(const std::string &name) : HttpActor(name)
    {
    }

    bool receivedHealthcheckReq_ = false;

private:
    virtual void Init() override
    {
        AddRoute("/healthcheck", &MockHealthCheckCollector::HandleHealthcheckRequest);
    }

    litebus::Future<litebus::http::Response> HandleHealthcheckRequest(const litebus::http::Request &request);
};

litebus::Future<litebus::http::Response> MockHealthCheckCollector::HandleHealthcheckRequest(const litebus::http::Request &request)
{
    METRICS_LOG_DEBUG("Receive healthCheck request {}", request.url.path);
    receivedHealthcheckReq_ = true;
    return litebus::http::Ok("Ok", litebus::http::ResponseBodyType::TEXT);
}

class HttpHeartbeatObserverTest : public ::testing::Test {
protected:
    std::shared_ptr<MockHealthCheckCollector> mockCollector_ = nullptr;
    std::shared_ptr<exporters::metrics::HttpHeartbeatObserver> observer_ = nullptr;

void SetUp()
{
    mockCollector_ = std::make_shared<MockHealthCheckCollector>(MOCK_COLLECTOR_NAME);
    litebus::Spawn(mockCollector_);
}

void TearDown()
{
    if (mockCollector_ != nullptr) {
        litebus::Terminate(mockCollector_->GetAID());
        litebus::Await(mockCollector_->GetAID());
        mockCollector_ = nullptr;
    }
}
};

TEST_F(HttpHeartbeatObserverTest, Start)
{
    exporters::metrics::HeartbeatParam heartbeatParam;
    heartbeatParam.heartbeatInterval = 3000;
    observer_ = std::make_shared<exporters::metrics::HttpHeartbeatObserver>(heartbeatParam);
    auto onChangeCbCalled = false;
    observer_->RegisterOnHealthChangeCb([&onChangeCbCalled](bool newStatus) {
        if (newStatus) {
            onChangeCbCalled = true;
        }
    });

    // url is empty
    observer_->Start();
    EXPECT_FALSE(mockCollector_->receivedHealthcheckReq_);
    EXPECT_FALSE(mockCollector_->receivedHealthcheckReq_);

    // url is valid
    observer_->url_ = HEALTH_CHECK_URL;
    observer_->Start();
    EXPECT_TRUE(mockCollector_->receivedHealthcheckReq_);
    EXPECT_TRUE(onChangeCbCalled);

    // url is invalid
    mockCollector_->receivedHealthcheckReq_ = false;
    observer_->url_ = "127:0.0.1:8000/invalid/path";
    observer_->Start();
    EXPECT_FALSE(observer_->healthy_.load());
    EXPECT_FALSE(mockCollector_->receivedHealthcheckReq_);
}
}
