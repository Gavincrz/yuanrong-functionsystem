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

#include "meta_store_client/meta_store_client.h"

#include <gtest/gtest.h>

#include <string>

#include "common/etcd_service/etcd_service_driver.h"
#include "meta_store_client/election/meta_store_election_client_strategy.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/lease/etcd_lease_client_strategy.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/utils/meta_store_kv_operation.h"
#include "mocks/mock_etcd_election_service.h"
#include "mocks/mock_etcd_lease_service.h"
#include "mocks/mock_etcd_watch_service.h"
#include "utils/future_test_helper.h"
#include "utils/grpc_client_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;
using namespace testing;
using GrpcCode = ::grpc::StatusCode;

const GrpcSslConfig sslConfig{};

class MetaStoreClientTest : public ::testing::Test {
protected:
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static grpc::Server *electionServer_;
    inline static std::shared_ptr<MockEtcdElectionService> electionService_;
    inline static std::shared_ptr<MockEtcdLeaseService> leaseService_;

    inline static grpc::Server *watchServer_;
    inline static std::shared_ptr<MockEtcdWatchService> watchService_;

    inline static litebus::Future<bool> sigReceived_;
    inline static std::string metaStoreServerHost_;
    inline static std::string watchHost_;
    inline static std::string electionHost_;

    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
        int watchPort = functionsystem::test::FindAvailablePort();
        watchHost_ = "127.0.0.1:" + std::to_string(watchPort);
        int electionPort = functionsystem::test::FindAvailablePort();
        electionHost_ = "127.0.0.1:" + std::to_string(electionPort);

        electionService_ = std::make_shared<MockEtcdElectionService>();
        leaseService_ = std::make_shared<MockEtcdLeaseService>();

        // start grpc server for mock election service
        auto p = std::make_shared<litebus::Promise<bool>>();
        YRLOG_DEBUG("start election grpc server {}");
        auto t = std::thread(StartMockEtcdElectionService, p, &electionServer_, electionHost_);
        t.detach();
        p->GetFuture().Get();

        watchService_ = std::make_shared<MockEtcdWatchService>();
        // start grpc server for mock watch service
        auto wp = std::make_shared<litebus::Promise<bool>>();
        YRLOG_DEBUG("start watch grpc server {}");
        auto wt = std::thread(StartMockEtcdWatchService, wp, &watchServer_, watchHost_);
        wt.detach();
        wp->GetFuture().Get();
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        etcdSrvDriver_->StopServer();

        if (electionServer_ != nullptr) {
            electionServer_->Shutdown(std::chrono::system_clock::now());
            electionServer_ = nullptr;
        }

        electionService_ = nullptr;
        leaseService_ = nullptr;

        if (watchServer_ != nullptr) {
            watchServer_->Shutdown(std::chrono::system_clock::now());
            watchServer_ = nullptr;
        }
        watchService_ = nullptr;
    }

    static void sigHandler(int signum)
    {
        sigReceived_.SetValue(true);
    }

protected:
    MetaStoreTimeoutOption metaStoreTimeoutOpt_ = {
        .operationRetryIntervalLowerBound = 10,
        .operationRetryIntervalUpperBound = 100,
        .operationRetryTimes = 2,
        .grpcTimeout = 1,
    };

    void SetUp() override
    {
        PutOption op = { .leaseId = 0, .prevKv = false };
        MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_,
                                                .metaStoreAddress = "",
                                                .enableMetaStore = false,
                                                .etcdTablePrefix = "/test" });
        client.Init();
        client.UpdateMetaStoreAddress(metaStoreServerHost_);
        client.Put("llt/sn/workers/xxx", "1.0", op).Get();
        client.Put("llt/sn/workers/yyy", "1.0", op).Get();
        client.Put("llt/sn/workers/zzz", "1.0", op).Get();
        client.Put("llt/sn/proxy/zzz", "1.0", op).Get();
    }

    void TearDown() override
    {
        DeleteOption op = { .prevKv = false, .prefix = true };

        MetaStoreClient metastoreClient(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_,
                                                         .metaStoreAddress = "",
                                                         .enableMetaStore = false,
                                                         .etcdTablePrefix = "/test" });
        metastoreClient.Init();
        metastoreClient.Delete("llt/sn/workers/", op).Get();  // delete all llt
    }

    static void StartMockEtcdElectionService(std::shared_ptr<litebus::Promise<bool>> p, grpc::Server **s,
                                             const std::string &serverAddr)
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort(serverAddr, grpc::InsecureServerCredentials());
        builder.RegisterService(electionService_.get());
        builder.RegisterService(leaseService_.get());
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        YRLOG_DEBUG("election server listening on {}", serverAddr);
        *s = server.get();
        p->SetValue(true);
        server->Wait();
    }

    static void StartMockEtcdWatchService(std::shared_ptr<litebus::Promise<bool>> p, grpc::Server **s,
                                             const std::string &serverAddr)
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort(serverAddr, grpc::InsecureServerCredentials());
        builder.RegisterService(watchService_.get());
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        YRLOG_DEBUG("watch server listening on {}", serverAddr);
        *s = server.get();
        p->SetValue(true);
        server->Wait();
    }

    ::grpc::Status GrpcStatus(const GrpcCode &code, const std::string &msg = "")
    {
        return ::grpc::Status(code, msg);
    }

    static void StartElectionGrantLease()
    {
        ::etcdserverpb::LeaseGrantResponse leaseGrantResponse;
        leaseGrantResponse.set_id(123);
        EXPECT_CALL(*leaseService_, LeaseGrant)
            .WillRepeatedly(DoAll(SetArgPointee<2>(leaseGrantResponse), Return(::grpc::Status::OK)));
    }
};

