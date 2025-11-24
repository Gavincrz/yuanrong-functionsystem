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

#include "meta_store_client/election/election_client_strategy.h"

#include <gtest/gtest.h>

#include "async/future.hpp"
#include "meta_store_client/election/meta_store_election_client_strategy.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;
using namespace testing;
static MetaStoreTimeoutOption metaStoreTimeoutOpt = {
    .operationRetryIntervalLowerBound = 100,
    .operationRetryIntervalUpperBound = 200,
    .operationRetryTimes = 2,
    .grpcTimeout = 1000,
};

class MockElectionServiceActor : public litebus::ActorBase {
public:
    MockElectionServiceActor() : ActorBase("ElectionServiceActor")
    {
    }

    MOCK_METHOD(::v3electionpb::CampaignResponse, Campaign, (const ::v3electionpb::CampaignRequest &));
    void ReceiveCampaign(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        messages::MetaStoreRequest req;
        RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Campaign MetaStoreResponse");

        ::v3electionpb::CampaignRequest request;
        RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                       "failed to parse Campaign CampaignRequest: " + req.requestid());

        messages::MetaStoreResponse res;
        res.set_responseid(req.requestid());
        res.set_responsemsg(Campaign(request).SerializeAsString());
        Send(from, "OnCampaign", res.SerializeAsString());
    }

    MOCK_METHOD(::v3electionpb::LeaderResponse, Leader, (const ::v3electionpb::LeaderRequest &));
    void ReceiveLeader(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        messages::MetaStoreRequest req;
        RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Leader MetaStoreResponse");

        ::v3electionpb::LeaderRequest request;
        RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                       "failed to parse Leader LeaderRequest: " + req.requestid());

        messages::MetaStoreResponse res;
        res.set_responseid(req.requestid());
        res.set_responsemsg(Leader(request).SerializeAsString());
        Send(from, "OnLeader", res.SerializeAsString());
    }

    MOCK_METHOD(::v3electionpb::ResignResponse, Resign, (const ::v3electionpb::ResignRequest &));
    void ReceiveResign(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        messages::MetaStoreRequest req;
        RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Resign MetaStoreResponse");

        ::v3electionpb::ResignRequest request;
        RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                       "failed to parse Resign LeaderRequest: " + req.requestid());

        messages::MetaStoreResponse res;
        res.set_responseid(req.requestid());
        res.set_responsemsg(Resign(request).SerializeAsString());
        Send(from, "OnResign", res.SerializeAsString());
    }

    MOCK_METHOD(messages::MetaStore::ObserveResponse, Observe, (const ::v3electionpb::LeaderRequest &));
    void ReceiveObserve(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        messages::MetaStoreRequest req;
        RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Observe MetaStoreResponse");

        ::v3electionpb::LeaderRequest request;
        RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                       "failed to parse Observe LeaderRequest: " + req.requestid());

        messages::MetaStoreResponse res;
        res.set_responseid(req.requestid());
        res.set_responsemsg(Observe(request).SerializeAsString());
        Send(from, "OnObserve", res.SerializeAsString());
    }

    MOCK_METHOD(messages::MetaStore::ObserveResponse, CancelObserve,
                (const messages::MetaStore::ObserveCancelRequest &));
    void ReceiveCancelObserve(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        messages::MetaStoreRequest req;
        RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse CancelObserve MetaStoreResponse");

        messages::MetaStore::ObserveCancelRequest request;
        RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                       "failed to parse CancelObserve ObserveCancelRequest: " + req.requestid());

        messages::MetaStoreResponse res;
        res.set_responseid(req.requestid());
        res.set_responsemsg(CancelObserve(request).SerializeAsString());
        Send(from, "OnObserve", res.SerializeAsString());
    }

    void SendObserveEvent(const litebus::AID &to, const messages::MetaStore::ObserveResponse &leaderResponse)
    {
        messages::MetaStoreResponse res;
        res.set_responseid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        res.set_responsemsg(leaderResponse.SerializeAsString());
        Send(to, "OnObserve", res.SerializeAsString());
    }

protected:
    void Init() override
    {
        Receive("Campaign", &MockElectionServiceActor::ReceiveCampaign);
        Receive("Leader", &MockElectionServiceActor::ReceiveLeader);
        Receive("Resign", &MockElectionServiceActor::ReceiveResign);
        Receive("Observe", &MockElectionServiceActor::ReceiveObserve);
        Receive("CancelObserve", &MockElectionServiceActor::ReceiveCancelObserve);
    }
};

class ElectionClientStrategyTest : public ::testing::Test {
public:
    ElectionClientStrategyTest() = default;
    ~ElectionClientStrategyTest() override = default;
    [[maybe_unused]] static void SetUpTestSuite()
    {
        mockElectionService_ = std::make_shared<MockElectionServiceActor>();
        litebus::Spawn(mockElectionService_);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        litebus::Terminate(mockElectionService_->GetAID());
        litebus::Await(mockElectionService_->GetAID());
        mockElectionService_ = nullptr;
    }

protected:
    void SetUp() override
    {
        client_ = MakeMetaStoreElectionClientStrategy();
        litebus::Spawn(client_);
    }

    void TearDown() override
    {
        litebus::Terminate(client_->GetAID());
        litebus::Await(client_);
        client_ = MakeMetaStoreElectionClientStrategy();  // create new client
    }

    static std::shared_ptr<MetaStoreElectionClientStrategy> MakeMetaStoreElectionClientStrategy()
    {
        auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        return std::make_shared<MetaStoreElectionClientStrategy>(
            "meta_store_election_client" + uuid.ToString(), "127.0.0.1:" + std::to_string(port), metaStoreTimeoutOpt);
    }

protected:
    std::shared_ptr<MetaStoreElectionClientStrategy> client_ = nullptr;
    inline static std::shared_ptr<MockElectionServiceActor> mockElectionService_ = nullptr;
};

