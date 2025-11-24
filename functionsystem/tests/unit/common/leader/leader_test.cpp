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

#include <async/async.hpp>

#include "common/constants/actor_name.h"
#include "common/constants/signal.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "common/explorer/etcd_explorer_actor.h"
#include "common/explorer/k8s_explorer_actor.h"
#include "common/explorer/explorer.h"
#include "common/leader/etcd_leader_actor.h"
#include "common/leader/k8s_leader_actor.h"
#include "common/logs/logging.h"
#include "common/metadata/metadata.h"
#include "common/utils/generate_message.h"
#include "mocks/mock_kube_client.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_scheduler.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace functionsystem;
using namespace ::testing;
using namespace functionsystem::explorer;
using namespace functionsystem::leader;
using schedule_decision::ScheduleResult;

const std::string DEFAULT_ELECTION_KEY = "key001";
const std::string DEFAULT_ELECTION_PROPOSAL = "proposal001";
const int64_t DEFAULT_LEASE_ID = 1234567;
const int64_t DEFAULT_LEASE_TTL = 300;

CampaignResponse makeSuccessCampaignResponse(std::string name, std::string key)
{
    auto response = CampaignResponse{
        .status = Status::OK(),
        .header = {},
        .leader =
            LeaderKey{
                .name = name,
                .key = key,
                .rev = 123456,
                .lease = 123456,
            },
    };
    return response;
}

CampaignResponse makeFailedCampaignResponse(std::string name, std::string key)
{
    auto response = CampaignResponse{
        .status = Status(StatusCode::FAILED, "failed to campaign"),
        .header = {},
        .leader =
            LeaderKey{
                .name = name,
                .key = key,
                .rev = 123456,
                .lease = 123456,
            },
    };
    return response;
}

/**

Need to add
1. when keep alive is aborted
2. when keep alive fails
3. when grant fails
*/
class LeaderTest : public ::testing::Test {
public:
    void SetUp() override
    {
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        mockMetaClient_ = std::make_shared<MockMetaStoreClient>("127.0.0.1:" + std::to_string(metaStoreServerPort));

        // mock grant
        auto leaseGrantResponseFuture = litebus::Future<LeaseGrantResponse>();
        auto leaseGrantResponse = LeaseGrantResponse{
            .status = Status::OK(), .header = {}, .leaseId = DEFAULT_LEASE_ID, .ttl = DEFAULT_LEASE_TTL
        };
        leaseGrantResponseFuture.SetValue(leaseGrantResponse);

        EXPECT_CALL(*mockMetaClient_, Grant).WillRepeatedly(Return(leaseGrantResponseFuture));

        // mock keep alive
        auto leaseKeepAliveResponseFuture = litebus::Future<LeaseKeepAliveResponse>();
        auto leaseKeepAliveResponse = LeaseKeepAliveResponse{
            .status = Status::OK(), .header = {}, .leaseId = DEFAULT_LEASE_ID, .ttl = DEFAULT_LEASE_TTL
        };
        leaseKeepAliveResponseFuture.SetValue(leaseKeepAliveResponse);
        EXPECT_CALL(*mockMetaClient_, KeepAliveOnce).WillRepeatedly(Return(leaseKeepAliveResponseFuture));

        // mock campaign
        auto campaignResponseFuture = litebus::Future<CampaignResponse>();
        auto campaignResponse = makeSuccessCampaignResponse(DEFAULT_ELECTION_PROPOSAL, DEFAULT_ELECTION_KEY);
        campaignResponseFuture.SetValue(campaignResponse);
        EXPECT_CALL(*mockMetaClient_, Campaign).WillRepeatedly(Return(campaignResponseFuture));

        auto resignResponseFuture = litebus::Future<ResignResponse>();
        resignResponseFuture.SetValue(ResignResponse());
        EXPECT_CALL(*mockMetaClient_, Resign).WillRepeatedly(Return(resignResponseFuture));
    }

    void TearDown() override
    {
    }

private:
    std::shared_ptr<MockMetaStoreClient> mockMetaClient_;
};