TEST_F(MetaStoreClientTest, OperateEtcdFailed)  // NOLINT
{
    auto helper = GrpcClientHelper(10);
    MetaStoreClient errorClient(MetaStoreConfig{ .etcdAddress = "127.0.0.1:33333", .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    errorClient.Init();
    PutOption op = { .leaseId = 0, .prevKv = false };
    auto response = errorClient.Put("llt/sn/error/put", "2.0", op).Get();
    EXPECT_TRUE(response->status.IsError());
    EXPECT_FALSE(response->status.GetMessage().empty());
    DeleteOption delOp = { false, false };
    auto delResponse = errorClient.Delete("llt/sn/error/delete", delOp).Get();
    EXPECT_TRUE(delResponse->status.IsError());
    EXPECT_FALSE(delResponse->status.GetMessage().empty());
    GetOption getOp = { false, false, false, 0, SortOrder::DESCEND, SortTarget::MODIFY };
    auto getResponse = errorClient.Get("llt/sn/error/get", getOp).Get();
    EXPECT_TRUE(getResponse->status.IsError());
    EXPECT_FALSE(getResponse->status.GetMessage().empty());
}

TEST_F(MetaStoreClientTest, PutKeyValue)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_ , .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test" }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    PutOption op = { .leaseId = 0, .prevKv = false };
    auto response = client.Put("llt/sn/workers/xxx", "2.0", op).Get();
    EXPECT_TRUE(response->prevKv.key().empty());
    EXPECT_TRUE(response->prevKv.value().empty());

    op.prevKv = true;  // return prev key-value
    response = client.Put("llt/sn/workers/xxx", "3.0", op).Get();
    EXPECT_EQ(TrimKeyPrefix(response->prevKv.key(), "/test"), "llt/sn/workers/xxx");
    EXPECT_EQ(response->prevKv.value(), "2.0");
}

TEST_F(MetaStoreClientTest, DeleteKeyValue)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    DeleteOption op = { false, false };
    auto response = client.Delete("llt/sn/workers/xxx", op).Get();

    EXPECT_EQ(response->deleted, 1);
    EXPECT_EQ(response->prevKvs.size(), static_cast<uint32_t>(0));
}

TEST_F(MetaStoreClientTest, DeleteKeyValuePrevKv)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    DeleteOption op = { .prevKv = true, .prefix = false };
    auto response = client.Delete("llt/sn/workers/xxx", op).Get();

    EXPECT_EQ(response->deleted, 1);
    EXPECT_EQ(response->prevKvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(TrimKeyPrefix(response->prevKvs[0].key(), "/test"), "llt/sn/workers/xxx");
    EXPECT_EQ(response->prevKvs[0].value(), "1.0");
}

TEST_F(MetaStoreClientTest, DeleteKeyValuePrefix)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    DeleteOption op = { false, true };
    auto response = client.Delete("llt/sn/workers/", op).Get();

    EXPECT_EQ(response->deleted, 3);
    EXPECT_TRUE(response->prevKvs.empty());
}

TEST_F(MetaStoreClientTest, DeleteKeyValuePrevPrefix)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    DeleteOption op = { true, true };
    auto response = client.Delete("llt/sn/workers/", op).Get();

    EXPECT_EQ(response->deleted, 3);
    EXPECT_EQ(response->prevKvs.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(TrimKeyPrefix(response->prevKvs[0].key(), "/test"), "llt/sn/workers/xxx");
    EXPECT_EQ(response->prevKvs[0].value(), "1.0");
}

