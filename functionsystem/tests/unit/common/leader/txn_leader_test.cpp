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
#include "common/explorer/explorer.h"
#include "common/leader/txn_leader_actor.h"
#include "common/metadata/metadata.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace ::functionsystem::leader;
using namespace ::functionsystem::explorer;

class TxnLeaderTest : public ::testing::Test {
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
        std::string metaStoreServerHost = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost);

        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost }, {}, {});
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

TEST_F(TxnLeaderTest, TxnLeader_Success_Test)
{
    explorer::ElectionInfo election{ .identity = "127.0.0.1:80", .mode = TXN_ELECTION_MODE };
    std::shared_ptr<TxnLeaderActor> actor =
        std::make_shared<TxnLeaderActor>(DEFAULT_MASTER_ELECTION_KEY, election, metaStoreClient_);
    litebus::AID aid = litebus::Spawn(actor);

    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->watcher_ != nullptr; });  // initialize done

    EXPECT_AWAIT_TRUE([&]() -> bool { return !actor->campaigning_; });  // begin to elect

    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->leaseID_ != -1; });  // grant done

    litebus::Async(aid, &TxnLeaderActor::KeepAlive, actor->leaseID_);  // keep alive

    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->leader_; });  // elect success

    litebus::Terminate(aid);
    litebus::Await(aid);
}

TEST_F(TxnLeaderTest, TxnLeader_Change_Test)
{
    explorer::ElectionInfo election{ .identity = "127.0.0.1:80", .mode = TXN_ELECTION_MODE };
    std::shared_ptr<TxnLeaderActor> actor =
        std::make_shared<TxnLeaderActor>(DEFAULT_MASTER_ELECTION_KEY, election, metaStoreClient_);
    litebus::AID aid = litebus::Spawn(actor);

    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->watcher_ != nullptr; });  // initialize done

    EXPECT_AWAIT_TRUE([&]() -> bool { return !actor->campaigning_; });  // begin to elect

    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->leaseID_ != -1; });  // grant done
    // keep alive with illegal lease
    litebus::Async(aid, &TxnLeaderActor::KeepAlive, -1);

    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->leader_; });

    int64_t historyLeaseID = actor->leaseID_;
    EXPECT_AWAIT_READY(metaStoreClient_->Delete(DEFAULT_MASTER_ELECTION_KEY, {}));
    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->leaseID_ != historyLeaseID; });  // elect again

    litebus::Terminate(aid);
    litebus::Await(aid);
}

TEST_F(TxnLeaderTest, TxnLeader_Fail_Test)
{
    EXPECT_AWAIT_READY(metaStoreClient_->Put(DEFAULT_MASTER_ELECTION_KEY, "127.0.0.1:80", {}));

    explorer::ElectionInfo election{ .identity = "127.0.0.1:80", .mode = TXN_ELECTION_MODE };
    std::shared_ptr<TxnLeaderActor> actor =
        std::make_shared<TxnLeaderActor>(DEFAULT_MASTER_ELECTION_KEY, election, metaStoreClient_);
    litebus::AID aid = litebus::Spawn(actor);

    EXPECT_AWAIT_TRUE([&]() -> bool { return actor->watcher_ != nullptr; });  // initialize done

    // there's already a leader, do not elect.
    EXPECT_FALSE(actor->campaigning_);
    EXPECT_EQ(actor->leaseID_, -1);
    EXPECT_FALSE(actor->leader_);

    actor->leaseID_ = 0;  // mock grant success
    litebus::Async(aid, &TxnLeaderActor::KeepAlive, -1);

    auto getResponse = metaStoreClient_->Get(DEFAULT_MASTER_ELECTION_KEY, {});
    ASSERT_AWAIT_READY(getResponse);

    auto result = litebus::Async(aid, &TxnLeaderActor::Sync, getResponse.Get());
    ASSERT_AWAIT_READY(result);
    EXPECT_TRUE(result.Get().status.IsOk());

    litebus::Terminate(aid);
    litebus::Await(aid);
}

}  // namespace functionsystem::test