TEST_F(LeaderTest, CampaignSuccess)
{
    // const values
    const std::string electionProposal = "proposal001";
    const std::string electionKey = "key001";

    // make explorer actor and bind it, so later we can trigger events from ExplorerActor side
    // the chain is like: businessCode <-> leader <-> explorer <-> explorerActor <-> metastore
    explorer::ElectionInfo electionInfo{ .identity = "proposal001", .mode = ETCD_ELECTION_MODE, .electKeepAliveInterval= 30, .electLeaseTTL = 300, };
    auto explorerActor = std::make_shared<EtcdExplorerActor>(electionKey, electionInfo,
                                                         litebus::Option<LeaderInfo>(), mockMetaClient_);
    litebus::Spawn(explorerActor);
    Explorer::GetInstance().BindExplorerActor(electionKey, explorerActor);

    // mock campaign so the campaign would be success
    // expect to be called once, recampaign shouldn't happen
    auto successCampaignResponseFuture = litebus::Future<CampaignResponse>();
    EXPECT_CALL(*mockMetaClient_, Campaign)
        .WillOnce(testing::Invoke(
            [&successCampaignResponseFuture](const std::string &name, int64_t lease, const std::string &value) {
                YRLOG_INFO("third campaign called");
                successCampaignResponseFuture.SetValue(makeSuccessCampaignResponse(value, name));
                return successCampaignResponseFuture;
            }));

    // make leader actor for test
    auto leaderActor = std::make_shared<EtcdLeaderActor>(electionKey, electionInfo, mockMetaClient_);
    litebus::Spawn(leaderActor);

    LeaderInfo cachedLeaderInfo;
    // TEST 1: register callback
    leaderActor->RegisterPublishLeaderCallBack([&cachedLeaderInfo](const LeaderInfo &lf) { cachedLeaderInfo = lf; });

    // new promise to check whether the callback is triggered
    auto becomeLeaderPromise = std::make_shared<litebus::Promise<Status>>();
    auto resignPromise = std::make_shared<litebus::Promise<Status>>();
    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::RegisterCallbackWhenBecomeLeader,
                   [becomeLeaderPromise]() { becomeLeaderPromise->SetValue(Status::OK()); });
    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::RegisterCallbackWhenResign,
                   [resignPromise]() { resignPromise->SetValue(Status::OK()); });
    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::Elect);

    ASSERT_AWAIT_READY(successCampaignResponseFuture);
    ASSERT_AWAIT_TRUE([&]() -> bool { return cachedLeaderInfo.address == "proposal001"; });
    // set event, mock when observe receive event and become leader
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent,
                   LeaderResponse{ .status = Status::OK(),
                                   .header = {},
                                   .kv = std::make_pair(electionKey + "/123456", electionProposal) });
    EXPECT_TRUE(!becomeLeaderPromise->GetFuture().IsOK());

    // set event, mock when observe receive event and become leader
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent,
                   LeaderResponse{ .status = Status::OK(),
                                   .header = {},
                                   .kv = std::make_pair(electionKey + "/123456", electionProposal) });
    ASSERT_AWAIT_READY(becomeLeaderPromise->GetFuture());

    // set event, mock when observe receive event and become no longer the leader
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent,
                   LeaderResponse{ .status = Status::OK(),
                                   .header = {},
                                   .kv = std::make_pair(electionKey + "/123456", "anotherProposal") });
    ASSERT_AWAIT_READY(resignPromise->GetFuture());

    Explorer::GetInstance().UnbindExplorerActor(electionKey);
    litebus::Terminate(explorerActor->GetAID());
    litebus::Await(explorerActor->GetAID());
    litebus::Terminate(leaderActor->GetAID());
    litebus::Await(leaderActor->GetAID());
}