TEST_F(MetaStoreClientTest, GetKeyValue)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    GetOption op = { false, false, false, 0, SortOrder::DESCEND, SortTarget::MODIFY };
    auto response = client.Get("llt/sn/workers/xxx", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(TrimKeyPrefix(response->kvs[0].key(), "/test"), "llt/sn/workers/xxx");
    EXPECT_EQ(response->kvs[0].value(), "1.0");

    op.prefix = false, op.keysOnly = false, op.countOnly = true;
    response = client.Get("llt/sn/workers/xxx", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(0));
    EXPECT_EQ(response->count, 1);

    op.prefix = true, op.keysOnly = false, op.countOnly = true;
    response = client.Get("llt/sn/workers/", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(0));
    EXPECT_EQ(response->count, 3);

    op.prefix = true, op.keysOnly = false, op.countOnly = false;
    response = client.Get("llt/sn/workers/", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(TrimKeyPrefix(response->kvs[0].key(), "/test"), "llt/sn/workers/zzz");  // DESCEND by MODIFY
    EXPECT_EQ(response->kvs[0].value(), "1.0");
    EXPECT_EQ(response->count, 3);

    op.prefix = true, op.keysOnly = true, op.countOnly = false, op.sortTarget = SortTarget::KEY;
    response = client.Get("llt/sn/workers/", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(TrimKeyPrefix(response->kvs[0].key(), "/test"), "llt/sn/workers/zzz");  // DESCEND by MODIFY
    EXPECT_TRUE(response->kvs[0].value().empty());
}

TEST_F(MetaStoreClientTest, TransactionTxn)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    auto transaction = client.BeginTransaction();

    transaction->If(TxnCompare::OfValue("llt/sn/workers/xxx", CompareOperator::EQUAL, "1.0"));

    DeleteOption delOption = { true, false };
    transaction->Then(TxnOperation::Create("llt/sn/workers/xxx", delOption));

    PutOption putOption = { .leaseId = 0, .prevKv = true };
    transaction->Then(TxnOperation::Create("llt/sn/workers/yyy", "2.0", putOption));

    GetOption getOption = { true, false, false, 0, SortOrder::DESCEND, SortTarget::KEY };
    transaction->Then(TxnOperation::Create("llt/sn/workers/", getOption));

    transaction->Else(TxnOperation::Create("llt/sn/workers/zzz", "2.0", putOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/yyy", delOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/", getOption));

    std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();

    EXPECT_TRUE(txnResponse->success);
    EXPECT_EQ(txnResponse->responses.size(), static_cast<uint32_t>(3));

    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[0].response).prevKvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(TrimKeyPrefix(std::get<DeleteResponse>(txnResponse->responses[0].response).prevKvs[0].key(), "/test"), "llt/sn/workers/xxx");

    EXPECT_EQ(TrimKeyPrefix(std::get<PutResponse>(txnResponse->responses[1].response).prevKv.key(), "/test"), "llt/sn/workers/yyy");
    EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[1].response).prevKv.value(), "1.0");

    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs.size(), static_cast<uint32_t>(3)); // Txn中的查询始终查的缓存 cache_
    EXPECT_EQ(TrimKeyPrefix(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].key(), "/test"), "llt/sn/workers/zzz");
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].value(), "1.0");
}

TEST_F(MetaStoreClientTest, TransactionTxnTest)
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    auto transaction = client.BeginTransaction();
    transaction->If(TxnCompare::OfVersion("llt/sn/workers/xxx", CompareOperator::EQUAL, 1));
    transaction->If(TxnCompare::OfCreateVersion("llt/sn/workers/xxx", CompareOperator::GREATER, 1));
    transaction->If(TxnCompare::OfModifyVersion("llt/sn/workers/xxx", CompareOperator::LESS, 100));
    transaction->If(TxnCompare::OfLease("llt/sn/workers/xxx", CompareOperator::EQUAL, 1));
    std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();
    EXPECT_FALSE(txnResponse->success);
}

TEST_F(MetaStoreClientTest, TransactionTxnElse)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    auto transaction = client.BeginTransaction();

    transaction->If(TxnCompare::OfValue("llt/sn/workers/xxx", CompareOperator::EQUAL, "2.0"));

    DeleteOption delOption = { true, false };
    transaction->Then(TxnOperation::Create("llt/sn/workers/xxx", delOption));

    PutOption putOption = { .leaseId = 0, .prevKv = true };
    transaction->Then(TxnOperation::Create("llt/sn/workers/yyy", "2.0", putOption));

    GetOption getOption = { true, false, false, 0, SortOrder::DESCEND, SortTarget::KEY };
    transaction->Then(TxnOperation::Create("llt/sn/workers/", getOption));

    transaction->Else(TxnOperation::Create("llt/sn/workers/zzz", "2.0", putOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/yyy", delOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/", getOption));

    std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();

    EXPECT_FALSE(txnResponse->success);
    EXPECT_EQ(txnResponse->responses.size(), static_cast<uint32_t>(3));

    EXPECT_EQ(TrimKeyPrefix(std::get<PutResponse>(txnResponse->responses[0].response).prevKv.key(), "/test"), "llt/sn/workers/zzz");
    EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[0].response).prevKv.value(), "1.0");

    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[1].response).prevKvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(TrimKeyPrefix(std::get<DeleteResponse>(txnResponse->responses[1].response).prevKvs[0].key(), "/test"), "llt/sn/workers/yyy");
    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[1].response).prevKvs[0].value(), "1.0");

    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs.size(), static_cast<uint32_t>(3)); // Txn中的查询始终查的缓存 cache_
    EXPECT_EQ(TrimKeyPrefix(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].key(), "/test"), "llt/sn/workers/zzz");
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].value(), "1.0"); // Txn中的查询始终查的缓存 cache_
}

TEST_F(MetaStoreClientTest, GrantLease)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    bool put = false, deleted = false;
    auto observer = [&](const std::vector<WatchEvent> &events, bool) -> bool {
        for (const WatchEvent &event : events) {
            switch (event.eventType) {
                case EVENT_TYPE_PUT: {
                    put = true;
                    break;
                }
                case EVENT_TYPE_DELETE: {
                    deleted = true;
                    break;
                }
                default: {
                    break;
                }
            }
        }
        return true;
    };
    WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
    auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    auto watcher = client.Watch("llt/sn/workers", option, observer, syncer).Get();  // await
    ASSERT_AWAIT_TRUE([&watcher]() -> bool { return watcher->GetWatchId() == 0; });

    LeaseGrantResponse leaseGrantResponse = client.Grant(3).Get();  // await
    EXPECT_NE(leaseGrantResponse.leaseId, 0);

    PutOption putOption = { .leaseId = leaseGrantResponse.leaseId, .prevKv = false };
    auto putResponse = client.Put("llt/sn/workers/vvv", "1.0", putOption).Get();  // await
    EXPECT_EQ(putResponse->status, Status::OK());
    ASSERT_AWAIT_TRUE([&put]() -> bool { return put; });

    auto start = std::chrono::steady_clock::now();
    ASSERT_AWAIT_TRUE([&]() -> bool {
        if (!deleted) {
            return false;
        }

        // key-value will be deleted automatically after 3s.
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() >= 2500;
    });
}

