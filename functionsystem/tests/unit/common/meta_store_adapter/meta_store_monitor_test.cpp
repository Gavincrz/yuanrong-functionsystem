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

#include "meta_store_monitor/meta_store_monitor.h"
#include <gtest/gtest.h>
#include "mocks/mock_meta_store_client.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

class MetaStoreMonitorTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
    }
    inline static std::string metaStoreServerHost_;
    std::shared_ptr<MetaStoreMonitorActor> actor_;
    std::shared_ptr<MockMetaStoreClient> testMetaStoreClient_;
};

TEST_F(MetaStoreMonitorTest, CheckMetaStoreStatusTest)
{
    testMetaStoreClient_ = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    actor_ =
        std::make_shared<MetaStoreMonitorActor>(metaStoreServerHost_, MetaStoreMonitorParam(), testMetaStoreClient_);
    litebus::Spawn(actor_);

    StatusResponse errorResp;
    errorResp.status = Status(StatusCode::FAILED, "healthcheck failed");
    StatusResponse correctResp;
    correctResp.status = Status(StatusCode::SUCCESS, "healthcheck success");

    actor_->SetAlarmLevel(metrics::AlarmLevel::OFF);
    EXPECT_CALL(*testMetaStoreClient_, HealthCheck).WillOnce(testing::Return(errorResp));
    actor_->CheckMetaStoreStatus();
    ASSERT_AWAIT_TRUE([&]() { return !actor_->isChecking_; });
    EXPECT_CALL(*testMetaStoreClient_, HealthCheck).WillOnce(testing::Return(correctResp));
    actor_->CheckMetaStoreStatus();
    ASSERT_AWAIT_TRUE([&]() { return !actor_->isChecking_; });

    actor_->SetAlarmLevel(metrics::AlarmLevel::MAJOR);
    EXPECT_CALL(*testMetaStoreClient_, HealthCheck).WillOnce(testing::Return(errorResp));
    actor_->CheckMetaStoreStatus();
    ASSERT_AWAIT_TRUE([&]() { return !actor_->isChecking_; });

    EXPECT_CALL(*testMetaStoreClient_, HealthCheck).WillOnce(testing::Return(correctResp));
    actor_->CheckMetaStoreStatus();
    ASSERT_AWAIT_TRUE([&]() { return !actor_->isChecking_; });

    actor_->SetAlarmLevel(metrics::AlarmLevel::CRITICAL);
    EXPECT_CALL(*testMetaStoreClient_, HealthCheck).WillOnce(testing::Return(errorResp));
    actor_->CheckMetaStoreStatus();
    ASSERT_AWAIT_TRUE([&]() { return !actor_->isChecking_; });

    EXPECT_CALL(*testMetaStoreClient_, HealthCheck).WillOnce(testing::Return(correctResp));
    actor_->CheckMetaStoreStatus();
    ASSERT_AWAIT_TRUE([&]() { return !actor_->isChecking_; });

    EXPECT_CALL(*testMetaStoreClient_, HealthCheck)
        .WillOnce(
            testing::DoAll(testing::Invoke([&]() { actor_->CheckMetaStoreStatus(); }), testing::Return(correctResp)));
    actor_->CheckMetaStoreStatus();
    ASSERT_AWAIT_TRUE([&]() { return !actor_->isChecking_; });

    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
    actor_ = nullptr;
    testMetaStoreClient_ = nullptr;
}

class MetaStoreObserver : public MetaStoreHealthyObserver {
public:
   MetaStoreObserver() = default;
   ~MetaStoreObserver() override = default;

   void OnHealthyStatus(const Status &status) override
   {
       YRLOG_INFO("meta store OnHealthyStatus");
       std::lock_guard<std::mutex> lock(mutex_);
       healthyStatus_.emplace_back(status);
   }

   std::vector<Status> GetStatus()
   {
       std::lock_guard<std::mutex> lock(mutex_);
       return healthyStatus_;
   }

private:
    std::mutex mutex_;
    std::vector<Status> healthyStatus_;
};

TEST_F(MetaStoreMonitorTest, ObserverMetaHealthy)
{
   testMetaStoreClient_ = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
   MetaStoreMonitorParam param {
       .maxTolerateFailedTimes = 3,
       .checkIntervalMs = 100,
       .timeoutMs = 200,
   };
   auto monitor = std::make_shared<MetaStoreMonitor>(metaStoreServerHost_, param, testMetaStoreClient_);
   auto observer = std::make_shared<MetaStoreObserver>();
   monitor->RegisterHealthyObserver(observer);

   StatusResponse errorResp;
   errorResp.status = Status(StatusCode::FAILED, "healthcheck failed");
   StatusResponse correctResp;
   correctResp.status = Status(StatusCode::SUCCESS, "healthcheck success");

   EXPECT_CALL(*testMetaStoreClient_, IsConnected)
       .WillOnce(testing::Return(true));
   EXPECT_CALL(*testMetaStoreClient_, BindReconnectedCallBack).WillOnce(testing::Return());
   EXPECT_CALL(*testMetaStoreClient_, HealthCheck)
       .WillOnce(testing::Return(correctResp))
       .WillOnce(testing::Return(errorResp))
       .WillOnce(testing::Return(errorResp))
       .WillOnce(testing::Return(errorResp))
       .WillOnce(testing::Return(errorResp))
       .WillOnce(testing::Return(correctResp))
       .WillOnce(testing::Return(errorResp))
       .WillOnce(testing::Return(errorResp))
       .WillOnce(testing::Return(errorResp))
       .WillRepeatedly(testing::Return(correctResp));

   auto status = monitor->CheckMetaStoreConnected();
   EXPECT_EQ(status.IsOk(), true);

   ASSERT_AWAIT_TRUE([&]() { return observer->GetStatus().size() >= 3; });
   auto listStatus = observer->GetStatus();
   EXPECT_EQ(listStatus[0].IsOk(), false);
   EXPECT_EQ(listStatus[1].IsOk(), true);
   EXPECT_EQ(listStatus[2].IsOk(), false);
   monitor = nullptr;
}

}  // namespace functionsystem::test

