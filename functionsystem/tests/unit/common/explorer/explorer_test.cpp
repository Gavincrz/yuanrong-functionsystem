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

#include "common/explorer/explorer.h"

#include <gtest/gtest.h>

#include <async/async.hpp>

#include "common/constants/actor_name.h"
#include "common/constants/signal.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "common/explorer/etcd_explorer_actor.h"
#include "common/explorer/k8s_explorer_actor.h"
#include "common/explorer/txn_explorer_actor.h"
#include "common/logs/logging.h"
#include "common/metadata/metadata.h"
#include "common/utils/generate_message.h"
#include "mocks/mock_kube_client.h"
#include "mocks/mock_meta_store_client.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace functionsystem;
using namespace functionsystem::explorer;
using namespace ::testing;

const std::string DEFAULT_ELECTION_KEY = "key001";
const std::string DEFAULT_ELECTION_PROPOSAL = "proposal001";
const int64_t DEFAULT_LEASE_ID = 1234567;
const int64_t DEFAULT_LEASE_TTL = 300;

class ExplorerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        YRLOG_DEBUG("ExplorerTest SetUp method called");
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        mockMetaClient_ = std::make_shared<MockMetaStoreClient>("127.0.0.1:" + std::to_string(metaStoreServerPort));
    }

    void TearDown() override
    {
        Explorer::GetInstance().Clear();
    }

protected:
    std::shared_ptr<MockMetaStoreClient> mockMetaClient_;
};

TEST_F(ExplorerTest, StandaloneMode)
{
    // TEST 0: in standalone mode
    // EXPECT 0: observe won't be called
    EXPECT_CALL(*mockMetaClient_, Observe).Times(0);

    // TEST 1: New leader info, and new standalone explorer
    explorer::LeaderInfo leaderInfo{ .name = DEFAULT_MASTER_ELECTION_KEY, .address = "123" };
    explorer::ElectionInfo electionInfo{ .identity = "123", .mode = STANDALONE_MODE };
    (void)Explorer::CreateExplorer(electionInfo, leaderInfo, mockMetaClient_, nullptr, "default");

    LeaderInfo cachedLeaderInfo;
    // TEST 1.1: register callback
    Explorer::GetInstance().AddLeaderChangedCallback(
        "cbid", [&cachedLeaderInfo](const LeaderInfo &lf) { cachedLeaderInfo = lf; });

    ASSERT_AWAIT_TRUE([&cachedLeaderInfo]() { return cachedLeaderInfo.address == "123"; });
}

TEST_F(ExplorerTest, EtcdElectionMode)
{
    explorer::LeaderInfo leaderInfo{ .name = DEFAULT_MASTER_ELECTION_KEY, .address = "123" };
    explorer::ElectionInfo electionInfo{ .identity = "123", .mode = ETCD_ELECTION_MODE };
    (void)Explorer::CreateExplorer(electionInfo, leaderInfo, mockMetaClient_, nullptr, "default");

    auto explorerActor = Explorer::GetInstance().GetExplorer(DEFAULT_MASTER_ELECTION_KEY);

    LeaderInfo cachedLeaderInfo;
    // TEST 1: register callback
    Explorer::GetInstance().AddLeaderChangedCallback(
        "cbid", [&cachedLeaderInfo](const LeaderInfo &lf) { cachedLeaderInfo = lf; });

    // TEST 1.1: mock event, use OnObserveEvent to mock update the first leader
    LeaderResponse response1{ .status = Status::OK(),
                              .header = {},
                              .kv = std::make_pair(DEFAULT_MASTER_ELECTION_KEY, "name") };
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent, response1);

    // TEST 2: callback is triggered
    ASSERT_AWAIT_TRUE([&]() -> bool { return cachedLeaderInfo.address == response1.kv.second; });

    // TEST 3: next event, use OnObserveEvent to mock leader changes
    LeaderResponse response2{ .status = Status::OK(),
                              .header = {},
                              .kv = std::make_pair(DEFAULT_MASTER_ELECTION_KEY, "name2") };
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::OnObserveEvent, response2);

    // EXPECT 3.2: leaderinfo updated to the response2
    ASSERT_AWAIT_TRUE([&]() -> bool { return cachedLeaderInfo.address == response2.kv.second; });

    explorer::LeaderInfo leaderInfo1{ .name = "name3", .address = "456", .electRevision = 1000 };
    litebus::Async(explorerActor->GetAID(), &EtcdExplorerActor::FastPublish, leaderInfo1);
    ASSERT_AWAIT_TRUE([&]() -> bool { return cachedLeaderInfo.address == leaderInfo1.address; });
    explorer::LeaderInfo leaderInfo2{ .name = "name3", .address = "789", .electRevision = 999 };
    explorerActor->FastPublish(leaderInfo2);
    EXPECT_EQ(cachedLeaderInfo.address, "456");
    // unregister all of them and clear the explorers
    Explorer::GetInstance().RemoveLeaderChangedCallback("cbid");

    // unregister key that doesn't exist
    auto status = Explorer::GetInstance().RemoveLeaderChangedCallback("cbid");
    EXPECT_TRUE(status.IsOk());

    litebus::Terminate(explorerActor->GetAID());
    litebus::Await(explorerActor->GetAID());
}