TEST_F(MetaStoreClientTest, RevokeLease)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_,
                                            .metaStoreAddress = "",
                                            .enableMetaStore = false,
                                            .etcdTablePrefix = "/test" },
                           sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    bool put = false, deleted = false;
    auto observer = [&](const std::vector<WatchEvent> &events, bool) -> bool {
        for (const WatchEvent &event : events) {
            switch (event.eventType) {
                case EVENT_TYPE_PUT: {
                    put = true;
                    break;
                }
                case EVENT_TYPE_DELETE: {
                    deleted = true;
                    break;
                }
                default: {
                    break;
                }
            }
        }
        return true;
    };
    WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
    auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    auto watcher = client.Watch("llt/sn/workers", option, observer, syncer).Get();  // await
    ASSERT_AWAIT_TRUE([&watcher]() -> bool { return watcher->GetWatchId() == 0; });

    LeaseGrantResponse leaseGrantResponse = client.Grant(30).Get();  // await
    EXPECT_NE(leaseGrantResponse.leaseId, 0);

    PutOption putOption = { .leaseId = leaseGrantResponse.leaseId, .prevKv = false };
    auto putResponse = client.Put("llt/sn/workers/vvv", "1.0", putOption).Get();  // await
    EXPECT_EQ(putResponse->status, Status::OK());
    ASSERT_AWAIT_TRUE([&put]() -> bool { return put; });

    LeaseRevokeResponse revokeResponse = client.Revoke(leaseGrantResponse.leaseId).Get();
    EXPECT_EQ(revokeResponse.status, Status::OK());

    auto start = std::chrono::steady_clock::now();
    ASSERT_AWAIT_TRUE([&]() -> bool {
        if (!deleted) {
            return false;
        }

        // Call [Invoke], key-value deleted within 1 second.
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() < 500;
    });
}

TEST_F(MetaStoreClientTest, KeepAliveLease)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_,
                                            .metaStoreAddress = "",
                                            .enableMetaStore = false,
                                            .etcdTablePrefix = "/test" },
                           sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    bool put = false, deleted = false;
    auto observer = [&](const std::vector<WatchEvent> &events, bool) -> bool {
        for (const WatchEvent &event : events) {
            switch (event.eventType) {
                case EVENT_TYPE_PUT: {
                    put = true;
                    break;
                }
                case EVENT_TYPE_DELETE: {
                    deleted = true;
                    break;
                }
                default: {
                    break;
                }
            }
        }
        return true;
    };
    WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
    auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    auto watcher = client.Watch("llt/sn/workers", option, observer, syncer).Get();  // await
    ASSERT_AWAIT_TRUE([&watcher]() -> bool { return watcher->GetWatchId() == 0; });

    LeaseGrantResponse leaseGrantResponse = client.Grant(1).Get();  // await
    int64_t leaseId = leaseGrantResponse.leaseId;
    EXPECT_NE(leaseId, 0);
    PutOption putOption = { .leaseId = leaseId, .prevKv = false };
    auto putResponse = client.Put("llt/sn/workers/vvv", "1.0", putOption).Get();  // await
    EXPECT_EQ(putResponse->status, Status::OK());
    ASSERT_AWAIT_TRUE([&put]() -> bool { return put; });

    EXPECT_EQ(client.KeepAliveOnce(leaseId).Get().status, Status::OK());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_FALSE(deleted);
    ASSERT_AWAIT_TRUE([&deleted]() -> bool { return deleted; });

    auto helper = GrpcClientHelper(10);
    MetaStoreClient invalid_client(MetaStoreConfig{
        .etcdAddress = "127.0.0.1:123",
        .metaStoreAddress = "",
        .enableMetaStore= false,
        .etcdTablePrefix = "/test"
    });
    invalid_client.Init();
    EXPECT_EQ(invalid_client.KeepAliveOnce(123).Get().status.StatusCode(), StatusCode::FAILED);
}

