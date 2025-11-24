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

#include "async/async.hpp"
#include "common/etcd_service/etcd_service_driver.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "kv_service_actor.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/port_helper.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;
using namespace test;
using ::testing::Invoke;
using ::testing::Return;

class MetaStoreKvTest : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_, "MetaStoreLeaseTest-");

        MetaStoreTimeoutOption options;
        options.operationRetryIntervalLowerBound = 10;
        options.operationRetryIntervalUpperBound = 100;
        options.operationRetryTimes = 3;
        options.grpcTimeout = 1;
        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }, GrpcSslConfig(), options);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        etcdSrvDriver_->StopServer();
    }

public:
    inline static std::string metaStoreServerHost_;
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::shared_ptr<MetaStoreClient> metaStoreClient_;
};

#define RECEIVE_HELPER(Operation)                                                             \
    void On##Operation##Event(const litebus::AID &aid, std::string &&name, std::string &&msg) \
    {                                                                                         \
        On##Operation(aid, std::move(name), std::move(msg));                                  \
    }                                                                                         \
    MOCK_METHOD(void, On##Operation, (const litebus::AID, std::string, std::string));

class KvTestActor : public litebus::ActorBase {
public:
    KvTestActor() : litebus::ActorBase("KvTestActor")
    {
    }

    void Init() override
    {
        Receive("OnPut", &KvTestActor::OnPutEvent);
        Receive("OnDelete", &KvTestActor::OnDeleteEvent);
        Receive("OnGet", &KvTestActor::OnGetEvent);
        Receive("OnTxn", &KvTestActor::OnTxnEvent);
        Receive("OnWatch", &KvTestActor::OnWatchEvent);
        Receive("OnGetAndWatch", &KvTestActor::OnGetAndWatchEvent);
    }

    void SendKvMessage(const litebus::AID &to, std::string name, std::string msg)
    {
        Send(to, std::move(name), std::move(msg));
    }

    RECEIVE_HELPER(Put);
    RECEIVE_HELPER(Delete);
    RECEIVE_HELPER(Get);
    RECEIVE_HELPER(Txn);
    RECEIVE_HELPER(Watch);
    RECEIVE_HELPER(GetAndWatch);
};

TEST_F(MetaStoreKvTest, KvInitExploreTest)  // NOLINT
{
    auto actor1 = std::make_shared<KvServiceActor>();
    actor1->InitExplorer();
    EXPECT_FALSE(actor1->needExplore_);

    litebus::AID fakeAID;
    auto actor2 = std::make_shared<KvServiceActor>(fakeAID, false);
    actor2->InitExplorer();
    EXPECT_FALSE(actor2->needExplore_);

    // fake AID, explore is true
    auto actor3 = std::make_shared<KvServiceActor>(fakeAID, true);
    actor3->InitExplorer();
    EXPECT_FALSE(actor3->needExplore_);
    EXPECT_EQ(actor3->curStatus_, MASTER_BUSINESS);
    EXPECT_EQ(actor3->businesses_.size(), 2);
    EXPECT_EQ(actor3->business_, actor3->businesses_[MASTER_BUSINESS]);

    // needExplore_ == false, ignore UpdateLeaderInfo
    actor3->UpdateLeaderInfo(GetLeaderInfo(actor2->GetAID()));
    EXPECT_EQ(actor3->curStatus_, MASTER_BUSINESS);
    EXPECT_EQ(actor3->business_, actor3->businesses_[MASTER_BUSINESS]);

    // real AID, but explore false
    auto actor4 = std::make_shared<KvServiceActor>(actor3->GetAID(), false);
    actor4->InitExplorer();
    EXPECT_FALSE(actor4->needExplore_);

    // real AID, and explore is true
    auto actor5 = std::make_shared<KvServiceActor>(actor3->GetAID(), true);
    actor5->InitExplorer();
    EXPECT_TRUE(actor5->needExplore_);
    EXPECT_EQ(actor5->curStatus_, SLAVE_BUSINESS);
    EXPECT_EQ(actor5->business_, actor5->businesses_[SLAVE_BUSINESS]);

    // needExplore_ == true, handle UpdateLeaderInfo
    actor5->UpdateLeaderInfo(GetLeaderInfo(actor5->GetAID()));
    EXPECT_EQ(actor5->curStatus_, MASTER_BUSINESS);
    EXPECT_EQ(actor5->business_, actor5->businesses_[MASTER_BUSINESS]);
    actor5->UpdateLeaderInfo(GetLeaderInfo(fakeAID));
    EXPECT_EQ(actor5->curStatus_, SLAVE_BUSINESS);
    EXPECT_EQ(actor5->business_, actor5->businesses_[SLAVE_BUSINESS]);

    // with prefix, only used in test
    std::string prefix = "prefix";
    auto actor7 = std::make_shared<KvServiceActor>(prefix);
    actor7->InitExplorer();
    EXPECT_FALSE(actor7->needExplore_);
    EXPECT_EQ(actor7->curStatus_, MASTER_BUSINESS);
    EXPECT_EQ(actor7->business_, actor7->businesses_[MASTER_BUSINESS]);
}

TEST_F(MetaStoreKvTest, KvSwitchoverTest)  // NOLINT
{
    auto persistActor =
        std::make_shared<EtcdKvClientStrategy>("Persist", metaStoreServerHost_, MetaStoreTimeoutOption{});
    litebus::Spawn(persistActor);
    auto backupActor = std::make_shared<BackupActor>("BackupActor", persistActor->GetAID());
    litebus::Spawn(backupActor);

    litebus::AID serviceAID;
    auto actor = std::make_shared<KvServiceActor>(backupActor->GetAID(), true);
    litebus::Spawn(actor);
    EXPECT_TRUE(actor->needExplore_);

    auto accessorActor = std::make_shared<KvServiceAccessorActor>(actor->GetAID());
    litebus::Spawn(accessorActor);

    // slave test
    auto updated = litebus::Async(actor->GetAID(), &KvServiceActor::UpdateLeaderInfo, GetLeaderInfo(serviceAID));
    ASSERT_AWAIT_READY(updated);
    EXPECT_EQ(actor->curStatus_, SLAVE_BUSINESS);

    auto testActor = std::make_shared<KvTestActor>();
    litebus::Spawn(testActor);

    messages::MetaStore::PutRequest putRequest;
    putRequest.set_requestid("1");
    putRequest.set_key("/123");
    putRequest.set_value("123");
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Put",
                   putRequest.SerializeAsString());

    messages::MetaStoreRequest req1;
    ::etcdserverpb::DeleteRangeRequest deleteReq;
    deleteReq.set_key("/123");
    req1.set_requestid("2");
    req1.set_requestmsg(deleteReq.SerializeAsString());
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Delete",
                   req1.SerializeAsString());

    messages::MetaStoreRequest req2;
    ::etcdserverpb::RangeRequest getReq;
    getReq.set_key("/123");
    req2.set_requestid("3");
    req2.set_requestmsg(getReq.SerializeAsString());
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Get",
                   req2.SerializeAsString());

    messages::MetaStoreRequest req3;
    ::etcdserverpb::TxnRequest txnReq;
    req3.set_requestid("4");
    req3.set_requestmsg(txnReq.SerializeAsString());
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Txn",
                   req3.SerializeAsString());

    messages::MetaStoreRequest req4;
    etcdserverpb::WatchRequest watchReq;
    req4.set_requestid("5");
    req4.set_requestmsg(watchReq.SerializeAsString());
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Watch",
                   req4.SerializeAsString());

    messages::MetaStoreRequest req5;
    req5.set_requestid("6");
    req5.set_requestmsg(watchReq.SerializeAsString());
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "GetAndWatch",
                   req5.SerializeAsString());

    sleep(1);

    // master test
    updated = litebus::Async(actor->GetAID(), &KvServiceActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));
    ASSERT_AWAIT_READY(updated);
    EXPECT_EQ(actor->curStatus_, MASTER_BUSINESS);

    litebus::Future<std::string> msg;
    EXPECT_CALL(*testActor, OnPut).WillOnce(testing::DoAll(test::FutureArg<2>(&msg), Return()));
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Put",
                   putRequest.SerializeAsString());
    messages::MetaStore::PutResponse resp;
    ASSERT_AWAIT_READY(msg);
    EXPECT_TRUE(resp.ParseFromString(msg.Get()));
    EXPECT_EQ(resp.requestid(), "1");

    msg = {};
    EXPECT_CALL(*testActor, OnDelete).WillOnce(testing::DoAll(test::FutureArg<2>(&msg), Return()));
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Delete",
                   req1.SerializeAsString());
    ASSERT_AWAIT_READY(msg);
    EXPECT_TRUE(resp.ParseFromString(msg.Get()));
    EXPECT_EQ(resp.requestid(), "2");

    msg = {};
    EXPECT_CALL(*testActor, OnGet).WillOnce(testing::DoAll(test::FutureArg<2>(&msg), Return()));
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Get",
                   req2.SerializeAsString());
    ASSERT_AWAIT_READY(msg);
    EXPECT_TRUE(resp.ParseFromString(msg.Get()));
    EXPECT_EQ(resp.requestid(), "3");

    msg = {};
    EXPECT_CALL(*testActor, OnTxn).WillOnce(testing::DoAll(test::FutureArg<2>(&msg), Return()));
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Txn",
                   req3.SerializeAsString());
    ASSERT_AWAIT_READY(msg);
    EXPECT_TRUE(resp.ParseFromString(msg.Get()));
    EXPECT_EQ(resp.requestid(), "4");

    msg = {};
    EXPECT_CALL(*testActor, OnWatch).WillOnce(testing::DoAll(test::FutureArg<2>(&msg), Return()));
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "Watch",
                   req4.SerializeAsString());
    ASSERT_AWAIT_READY(msg);
    EXPECT_TRUE(resp.ParseFromString(msg.Get()));
    EXPECT_EQ(resp.requestid(), "5");

    msg = {};
    EXPECT_CALL(*testActor, OnGetAndWatch).WillOnce(testing::DoAll(test::FutureArg<2>(&msg), Return()));
    litebus::Async(testActor->GetAID(), &KvTestActor::SendKvMessage, accessorActor->GetAID(), "GetAndWatch",
                   req5.SerializeAsString());
    ASSERT_AWAIT_READY(msg);
    EXPECT_TRUE(resp.ParseFromString(msg.Get()));
    EXPECT_EQ(resp.requestid(), "6");

    litebus::Terminate(accessorActor->GetAID());
    litebus::Await(accessorActor->GetAID());
    litebus::Terminate(testActor->GetAID());
    litebus::Await(testActor->GetAID());
    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
    litebus::Terminate(backupActor->GetAID());
    litebus::Await(backupActor->GetAID());
    litebus::Terminate(persistActor->GetAID());
    litebus::Await(persistActor->GetAID());
}