TEST_F(LeaderTest, CampaignFailed)
{
    // const values
    const std::string electionProposal = "proposal002";
    const std::string electionKey = "key002";

    // mock campaign so the campaign would be success
    // expect to be called once, recampaign shouldn't happen
    auto failedCampaignResponseFuture1 = litebus::Future<CampaignResponse>();
    auto failedCampaignResponseFuture2 = litebus::Future<CampaignResponse>();
    auto successCampaignResponseFuture = litebus::Future<CampaignResponse>();

    EXPECT_CALL(*mockMetaClient_, Campaign)
        .WillOnce(testing::Invoke(
            [&failedCampaignResponseFuture1](const std::string &name, int64_t lease, const std::string &value) {
                YRLOG_INFO("first campaign called");
                failedCampaignResponseFuture1.SetValue(makeFailedCampaignResponse(value, name));
                return failedCampaignResponseFuture1;
            }))
        .WillOnce(testing::Invoke(
            [&failedCampaignResponseFuture2](const std::string &name, int64_t lease, const std::string &value) {
                YRLOG_INFO("second campaign called");
                failedCampaignResponseFuture2.SetValue(makeFailedCampaignResponse(value, name));
                return failedCampaignResponseFuture2;
            }))
        .WillOnce(testing::Invoke(
            [&successCampaignResponseFuture](const std::string &name, int64_t lease, const std::string &value) {
                YRLOG_INFO("third campaign called");
                successCampaignResponseFuture.SetValue(makeSuccessCampaignResponse(value, name));
                return successCampaignResponseFuture;
            }));

    // make leader actor for test
    explorer::ElectionInfo electionInfo{
        .identity = electionProposal, .mode = ETCD_ELECTION_MODE, .electKeepAliveInterval = 30, .electLeaseTTL = 300,
    };
    auto leaderActor = std::make_shared<EtcdLeaderActor>(electionKey, electionInfo, mockMetaClient_);
    litebus::Spawn(leaderActor);

    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::Elect);

    ASSERT_AWAIT_READY(failedCampaignResponseFuture1);
    ASSERT_AWAIT_READY(failedCampaignResponseFuture2);
    ASSERT_AWAIT_READY(successCampaignResponseFuture);
    litebus::Terminate(leaderActor->GetAID());
    litebus::Await(leaderActor->GetAID());
}

TEST_F(LeaderTest, GrantFailed)
{
    explorer::ElectionInfo electionInfo{
        .identity = "proposal001", .mode = ETCD_ELECTION_MODE, .electKeepAliveInterval = 30, .electLeaseTTL = 300,};
    auto explorerActor = std::make_shared<EtcdExplorerActor>(DEFAULT_ELECTION_KEY, electionInfo,
                                                         litebus::Option<LeaderInfo>(), mockMetaClient_);
    litebus::Spawn(explorerActor);
    Explorer::GetInstance().BindExplorerActor(DEFAULT_ELECTION_KEY, explorerActor);
    auto failedGrantFuture1 = litebus::Future<LeaseGrantResponse>();
    LeaseGrantResponse failedGrantResponse{ .status = Status(StatusCode::FAILED) };

    auto leaseGrantResponse = LeaseGrantResponse{
        .status = Status::OK(), .header = {}, .leaseId = DEFAULT_LEASE_ID, .ttl = DEFAULT_LEASE_TTL
    };

    EXPECT_CALL(*mockMetaClient_, Grant)
        .WillOnce(Return(failedGrantResponse))
        .WillRepeatedly(Return(leaseGrantResponse));

    auto successCampaignResponseFuture = litebus::Future<CampaignResponse>();
    EXPECT_CALL(*mockMetaClient_, Campaign)
        .WillOnce(testing::Invoke(
            [&successCampaignResponseFuture](const std::string &name, int64_t lease, const std::string &value) {
                YRLOG_INFO("third campaign called");
                successCampaignResponseFuture.SetValue(makeSuccessCampaignResponse(value, name));
                return successCampaignResponseFuture;
            }));

    // make leader actor for test
    auto leaderActor =
        std::make_shared<EtcdLeaderActor>(DEFAULT_ELECTION_KEY, electionInfo, mockMetaClient_);
    litebus::Spawn(leaderActor);

    auto becomeLeaderPromise = std::make_shared<litebus::Promise<Status>>();
    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::RegisterCallbackWhenBecomeLeader,
                   [becomeLeaderPromise]() { becomeLeaderPromise->SetValue(Status::OK()); });

    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::Elect);

    ASSERT_AWAIT_READY(successCampaignResponseFuture);
    // set event, mock when observe receive event and become leader
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent,
                   LeaderResponse{ .status = Status::OK(),
                                   .header = {},
                                   .kv = std::make_pair(DEFAULT_ELECTION_KEY + "/123456", DEFAULT_ELECTION_PROPOSAL) });
    EXPECT_TRUE(!becomeLeaderPromise->GetFuture().IsOK());

    ASSERT_AWAIT_READY(becomeLeaderPromise->GetFuture());
    EXPECT_EQ(becomeLeaderPromise->GetFuture().Get().StatusCode(), static_cast<int32_t>(Status::OK()));

    Explorer::GetInstance().UnbindExplorerActor(DEFAULT_ELECTION_KEY);
    litebus::Terminate(explorerActor->GetAID());
    litebus::Await(explorerActor->GetAID());
    litebus::Terminate(leaderActor->GetAID());
    litebus::Await(leaderActor->GetAID());
}