TEST_F(MetaStoreClientTest, CreateWatcher)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    int count = 0;
    auto observer =
        [&count](const std::vector<WatchEvent> &events, bool) -> bool {
        for (const WatchEvent &event : events) {
            switch (event.eventType) {
                case EVENT_TYPE_PUT:
                    EXPECT_EQ(TrimKeyPrefix(event.kv.key(), "/test"), "llt/sn/workers/xxx");
                    EXPECT_EQ(event.kv.value(), "2.0");

                    EXPECT_EQ(TrimKeyPrefix(event.prevKv.key(), "/test"), "llt/sn/workers/xxx");
                    EXPECT_EQ(event.prevKv.value(), "1.0");
                    count++;
                    break;
                case EVENT_TYPE_DELETE:
                    EXPECT_EQ(TrimKeyPrefix(event.kv.key(), "/test"), "llt/sn/workers/yyy");
                    EXPECT_EQ(event.kv.value(), "");

                    EXPECT_EQ(TrimKeyPrefix(event.prevKv.key(), "/test"), "llt/sn/workers/yyy");
                    EXPECT_EQ(event.prevKv.value(), "1.0");
                    count++;
                    break;
            }
        }
        return true;
    };

    auto revision = client.Get("llt/sn/workers", { .prefix = true }).Get()->header.revision;

    WatchOption option = { .prefix = true, .prevKv = true, .revision = revision + 1 };
    auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    auto watcher = client.Watch("llt/sn/workers", option, observer, syncer).Get();
    // receive put and delete event
    ASSERT_AWAIT_TRUE([&watcher]() -> bool { return watcher->GetWatchId() == 0; });

    WatchOption option_ = { .prefix = false, .prevKv = true, .revision = revision + 1 };
    auto watcher_ = client.Watch("llt/sn/workers/xxx", option_, observer, syncer).Get();
    // only receive put event
    ASSERT_AWAIT_TRUE([&watcher_]() -> bool { return watcher_->GetWatchId() == 1; });

    PutOption putOption = { .leaseId = 0, .prevKv = false };  // for 0 and 1 watcher
    client.Put("llt/sn/workers/xxx", "2.0", putOption).Get();

    DeleteOption deleteOption = { .prevKv = false, .prefix = false };  // only for 0 watcher
    client.Delete("llt/sn/workers/yyy", deleteOption).Get();

    ASSERT_AWAIT_TRUE([&count]() -> bool { return count == 3; });  // put->put->delete
}

