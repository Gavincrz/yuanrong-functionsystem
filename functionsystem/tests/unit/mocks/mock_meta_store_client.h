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

#ifndef UT_MOCKS_MOCK_META_STORE_CLIENT_H
#define UT_MOCKS_MOCK_META_STORE_CLIENT_H

#include <gmock/gmock.h>

#include "meta_store_client/meta_store_client.h"

namespace functionsystem::test {

class MockMetaStoreClient : public MetaStoreClient {
public:
    explicit MockMetaStoreClient(const std::string &address)
        : MetaStoreClient(MetaStoreConfig{ .etcdAddress = address })
    {
    }
    MOCK_METHOD(litebus::Future<std::shared_ptr<PutResponse>>, Put,
                (const std::string &key, const std::string &value, const PutOption &option), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<functionsystem::DeleteResponse>>, Delete,
                (const std::string &key, const DeleteOption &option), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<GetResponse>>, Get, (const std::string &key, const GetOption &option),
                (override));

    MOCK_METHOD(litebus::Future<LeaseGrantResponse>, Grant, (int ttl), (override));

    MOCK_METHOD(litebus::Future<LeaseRevokeResponse>, Revoke, (int64_t leaseID), (override));

    MOCK_METHOD(litebus::Future<LeaseKeepAliveResponse>, KeepAliveOnce, (int64_t leaseID), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<Watcher>>, Watch,
                (const std::string &key, const WatchOption &option,
                 const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer,
                 const SyncerFunction &syncer),
                (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<Watcher>>, GetAndWatch,
                (const std::string &key, const WatchOption &option,
                 const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer,
                 const SyncerFunction &syncer),
                (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<Watcher>>, GetAndWatchWithHandler,
                (const std::string &key, const WatchOption &option,
                 const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer,
                 const SyncerFunction &syncer, const ResponseHandler &handler),
                (override));

    MOCK_METHOD(std::shared_ptr<meta_store::TxnTransaction>, BeginTransaction, (), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<::etcdserverpb::TxnResponse>>, Commit,
                (const ::etcdserverpb::TxnRequest &, bool), (override));

    // election methods
    MOCK_METHOD(litebus::Future<CampaignResponse>, Campaign,
                (const std::string &name, int64_t lease, const std::string &value), (override));

    MOCK_METHOD(litebus::Future<LeaderResponse>, Leader, (const std::string &name), (override));

    MOCK_METHOD(litebus::Future<ResignResponse>, Resign, (const LeaderKey &leader), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<Observer>>, Observe,
                (const std::string &name, const std::function<void(LeaderResponse)> &callback), (override));

    MOCK_METHOD(litebus::Future<StatusResponse>, HealthCheck, (), (override));

    MOCK_METHOD(litebus::Future<bool>, IsConnected, (), (override));

    MOCK_METHOD(void, BindReconnectedCallBack, (const std::function<void(const std::string &)> &callback), (override));
};

}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_META_STORE_CLIENT_H
