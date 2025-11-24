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

#include "function_proxy/local_scheduler/abnormal_processor/abnormal_processor.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/exec_utils.h"
#include "gmock/gmock-actions.h"
#include "gmock/gmock-spec-builders.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_observer.h"
#include "mocks/mock_function_agent_mgr.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace local_scheduler;
using namespace ::testing;

class MockRaiseWrapper : public RaiseWrapper {
public:
    MockRaiseWrapper() : RaiseWrapper()
    {
    }
    ~MockRaiseWrapper() override = default;
    MOCK_METHOD(void, Raise, (int sig), (override));
};

class AbnormalProcessorTest : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        metaStoreAddress_ = "127.0.0.1:" + std::to_string(FindAvailablePort());
    }
    
    void SetUp() override
    {
        mockObserver_ = std::make_shared<MockObserver>();
        mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>(metaStoreAddress_);
        mockInstanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        mockFuntionAgentMgr_ = std::make_shared<MockFunctionAgentMgr>("funcAgentMgr", nullptr);
        mockRaiseWrapper_ = std::make_shared<MockRaiseWrapper>();
        abnormalProcessor_ = std::make_shared<AbnormalProcessorActor>("nodeID");
        abnormalProcessor_->BindObserver(mockObserver_);
        abnormalProcessor_->BindInstanceCtrl(mockInstanceCtrl_);
        abnormalProcessor_->BindRaiseWrapper(mockRaiseWrapper_);
        abnormalProcessor_->BindMetaStoreClient(mockMetaStoreClient_);
        abnormalProcessor_->BindFunctionAgentMgr(mockFuntionAgentMgr_);
        abnormalProcessor_->SetQueryInterval(10);
        litebus::Spawn(abnormalProcessor_);
    }
    void TearDown() override
    {
        litebus::Terminate(abnormalProcessor_->GetAID());
        litebus::Await(abnormalProcessor_->GetAID());
    }

protected:
    std::shared_ptr<AbnormalProcessorActor> abnormalProcessor_;
    std::shared_ptr<MockObserver> mockObserver_;
    std::shared_ptr<MockInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockMetaStoreClient> mockMetaStoreClient_;
    std::shared_ptr<MockRaiseWrapper> mockRaiseWrapper_;
    std::shared_ptr<MockFunctionAgentMgr> mockFuntionAgentMgr_;
    inline static std::string metaStoreAddress_;
};

/**
 * Feature: AbnormalProcessorTest RegisterWatchAbnormal
 * Description: When the startup is normal, register the abnormal etcd event.
When an exception event is detected, check whether any local instance is not taken over in polling mode until all local
instances are taken over and exit the process.
 * Steps:
 * 1. Create AbnormalProcessor
 * 2. Mock meta client register watch
 * 3. trigger watch event
 * 4. mock 2 times get local instances from observer
 *    time 1 return 2 instance
 *    time 2 return 0 instance
 * 5. mock raise 9
 * Expectation:
 * 1. raise expected called with num 9
 */
TEST_F(AbnormalProcessorTest, RegisterWatchAbnormal)
{
    std::string key = "/yr/abnormal/localscheduler/nodeID";
    auto getResponse = std::make_shared<GetResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto watcher = std::make_shared<Watcher>();
    EXPECT_CALL(*mockMetaStoreClient_, Watch(key, ::testing::_, ::testing::_, ::testing::_)).WillOnce(Return(watcher));
    EXPECT_CALL(*mockInstanceCtrl_, SetAbnormal).WillOnce(Return());
    EXPECT_CALL(*mockFuntionAgentMgr_, SetAbnormal).WillOnce(Return());
    std::vector<std::string> twoInstances{ "1", "2" };
    EXPECT_CALL(*mockObserver_, GetLocalInstances)
        .WillOnce(Return(twoInstances))
        .WillOnce(Return(std::vector<std::string>()));
    auto deleteResponse = std::make_shared<DeleteResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Delete).WillOnce(Return(deleteResponse));
    litebus::Future<int> sig;
    EXPECT_CALL(*mockRaiseWrapper_, Raise).WillOnce(DoAll(FutureArg<0>(&sig), Return()));

    auto future = litebus::Async(abnormalProcessor_->GetAID(), &AbnormalProcessorActor::CheckLocalSchedulerIsLegal);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get(), true);

    auto jsonStr = R"({"isAbnormal":"true"})";
    KeyValue kv;
    kv.set_key(key);
    kv.set_value(jsonStr);
    KeyValue prevKv;
    auto event = WatchEvent{ EventType::EVENT_TYPE_PUT, kv, prevKv };
    std::vector<WatchEvent> events{ event };
    litebus::Async(abnormalProcessor_->GetAID(), &AbnormalProcessorActor::SchedulerAbnormalWatcher, events);

    ASSERT_AWAIT_READY(sig);
    EXPECT_EQ(sig.IsOK(), true);
    EXPECT_EQ(sig.Get(), 2);
};