TEST_F(MetaStoreClientTest, WatchCanceledByServerTest)  // NOLINT
{
    MetaStoreClient client(
        MetaStoreConfig{
            .etcdAddress = watchHost_, .metaStoreAddress = "", .enableMetaStore = false, .etcdTablePrefix = "/test" },
        sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    auto watchActor = std::make_shared<MockEtcdWatchActor>();
    litebus::Spawn(watchActor);
    watchService_->BindActor(watchActor);

    litebus::Future<::etcdserverpb::WatchCreateRequest> createRequest1;
    EXPECT_CALL(*watchActor, Create).WillOnce(testing::DoAll(FutureArg<0>(&createRequest1), testing::Return()));

    WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
    auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    auto watcher =
        client
            .Watch(
                "/test", option, [](const std::vector<WatchEvent> &events, bool) -> bool { return true; }, syncer)
            .Get();

    ASSERT_AWAIT_READY(createRequest1);
    EXPECT_EQ(TrimKeyPrefix(createRequest1.Get().key(), "/test"), "/test");
    ::etcdserverpb::WatchResponse response;
    response.set_watch_id(0);
    response.set_created(true);
    response.mutable_header()->set_revision(100);
    litebus::Async(watchActor->GetAID(), &MockEtcdWatchActor::Response, response);

    response.set_created(false);
    litebus::Async(watchActor->GetAID(), &MockEtcdWatchActor::Response, response);

    litebus::Future<::etcdserverpb::WatchCreateRequest> createRequest2;
    EXPECT_CALL(*watchActor, Create).WillOnce(testing::DoAll(FutureArg<0>(&createRequest2), testing::Return()));

    response.set_cancel_reason("by server");
    response.set_canceled(true);
    response.set_compact_revision(99);
    litebus::Async(watchActor->GetAID(), &MockEtcdWatchActor::Response, response);
    ASSERT_AWAIT_READY(createRequest2);
    EXPECT_EQ(TrimKeyPrefix(createRequest2.Get().key(), "/test"), "/test");
    EXPECT_EQ(createRequest2.Get().start_revision(), 101);

    watchService_->ShutdownWatch();
    litebus::Terminate(watchActor->GetAID());
    litebus::Await(watchActor->GetAID());
}

TEST_F(MetaStoreClientTest, CloseWatcher)  // NOLINT
{
    const std::string key =
        "/sn/instance/business/yrk/tenant/12345678901234561234567890123456/function"
        "/0-yrcpp-yr-refcount/version/$latest/defaultaz/cf8e2758-dab0-4775-adff-a746df288052";
    MetaStoreClient watchClient(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_ , .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test" }, sslConfig, metaStoreTimeoutOpt_);
    watchClient.Init();
    WatchEvent event;
    WatchOption watchOption = { .prefix = true, .prevKv = false, .revision = 0 };
    auto observer = [&event](const std::vector<WatchEvent> &events, bool) -> bool {
        event = events.front();
        return true;
    };
    auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    auto watcher = watchClient.Watch("/sn/instance/business/yrk/tenant/", watchOption, observer, syncer).Get();
    ASSERT_AWAIT_TRUE([&watcher]() -> bool { return watcher->GetWatchId() == 0; });

    MetaStoreClient handleClient(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_ , .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test" }, sslConfig, metaStoreTimeoutOpt_);
    handleClient.Init();
    PutOption putOption = { .leaseId = 0, .prevKv = false };
    handleClient.Put(key, "1.0", putOption).Get();

    ASSERT_AWAIT_TRUE([&]() -> bool { return TrimKeyPrefix(event.kv.key(), "/test") == key; });

    watcher->Close();
    watcher->Close();
    watcher->Close();

    DeleteOption deleteOption = { .prevKv = false, .prefix = false };  // only for 0 watcher
    handleClient.Delete(key, deleteOption).Get();
}

TEST_F(MetaStoreClientTest, ReconnectKeepAliveLease)
{
    EtcdLeaseClientStrategy leaseClient("test", "127.0.0.1", sslConfig, metaStoreTimeoutOpt_);
    EXPECT_TRUE(leaseClient.ReconnectKeepAliveLease());
}

TEST_F(MetaStoreClientTest, DoGrant)
{
    auto leaseClient = std::make_shared<EtcdLeaseClientStrategy>("test", "127.0.0.1", sslConfig, metaStoreTimeoutOpt_);
    litebus::Spawn(leaseClient);

    auto promise = std::make_shared<litebus::Promise<LeaseGrantResponse>>();
    etcdserverpb::LeaseGrantRequest request;
    int retryTimes = KV_OPERATE_RETRY_TIMES - 1;
    leaseClient->DoGrant(promise, request, retryTimes);
    EXPECT_FALSE(promise->GetFuture().IsOK());
    EXPECT_THAT(promise->GetFuture().Get().status.GetMessage(), testing::HasSubstr("grant failed"));

    leaseClient->OnHealthyStatus(Status(StatusCode::FAILED));
    promise = std::make_shared<litebus::Promise<LeaseGrantResponse>>();
    leaseClient->DoGrant(promise, request, retryTimes);
    ASSERT_AWAIT_READY(promise->GetFuture());
    EXPECT_FALSE(promise->GetFuture().Get().status.IsOk());

    litebus::Terminate(leaseClient->GetAID());
    litebus::Await(leaseClient->GetAID());
}

TEST_F(MetaStoreClientTest, DoRevoke)
{
    auto leaseClient = std::make_shared<EtcdLeaseClientStrategy>("test", "127.0.0.1", sslConfig, metaStoreTimeoutOpt_);
    litebus::Spawn(leaseClient);

    auto promise = std::make_shared<litebus::Promise<LeaseRevokeResponse>>();
    etcdserverpb::LeaseRevokeRequest request;
    int retryTimes = KV_OPERATE_RETRY_TIMES - 1;
    leaseClient->DoRevoke(promise, request, retryTimes);

    litebus::Terminate(leaseClient->GetAID());
    litebus::Await(leaseClient->GetAID());
}

TEST_F(MetaStoreClientTest, CampaignTest)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = electionHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    StartElectionGrantLease();

    EXPECT_CALL(*electionService_, Campaign)
        .WillOnce(Return(GrpcStatus(GrpcCode::UNAVAILABLE)))
        .WillOnce(Return(GrpcStatus(GrpcCode::UNKNOWN)));

    auto campaignRsp = client.Campaign("llt/sn/worker", 123, "fake_value");
    ASSERT_AWAIT_READY(campaignRsp);
    EXPECT_TRUE(campaignRsp.Get().status.IsError());  // etcdserver: requested lease not found
    EXPECT_EQ(campaignRsp.Get().status.StatusCode(), StatusCode::GRPC_UNKNOWN);

    LeaderKey leader;
    int campaignCount = 0;
    auto onCampaign = [&](const CampaignResponse &campaignResponse) {
        EXPECT_TRUE(campaignResponse.status.IsOk());
        leader = campaignResponse.leader;
        campaignCount++;
        return campaignResponse;
    };

    bool isResigned = false;
    auto waitForResign = [&]() {
        // wait until the first leader is resigned
        ASSERT_AWAIT_TRUE([&]() -> bool { return isResigned; });
    };

    ::v3electionpb::CampaignResponse campaignResponse1;
    EXPECT_CALL(*electionService_, Campaign)
        .WillOnce(DoAll(SetArgPointee<2>(campaignResponse1), Return(::grpc::Status::OK)))
        // wait until the first leader is resigned
        .WillOnce(
            DoAll(InvokeWithoutArgs(waitForResign), SetArgPointee<2>(campaignResponse1), Return(::grpc::Status::OK)));

    // two campaign for one leader
    // set lease time 20s, longer than ASSERT_AWAIT_TRUE timeout, so the lease won't expire
    (void)client.Campaign("llt/sn/worker", client.Grant(int(20)).Get().leaseId, "fake_value").Then(onCampaign);
    (void)client.Campaign("llt/sn/worker", client.Grant(int(20)).Get().leaseId, "fake_value2").Then(onCampaign);

    ASSERT_AWAIT_TRUE([&]() -> bool { return campaignCount == 1; });

    ::v3electionpb::ResignResponse resignResponse1;
    EXPECT_CALL(*electionService_, Resign)
        .WillOnce(DoAll(SetArgPointee<2>(resignResponse1), Return(::grpc::Status::OK)));

    auto resignRsp = client.Resign(leader);
    ASSERT_AWAIT_READY(resignRsp);
    EXPECT_TRUE(resignRsp.Get().status.IsOk());
    isResigned = true;

    EXPECT_CALL(*electionService_, Resign)
        .WillOnce(DoAll(SetArgPointee<2>(resignResponse1), Return(::grpc::Status::OK)));

    // after first leader resign, the other will get leader
    ASSERT_AWAIT_TRUE([&]() -> bool { return campaignCount == 2; });
    resignRsp = client.Resign(leader);
    ASSERT_AWAIT_READY(resignRsp);
    EXPECT_TRUE(resignRsp.Get().status.IsOk());
}

TEST_F(MetaStoreClientTest, LeaderTest)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = electionHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    StartElectionGrantLease();

    EXPECT_CALL(*electionService_, Leader)
        .WillOnce(Return(GrpcStatus(GrpcCode::UNAVAILABLE)))
        .WillOnce(Return(GrpcStatus(GrpcCode::UNKNOWN)));

    auto leaderRsp = client.Leader("llt/sn/worker");
    ASSERT_AWAIT_READY(leaderRsp);
    EXPECT_TRUE(leaderRsp.Get().status.IsError());  // election: no leader
    EXPECT_EQ(leaderRsp.Get().status.StatusCode(), StatusCode::GRPC_UNKNOWN);

    ::v3electionpb::CampaignResponse campaignResponse1;
    EXPECT_CALL(*electionService_, Campaign)
        .WillOnce(DoAll(SetArgPointee<2>(campaignResponse1), Return(::grpc::Status::OK)));

    auto campaignRsp = client.Campaign("llt/sn/worker", client.Grant(int(20)).Get().leaseId, "fake_value");
    ASSERT_AWAIT_READY(campaignRsp);
    EXPECT_TRUE(campaignRsp.Get().status.IsOk());

    ::v3electionpb::LeaderResponse leaderResponse1;
    leaderResponse1.mutable_kv()->set_key("key");
    leaderResponse1.mutable_kv()->set_value("value");
    EXPECT_CALL(*electionService_, Leader)
        .WillOnce(DoAll(SetArgPointee<2>(leaderResponse1), Return(::grpc::Status::OK)));

    leaderRsp = client.Leader("llt/sn/worker");
    ASSERT_AWAIT_READY(leaderRsp);
    EXPECT_TRUE(leaderRsp.Get().status.IsOk());
    EXPECT_EQ(leaderRsp.Get().kv.first, "key");
    EXPECT_EQ(leaderRsp.Get().kv.second, "value");
}

TEST_F(MetaStoreClientTest, ResignTest)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = electionHost_, .metaStoreAddress = "", .enableMetaStore= false, .etcdTablePrefix = "/test"  }, sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    StartElectionGrantLease();

    ::v3electionpb::ResignResponse resignResponse1;
    EXPECT_CALL(*electionService_, Resign)
        .WillOnce(Return(GrpcStatus(GrpcCode::UNAVAILABLE)))
        .WillOnce(Return(GrpcStatus(GrpcCode::UNAVAILABLE)));
    LeaderKey fakeLeaderKey = { .name = "fake_key", .key = "fake_key", .rev = 123, .lease = 123 };
    auto resignRsp = client.Resign(fakeLeaderKey);
    ASSERT_AWAIT_READY(resignRsp);
    EXPECT_TRUE(resignRsp.Get().status.IsError());  // retry timeout
    EXPECT_EQ(resignRsp.Get().status.StatusCode(), StatusCode::GRPC_UNAVAILABLE);

    EXPECT_CALL(*electionService_, Resign)
        .WillOnce(DoAll(SetArgPointee<2>(resignResponse1), Return(::grpc::Status::OK)));
    resignRsp = client.Resign(fakeLeaderKey);
    ASSERT_AWAIT_READY(resignRsp);
    EXPECT_TRUE(resignRsp.Get().status.IsOk());  // etcd will return success, when resign invalid leader

    ::v3electionpb::CampaignResponse campaignResponse1;
    EXPECT_CALL(*electionService_, Campaign)
        .WillOnce(DoAll(SetArgPointee<2>(campaignResponse1), Return(::grpc::Status::OK)));
    auto campaignRsp = client.Campaign("llt/sn/worker", client.Grant(int(20)).Get().leaseId, "fake_value");
    ASSERT_AWAIT_READY(campaignRsp);
    EXPECT_TRUE(campaignRsp.Get().status.IsOk());

    resignResponse1.mutable_header()->set_revision(123);
    EXPECT_CALL(*electionService_, Resign)
        .WillOnce(DoAll(SetArgPointee<2>(resignResponse1), Return(::grpc::Status::OK)));
    resignRsp = client.Resign(campaignRsp.Get().leader);
    ASSERT_AWAIT_READY(resignRsp);
    EXPECT_TRUE(resignRsp.Get().status.IsOk());
    EXPECT_NE(resignRsp.Get().header.revision, 0);
}