TEST_F(ElectionClientStrategyTest, CampaignTest)
{
    litebus::Future<::v3electionpb::CampaignRequest> req;
    ::v3electionpb::CampaignResponse ret;
    ret.mutable_leader()->set_name("name2");
    ret.mutable_leader()->set_key("key");
    EXPECT_CALL(*mockElectionService_, Campaign).WillOnce(DoAll(test::FutureArg<0>(&req), Return(ret)));
    auto resp = litebus::Async(client_->GetAID(), &MetaStoreElectionClientStrategy::Campaign, "name", 123, "value");
    ASSERT_AWAIT_READY(req);
    EXPECT_EQ(req.Get().name(), "name");
    EXPECT_EQ(req.Get().lease(), 123);
    EXPECT_EQ(req.Get().value(), "value");

    ASSERT_AWAIT_READY(resp);
    EXPECT_TRUE(resp.Get().status.IsOk());
    EXPECT_EQ(resp.Get().leader.name, "name2");
    EXPECT_EQ(resp.Get().leader.key, "key");
}

TEST_F(ElectionClientStrategyTest, LeaderTest)
{
    litebus::Future<::v3electionpb::LeaderRequest> req;
    ::v3electionpb::LeaderResponse ret;
    ret.mutable_kv()->set_key("key");
    ret.mutable_kv()->set_value("value");
    EXPECT_CALL(*mockElectionService_, Leader).WillOnce(DoAll(test::FutureArg<0>(&req), Return(ret)));
    auto resp = litebus::Async(client_->GetAID(), &MetaStoreElectionClientStrategy::Leader, "name");
    ASSERT_AWAIT_READY(req);
    EXPECT_EQ(req.Get().name(), "name");

    ASSERT_AWAIT_READY(resp);
    EXPECT_TRUE(resp.Get().status.IsOk());
    EXPECT_EQ(resp.Get().kv.first, "key");
    EXPECT_EQ(resp.Get().kv.second, "value");
}

TEST_F(ElectionClientStrategyTest, ResignTest)
{
    litebus::Future<::v3electionpb::ResignRequest> req;
    ::v3electionpb::ResignResponse ret;
    EXPECT_CALL(*mockElectionService_, Resign).WillOnce(DoAll(test::FutureArg<0>(&req), Return(ret)));
    LeaderKey key{ .name = "name", .key = "key", .rev = 123, .lease = 1234 };
    auto resp = litebus::Async(client_->GetAID(), &MetaStoreElectionClientStrategy::Resign, key);
    ASSERT_AWAIT_READY(req);
    EXPECT_EQ(req.Get().leader().name(), "name");
    EXPECT_EQ(req.Get().leader().key(), "key");
    EXPECT_EQ(req.Get().leader().rev(), 123);
    EXPECT_EQ(req.Get().leader().lease(), 1234);

    ASSERT_AWAIT_READY(resp);
    EXPECT_TRUE(resp.Get().status.IsOk());
}

TEST_F(ElectionClientStrategyTest, ObserveTest)
{
    litebus::Future<::v3electionpb::LeaderRequest> req;
    messages::MetaStore::ObserveResponse createRet;
    createRet.set_name("/key");
    createRet.set_observeid(1);
    createRet.set_iscreate(true);
    EXPECT_CALL(*mockElectionService_, Observe).WillOnce(DoAll(test::FutureArg<0>(&req), Return(createRet)));

    litebus::Promise<LeaderResponse> promise;
    auto callback = [&](const LeaderResponse &response) {
        YRLOG_DEBUG("ObserveTest receive observe event, key: {}", response.kv.first);
        EXPECT_TRUE(response.status.IsOk());
        promise.SetValue(response);
    };

    auto observer = litebus::Async(client_->GetAID(), &MetaStoreElectionClientStrategy::Observe, "/key", callback);
    ASSERT_AWAIT_READY(observer);
    ASSERT_AWAIT_READY(req);
    EXPECT_EQ(req.Get().name(), "/key");

    // created
    ASSERT_AWAIT_TRUE([&]() { return client_->readyObservers_.find(1) != client_->readyObservers_.end(); });

    // observe event
    v3electionpb::LeaderResponse leader;
    leader.mutable_kv()->set_key("/key/123");
    leader.mutable_kv()->set_value("value");
    messages::MetaStore::ObserveResponse eventRet;
    eventRet.set_name("/key");
    eventRet.set_observeid(1);
    eventRet.set_responsemsg(leader.SerializeAsString());
    mockElectionService_->SendObserveEvent(client_->GetAID(), eventRet);

    ASSERT_AWAIT_READY(promise.GetFuture());
    EXPECT_EQ(promise.GetFuture().Get().kv.first, "/key/123");
    EXPECT_EQ(promise.GetFuture().Get().kv.second, "value");

    // observe cancel
    litebus::Future<messages::MetaStore::ObserveCancelRequest> cancelRequest;
    messages::MetaStore::ObserveResponse cancelRet;
    cancelRet.set_name("/key");
    cancelRet.set_observeid(1);
    cancelRet.set_iscancel(true);
    EXPECT_CALL(*mockElectionService_, CancelObserve)
        .WillOnce(DoAll(test::FutureArg<0>(&cancelRequest), Return(cancelRet)));

    observer.Get()->Shutdown();
    ASSERT_AWAIT_TRUE([&]() { return client_->readyObservers_.find(1) == client_->readyObservers_.end(); });

    litebus::Async(client_->GetAID(), &meta_store::MetaStoreElectionClientStrategy::OnAddressUpdated, "127.0.0.1");
}
}  // namespace functionsystem::meta_store::test