/**
 * Feature: AbnormalProcessorTest StartWithAbnormal
 * Description: If the startup is abnormal, the local instance is polled and exits directly.
 * Steps:
 * 1. Create AbnormalProcessor
 * 2. Mock meta client Get abnormal
 * 4. mock 1 times get local instances from observer
 *    time 1 return 0 instance
 * 5. mock raise 9
 * Expectation:
 * 1. raise expected called with num 9
 */
TEST_F(AbnormalProcessorTest, StartWithAbnormal)
{
    std::string key = "/yr/abnormal/localscheduler/nodeID";
    auto jsonStr = R"({"nodeName":"nodeName-1","instanceManagerActorAid":"instanceManagerActorAid-1"})";
    KeyValue kv;
    kv.set_key(key);
    kv.set_value(jsonStr);
    auto getResponse = std::make_shared<GetResponse>();
    getResponse->kvs.push_back(std::move(kv));
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    EXPECT_CALL(*mockInstanceCtrl_, SetAbnormal).WillOnce(Return());
    EXPECT_CALL(*mockFuntionAgentMgr_, SetAbnormal).WillOnce(Return());
    EXPECT_CALL(*mockObserver_, GetLocalInstances).WillOnce(Return(std::vector<std::string>()));
    auto deleteResponse = std::make_shared<DeleteResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Delete).WillOnce(Return(deleteResponse));
    litebus::Future<int> sig;
    EXPECT_CALL(*mockRaiseWrapper_, Raise).WillOnce(DoAll(FutureArg<0>(&sig), Return()));

    auto future = litebus::Async(abnormalProcessor_->GetAID(), &AbnormalProcessorActor::CheckLocalSchedulerIsLegal);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get(), false);

    ASSERT_AWAIT_READY(sig);
    EXPECT_EQ(sig.IsOK(), true);
    EXPECT_EQ(sig.Get(), 2);
}

TEST_F(AbnormalProcessorTest, StartWithAbnormalWithWrongJson)
{
    std::string key = "/yr/abnormal/localscheduler/nodeID";
    auto notJsonStr = "not a json";
    KeyValue kv;
    kv.set_key(key);
    kv.set_value(notJsonStr);
    auto getResponse = std::make_shared<GetResponse>();
    getResponse->kvs.push_back(std::move(kv));
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    EXPECT_CALL(*mockInstanceCtrl_, SetAbnormal).WillOnce(Return());
    EXPECT_CALL(*mockFuntionAgentMgr_, SetAbnormal).WillOnce(Return());
    EXPECT_CALL(*mockObserver_, GetLocalInstances).WillOnce(Return(std::vector<std::string>()));
    auto deleteResponse = std::make_shared<DeleteResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Delete).WillOnce(Return(deleteResponse));
    litebus::Future<int> sig;
    EXPECT_CALL(*mockRaiseWrapper_, Raise).WillOnce(DoAll(FutureArg<0>(&sig), Return()));

    auto future = litebus::Async(abnormalProcessor_->GetAID(), &AbnormalProcessorActor::CheckLocalSchedulerIsLegal);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get(), false);

    ASSERT_AWAIT_READY(sig);
    EXPECT_EQ(sig.IsOK(), true);
    EXPECT_EQ(sig.Get(), 2);
}

TEST_F(AbnormalProcessorTest, AbnormalSyncerTest)
{
    {
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status(StatusCode::FAILED, "");
        auto future = abnormalProcessor_->AbnormalSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());
    }

    {
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        auto future = abnormalProcessor_->AbnormalSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
    }

    {
        std::string key = "/yr/abnormal/localscheduler/nodeID";
        EXPECT_CALL(*mockInstanceCtrl_, SetAbnormal).WillOnce(Return());
        EXPECT_CALL(*mockFuntionAgentMgr_, SetAbnormal).WillOnce(Return());
        std::vector<std::string> twoInstances{ "1", "2" };
        EXPECT_CALL(*mockObserver_, GetLocalInstances)
            .WillOnce(Return(twoInstances))
            .WillOnce(Return(std::vector<std::string>()));
        auto deleteResponse = std::make_shared<DeleteResponse>();
        EXPECT_CALL(*mockMetaStoreClient_, Delete).WillOnce(Return(deleteResponse));
        litebus::Future<int> sig;
        EXPECT_CALL(*mockRaiseWrapper_, Raise).WillOnce(DoAll(FutureArg<0>(&sig), Return()));

        auto jsonStr = R"({"isAbnormal":"true"})";
        KeyValue getKeyValue;
        getKeyValue.set_key(key);
        getKeyValue.set_value(jsonStr);

        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        rep->kvs.emplace_back(getKeyValue);

        auto future = abnormalProcessor_->AbnormalSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());

        ASSERT_AWAIT_READY(sig);
        EXPECT_EQ(sig.IsOK(), true);
        EXPECT_EQ(sig.Get(), 2);
    }

};


}  // namespace functionsystem::test