TEST_F(MetaStoreClientTest, ObserveTest)  // NOLINT
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = electionHost_,
                                            .metaStoreAddress = "",
                                            .enableMetaStore = false,
                                            .etcdTablePrefix = "/test" },
                           sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    StartElectionGrantLease();

    ::v3electionpb::CampaignResponse campaignResponse1;
    EXPECT_CALL(*electionService_, Campaign)
        .WillOnce(DoAll(SetArgPointee<2>(campaignResponse1), Return(::grpc::Status::OK)));

    // campaign before observe
    auto campaignRsp = client.Campaign("llt/sn/worker", client.Grant(int(20)).Get().leaseId, "value1");
    ASSERT_AWAIT_READY(campaignRsp);
    EXPECT_TRUE(campaignRsp.Get().status.IsOk());

    int count = 0;
    auto observer = client.Observe("llt/sn/worker", [&](const LeaderResponse &leaderRsp) {
        YRLOG_DEBUG("receive observe event, key: {}, value: {}", leaderRsp.kv.first, leaderRsp.kv.second);
        count++;
        EXPECT_EQ(leaderRsp.kv.first, "key" + std::to_string(count));
        EXPECT_EQ(leaderRsp.kv.second, "value" + std::to_string(count));
    });
    ASSERT_AWAIT_READY(observer);

    ::v3electionpb::LeaderResponse leaderResponse1;
    leaderResponse1.mutable_kv()->set_key("key1");
    leaderResponse1.mutable_kv()->set_value("value1");
    electionService_->ObserveEvent(leaderResponse1);

    ASSERT_AWAIT_TRUE([&]() -> bool { return count == 1; });

    ::v3electionpb::ResignResponse resignResponse1;
    EXPECT_CALL(*electionService_, Resign)
        .WillOnce(DoAll(SetArgPointee<2>(resignResponse1), Return(::grpc::Status::OK)));

    auto resignRsp = client.Resign(campaignRsp.Get().leader);
    ASSERT_AWAIT_READY(resignRsp);
    EXPECT_TRUE(resignRsp.Get().status.IsOk());

    EXPECT_CALL(*electionService_, Campaign)
        .WillOnce(DoAll(SetArgPointee<2>(campaignResponse1), Return(::grpc::Status::OK)));

    campaignRsp = client.Campaign("llt/sn/worker", client.Grant(int(20)).Get().leaseId, "value2");
    ASSERT_AWAIT_READY(campaignRsp);
    EXPECT_TRUE(campaignRsp.Get().status.IsOk());

    ::v3electionpb::LeaderResponse leaderResponse2;
    leaderResponse2.mutable_kv()->set_key("key2");
    leaderResponse2.mutable_kv()->set_value("value2");
    electionService_->ObserveEvent(leaderResponse2);

    ASSERT_AWAIT_TRUE([&]() -> bool { return count == 2; });
    electionService_->ShutdownObserver();
    observer.Get()->Shutdown();
}