TEST_F(LeaderTest, LeaderChangeNoOneself)
{
    explorer::ElectionInfo electionInfo{
        .identity = "proposal001", .mode = ETCD_ELECTION_MODE, .electKeepAliveInterval = 30, .electLeaseTTL = 300,};
    auto explorerActor = std::make_shared<EtcdExplorerActor>(DEFAULT_ELECTION_KEY, electionInfo,
                                                         litebus::Option<LeaderInfo>(), mockMetaClient_);
    litebus::Spawn(explorerActor);
    Explorer::GetInstance().BindExplorerActor(DEFAULT_ELECTION_KEY, explorerActor);

    int32_t successCampaignCnt = 0;
    EXPECT_CALL(*mockMetaClient_, Campaign)
        .WillRepeatedly(
            testing::Invoke([&successCampaignCnt](const std::string &name, int64_t lease, const std::string &value) {
                YRLOG_INFO("third campaign called");
                successCampaignCnt++;
                return makeSuccessCampaignResponse(value, name);
            }));

    // make leader actor for test
    auto leaderActor =
        std::make_shared<EtcdLeaderActor>(DEFAULT_ELECTION_KEY, electionInfo, mockMetaClient_);
    litebus::Spawn(leaderActor);

    auto becomeLeaderPromise = std::make_shared<litebus::Promise<Status>>();
    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::RegisterCallbackWhenBecomeLeader,
                   [becomeLeaderPromise]() { becomeLeaderPromise->SetValue(Status::OK()); });

    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::Elect);

    ASSERT_AWAIT_TRUE([&successCampaignCnt]() -> bool { return successCampaignCnt == 1; });
    // set event, mock when observe receive event and become leader
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent,
                   LeaderResponse{ .status = Status::OK(), .header = {}, .kv = std::make_pair("123456", "123456") });

    ASSERT_AWAIT_TRUE([&successCampaignCnt]() -> bool { return successCampaignCnt == 2; });

    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent,
                   LeaderResponse{ .status = Status::OK(),
                                   .header = {},
                                   .kv = std::make_pair(DEFAULT_ELECTION_KEY + "/123456", DEFAULT_ELECTION_PROPOSAL) });

    EXPECT_TRUE(!becomeLeaderPromise->GetFuture().IsOK());

    ASSERT_AWAIT_READY(becomeLeaderPromise->GetFuture());
    EXPECT_EQ(becomeLeaderPromise->GetFuture().Get().StatusCode(), static_cast<int32_t>(Status::OK()));

    Explorer::GetInstance().UnbindExplorerActor(DEFAULT_ELECTION_KEY);
    litebus::Terminate(explorerActor->GetAID());
    litebus::Await(explorerActor->GetAID());
    litebus::Terminate(leaderActor->GetAID());
    litebus::Await(leaderActor->GetAID());
}