TEST_F(ExplorerTest, K8sElectionMode)
{
    explorer::LeaderInfo leaderInfo{ .name = FUNCTION_MASTER_K8S_LEASE_NAME, .address = "123" };
    explorer::ElectionInfo electionInfo{
        .identity = "123",
        .mode = K8S_ELECTION_MODE,
        .electKeepAliveInterval = 30,
        .electLeaseTTL = 300,
    };
    auto mockClient1 = std::make_shared<MockKubeClient>();
    (void)Explorer::CreateExplorer(electionInfo, leaderInfo, mockMetaClient_, mockClient1, "default");
    LeaderInfo cachedLeaderInfo;
    // TEST 1: register callback
    Explorer::GetInstance().AddLeaderChangedCallback(
        "cbid", [&cachedLeaderInfo](const LeaderInfo &lf) { cachedLeaderInfo = lf; });
    auto lease = MockKubeClient::CreateLease("123");
    litebus::Promise<std::shared_ptr<V1Lease>> promise;
    promise.SetValue(lease);
    EXPECT_CALL(*mockClient1, ReadNamespacedLease).WillRepeatedly(testing::Return(promise.GetFuture()));
    auto explorerActor = Explorer::GetInstance().GetExplorer(FUNCTION_MASTER_K8S_LEASE_NAME);
    litebus::Async(explorerActor->GetAID(), &K8sExplorerActor::FastPublish, LeaderInfo{});
    litebus::Async(explorerActor->GetAID(), &K8sExplorerActor::Observe);
    ASSERT_AWAIT_TRUE([&]() -> bool { return cachedLeaderInfo.address == "123"; });
}

TEST_F(ExplorerTest, TxnElectionMode)
{
    auto watcher = std::make_shared<Watcher>([](int64_t watchID) {});
    EXPECT_CALL(*mockMetaClient_, GetAndWatch(testing::_,testing::_,testing::_,testing::_)).WillOnce(testing::Return(watcher));

    explorer::ElectionInfo electionInfo{ .identity = "123", .mode = TXN_ELECTION_MODE };
    explorer::LeaderInfo leaderInfo{ .name = DEFAULT_MASTER_ELECTION_KEY, .address = "123" };
    (void)Explorer::CreateExplorer(electionInfo, leaderInfo, mockMetaClient_, nullptr, "default");

    auto actor = Explorer::GetInstance().GetExplorer(DEFAULT_MASTER_ELECTION_KEY);
    auto *explorer = dynamic_cast<TxnExplorerActor *>(actor.get());
    EXPECT_NE(explorer, nullptr);

    EXPECT_AWAIT_TRUE([&]() -> bool { return explorer->watcher_ != nullptr; });

    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
}

}  // namespace functionsystem::test