TEST_F(MetaStoreClientTest, FallbreakTest)
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_,
                                            .metaStoreAddress = "",
                                            .enableMetaStore = false,
                                            .etcdTablePrefix = "/test" },
                           sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    client.OnHealthyStatus(Status(GRPC_UNKNOWN, "healthy checkfailed"));

    auto putRsp = client.Put("", "", PutOption());
    EXPECT_TRUE(putRsp.Get()->status.IsError());

    auto deleteRsp = client.Delete("", DeleteOption());
    EXPECT_TRUE(deleteRsp.Get()->status.IsError());

    auto getRsp = client.Get("", GetOption());
    EXPECT_TRUE(getRsp.Get()->status.IsError());

    auto txnRsp = client.BeginTransaction()->Commit();
    EXPECT_TRUE(txnRsp.Get()->status.IsError());

    auto grantRsp = client.Grant(20);
    EXPECT_TRUE(grantRsp.Get().status.IsError());

    auto revokeRsp = client.Revoke(1);
    EXPECT_TRUE(revokeRsp.Get().status.IsError());

    auto campaignRsp = client.Campaign("", 1, "");
    EXPECT_TRUE(campaignRsp.Get().status.IsError());

    auto leaderRsp = client.Leader("");
    EXPECT_TRUE(leaderRsp.Get().status.IsError());

    auto resignRsp = client.Resign(LeaderKey());
    EXPECT_TRUE(resignRsp.Get().status.IsError());

    client.OnHealthyStatus(Status::OK());
}

TEST_F(MetaStoreClientTest, IsConnectedTest)
{
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_,
                                            .metaStoreAddress = "",
                                            .enableMetaStore = false,
                                            .etcdTablePrefix = "/test" },
                           sslConfig, metaStoreTimeoutOpt_);
    client.Init();
    auto isConnected = client.IsConnected();
    ASSERT_AWAIT_READY(isConnected);
    EXPECT_TRUE(isConnected.Get());
}

TEST_F(MetaStoreClientTest, MetaStoreElectionTest)
{
    auto metaStoreElectionClient = std::make_shared<meta_store::MetaStoreElectionClientStrategy>(
        "MetaStoreElectionClientStrategy", "127.0.0.1", metaStoreTimeoutOpt_, "");
    litebus::Spawn(metaStoreElectionClient);

    litebus::Async(metaStoreElectionClient->GetAID(), &meta_store::MetaStoreElectionClientStrategy::Campaign, "", 0,
                   "");
    litebus::Async(metaStoreElectionClient->GetAID(), &meta_store::MetaStoreElectionClientStrategy::Leader, "");
    LeaderKey key;
    litebus::Async(metaStoreElectionClient->GetAID(), &meta_store::MetaStoreElectionClientStrategy::Resign, key);
    litebus::Async(metaStoreElectionClient->GetAID(), &meta_store::MetaStoreElectionClientStrategy::Observe, "",
                   [](const LeaderResponse &) {});

    litebus::Terminate(metaStoreElectionClient->GetAID());
    litebus::Await(metaStoreElectionClient->GetAID());
}
}  // namespace functionsystem::meta_store::test
