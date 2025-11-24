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
#include "lease_service_actor.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/port_helper.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;
using namespace test;
using ::testing::Invoke;
using ::testing::Return;

class MetaStoreLeaseTest : public ::testing::Test {
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

class LeaseTestActor : public litebus::ActorBase {
public:
    LeaseTestActor() : litebus::ActorBase("LeaseTestActor")
    {
    }

    void Init() override
    {
        Receive("GrantCallback", &LeaseTestActor::GrantCallback);
        Receive("RevokeCallback", &LeaseTestActor::RevokeCallback);
        Receive("KeepAliveCallback", &LeaseTestActor::KeepAliveCallback);
    }

    void SendLeaseMessage(const litebus::AID &to, std::string name, std::string msg)
    {
        Send(to, std::move(name), std::move(msg));
    }

    MOCK_METHOD(void, GrantCallback, (const litebus::AID &, std::string &&, std::string &&));
    MOCK_METHOD(void, RevokeCallback, (const litebus::AID &, std::string &&, std::string &&));
    MOCK_METHOD(void, KeepAliveCallback, (const litebus::AID &, std::string &&, std::string &&));
};

TEST_F(MetaStoreLeaseTest, LeaseInitExploreTest)  // NOLINT
{
    litebus::AID serviceAID;
    auto actor1 = std::make_shared<LeaseServiceActor>(serviceAID);
    actor1->InitExplorer();
    EXPECT_FALSE(actor1->needExplore_);

    litebus::AID fakeAID;
    auto actor2 = std::make_shared<LeaseServiceActor>(serviceAID, false, fakeAID);
    actor2->InitExplorer();
    EXPECT_FALSE(actor2->needExplore_);

    // fake AID, explore is true
    auto actor3 = std::make_shared<LeaseServiceActor>(serviceAID, true, fakeAID);
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
    auto actor4 = std::make_shared<LeaseServiceActor>(serviceAID, false, actor3->GetAID());
    actor4->InitExplorer();
    EXPECT_FALSE(actor4->needExplore_);

    // real AID, and explore is true
    auto actor5 = std::make_shared<LeaseServiceActor>(serviceAID, true, actor3->GetAID());
    actor5->InitExplorer();
    EXPECT_TRUE(actor5->needExplore_);
    EXPECT_EQ(actor5->curStatus_, SLAVE_BUSINESS);
    EXPECT_EQ(actor5->business_, actor5->businesses_[SLAVE_BUSINESS]);

    // needExplore_ == true, handle UpdateLeaderInfo
    actor5->UpdateLeaderInfo(GetLeaderInfo(actor5->GetAID()));
    EXPECT_EQ(actor5->curStatus_, MASTER_BUSINESS);
    EXPECT_EQ(actor5->business_, actor5->businesses_[MASTER_BUSINESS]);
    actor5->UpdateLeaderInfo(GetLeaderInfo(serviceAID));
    EXPECT_EQ(actor5->curStatus_, SLAVE_BUSINESS);
    EXPECT_EQ(actor5->business_, actor5->businesses_[SLAVE_BUSINESS]);
}

TEST_F(MetaStoreLeaseTest, LeaseSwitchoverTest)  // NOLINT
{
    auto persistActor =
        std::make_shared<EtcdKvClientStrategy>("Persist", metaStoreServerHost_, MetaStoreTimeoutOption{});
    litebus::Spawn(persistActor);
    auto backupActor = std::make_shared<BackupActor>("BackupActor", persistActor->GetAID());
    litebus::Spawn(backupActor);

    litebus::AID serviceAID;
    auto actor = std::make_shared<LeaseServiceActor>(serviceAID, true, backupActor->GetAID());
    litebus::Spawn(actor);
    EXPECT_TRUE(actor->needExplore_);

    // slave test
    auto updated = litebus::Async(actor->GetAID(), &LeaseServiceActor::UpdateLeaderInfo, GetLeaderInfo(serviceAID));
    ASSERT_AWAIT_READY(updated);
    EXPECT_EQ(actor->curStatus_, SLAVE_BUSINESS);

    auto status = litebus::Async(actor->GetAID(), &LeaseServiceActor::Start);
    ASSERT_AWAIT_READY(status);
    ASSERT_AWAIT_TRUE([&]() { return actor->running_; });

    auto testActor = std::make_shared<LeaseTestActor>();
    litebus::Spawn(testActor);

    {
        EXPECT_CALL(*testActor, GrantCallback).Times(0);
        EXPECT_CALL(*testActor, RevokeCallback).Times(0);
        EXPECT_CALL(*testActor, KeepAliveCallback).Times(0);

        litebus::Async(testActor->GetAID(), &LeaseTestActor::SendLeaseMessage, actor->GetAID(), "ReceiveGrant", "");
        litebus::Async(testActor->GetAID(), &LeaseTestActor::SendLeaseMessage, actor->GetAID(), "ReceiveRevoke", "");
        litebus::Async(testActor->GetAID(), &LeaseTestActor::SendLeaseMessage, actor->GetAID(), "ReceiveKeepAliveOnce",
                       "");
    }
    sleep(1);

    // master test
    updated = litebus::Async(actor->GetAID(), &LeaseServiceActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));
    ASSERT_AWAIT_READY(updated);
    EXPECT_EQ(actor->curStatus_, MASTER_BUSINESS);