TEST_F(LeaderTest, RepeatElect)
{
    explorer::ElectionInfo electionInfo{
        .identity = "proposal001", .mode = ETCD_ELECTION_MODE, .electKeepAliveInterval = 30, .electLeaseTTL = 300,};
    auto explorerActor = std::make_shared<EtcdExplorerActor>(DEFAULT_ELECTION_KEY, electionInfo,
                                                         litebus::Option<LeaderInfo>(), mockMetaClient_);
    litebus::Spawn(explorerActor);
    Explorer::GetInstance().BindExplorerActor(DEFAULT_ELECTION_KEY, explorerActor);

    auto successCampaignResponseFuture = litebus::Future<CampaignResponse>();
    EXPECT_CALL(*mockMetaClient_, Campaign)
        .WillOnce(testing::Invoke(
            [&successCampaignResponseFuture](const std::string &name, int64_t lease, const std::string &value) {
                YRLOG_INFO("third campaign called");
                successCampaignResponseFuture.SetValue(makeSuccessCampaignResponse(value, name));
                return successCampaignResponseFuture;
            }));

    // make leader actor for test
    auto leaderActor =
        std::make_shared<EtcdLeaderActor>(DEFAULT_ELECTION_KEY, electionInfo, mockMetaClient_);
    litebus::Spawn(leaderActor);

    auto becomeLeaderPromise = std::make_shared<litebus::Promise<Status>>();
    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::RegisterCallbackWhenBecomeLeader,
                   [becomeLeaderPromise]() { becomeLeaderPromise->SetValue(Status::OK()); });

    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::Elect);
    litebus::Async(leaderActor->GetAID(), &EtcdLeaderActor::Elect);

    ASSERT_AWAIT_READY(successCampaignResponseFuture);
    // set event, mock when observe receive event and become leader
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent,
                   LeaderResponse{ .status = Status::OK(),
                                   .header = {},
                                   .kv = std::make_pair(DEFAULT_ELECTION_KEY + "/123456", DEFAULT_ELECTION_PROPOSAL) });
    EXPECT_TRUE(!becomeLeaderPromise->GetFuture().IsOK());

    ASSERT_AWAIT_READY(becomeLeaderPromise->GetFuture());
    EXPECT_EQ(becomeLeaderPromise->GetFuture().Get().StatusCode(), static_cast<int32_t>(Status::OK()));

    Explorer::GetInstance().UnbindExplorerActor(DEFAULT_ELECTION_KEY);
    litebus::Terminate(explorerActor->GetAID());
    litebus::Await(explorerActor->GetAID());
    litebus::Terminate(leaderActor->GetAID());
    litebus::Await(leaderActor->GetAID());
}

/**
 * Feature: K8s Election
 * Description: elect without leader
 * 1. create 3 candidate wait to elect
 * 2. invoke elect
 * Expectation: one become leader, other become candidate
 */