TEST_F(MetaStoreKvTest, KvWatchTest)  // NOLINT
{
    DeleteOption opt;
    opt.prefix = true;
    metaStoreClient_->Delete("/", opt);

    auto persistActor =
        std::make_shared<EtcdKvClientStrategy>("Persist", metaStoreServerHost_, MetaStoreTimeoutOption{});
    litebus::Spawn(persistActor);
    auto backupActor = std::make_shared<BackupActor>("BackupActor", persistActor->GetAID());
    litebus::Spawn(backupActor);

    // sync key
    ::mvccpb::KeyValue kv;
    kv.set_key("/1234");
    kv.set_value("1234");
    kv.set_mod_revision(9);
    metaStoreClient_->Put("/metastore/kv//1234", kv.SerializeAsString(), {});
    ASSERT_AWAIT_TRUE([&]() { return !metaStoreClient_->Get("/metastore/kv//1234", {}).Get()->kvs.empty(); });

    litebus::AID serviceAID;
    auto actor = std::make_shared<KvServiceActor>(backupActor->GetAID(), true);
    actor->modRevision_ = 15;

    litebus::Spawn(actor);
    EXPECT_TRUE(actor->needExplore_);
    auto accessorActor = std::make_shared<KvServiceAccessorActor>(actor->GetAID());
    litebus::Spawn(accessorActor);
    sleep(1);

    auto updated = litebus::Async(actor->GetAID(), &KvServiceActor::UpdateLeaderInfo, GetLeaderInfo(serviceAID));
    ASSERT_AWAIT_READY(updated);
    EXPECT_EQ(actor->curStatus_, SLAVE_BUSINESS);

    // early revision ignore
    kv.set_mod_revision(11);
    metaStoreClient_->Put("/metastore/kv//1234", kv.SerializeAsString(), {});

    kv.set_mod_revision(16);
    metaStoreClient_->Put("/metastore/kv//1234", kv.SerializeAsString(), {});
    ASSERT_AWAIT_TRUE([&]() { return actor->cache_.size() == 1; });
    ASSERT_AWAIT_TRUE([&]() { return actor->cache_.find("/1234") != actor->cache_.end(); });
    EXPECT_EQ(actor->cache_.at("/1234").key(), "/1234");
    EXPECT_EQ(actor->cache_.at("/1234").value(), "1234");
    ASSERT_AWAIT_TRUE([&]() { return actor->cache_.at("/1234").mod_revision() == 16; });
    ASSERT_AWAIT_TRUE([&]() { return actor->modRevision_ >= 16; });

    metaStoreClient_->Delete("/metastore/kv//1234", {});
    ASSERT_AWAIT_TRUE([&]() { return actor->cache_.size() == 0; });

    litebus::Terminate(accessorActor->GetAID());
    litebus::Await(accessorActor->GetAID());
    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
    litebus::Terminate(backupActor->GetAID());
    litebus::Await(backupActor->GetAID());
    litebus::Terminate(persistActor->GetAID());
    litebus::Await(persistActor->GetAID());
}

TEST_F(MetaStoreKvTest, KvSlaveTest)  // NOLINT
{
    auto actor1 = std::make_shared<KvServiceActor>();
    EXPECT_FALSE(actor1->needExplore_);

    auto member = std::make_shared<KvServiceActor::Member>();
    auto slaveBusiness = std::make_shared<KvServiceActor::SlaveBusiness>(member, actor1);
    slaveBusiness->OnChange();

    auto request = std::make_shared<messages::MetaStoreRequest>();
    auto status = slaveBusiness->AsyncTxn(litebus::AID(), request);
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsError());

    status = slaveBusiness->AsyncGet(litebus::AID(), request);
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsError());

    status = slaveBusiness->AsyncDelete(litebus::AID(), request);
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsError());

    auto putRequest = std::make_shared<messages::MetaStore::PutRequest>();
    status = slaveBusiness->AsyncPut(litebus::AID(), putRequest);
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsError());

    status = slaveBusiness->AsyncGetAndWatch(litebus::AID(), request);
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsError());

    status = slaveBusiness->AsyncWatch(litebus::AID(), request);
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsError());
}
}  // namespace functionsystem::meta_store::test