    ::etcdserverpb::LeaseGrantRequest grantRequest;
    messages::MetaStoreRequest req;
    req.set_requestid("1");
    req.set_requestmsg(grantRequest.SerializeAsString());
    EXPECT_CALL(*testActor, GrantCallback).WillOnce(Return());
    litebus::Async(testActor->GetAID(), &LeaseTestActor::SendLeaseMessage, actor->GetAID(), "ReceiveGrant",
                   req.SerializeAsString());

    ::etcdserverpb::LeaseRevokeRequest revokeRequest;
    req.set_requestid("2");
    req.set_requestmsg(revokeRequest.SerializeAsString());
    EXPECT_CALL(*testActor, RevokeCallback).WillOnce(Return());
    litebus::Async(testActor->GetAID(), &LeaseTestActor::SendLeaseMessage, actor->GetAID(), "ReceiveRevoke",
                   req.SerializeAsString());

    ::etcdserverpb::LeaseKeepAliveRequest keepAliveRequest;
    req.set_requestid("3");
    req.set_requestmsg(keepAliveRequest.SerializeAsString());
    bool finished = false;
    EXPECT_CALL(*testActor, KeepAliveCallback).WillOnce(testing::Assign(&finished, true));
    litebus::Async(testActor->GetAID(), &LeaseTestActor::SendLeaseMessage, actor->GetAID(), "ReceiveKeepAliveOnce",
                   req.SerializeAsString());
    ASSERT_AWAIT_TRUE([&]() { return finished; });

    litebus::Terminate(testActor->GetAID());
    litebus::Await(testActor->GetAID());
    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
    litebus::Terminate(backupActor->GetAID());
    litebus::Await(backupActor->GetAID());
    litebus::Terminate(persistActor->GetAID());
    litebus::Await(persistActor->GetAID());
}

TEST_F(MetaStoreLeaseTest, LeaseWatchTest)  // NOLINT
{
    DeleteOption opt;
    opt.prefix = true;
    metaStoreClient_->Delete("/", opt);

    auto persistActor =
        std::make_shared<EtcdKvClientStrategy>("Persist", metaStoreServerHost_, MetaStoreTimeoutOption{});
    litebus::Spawn(persistActor);
    auto backupActor = std::make_shared<BackupActor>("BackupActor", persistActor->GetAID());
    litebus::Spawn(backupActor);

    litebus::AID serviceAID;
    auto actor = std::make_shared<LeaseServiceActor>(serviceAID, true, backupActor->GetAID());
    litebus::Spawn(actor);
    EXPECT_TRUE(actor->needExplore_);

    // slave test
    auto updated = litebus::Async(actor->GetAID(), &LeaseServiceActor::UpdateLeaderInfo, GetLeaderInfo(serviceAID));
    ASSERT_AWAIT_READY(updated);
    EXPECT_EQ(actor->curStatus_, SLAVE_BUSINESS);

    auto status = litebus::Async(actor->GetAID(), &LeaseServiceActor::Start);
    ASSERT_AWAIT_READY(status);
    ASSERT_AWAIT_TRUE([&]() { return actor->running_; });

    ::messages::Lease lease;
    lease.set_id(1234);
    lease.set_ttl(123);
    metaStoreClient_->Put("/metastore/lease/1234", lease.SerializeAsString(), {});
    ASSERT_AWAIT_TRUE([&]() { return actor->leases_.size() == 1; });
    ASSERT_AWAIT_TRUE([&]() { return actor->leases_.find(1234) != actor->leases_.end(); });
    EXPECT_EQ(actor->leases_.at(1234).id(), 1234);
    EXPECT_EQ(actor->leases_.at(1234).ttl(), 123);
    EXPECT_NE(actor->leases_.at(1234).expiry(), 0);

    metaStoreClient_->Delete("/metastore/lease/1234", {});
    ASSERT_AWAIT_TRUE([&]() { return actor->leases_.size() == 0; });

    metaStoreClient_->Put("/metastore/lease/1234", lease.SerializeAsString(), {});
    ASSERT_AWAIT_TRUE([&]() { return actor->leases_.size() == 1; });

    // master test
    updated = litebus::Async(actor->GetAID(), &LeaseServiceActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));
    ASSERT_AWAIT_READY(updated);
    EXPECT_EQ(actor->curStatus_, MASTER_BUSINESS);

    metaStoreClient_->Delete("/metastore/lease/1234", {});
    ASSERT_AWAIT_TRUE([&]() { return actor->leases_.size() == 1; });

    metaStoreClient_->Put("/metastore/lease/1234", lease.SerializeAsString(), {});
    ASSERT_AWAIT_TRUE([&]() { return actor->leases_.size() == 1; });

    litebus::Terminate(actor->GetAID());
    litebus::Await(actor->GetAID());
    litebus::Terminate(backupActor->GetAID());
    litebus::Await(backupActor->GetAID());
    litebus::Terminate(persistActor->GetAID());
    litebus::Await(persistActor->GetAID());
}

TEST_F(MetaStoreLeaseTest, LeaseSlaveTest)  // NOLINT
{
    litebus::AID serviceAID;
    auto actor1 = std::make_shared<LeaseServiceActor>(serviceAID);
    EXPECT_FALSE(actor1->needExplore_);

    auto member = std::make_shared<LeaseServiceActor::Member>();
    auto slaveBusiness = std::make_shared<LeaseServiceActor::SlaveBusiness>(member, actor1);
    slaveBusiness->OnChange();

    messages::MetaStoreRequest request;
    slaveBusiness->ReceiveKeepAlive(litebus::AID(), request);
    slaveBusiness->ReceiveRevoke(litebus::AID(), request);
    slaveBusiness->ReceiveGrant(litebus::AID(), request);
}
}  // namespace functionsystem::meta_store::test