TEST_F(LeaderTest, K8sMultiElectWithoutLeader)
{
    auto mockClient1 = std::make_shared<MockKubeClient>();
    explorer::ElectionInfo electionInfo1{.identity = "proposal001", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300,  .electRenewInterval= 300,};
    auto candidateLeader1 = std::make_shared<K8sLeaderActor>("function-master", electionInfo1, mockClient1, "default");
    explorer::ElectionInfo electionInfo2{.identity = "proposal002", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300, .electRenewInterval= 300,};
    auto mockClient2 = std::make_shared<MockKubeClient>();
    auto candidateLeader2 = std::make_shared<K8sLeaderActor>("function-master", electionInfo2, mockClient2, "default");
    explorer::ElectionInfo electionInfo3{.identity = "proposal003", .mode = K8S_ELECTION_MODE,.electKeepAliveInterval = 1,  .electLeaseTTL = 300, .electRenewInterval= 300,};
    auto mockClient3 = std::make_shared<MockKubeClient>();
    auto candidateLeader3 = std::make_shared<K8sLeaderActor>("function-master", electionInfo3, mockClient3, "default");
    litebus::Promise<std::shared_ptr<V1Lease>> errPromise;
    errPromise.SetFailed(404);
    EXPECT_CALL(*mockClient1, ReadNamespacedLease).WillOnce(testing::Return(errPromise.GetFuture())).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal002")));
    EXPECT_CALL(*mockClient2, ReadNamespacedLease).WillOnce(testing::Return(errPromise.GetFuture())).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal002")));
    EXPECT_CALL(*mockClient3, ReadNamespacedLease).WillOnce(testing::Return(errPromise.GetFuture())).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal002")));
    EXPECT_CALL(*mockClient1, CreateNamespacedLease).WillRepeatedly(testing::Return(errPromise.GetFuture()));
    EXPECT_CALL(*mockClient2, CreateNamespacedLease).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal002")));
    EXPECT_CALL(*mockClient3, CreateNamespacedLease).WillRepeatedly(testing::Return(errPromise.GetFuture()));
    EXPECT_CALL(*mockClient2, ReplaceNamespacedLease).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal002")));
    litebus::Spawn(candidateLeader1);
    litebus::Spawn(candidateLeader2);
    litebus::Spawn(candidateLeader3);
    litebus::Async(candidateLeader1->GetAID(), &K8sLeaderActor::Elect);
    litebus::Async(candidateLeader2->GetAID(), &K8sLeaderActor::Elect);
    litebus::Async(candidateLeader3->GetAID(), &K8sLeaderActor::Elect);
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader1->GetObservedRecord() != nullptr; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader2->GetObservedRecord() != nullptr; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader3->GetObservedRecord() != nullptr; });
    EXPECT_EQ("proposal002", candidateLeader1->GetObservedRecord()->holderIdentity);
    EXPECT_EQ("proposal002", candidateLeader2->GetObservedRecord()->holderIdentity);
    EXPECT_EQ("proposal002", candidateLeader3->GetObservedRecord()->holderIdentity);
    litebus::Terminate(candidateLeader1->GetAID());
    litebus::Await(candidateLeader1->GetAID());
    litebus::Terminate(candidateLeader2->GetAID());
    litebus::Await(candidateLeader2->GetAID());
    litebus::Terminate(candidateLeader3->GetAID());
    litebus::Await(candidateLeader3->GetAID());
}

/**
 * Feature: K8s Election
 * Description: elect with empty leader
 * 1. create 3 candidate wait to elect
 * 2. invoke elect
 * Expectation: one become leader, other become candidate
 */
TEST_F(LeaderTest, K8sMultiElectWithEmptyLeader)
{
    auto mockClient1 = std::make_shared<MockKubeClient>();
    explorer::ElectionInfo electionInfo1{.identity = "proposal001", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300,  .electRenewInterval= 300,};
    auto candidateLeader1 = std::make_shared<K8sLeaderActor>("function-master", electionInfo1, mockClient1, "default");
    explorer::ElectionInfo electionInfo2{.identity = "proposal002", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300, .electRenewInterval= 300,};
    auto mockClient2 = std::make_shared<MockKubeClient>();
    auto candidateLeader2 = std::make_shared<K8sLeaderActor>("function-master", electionInfo2, mockClient2, "default");
    explorer::ElectionInfo electionInfo3{.identity = "proposal003", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300, .electRenewInterval= 300,};
    auto mockClient3 = std::make_shared<MockKubeClient>();
    auto candidateLeader3 = std::make_shared<K8sLeaderActor>("function-master", electionInfo3, mockClient3, "default");
    litebus::Promise<std::shared_ptr<V1Lease>> errPromise;
    errPromise.SetFailed(400);
    EXPECT_CALL(*mockClient1, ReadNamespacedLease).WillOnce(testing::Return(MockKubeClient::CreateLease(""))).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal003")));
    EXPECT_CALL(*mockClient2, ReadNamespacedLease).WillOnce(testing::Return(MockKubeClient::CreateLease(""))).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal003")));
    EXPECT_CALL(*mockClient3, ReadNamespacedLease).WillOnce(testing::Return(MockKubeClient::CreateLease(""))).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal003")));
    EXPECT_CALL(*mockClient1, ReplaceNamespacedLease).WillRepeatedly(testing::Return(errPromise.GetFuture()));
    EXPECT_CALL(*mockClient2, ReplaceNamespacedLease).WillRepeatedly(testing::Return(errPromise.GetFuture()));
    EXPECT_CALL(*mockClient3, ReplaceNamespacedLease).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal003")));
    litebus::Spawn(candidateLeader1);
    litebus::Spawn(candidateLeader2);
    litebus::Spawn(candidateLeader3);
    litebus::Async(candidateLeader1->GetAID(), &K8sLeaderActor::Elect);
    litebus::Async(candidateLeader2->GetAID(), &K8sLeaderActor::Elect);
    litebus::Async(candidateLeader3->GetAID(), &K8sLeaderActor::Elect);
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader1->GetObservedRecord() != nullptr && candidateLeader1->GetObservedRecord()->holderIdentity != ""; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader2->GetObservedRecord() != nullptr && candidateLeader2->GetObservedRecord()->holderIdentity != ""; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader3->GetObservedRecord() != nullptr && candidateLeader3->GetObservedRecord()->holderIdentity != ""; });
    EXPECT_EQ("proposal003", candidateLeader1->GetObservedRecord()->holderIdentity);
    EXPECT_EQ("proposal003", candidateLeader2->GetObservedRecord()->holderIdentity);
    EXPECT_EQ("proposal003", candidateLeader3->GetObservedRecord()->holderIdentity);
    litebus::Terminate(candidateLeader1->GetAID());
    litebus::Await(candidateLeader1->GetAID());
    litebus::Terminate(candidateLeader2->GetAID());
    litebus::Await(candidateLeader2->GetAID());
    litebus::Terminate(candidateLeader3->GetAID());
    litebus::Await(candidateLeader3->GetAID());
}

