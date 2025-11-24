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

#define private public
#include "meta_store_client/meta_store_client_mgr.h"
#include "meta_store_client/election/etcd_election_client_strategy.h"
#include "meta_store_client/election/meta_store_election_client_strategy.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/key_value/meta_store_kv_client_strategy.h"
#include "meta_store_client/lease/etcd_lease_client_strategy.h"
#include "meta_store_client/lease/meta_store_lease_client_strategy.h"
#include "meta_store_client/maintenance/etcd_maintenance_client_strategy.h"
#include "meta_store_client/maintenance/meta_store_maintenance_client_strategy.h"

namespace functionsystem::meta_store::test {
using namespace testing;

const GrpcSslConfig sslConfig{};
const std::string metaStoreServerHost{ "127.1.1.0:3333" };

class MetaStoreClientMgrTest : public ::testing::Test {
};

TEST_F(MetaStoreClientMgrTest, EtcdMode)
{
    MetaStoreConfig metaStoreConfig{ .etcdAddress = metaStoreServerHost, .metaStoreAddress = "",
                                     .enableMetaStore= false, .etcdTablePrefix = "/test" };
    auto metaStoreClientMgr = std::make_shared<MetaStoreClientMgr>(metaStoreConfig);
    EXPECT_EQ(metaStoreClientMgr->Init(), Status::OK());
    EXPECT_NE(metaStoreClientMgr->etcdKvClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->etcdMaintenanceClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->etcdLeaseClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->etcdElectionClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->msKvClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->msMaintenanceClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->msLeaseClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->msElectionClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->GetKvClient(""), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetMaintenanceClient(), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetLeaseClient(), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetElectionClient(), nullptr);
}

TEST_F(MetaStoreClientMgrTest, MetaStorePassThroughMode)
{
    MetaStoreConfig metaStoreConfig{ .etcdAddress = metaStoreServerHost, .metaStoreAddress = metaStoreServerHost,
                                     .enableMetaStore = true, .isMetaStorePassthrough = true, .etcdTablePrefix = "/test" };
    auto metaStoreClientMgr = std::make_shared<MetaStoreClientMgr>(metaStoreConfig);
    EXPECT_EQ(metaStoreClientMgr->Init(), Status::OK());
    EXPECT_EQ(metaStoreClientMgr->etcdKvClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->etcdMaintenanceClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->etcdLeaseClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->etcdElectionClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msKvClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msMaintenanceClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msLeaseClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msElectionClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->GetKvClient(""), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetMaintenanceClient(), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetLeaseClient(), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetElectionClient(), nullptr);
}

TEST_F(MetaStoreClientMgrTest, MetaStoreLocalModeNoEtcd)
{
    // no key needed to be stored in etcd
    MetaStoreConfig metaStoreConfig{ .etcdAddress = metaStoreServerHost, .metaStoreAddress = metaStoreServerHost,
                                     .enableMetaStore = true, .isMetaStorePassthrough = false,
                                     .etcdTablePrefix = "/test" };
    auto metaStoreClientMgr = std::make_shared<MetaStoreClientMgr>(metaStoreConfig);
    EXPECT_EQ(metaStoreClientMgr->Init(), Status::OK());
    metaStoreClientMgr->UpdateMetaStoreAddress(metaStoreServerHost);
    EXPECT_EQ(metaStoreClientMgr->etcdKvClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->etcdMaintenanceClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->etcdLeaseClient_, nullptr);
    EXPECT_EQ(metaStoreClientMgr->etcdElectionClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msKvClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msMaintenanceClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msLeaseClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msElectionClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->GetKvClient(""), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetMaintenanceClient(), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetLeaseClient(), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetElectionClient(), nullptr);
}

TEST_F(MetaStoreClientMgrTest, MetaStoreLocalModeWithEtcd)
{
    // key(/yr/pool) needed to be stored in etcd
    MetaStoreConfig metaStoreConfig{ .etcdAddress = metaStoreServerHost, .metaStoreAddress = metaStoreServerHost,
                                     .enableMetaStore = true, .isMetaStorePassthrough = false,
                                     .etcdTablePrefix = "/test", .excludedKeys = { "yr/pool" } };
    auto metaStoreClientMgr = std::make_shared<MetaStoreClientMgr>(metaStoreConfig);
    EXPECT_EQ(metaStoreClientMgr->Init(), Status::OK());
    EXPECT_NE(metaStoreClientMgr->etcdKvClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->etcdMaintenanceClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->etcdLeaseClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->etcdElectionClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msKvClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msMaintenanceClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msLeaseClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->msElectionClient_, nullptr);
    EXPECT_NE(metaStoreClientMgr->GetKvClient(""), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetMaintenanceClient(), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetLeaseClient(), nullptr);
    EXPECT_NE(metaStoreClientMgr->GetElectionClient(), nullptr);
    metaStoreClientMgr->UpdateMetaStoreAddress("http://127.1.1.0:3334");
    EXPECT_EQ(metaStoreClientMgr->metaStoreExplorer_->Explore().Get(), "http://127.1.1.0:3334");

}
}
