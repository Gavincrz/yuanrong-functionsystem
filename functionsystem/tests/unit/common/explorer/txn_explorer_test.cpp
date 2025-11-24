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
#include "common/etcd_service/etcd_service_driver.h"
#include "common/explorer/txn_explorer_actor.h"
#include "common/metadata/metadata.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace ::functionsystem::explorer;

class TxnExplorerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        EXPECT_AWAIT_READY(metaStoreClient_->Delete("/", { .prefix = true }));
    }

    void TearDown() override
    {
    }

    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        etcdSrvDriver_->StartServer("127.0.0.1:" + std::to_string(metaStoreServerPort));
        metaStoreClient_ =
            MetaStoreClient::Create({ .etcdAddress = "127.0.0.1:" + std::to_string(metaStoreServerPort) }, {}, {});
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        metaStoreClient_ = nullptr;

        etcdSrvDriver_->StopServer();
        etcdSrvDriver_ = nullptr;
    }

protected:
    inline static std::shared_ptr<MetaStoreClient> metaStoreClient_;
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
};

TEST_F(TxnExplorerTest, TxnElectionTest)
{
    explorer::ElectionInfo election{ .identity = "127.0.0.1:80", .mode = TXN_ELECTION_MODE };
    explorer::LeaderInfo leader{ .name = DEFAULT_MASTER_ELECTION_KEY, .address = "127.0.0.1:80" };

    std::shared_ptr<TxnExplorerActor> actor =
        std::make_shared<TxnExplorerActor>(DEFAULT_MASTER_ELECTION_KEY, election, leader, metaStoreClient_);
    litebus::AID aid = litebus::Spawn(actor);

    // 1. initialize success, watch leader info
    std::shared_ptr<LeaderInfo> leaderInfo = std::make_shared<LeaderInfo>();
    litebus::Async(aid, &TxnExplorerActor::RegisterLeaderChangedCallback, "127.0.0.1:80",
                   [&](const LeaderInfo &info) { leaderInfo->address = info.address; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->watcher_ != nullptr; });

    // 2. receive a leader info
    EXPECT_AWAIT_READY(metaStoreClient_->Put(DEFAULT_MASTER_ELECTION_KEY, "127.0.0.1:80", {}));
    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->cachedLeaderInfo_.address == "127.0.0.1:80"; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return leaderInfo->address == "127.0.0.1:80"; });

    // 3. illegal revision, not update leader info
    auto latest = metaStoreClient_->Get(DEFAULT_MASTER_ELECTION_KEY, {});
    EXPECT_AWAIT_READY(latest);
    actor->electRevision_ = latest.Get()->header.revision + 2;       // illegal revision
    actor->cachedLeaderInfo_.electRevision = actor->electRevision_;  // illegal revision
    EXPECT_AWAIT_READY(metaStoreClient_->Put(DEFAULT_MASTER_ELECTION_KEY, "127.0.0.1:81", {}));
    EXPECT_TRUE(actor->cachedLeaderInfo_.address == "127.0.0.1:80");

    auto getResponse = metaStoreClient_->Get(DEFAULT_MASTER_ELECTION_KEY, {});
    ASSERT_AWAIT_READY(getResponse);
    auto result = litebus::Async(aid, &TxnExplorerActor::Sync, getResponse.Get());
    ASSERT_AWAIT_READY(result);
    EXPECT_TRUE(result.Get().status.IsOk());

    EXPECT_AWAIT_READY(metaStoreClient_->Delete(DEFAULT_MASTER_ELECTION_KEY, {}));
    litebus::Async(aid, &TxnExplorerActor::FastPublish, LeaderInfo{});

    litebus::Terminate(aid);
    litebus::Await(aid);
}

}  // namespace functionsystem::test