/**
 * Feature: K8s Election
 * Description: elect without leader is overdue
 * 1. create 3 candidate wait to elect
 * 2. invoke elect
 * Expectation: one become leader, other become candidate
 */
TEST_F(LeaderTest, K8sMultiElectWithLeaderOverdue)
{
    auto mockClient1 = std::make_shared<MockKubeClient>();
    explorer::ElectionInfo electionInfo1{.identity = "proposal001", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300, .electRenewInterval= 300, };
    auto candidateLeader1 = std::make_shared<K8sLeaderActor>("function-master", electionInfo1, mockClient1, "default");
    explorer::ElectionInfo electionInfo2{.identity = "proposal002", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300, .electRenewInterval= 300,};
    auto mockClient2 = std::make_shared<MockKubeClient>();
    auto candidateLeader2 = std::make_shared<K8sLeaderActor>("function-master", electionInfo2, mockClient2, "default");
    explorer::ElectionInfo electionInfo3{.identity = "proposal003", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300, .electRenewInterval= 300,};
    auto mockClient3 = std::make_shared<MockKubeClient>();
    auto candidateLeader3 = std::make_shared<K8sLeaderActor>("function-master", electionInfo3, mockClient3, "default");
    litebus::Promise<std::shared_ptr<V1Lease>> errPromise;
    errPromise.SetFailed(400);
    EXPECT_CALL(*mockClient1, ReadNamespacedLease).WillOnce(testing::Return(MockKubeClient::CreateLease("proposal004", "2000-01-01 00:00:00", "2000-01-01 00:00:00"))).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal003")));
    EXPECT_CALL(*mockClient2, ReadNamespacedLease).WillOnce(testing::Return(MockKubeClient::CreateLease("proposal004", "2000-01-01 00:00:00", "2000-01-01 00:00:00"))).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal003")));
    EXPECT_CALL(*mockClient3, ReadNamespacedLease).WillOnce(testing::Return(MockKubeClient::CreateLease("proposal004", "2000-01-01 00:00:00", "2000-01-01 00:00:00"))).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal003")));
    EXPECT_CALL(*mockClient1, ReplaceNamespacedLease).WillRepeatedly(testing::Return(errPromise.GetFuture()));
    EXPECT_CALL(*mockClient2, ReplaceNamespacedLease).WillRepeatedly(testing::Return(errPromise.GetFuture()));
    EXPECT_CALL(*mockClient3, ReplaceNamespacedLease).WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal003")));
    litebus::Spawn(candidateLeader1);
    litebus::Spawn(candidateLeader2);
    litebus::Spawn(candidateLeader3);
    litebus::Async(candidateLeader1->GetAID(), &K8sLeaderActor::Elect);
    litebus::Async(candidateLeader2->GetAID(), &K8sLeaderActor::Elect);
    litebus::Async(candidateLeader3->GetAID(), &K8sLeaderActor::Elect);
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader1->GetObservedRecord() != nullptr && candidateLeader1->GetObservedRecord()->holderIdentity != "proposal004"; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader2->GetObservedRecord() != nullptr && candidateLeader2->GetObservedRecord()->holderIdentity != "proposal004"; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader3->GetObservedRecord() != nullptr && candidateLeader3->GetObservedRecord()->holderIdentity != "proposal004"; });
    EXPECT_EQ("proposal003", candidateLeader1->GetObservedRecord()->holderIdentity);
    EXPECT_EQ("proposal003", candidateLeader2->GetObservedRecord()->holderIdentity);
    EXPECT_EQ("proposal003", candidateLeader3->GetObservedRecord()->holderIdentity);
    litebus::Terminate(candidateLeader1->GetAID());
    litebus::Await(candidateLeader1->GetAID());
    litebus::Terminate(candidateLeader2->GetAID());
    litebus::Await(candidateLeader2->GetAID());
    litebus::Terminate(candidateLeader3->GetAID());
    litebus::Await(candidateLeader3->GetAID());
}

/**
 * Feature: K8s Election
 * Description: elect when other leader is valid
 * 1. create 1 candidate wait to elect
 * 2. invoke elect
 * Expectation: elect failed
 */
TEST_F(LeaderTest, K8sElectCandidateFail)
{
    auto mockClient1 = std::make_shared<MockKubeClient>();
    explorer::ElectionInfo electionInfo1{.identity = "proposal001", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300,  };
    auto candidateLeader1 = std::make_shared<K8sLeaderActor>("function-master", electionInfo1, mockClient1, "default");
    litebus::Promise<std::shared_ptr<V1Lease>> promise;
    promise.SetValue(MockKubeClient::CreateLease("proposal003"));
    EXPECT_CALL(*mockClient1, ReadNamespacedLease).WillRepeatedly(testing::Return(promise.GetFuture()));
    litebus::Spawn(candidateLeader1);
    litebus::Async(candidateLeader1->GetAID(), &K8sLeaderActor::Elect);
    ASSERT_AWAIT_TRUE([&]() -> bool { return candidateLeader1->GetObservedRecord() != nullptr; });
    EXPECT_EQ("proposal003", candidateLeader1->GetObservedRecord()->holderIdentity);
    litebus::Terminate(candidateLeader1->GetAID());
    litebus::Await(candidateLeader1->GetAID());
}

/**
 * Feature: K8s Election
 * Description: elect renew lease
 * 1. create 3 candidate wait to elect
 * 2. invoke elect
 * Expectation: one become leader, other become candidate
 */
TEST_F(LeaderTest, K8sElectRenewLease)
{
    auto mockClient1 = std::make_shared<MockKubeClient>();
    explorer::ElectionInfo electionInfo1{.identity = "proposal001", .mode = K8S_ELECTION_MODE, .electKeepAliveInterval = 1, .electLeaseTTL = 300, .electRenewInterval = 1 };
    auto candidateLeader1 = std::make_shared<K8sLeaderActor>("function-master", electionInfo1, mockClient1, "default");
    litebus::Promise<std::shared_ptr<V1Lease>> promise;
    promise.SetValue(MockKubeClient::CreateLease("proposal001"));
    EXPECT_CALL(*mockClient1, ReadNamespacedLease).WillRepeatedly(testing::Return(promise.GetFuture()));
    litebus::Future<std::shared_ptr<V1Lease>> body1;
    EXPECT_CALL(*mockClient1, ReplaceNamespacedLease)
        .WillOnce(testing::DoAll(test::FutureArg<2>(&body1), testing::Return(MockKubeClient::CreateLease("proposal001"))))
        .WillRepeatedly(testing::Return(MockKubeClient::CreateLease("proposal001")));
    litebus::Spawn(candidateLeader1);
    litebus::Async(candidateLeader1->GetAID(), &K8sLeaderActor::Elect);
    ASSERT_AWAIT_READY(body1);
    litebus::Terminate(candidateLeader1->GetAID());
    litebus::Await(candidateLeader1->GetAID());
}

}  // namespace functionsystem::test