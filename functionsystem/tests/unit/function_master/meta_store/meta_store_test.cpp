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

#include "async/async.hpp"
#include "common/proto/pb/message_pb.h"
#include "kv_service_accessor_actor.h"
#include "kv_service_actor.h"
#include "lease_service_actor.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/meta_store_client.h"
#include "meta_store_driver.h"
#include "meta_store_monitor/meta_store_monitor_factory.h"
#include "mock_store_client.h"
#include "mocks/mock_etcd_kv_service.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"
#include "watch_service_actor.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;
using namespace test;
using ::testing::Invoke;
using ::testing::Return;

class MetaStoreTest : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdKvService_ = std::make_shared<MockEtcdKvService>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        etcdAddress_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);

        litebus::Promise<bool> promise;
        std::thread thread = std::thread([&promise]() {
            ::grpc::ServerBuilder builder;
            builder.RegisterService(etcdKvService_.get());

            builder.AddListeningPort(etcdAddress_, grpc::InsecureServerCredentials());
            etcdServer_ = builder.BuildAndStart();  // start server

            promise.SetValue(true);  // init done

            etcdServer_->Wait();  // quit after shutdown
            etcdKvService_ = nullptr;
        });
        thread.detach();
        promise.GetFuture().Get();  // wait for init
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        localAddress_ = "127.0.0.1:" + std::to_string(port);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        if (etcdServer_ != nullptr) {
            etcdServer_->Shutdown(std::chrono::system_clock::now());
            etcdServer_ = nullptr;
        }
    }

    static void TimeLoop(int32_t timeSpan)
    {
        auto start = std::chrono::high_resolution_clock::now();
        while (true) {
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
            if (duration.count() >= timeSpan) {
                break;
            }
            std::this_thread::yield();
        }
    }

public:
    inline static std::string etcdAddress_;
    inline static std::shared_ptr<grpc::Server> etcdServer_ = nullptr;
    inline static std::shared_ptr<MockEtcdKvService> etcdKvService_ = nullptr;
    inline static std::string localAddress_;
};

TEST_F(MetaStoreTest, MetaStoreWithETCDPutTest)  // NOLINT
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backAID = litebus::Spawn(std::make_shared<BackupActor>("backupActor", persistAID));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backAID));
    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    const std::string value = "mock-value";
    const std::string key = "mock-key";

    {
        EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
            .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *req,
                                 ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
                EXPECT_TRUE(req->compare_size() == 0);
                EXPECT_TRUE(req->success_size() == 1);
                const auto &cmp = req->success(0);
                EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestPut);

                const auto &putRequest = cmp.request_put();
                EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + key);

                const auto &val = putRequest.value();
                ::mvccpb::KeyValue kv;
                EXPECT_TRUE(kv.ParseFromString(val));
                EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + kv.key());

                EXPECT_EQ(kv.key(), key);
                EXPECT_EQ(kv.value(), value);
                *response = ::etcdserverpb::TxnResponse{};
                response->mutable_header()->set_revision(1);
                return ::grpc::Status::OK;
            }));
        auto future = client.Put(key, value, PutOption{ .leaseId = 0, .prevKv = true, .asyncBackup = false });
        EXPECT_AWAIT_READY(future);
        EXPECT_TRUE(future.Get()->status.IsOk());
        // The first Put operation, no history data.
        EXPECT_TRUE(future.Get()->prevKv.key().empty());
    }

    {
        bool finished = false;
        EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
            .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                                 ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
                EXPECT_TRUE(request->success_size() == 1);
                EXPECT_TRUE(request->compare_size() == 0);

                const auto &cmp = request->success(0);
                EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestPut);
                const auto &putReq = cmp.request_put();
                EXPECT_TRUE(putReq.key() == std::string("/metastore/kv/") + key);

                const auto &val = putReq.value();
                ::mvccpb::KeyValue kv;
                EXPECT_TRUE(kv.ParseFromString(val));
                EXPECT_TRUE(putReq.key() == std::string("/metastore/kv/") + kv.key());

                EXPECT_TRUE(kv.value() == "mock-value-x");
                EXPECT_TRUE(kv.key() == key);
                *response = ::etcdserverpb::TxnResponse{};
                response->mutable_header()->set_revision(1);
                finished = true;
                return ::grpc::Status::OK;
            }));
        auto future = client.Put(key, "mock-value-x", PutOption{ .leaseId = 0, .prevKv = true, .asyncBackup = false });
        EXPECT_AWAIT_READY(future);
        EXPECT_TRUE(future.Get()->status.IsOk());
        // The non-first Put operation, the history data is { mock-key:mock-value }.
        EXPECT_EQ(future.Get()->prevKv.key(), key);
        EXPECT_AWAIT_TRUE([&]() { return finished; });
    }

    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);

    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);
    litebus::Terminate(backAID);
    litebus::Await(backAID);
    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, MetaStoreWithETCDDeleteTest)  // NOLINT
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>("BackupActor1", persistAID));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));
    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    const std::string key = "mock-key2";
    const std::string value = "mock-value";

    bool finished = false;
    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *resp) -> ::grpc::Status {
            EXPECT_TRUE(request->compare_size() == 0);
            EXPECT_TRUE(request->success_size() == 1);
            const auto &cmp = request->success(0);

            EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestPut);
            const auto &putRequest = cmp.request_put();
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + key);
            const auto &val = putRequest.value();

            ::mvccpb::KeyValue kv1;
            EXPECT_TRUE(kv1.ParseFromString(val));
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + kv1.key());
            EXPECT_TRUE(kv1.key() == key);
            EXPECT_TRUE(kv1.value() == value);
            *resp = ::etcdserverpb::TxnResponse{};
            resp->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }))
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
            EXPECT_TRUE(request->compare_size() == 0);
            EXPECT_TRUE(request->success_size() == 1);

            const auto &cmp = request->success(0);
            EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestDeleteRange);
            const auto &deleteRequest = cmp.request_delete_range();
            EXPECT_TRUE(deleteRequest.key() == std::string("/metastore/kv/") + key);
            EXPECT_TRUE(deleteRequest.range_end().empty());
            *response = ::etcdserverpb::TxnResponse{};
            response->mutable_header()->set_revision(1);
            finished = true;
            return ::grpc::Status::OK;
        }));

    auto fut = client.Put(key, value, PutOption{ .asyncBackup = false });
    EXPECT_AWAIT_READY(fut);

    auto future = client.Delete(key, DeleteOption{ .prevKv = true, .prefix = true, .asyncBackup = false });
    EXPECT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get()->status.IsOk());
    EXPECT_EQ(future.Get()->deleted, 1);
    EXPECT_EQ(future.Get()->prevKvs.at(0).key(), key);
    EXPECT_EQ(future.Get()->prevKvs.at(0).value(), value);

    EXPECT_AWAIT_TRUE([&]() { return finished; });

    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);
    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);

    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
    litebus::Terminate(backupAID);
    litebus::Await(backupAID);
}

TEST_F(MetaStoreTest, MetaStoreWithETCDTxnTest)  // NOLINT
{
    auto persist =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>("BackupActor3", persist));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));
    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});

    client.Init();

    const std::string key = "mock-key4";
    const std::string value = "mock-value";

    bool finished = false;
    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
            EXPECT_TRUE(request->compare_size() == 0);
            EXPECT_TRUE(request->success_size() == 1);
            const auto &cmp = request->success(0);
            EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestPut);
            const auto &putRequest = cmp.request_put();
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + key);
            const auto &val = putRequest.value();

            ::mvccpb::KeyValue kv;
            EXPECT_TRUE(kv.ParseFromString(val));
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + kv.key());
            EXPECT_TRUE(kv.value() == value);
            EXPECT_TRUE(kv.key() == key);
            *response = ::etcdserverpb::TxnResponse{};
            response->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }))
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
            EXPECT_TRUE(request->success_size() == 1);
            EXPECT_TRUE(request->compare_size() == 0);
            const auto &cmp = request->success(0);
            EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestDeleteRange);
            const auto &deleteRequest = cmp.request_delete_range();
            EXPECT_TRUE(deleteRequest.range_end().empty());
            EXPECT_TRUE(deleteRequest.key() == std::string("/metastore/kv/") + key);
            *response = ::etcdserverpb::TxnResponse{};
            response->mutable_header()->set_revision(1);
            finished = true;
            return ::grpc::Status::OK;
        }));

    auto fut = client.Put(key, value, PutOption{ .asyncBackup = false });
    EXPECT_AWAIT_READY(fut);

    auto transaction = client.BeginTransaction();

    transaction->If(TxnCompare::OfValue(key, CompareOperator::EQUAL, value));
    transaction->Then(TxnOperation::Create(key, DeleteOption{ true, false, false }));
    std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();

    EXPECT_TRUE(txnResponse->success);
    EXPECT_EQ(txnResponse->responses.size(), static_cast<long unsigned int>(1));
    EXPECT_AWAIT_TRUE([&]() { return finished; });

    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);
    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);
    litebus::Terminate(backupAID);
    litebus::Await(backupAID);

    litebus::Terminate(persist);
    litebus::Await(persist);
}

TEST_F(MetaStoreTest, MetaStoreWithETCDPutErrorTest)  // NOLINT
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_,
            MetaStoreTimeoutOption{
                .operationRetryIntervalLowerBound = 100,
                .operationRetryIntervalUpperBound = 200,
                .operationRetryTimes = 1,
                .grpcTimeout = GRPC_TIMEOUT_SECONDS}));
    auto backAID = litebus::Spawn(std::make_shared<BackupActor>("backupActor5", persistAID));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backAID));
    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },{}, MetaStoreTimeoutOption{});
    client.Init();

    const std::string value = "mock-value5";
    const std::string key = "mock-key5";

    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillRepeatedly(Return(::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "etcd is unavailable ...")));

    // failed to put key: <mock-key5, mock-value5>
    auto future = client.Put(key, value, PutOption{ .leaseId = 0, .prevKv = true, .asyncBackup = false });
    EXPECT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get()->status.IsError());

    // get key: <mock-key5, mock-value5>, expected: get nothing
    auto getFut = client.Get(key, GetOption{ .prefix = true });
    EXPECT_AWAIT_READY(getFut);
    EXPECT_TRUE(getFut.Get()->status.IsOk());
    EXPECT_EQ(getFut.Get()->count, 0);
    EXPECT_EQ(getFut.Get()->kvs.size(), 0);

    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);

    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);
    litebus::Terminate(backAID);
    litebus::Await(backAID);
    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, MetaStoreWithETCDDeleteErrorTest)  // NOLINT
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_,
            MetaStoreTimeoutOption{
                .operationRetryIntervalLowerBound = 100,
                .operationRetryIntervalUpperBound = 200,
                .operationRetryTimes = 1,
                .grpcTimeout = GRPC_TIMEOUT_SECONDS}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>("BackupActor6", persistAID));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));
    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },{}, MetaStoreTimeoutOption{});
    client.Init();

    const std::string key = "mock-key6";
    const std::string value = "mock-value6";

    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *resp) -> ::grpc::Status {
            EXPECT_TRUE(request->compare_size() == 0);
            EXPECT_TRUE(request->success_size() == 1);
            const auto &cmp = request->success(0);

            EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestPut);
            const auto &putRequest = cmp.request_put();
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + key);
            const auto &val = putRequest.value();

            ::mvccpb::KeyValue kv1;
            EXPECT_TRUE(kv1.ParseFromString(val));
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + kv1.key());
            EXPECT_TRUE(kv1.key() == key);
            EXPECT_TRUE(kv1.value() == value);
            *resp = ::etcdserverpb::TxnResponse{};
            resp->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }))
        .WillRepeatedly(Return(::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "etcd is unavailable ...")));

    // put key: <mock-key6, mock-value6>
    auto fut = client.Put(key, value, PutOption{ .asyncBackup = false });
    EXPECT_AWAIT_READY(fut);

    // failed to delete key: <mock-key6, mock-value6>
    auto future = client.Delete(key, DeleteOption{ .prevKv = true, .prefix = true, .asyncBackup = false });
    EXPECT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get()->status.IsError());
    EXPECT_EQ(future.Get()->deleted, 1);
    EXPECT_EQ(future.Get()->prevKvs.at(0).key(), key);
    EXPECT_EQ(future.Get()->prevKvs.at(0).value(), value);

    // get key: <mock-key6, mock-value6>
    auto getFut = client.Get(key, GetOption{ .prefix = true });
    EXPECT_AWAIT_READY(getFut);
    EXPECT_TRUE(getFut.Get()->status.IsOk());
    EXPECT_EQ(getFut.Get()->count, 1);
    EXPECT_EQ(getFut.Get()->kvs.at(0).key(), key);
    EXPECT_EQ(getFut.Get()->kvs.at(0).value(), value);

    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);
    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);

    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
    litebus::Terminate(backupAID);
    litebus::Await(backupAID);
}

TEST_F(MetaStoreTest, MetaStoreWithETCDTxnErrorTest)  // NOLINT
{
    auto persist =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_,
            MetaStoreTimeoutOption{
                .operationRetryIntervalLowerBound = 100,
                .operationRetryIntervalUpperBound = 200,
                .operationRetryTimes = 1,
                .grpcTimeout = GRPC_TIMEOUT_SECONDS}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>("BackupActor7", persist));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));
    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },{}, MetaStoreTimeoutOption{});
    client.Init();

    const std::string key = "mock-key7";
    const std::string value = "mock-value7";

    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
            EXPECT_TRUE(request->compare_size() == 0);
            EXPECT_TRUE(request->success_size() == 1);
            const auto &cmp = request->success(0);
            EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestPut);
            const auto &putRequest = cmp.request_put();
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + key);
            const auto &val = putRequest.value();

            ::mvccpb::KeyValue kv;
            EXPECT_TRUE(kv.ParseFromString(val));
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + kv.key());
            EXPECT_TRUE(kv.value() == value);
            EXPECT_TRUE(kv.key() == key);
            *response = ::etcdserverpb::TxnResponse{};
            response->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }))
        .WillRepeatedly(Return(::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "etcd is unavailable ...")));

    // put key: <mock-key7, mock-value7>
    auto fut = client.Put(key, value, PutOption{ .asyncBackup = false });
    EXPECT_AWAIT_READY(fut);

    // txn key: <mock-key7, mock-value7>
    auto transaction = client.BeginTransaction();
    transaction->If(TxnCompare::OfValue(key, CompareOperator::EQUAL, value));
    transaction->Then(TxnOperation::Create(key, DeleteOption{ true, false, false }));
    std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();
    EXPECT_TRUE(txnResponse->status.IsError());

    // get key: <mock-key7, mock-value7>
    auto getFut = client.Get(key, GetOption{ .prefix = true });
    EXPECT_AWAIT_READY(getFut);
    EXPECT_TRUE(getFut.Get()->status.IsOk());
    EXPECT_EQ(getFut.Get()->count, 1);
    EXPECT_EQ(getFut.Get()->kvs.at(0).key(), key);
    EXPECT_EQ(getFut.Get()->kvs.at(0).value(), value);

    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);
    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);
    litebus::Terminate(backupAID);
    litebus::Await(backupAID);

    litebus::Terminate(persist);
    litebus::Await(persist);
}

TEST_F(MetaStoreTest, MetaStoreWithETCDGetTest)  // NOLINT
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>("BackupActor4", persistAID));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));
    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));

    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    const std::string key = "mock-key1";
    const std::string value = "mock-value";

    bool finished = false;
    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
            EXPECT_TRUE(request->success_size() == 1);
            EXPECT_TRUE(request->compare_size() == 0);
            const auto &cmp = request->success(0);
            EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestPut);
            const auto &putRequest = cmp.request_put();
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + key);

            ::mvccpb::KeyValue kv;
            const auto &val = putRequest.value();
            EXPECT_TRUE(kv.ParseFromString(val));
            EXPECT_TRUE(putRequest.key() == std::string("/metastore/kv/") + kv.key());
            EXPECT_TRUE(kv.key() == key);
            EXPECT_TRUE(kv.value() == value);
            *response = ::etcdserverpb::TxnResponse{};
            response->mutable_header()->set_revision(1);
            finished = true;
            return ::grpc::Status::OK;
        }));

    auto fut = client.Put(key, value, PutOption{ .asyncBackup = false });
    EXPECT_AWAIT_READY(fut);

    auto future = client.Get(key, GetOption{ .prefix = true });
    EXPECT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get()->status.IsOk());
    EXPECT_EQ(future.Get()->count, 1);
    EXPECT_EQ(future.Get()->kvs.at(0).key(), key);
    EXPECT_EQ(future.Get()->kvs.at(0).value(), value);

    EXPECT_AWAIT_TRUE([&]() { return finished; });

    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);
    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);
    litebus::Terminate(backupAID);
    litebus::Await(backupAID);

    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, KvServiceActorTest)  // NOLINT
{
    auto kvActor = std::make_shared<meta_store::KvServiceActor>();
    etcdserverpb::PutRequest putRequest;
    putRequest.set_key("key");
    putRequest.set_value("1");
    etcdserverpb::PutResponse putResponse;
    kvActor->Put(&putRequest, &putResponse);
    // Range test
    etcdserverpb::RangeRequest request;
    ::etcdserverpb::RangeResponse response;
    request.set_sort_target(etcdserverpb::RangeRequest_SortTarget_VERSION);
    request.set_range_end("rangend");
    auto status = kvActor->Range(&request, &response);
    EXPECT_TRUE(status.ok());

    request.set_sort_target(etcdserverpb::RangeRequest_SortTarget_CREATE);
    status = kvActor->Range(&request, &response);
    EXPECT_TRUE(status.ok());

    request.set_sort_target(etcdserverpb::RangeRequest_SortTarget_RangeRequest_SortTarget_INT_MIN_SENTINEL_DO_NOT_USE_);
    status = kvActor->Range(&request, &response);
    EXPECT_TRUE(status.ok());

    request.set_sort_target(etcdserverpb::RangeRequest_SortTarget_VALUE);
    status = kvActor->Range(&request, &response);
    EXPECT_TRUE(status.ok());

    request.set_count_only(true);
    status = kvActor->Range(&request, &response);
    EXPECT_TRUE(status.ok());

    etcdserverpb::TxnRequest txnRequest;
    etcdserverpb::TxnResponse txnResponse;
    etcdserverpb::ResponseHeader header;
    auto compare = txnRequest.add_compare();
    compare->set_key("key");
    compare->set_value("1");
    // Txn test
    compare->set_result(etcdserverpb::Compare::EQUAL);
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_CREATE);
    TxnResults txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_FALSE(txnResponse.succeeded());
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_MOD);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_FALSE(txnResponse.succeeded());
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_VALUE);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_TRUE(txnResponse.succeeded());
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_LEASE);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_TRUE(txnResponse.succeeded());
    compare->set_target(::etcdserverpb::Compare_CompareTarget::
                            Compare_CompareTarget_Compare_CompareTarget_INT_MIN_SENTINEL_DO_NOT_USE_);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_FALSE(txnResponse.succeeded());

    compare->set_result(etcdserverpb::Compare::GREATER);
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_CREATE);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_TRUE(txnResponse.succeeded());
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_MOD);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_TRUE(txnResponse.succeeded());
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_VALUE);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_FALSE(txnResponse.succeeded());
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_LEASE);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_FALSE(txnResponse.succeeded());
    compare->set_target(::etcdserverpb::Compare_CompareTarget::
                            Compare_CompareTarget_Compare_CompareTarget_INT_MIN_SENTINEL_DO_NOT_USE_);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_FALSE(txnResponse.succeeded());

    compare->set_result(etcdserverpb::Compare_CompareResult_LESS);
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_CREATE);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_FALSE(txnResponse.succeeded());

    compare->set_result(etcdserverpb::Compare_CompareResult_NOT_EQUAL);
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_CREATE);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_TRUE(txnResponse.succeeded());

    compare->set_result(etcdserverpb::Compare_CompareResult_Compare_CompareResult_INT_MIN_SENTINEL_DO_NOT_USE_);
    compare->set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_CREATE);
    txn = kvActor->Txn(&txnRequest, &txnResponse, "");
    EXPECT_FALSE(txnResponse.succeeded());
}

TEST_F(MetaStoreTest, WatchServiceActorTest)  // NOLINT
{
    auto wsActor = std::make_shared<meta_store::WatchServiceActor>("wsActor");
    auto aid = litebus::AID();
    litebus::Spawn(wsActor);

    // Create test
    auto request = std::make_shared<etcdserverpb::WatchCreateRequest>();
    auto result = litebus::Async(wsActor->GetAID(), &WatchServiceActor::Create, aid, "uuid1", request);
    EXPECT_TRUE(result.Get());

    // Cancel test
    etcdserverpb::WatchCancelRequest canReq;
    canReq.set_watch_id(0);
    auto result1 = litebus::Async(wsActor->GetAID(), &WatchServiceActor::Cancel, aid, canReq.watch_id(), "");
    EXPECT_TRUE(result1.Get());

    // OnPut test
    mvccpb::KeyValue kv;
    mvccpb::KeyValue prevKv;
    etcdserverpb::WatchRequest rq;
    auto *args = rq.mutable_create_request();
    args->set_key("");
    args->set_prev_kv(true);
    args->set_start_revision(0);

    auto request2 = std::make_shared<etcdserverpb::WatchCreateRequest>(rq.create_request());
    auto result2 = litebus::Async(wsActor->GetAID(), &WatchServiceActor::Create, aid, "uuid2", request2);
    EXPECT_TRUE(result2.Get());

    auto request4 = std::make_shared<etcdserverpb::WatchCreateRequest>(rq.create_request());
    request4->set_key("key");
    auto result4 = litebus::Async(wsActor->GetAID(), &WatchServiceActor::Create, aid, "uuid4", request4);
    EXPECT_TRUE(result4.Get());

    auto request5 = std::make_shared<etcdserverpb::WatchCreateRequest>(rq.create_request());
    request5->set_range_end("1");
    request5->set_key("1");
    auto result5 = litebus::Async(wsActor->GetAID(), &WatchServiceActor::Create, aid, "uuid5", request5);
    EXPECT_TRUE(result5.Get());

    auto request6 = std::make_shared<etcdserverpb::WatchCreateRequest>(rq.create_request());
    request6->set_range_end("9");
    request6->set_key("1");
    auto result6 = litebus::Async(wsActor->GetAID(), &WatchServiceActor::Create, aid, "uuid6", request6);
    EXPECT_TRUE(result6.Get());
    kv.set_key("5");
    litebus::Async(wsActor->GetAID(), &WatchServiceActor::OnPut, kv, prevKv);

    // OnDeleteList test
    mvccpb::KeyValue kv2;
    auto vector = std::make_shared<std::vector<::mvccpb::KeyValue>>();
    vector->emplace_back(kv);
    vector->emplace_back(kv2);
    litebus::Async(wsActor->GetAID(), &WatchServiceActor::OnDeleteList, vector);

    // OnDelete test
    prevKv.set_key("5");
    litebus::Async(wsActor->GetAID(), &WatchServiceActor::OnDelete, prevKv);

    litebus::Terminate(wsActor->GetAID());
    litebus::Await(wsActor);
}

TEST_F(MetaStoreTest, MetaStoreClientAndMetaStoreServiceTest)  // NOLINT
{
    auto kvServiceActor = std::make_shared<functionsystem::meta_store::KvServiceActor>();
    litebus::Spawn(kvServiceActor);
    litebus::AID kvServerAccessorAID =
        litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServiceActor->GetAID()));
    auto leaseServiceActor = std::make_shared<functionsystem::meta_store::LeaseServiceActor>(kvServiceActor->GetAID());
    litebus::Spawn(leaseServiceActor);
    leaseServiceActor->Start();
    kvServiceActor->AddLeaseServiceActor(leaseServiceActor->GetAID());

    functionsystem::MetaStoreConfig metaStoreConfig{ .etcdAddress = localAddress_,
                                                     .metaStoreAddress = localAddress_,
                                                     .enableMetaStore = true };
    auto metaStoreClient = std::make_shared<functionsystem::MetaStoreClient>(
        metaStoreConfig, functionsystem::GrpcSslConfig{}, functionsystem::MetaStoreTimeoutOption());
    metaStoreClient->Init();

    auto func = [](const std::vector<WatchEvent> &events, bool) -> bool {
        for (const auto &event : events) {
            if (event.eventType == EventType::EVENT_TYPE_PUT) {
                std::cout << "watch put KV value: " + event.kv.key() + " " + event.kv.value() + "\n";
                std::cout << "watch put preKV value: " + event.prevKv.key() + " " + event.prevKv.value() + "\n";
                continue;
            }

            if (event.eventType == EventType::EVENT_TYPE_DELETE) {
                std::cout << "watch delete KV value: " + event.kv.key() + " " + event.kv.value() + "\n";
                std::cout << "watch delete preKV value: " + event.prevKv.key() + " " + event.prevKv.value() + "\n";
                continue;
            }

            std::cout << "the event's type is not supported for key({})\n";
        }
        return true;
    };

    auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    functionsystem::WatchOption watchOption{};
    watchOption.prevKv = true;
    auto watcher1 = metaStoreClient->Watch("key", watchOption, func, syncer);
    auto watcher2 = metaStoreClient->Watch("key2", watchOption, func, syncer);
    auto watcher3 = metaStoreClient->Watch("key3", watchOption, func, syncer);

    auto leaseGrantResponse = metaStoreClient->Grant(4000);
    auto leaseId = leaseGrantResponse.Get().leaseId;
    EXPECT_TRUE(leaseId > 0);

    // test Put
    auto response =
        metaStoreClient->Put("key", "1", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = true }).Get();
    EXPECT_TRUE(response->prevKv.key() == "");
    {
        auto response1 =
            metaStoreClient->Put("key", "value2", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = true })
                .Get();
        EXPECT_TRUE(response1->prevKv.key() == "key");
        EXPECT_TRUE(response1->prevKv.value() == "1");
    }

    {
        auto response1 =
            metaStoreClient
                ->Put(INSTANCE_PATH_PREFIX, "1", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = false })
                .Get();
        auto response2 =
            metaStoreClient
                ->Put(INSTANCE_PATH_PREFIX, "2", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = false })
                .Get();
        auto response3 =
            metaStoreClient
                ->Put(INSTANCE_PATH_PREFIX, "3", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = false })
                .Get();
        EXPECT_TRUE(response2->prevKv.value() == "");
        EXPECT_TRUE(response2->status.IsOk());
        EXPECT_TRUE(response3->prevKv.value() == "");
        EXPECT_TRUE(response3->status.IsOk());
    }

    {
        auto response1 =
            metaStoreClient
                ->Put(INSTANCE_ROUTE_PATH_PREFIX, "1", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = false })
                .Get();
        auto response2 =
            metaStoreClient
                ->Put(INSTANCE_ROUTE_PATH_PREFIX, "2", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = false })
                .Get();
        auto response3 =
            metaStoreClient
                ->Put(INSTANCE_ROUTE_PATH_PREFIX, "3", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = false })
                .Get();
        EXPECT_TRUE(response2->prevKv.value() == "");
        EXPECT_TRUE(response2->status.IsOk());
        EXPECT_TRUE(response3->prevKv.value() == "");
        EXPECT_TRUE(response3->status.IsOk());
    }

    // test Get
    auto response2 = metaStoreClient->Get("key", functionsystem::GetOption{}).Get();
    EXPECT_TRUE(response2->kvs.size() == 1);
    EXPECT_TRUE(response2->kvs[0].key() == "key");
    EXPECT_TRUE(response2->kvs[0].value() == "value2");

    // test Delete
    auto response3 =
        metaStoreClient->Delete("key", functionsystem::DeleteOption{ .prevKv = true, .prefix = false }).Get();
    EXPECT_TRUE(response3->prevKvs.size() == 1);
    EXPECT_TRUE(response3->prevKvs[0].key() == "key");
    EXPECT_TRUE(response3->prevKvs[0].value() == "value2");

    // test Txn
    metaStoreClient->Put("key2", "1", functionsystem::PutOption{ .leaseId = leaseId, .prevKv = true }).Get();
    auto transaction = metaStoreClient->BeginTransaction();
    transaction->If(functionsystem::meta_store::TxnCompare::OfVersion(
        "key2", functionsystem::meta_store::CompareOperator::EQUAL, 0));
    functionsystem::PutOption putOption{ leaseId, true };
    transaction->Then(functionsystem::meta_store::TxnOperation::Create("key2", "value", putOption));
    transaction->Else(functionsystem::meta_store::TxnOperation::Create("key2", "value", putOption));
    auto response4 = transaction->Commit().Get();
    auto response5 = std::get<functionsystem::PutResponse>(response4->responses[0].response);
    EXPECT_TRUE(response5.prevKv.key() == "key2");
    EXPECT_TRUE(response5.prevKv.value() == "1");

    auto transaction1 = metaStoreClient->BeginTransaction();
    transaction1->If(functionsystem::meta_store::TxnCompare::OfVersion(
        "key2", functionsystem::meta_store::CompareOperator::EQUAL, 0));
    functionsystem::GetOption getOption{};
    transaction1->Then(functionsystem::meta_store::TxnOperation::Create("key2", getOption));
    transaction1->Else(functionsystem::meta_store::TxnOperation::Create("key2", getOption));
    auto response6 = transaction1->Commit().Get();
    auto response7 = std::get<functionsystem::GetResponse>(response6->responses[0].response);
    EXPECT_TRUE(response7.kvs.size() == 1);
    EXPECT_TRUE(response7.kvs[0].key() == "key2");
    EXPECT_TRUE(response7.kvs[0].value() == "value");

    auto transaction2 = metaStoreClient->BeginTransaction();
    transaction2->If(functionsystem::meta_store::TxnCompare::OfVersion(
        "key2", functionsystem::meta_store::CompareOperator::EQUAL, 0));
    functionsystem::DeleteOption deleteOption{ true, false };
    transaction2->Then(functionsystem::meta_store::TxnOperation::Create("key2", deleteOption));
    transaction2->Else(functionsystem::meta_store::TxnOperation::Create("key2", deleteOption));
    auto response8 = transaction2->Commit().Get();
    auto response9 = std::get<functionsystem::DeleteResponse>(response8->responses[0].response);
    EXPECT_TRUE(response9.prevKvs.size() == 1);
    EXPECT_TRUE(response9.prevKvs[0].key() == "key2");
    EXPECT_TRUE(response9.prevKvs[0].value() == "value");

    // test grant lease
    auto leaseGrantResponse1 = metaStoreClient->Grant(4000).Get();
    auto leaseId1 = leaseGrantResponse1.leaseId;
    EXPECT_TRUE(leaseId1 > 0);

    // test keepalive lease
    auto leaseKeepAliveResponse = metaStoreClient->KeepAliveOnce(leaseId1).Get();
    EXPECT_TRUE(leaseId1 == leaseKeepAliveResponse.leaseId);
    EXPECT_TRUE(leaseKeepAliveResponse.ttl > 0);
    EXPECT_TRUE(leaseKeepAliveResponse.ttl <= 4000);

    // test revoke lease
    metaStoreClient->Put("key3", "value3", functionsystem::PutOption{ leaseId1, true }).Get();
    auto LeaseRevokeResponse = metaStoreClient->Revoke(leaseId1).Get();

    watcher1.Get()->Close();
    watcher2.Get()->Close();
    watcher3.Get()->Close();

    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);
    litebus::Terminate(kvServiceActor->GetAID());
    litebus::Await(kvServiceActor);
    litebus::Terminate(leaseServiceActor->GetAID());
    litebus::Await(leaseServiceActor);
}

bool ParseWatchResponse(etcdserverpb::WatchResponse &response, const std::string &msg)
{
    messages::MetaStoreResponse message;
    if (!message.ParseFromString(msg)) {
        return false;
    }
    if (!response.ParseFromString(message.responsemsg())) {
        return false;
    }
    return true;
}

TEST_F(MetaStoreTest, LitebusServiceActorTest)
{
    litebus::Promise<bool> prom;
    auto kvActor = std::make_shared<meta_store::KvServiceActor>();
    auto leaseActor = std::make_shared<meta_store::LeaseServiceActor>(kvActor->GetAID());
    auto client = std::make_shared<MockMetaStoreClientActor>("client");
    litebus::Spawn(kvActor);
    litebus::Spawn(leaseActor);
    litebus::Spawn(client);
    auto kvAccessorActor = std::make_shared<meta_store::KvServiceAccessorActor>(kvActor->GetAID());
    litebus::Spawn(kvAccessorActor);
    leaseActor->Start();
    auto watchUuid = litebus::uuid_generator::UUID::GetRandomUUID().ToString();

    EXPECT_CALL(*client, MockOnWatch)
        .Times(::testing::AtMost(3))
        .WillOnce(Invoke([&](const litebus::AID &from, std::string name, std::string msg) {  // OnCreate
            messages::MetaStoreResponse message;
            EXPECT_TRUE(message.ParseFromString(msg));
            EXPECT_TRUE(message.responseid() == watchUuid);
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(response.ParseFromString(message.responsemsg()));
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_TRUE(response.created());
        }))
        .WillOnce(Invoke([&prom](const litebus::AID &from, std::string name, std::string msg) {  // OnPut or Delete
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_EQ(response.events_size(), 1);
            auto &event = response.events(0);
            if (event.type() == ::mvccpb::Event_EventType::Event_EventType_DELETE) {
                EXPECT_EQ(event.kv().key(), "key");
                EXPECT_EQ(event.prev_kv().key(), "key");
                EXPECT_EQ(event.prev_kv().value(), "1");
                prom.SetValue(true);
            } else {
                EXPECT_EQ(event.type(), ::mvccpb::Event_EventType::Event_EventType_PUT);
                EXPECT_EQ(event.kv().key(), "key");
                EXPECT_EQ(event.kv().value(), "1");
            }
        }))
        .WillOnce(Invoke([&prom](const litebus::AID &from, std::string name, std::string msg) {  // OnDelete
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_EQ(response.events_size(), 1);
            auto &event = response.events(0);
            EXPECT_EQ(event.type(), ::mvccpb::Event_EventType::Event_EventType_DELETE);
            EXPECT_EQ(event.kv().key(), "key");
            EXPECT_EQ(event.prev_kv().key(), "key");
            EXPECT_EQ(event.prev_kv().value(), "1");
            prom.SetValue(true);
        }));
    // Create watch
    {
        messages::MetaStoreRequest req;
        auto request = std::make_shared<etcdserverpb::WatchRequest>();
        auto *args = request->mutable_create_request();
        args->set_key("key");
        args->set_prev_kv(true);  // prefix
        args->set_range_end(StringPlusOne("key"));
        args->set_start_revision(0);
        req.set_requestid(watchUuid);
        req.set_requestmsg(request->SerializeAsString());
        kvAccessorActor->AsyncWatch(client->GetAID(), "Watch", req.SerializeAsString());
    }
    // Put
    {
        EXPECT_CALL(*client, MockOnPut).WillOnce(Return());
        messages::MetaStore::PutRequest request;
        request.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request.set_key("key");
        request.set_value("1");
        kvAccessorActor->AsyncPut(client->GetAID(), "Put", request.SerializeAsString());
    }
    // Get
    {
        EXPECT_CALL(*client, MockOnGet).WillOnce(Return());
        messages::MetaStoreRequest req;
        req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        auto request = std::make_shared<etcdserverpb::RangeRequest>();
        request->set_key("key");
        request->set_range_end("kez");
        req.set_requestmsg(request->SerializeAsString());
        kvAccessorActor->AsyncGet(client->GetAID(), "Get", req.SerializeAsString());
    }
    // Delete
    {
        EXPECT_CALL(*client, MockOnDelete).WillOnce(Return());
        auto request = std::make_shared<etcdserverpb::DeleteRangeRequest>();
        messages::MetaStoreRequest req;
        req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request->set_key("key");
        request->set_range_end("kez");  // delete [key, kez)
        req.set_requestmsg(request->SerializeAsString());
        kvAccessorActor->AsyncDelete(client->GetAID(), "Delete", req.SerializeAsString());
    }

    // Grant
    {
        EXPECT_CALL(*client, MockGrantCallback).WillOnce(Return());
        messages::MetaStoreRequest req;
        req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        auto request = std::make_shared<etcdserverpb::LeaseGrantRequest>();
        request->set_ttl(8);
        request->set_id(1);
        req.set_requestmsg(request->SerializeAsString());
        leaseActor->ReceiveGrant(client->GetAID(), "ReceiveGrant", req.SerializeAsString());
    }

    // keepalive
    {
        EXPECT_CALL(*client, MockKeepAliveOnceCallback).WillOnce(Return());
        messages::MetaStoreRequest req;
        req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        auto request = std::make_shared<etcdserverpb::LeaseKeepAliveRequest>();
        request->set_id(1);
        req.set_requestmsg(request->SerializeAsString());
        leaseActor->ReceiveKeepAlive(client->GetAID(), "ReceiveKeepAlive", req.SerializeAsString());
    }

    // revoke
    {
        EXPECT_CALL(*client, MockRevokeCallback).WillOnce(Return());
        messages::MetaStoreRequest req;
        req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        auto request = std::make_shared<etcdserverpb::LeaseRevokeRequest>();
        request->set_id(1);
        req.set_requestmsg(request->SerializeAsString());
        leaseActor->ReceiveRevoke(client->GetAID(), "ReceiveRevoke", req.SerializeAsString());
    }

    EXPECT_AWAIT_READY(prom.GetFuture());
    litebus::Terminate(kvActor->GetAID());
    litebus::Await(kvActor);
    litebus::Terminate(kvAccessorActor->GetAID());
    litebus::Await(kvAccessorActor);
    litebus::Terminate(leaseActor->GetAID());
    litebus::Await(leaseActor);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(MetaStoreTest, RangeObserverCacheTest)
{
    litebus::Promise<bool> prom;
    auto kvActor = std::make_shared<meta_store::KvServiceActor>();
    auto client = std::make_shared<MockMetaStoreClientActor>("client");
    litebus::Spawn(kvActor);
    litebus::Spawn(client);
    kvActor->CheckAndCreateWatchServiceActor();
    auto kvAccessorActor = std::make_shared<meta_store::KvServiceAccessorActor>(kvActor->GetAID());
    litebus::Spawn(kvAccessorActor);

    auto watchUuid = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    litebus::Promise<bool> createProm;
    litebus::Promise<bool> putProm;
    EXPECT_CALL(*client, MockOnWatch)
        .Times(::testing::AtMost(3))
        .WillOnce(Invoke([&](const litebus::AID &from, std::string name, std::string msg) {  // OnCreate
            messages::MetaStoreResponse message;
            EXPECT_TRUE(message.ParseFromString(msg));
            EXPECT_TRUE(message.responseid() == watchUuid);
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(response.ParseFromString(message.responsemsg()));
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_TRUE(response.created());
            createProm.SetValue(true);
        }))
        .WillOnce(Invoke([&](const litebus::AID &from, std::string name, std::string msg) {  // OnPut or Delete
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_EQ(response.events_size(), 1);
            auto &event = response.events(0);
            if (event.type() == ::mvccpb::Event_EventType::Event_EventType_DELETE) {
                EXPECT_EQ(event.kv().key(), INSTANCE_ROUTE_PATH_PREFIX);
                EXPECT_EQ(event.prev_kv().key(), INSTANCE_ROUTE_PATH_PREFIX);
                EXPECT_EQ(event.prev_kv().value(), "1");
                prom.SetValue(true);
                putProm.SetValue(true);
            } else {
                EXPECT_EQ(event.type(), ::mvccpb::Event_EventType::Event_EventType_PUT);
                EXPECT_EQ(event.kv().key(), INSTANCE_ROUTE_PATH_PREFIX);
                EXPECT_EQ(event.kv().value(), "1");
                putProm.SetValue(true);
            }
        }))
        .WillOnce(Invoke([&](const litebus::AID &from, std::string name, std::string msg) {  // OnDelete
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_EQ(response.events_size(), 1);
            auto &event = response.events(0);
            EXPECT_EQ(event.type(), ::mvccpb::Event_EventType::Event_EventType_DELETE);
            EXPECT_EQ(event.kv().key(), INSTANCE_ROUTE_PATH_PREFIX);
            EXPECT_EQ(event.prev_kv().key(), INSTANCE_ROUTE_PATH_PREFIX);
            EXPECT_EQ(event.prev_kv().value(), "1");
            prom.SetValue(true);
            putProm.SetValue(true);
        }));
    // Create watch
    {
        messages::MetaStoreRequest req;
        auto request = std::make_shared<etcdserverpb::WatchRequest>();
        auto *args = request->mutable_create_request();
        args->set_key(INSTANCE_ROUTE_PATH_PREFIX);
        args->set_prev_kv(true);  // prefix
        args->set_range_end(StringPlusOne(INSTANCE_ROUTE_PATH_PREFIX));
        args->set_start_revision(0);
        req.set_requestid(watchUuid);
        req.set_requestmsg(request->SerializeAsString());
        kvAccessorActor->AsyncWatch(client->GetAID(), "Watch", req.SerializeAsString());
    }
    EXPECT_AWAIT_READY(createProm.GetFuture());
    // Put
    {
        EXPECT_CALL(*client, MockOnPut).WillOnce(Return());
        messages::MetaStore::PutRequest request;
        request.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request.set_key(INSTANCE_ROUTE_PATH_PREFIX);
        request.set_value("1");
        kvAccessorActor->AsyncPut(client->GetAID(), "Put", request.SerializeAsString());
    }
    // Delete
    {
        EXPECT_CALL(*client, MockOnDelete).WillOnce(Return());
        messages::MetaStoreRequest req;
        req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        auto request = std::make_shared<etcdserverpb::DeleteRangeRequest>();
        request->set_range_end(StringPlusOne(INSTANCE_ROUTE_PATH_PREFIX));
        request->set_key(INSTANCE_ROUTE_PATH_PREFIX);
        req.set_requestmsg(request->SerializeAsString());
        kvAccessorActor->AsyncDelete(client->GetAID(), "Delete", req.SerializeAsString());
    }
    EXPECT_AWAIT_READY(putProm.GetFuture());
    EXPECT_AWAIT_READY(prom.GetFuture());
    litebus::Terminate(kvActor->GetAID());
    litebus::Await(kvActor);
    litebus::Terminate(kvAccessorActor->GetAID());
    litebus::Await(kvAccessorActor);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

void AddEvent(std::shared_ptr<meta_store::WatchServiceAsyncPushActor> asyncPushActor, int64_t j)
{
    auto response = std::make_shared<UnsyncedEvents>();
    response->to.emplace_back(std::make_shared<std::pair<litebus::AID, int64_t>>(litebus::AID(), j));
    auto event = std::make_shared<::mvccpb::Event>();
    event->set_type(::mvccpb::Event_EventType::Event_EventType_PUT);
    event->mutable_kv()->set_key(std::to_string(j));
    event->mutable_kv()->set_value("1");
    response->event = event;
    asyncPushActor->AddToUnsyncedEvents(response);
}

TEST_F(MetaStoreTest, WatchServiceAsyncPushActorTest)
{
    auto asyncPushActor = std::make_shared<meta_store::WatchServiceAsyncPushActor>("pushActor");

    asyncPushActor->aboutToPush_ = true;
    std::vector<litebus::Future<bool>> futs;
    for (int j = 0; j < 1000; j++) {
        AddEvent(asyncPushActor, 0);
    }
    EXPECT_GT(asyncPushActor->pushEventCount_, static_cast<long unsigned int>(0));
}

TEST_F(MetaStoreTest, GetAndWatchTest)
{
    std::atomic<bool> put = false;
    std::atomic<bool> deleted = false;
    auto kvActor = std::make_shared<meta_store::KvServiceActor>();
    auto client = std::make_shared<MockMetaStoreClientActor>("client");
    litebus::Spawn(kvActor);
    litebus::Spawn(client);
    auto kvAccessorActor = std::make_shared<meta_store::KvServiceAccessorActor>(kvActor->GetAID());
    litebus::Spawn(kvAccessorActor);

    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    EXPECT_CALL(*client, MockOnGetAndWatch)
        .WillOnce(Invoke([&put, uuid](const litebus::AID &from, std::string name, std::string msg) {  // OnGetAndWatch
            messages::MetaStoreResponse message;
            EXPECT_TRUE(message.ParseFromString(msg));
            EXPECT_TRUE(message.responseid() == uuid);
            messages::GetAndWatchResponse rsp;
            etcdserverpb::RangeResponse rangeResp;
            etcdserverpb::WatchResponse watchResp;
            EXPECT_TRUE(rsp.ParseFromString(message.responsemsg()));
            EXPECT_TRUE(rangeResp.ParseFromString(rsp.getresponsemsg()));
            EXPECT_TRUE(watchResp.ParseFromString(rsp.watchresponsemsg()));
            EXPECT_TRUE(watchResp.created());
            EXPECT_EQ(rangeResp.kvs_size(), 1);
            const auto &kv = rangeResp.kvs(0);
            EXPECT_EQ(kv.key(), "key");
            EXPECT_EQ(kv.value(), "1.0");
            put = true;
        }));

    bool isPut = false;
    EXPECT_CALL(*client, MockOnWatch)
        .WillOnce(Invoke([&isPut](const litebus::AID &from, std::string name, std::string msg) {  // OnPut
            etcdserverpb::WatchResponse resp;
            EXPECT_TRUE(ParseWatchResponse(resp, msg));
            EXPECT_EQ(resp.events_size(), 1);
            auto &event = resp.events(0);
            EXPECT_EQ(event.type(), ::mvccpb::Event_EventType::Event_EventType_PUT);
            EXPECT_EQ(event.kv().value(), "2.0");
            EXPECT_EQ(event.kv().key(), "key");
            isPut = true;
        }))
        .WillOnce(Invoke([&deleted](const litebus::AID &from, std::string name, std::string msg) {  // OnDelete
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_EQ(response.events_size(), 1);
            auto &event = response.events(0);
            EXPECT_EQ(event.prev_kv().value(), "2.0");
            EXPECT_EQ(event.type(), ::mvccpb::Event_EventType::Event_EventType_DELETE);
            deleted = true;
        }));

    {
        messages::MetaStore::PutRequest request;
        request.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request.set_value("1.0");
        request.set_key("key");
        kvAccessorActor->AsyncPut(client->GetAID(), "Put", request.SerializeAsString());
    }

    messages::MetaStoreRequest req;
    auto request = std::make_shared<etcdserverpb::WatchRequest>();
    auto *args = request->mutable_create_request();
    args->set_key("key");
    args->set_prev_kv(true);  // prefix
    args->set_start_revision(0);
    args->set_range_end(StringPlusOne("key"));
    req.set_requestmsg(request->SerializeAsString());
    req.set_requestid(uuid);
    kvAccessorActor->AsyncGetAndWatch(client->GetAID(), "GetAndWatch", req.SerializeAsString());

    ASSERT_AWAIT_TRUE([&]() -> bool { return put; });

    // Put
    {
        EXPECT_CALL(*client, MockOnPut).WillOnce(Return());
        messages::MetaStore::PutRequest request;
        request.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request.set_key("key");
        request.set_value("2.0");
        kvAccessorActor->AsyncPut(client->GetAID(), "Put", request.SerializeAsString());
        ASSERT_AWAIT_TRUE([&]() { return isPut; });
    }
    // Delete
    {
        EXPECT_CALL(*client, MockOnDelete).WillOnce(Return());
        messages::MetaStoreRequest req;
        req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        auto request = std::make_shared<etcdserverpb::DeleteRangeRequest>();
        request->set_range_end("kez");  // delete [key, kez)
        request->set_key("key");

        req.set_requestmsg(request->SerializeAsString());
        kvAccessorActor->AsyncDelete(client->GetAID(), "Delete", req.SerializeAsString());
    }
    ASSERT_AWAIT_TRUE([&]() -> bool { return deleted; });

    litebus::Terminate(kvActor->GetAID());
    litebus::Await(kvActor);
    litebus::Terminate(kvAccessorActor->GetAID());
    litebus::Await(kvAccessorActor);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(MetaStoreTest, LinkTest)
{
    std::atomic<bool> put = false;
    std::atomic<bool> deleted = false;
    std::atomic<bool> canceled = false;
    auto kvActor = std::make_shared<meta_store::KvServiceActor>();
    auto client = std::make_shared<MockMetaStoreClientActor>("client");
    litebus::Spawn(kvActor);
    litebus::Spawn(client);
    auto kvAccessorActor = std::make_shared<meta_store::KvServiceAccessorActor>(kvActor->GetAID());
    litebus::Spawn(kvAccessorActor);

    EXPECT_CALL(*client, MockOnWatch)
        .WillOnce(Invoke([](const litebus::AID &from, std::string name, std::string msg) {  // OnPut
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_EQ(response.events_size(), 1);
            auto &event = response.events(0);
            EXPECT_EQ(event.type(), ::mvccpb::Event_EventType::Event_EventType_PUT);
            EXPECT_EQ(event.kv().key(), "key");
            EXPECT_EQ(event.kv().value(), "2.0");
        }))
        .WillOnce(Invoke([&deleted](const litebus::AID &from, std::string name, std::string msg) {  // OnDelete
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_EQ(response.events_size(), 1);
            auto &event = response.events(0);
            EXPECT_EQ(event.type(), ::mvccpb::Event_EventType::Event_EventType_DELETE);
            EXPECT_EQ(event.prev_kv().value(), "2.0");
            deleted = true;
        }))
        .WillOnce(Invoke([&canceled](const litebus::AID &from, std::string name, std::string msg) {  // OnCancel
            etcdserverpb::WatchResponse response;
            EXPECT_TRUE(ParseWatchResponse(response, msg));
            EXPECT_TRUE(response.canceled());
            EXPECT_EQ(response.cancel_reason(), "client disconnected");
            canceled = true;
        }));

    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    EXPECT_CALL(*client, MockOnGetAndWatch)
        .WillOnce(Invoke([&put, uuid](const litebus::AID &from, std::string name, std::string msg) {  // OnGetAndWatch
            messages::MetaStoreResponse message;
            EXPECT_TRUE(message.ParseFromString(msg));
            EXPECT_TRUE(message.responseid() == uuid);
            messages::GetAndWatchResponse response;
            etcdserverpb::RangeResponse rangeResp;
            etcdserverpb::WatchResponse watchResp;
            EXPECT_TRUE(response.ParseFromString(message.responsemsg()));
            EXPECT_TRUE(rangeResp.ParseFromString(response.getresponsemsg()));
            EXPECT_TRUE(watchResp.ParseFromString(response.watchresponsemsg()));
            EXPECT_TRUE(watchResp.created());
            EXPECT_EQ(rangeResp.kvs_size(), 1);
            const auto &kv = rangeResp.kvs(0);
            EXPECT_EQ(kv.key(), "key");
            EXPECT_EQ(kv.value(), "1.0");
            EXPECT_EQ(kv.mod_revision(), 1);
            put = true;
        }));

    {
        messages::MetaStore::PutRequest request;
        request.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request.set_key("key");
        request.set_value("1.0");
        kvAccessorActor->AsyncPut(client->GetAID(), "Put", request.SerializeAsString());
    }

    messages::MetaStoreRequest req;
    auto request = std::make_shared<etcdserverpb::WatchRequest>();
    auto *args = request->mutable_create_request();
    args->set_key("key");
    args->set_prev_kv(true);  // prefix
    args->set_range_end(StringPlusOne("key"));
    args->set_start_revision(0);
    req.set_requestid(uuid);
    req.set_requestmsg(request->SerializeAsString());
    kvAccessorActor->AsyncGetAndWatch(client->GetAID(), "GetAndWatch", req.SerializeAsString());

    ASSERT_AWAIT_TRUE([&]() -> bool { return put; });

    // Put
    {
        messages::MetaStore::PutRequest request;
        request.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request.set_key("key");
        request.set_value("2.0");
        kvAccessorActor->AsyncPut(client->GetAID(), "Put", request.SerializeAsString());
    }
    // Delete
    {
        EXPECT_CALL(*client, MockOnDelete).WillOnce(Return());
        messages::MetaStoreRequest req;
        req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        auto request = std::make_shared<etcdserverpb::DeleteRangeRequest>();
        request->set_key("key");
        request->set_range_end("kez");  // delete [key, kez)
        req.set_requestmsg(request->SerializeAsString());
        kvAccessorActor->AsyncDelete(client->GetAID(), "Delete", req.SerializeAsString());
    }
    ASSERT_AWAIT_TRUE([&]() -> bool { return deleted; });

    auto watchCounts = litebus::Async(kvActor->watchServiceActor_, &meta_store::WatchServiceActor::GetWatchCount);
    ASSERT_AWAIT_READY(watchCounts);
    EXPECT_EQ(watchCounts.Get().size(), 1);
    EXPECT_NE(watchCounts.Get().find(client->GetAID()), watchCounts.Get().end());
    EXPECT_EQ(watchCounts.Get().find(client->GetAID())->second, 1);

    litebus::Async(kvActor->watchServiceActor_, &meta_store::WatchServiceActor::Exited, client->GetAID());

    ASSERT_AWAIT_TRUE([&]() -> bool { return canceled; });

    ASSERT_AWAIT_TRUE([&]() -> bool {
        watchCounts = litebus::Async(kvActor->watchServiceActor_, &meta_store::WatchServiceActor::GetWatchCount);
        return watchCounts.Get().size() == 0;
    });

    litebus::Terminate(client->GetAID());
    litebus::Await(client);

    litebus::Terminate(kvActor->GetAID());
    litebus::Await(kvActor);
    litebus::Terminate(kvAccessorActor->GetAID());
    litebus::Await(kvAccessorActor);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(MetaStoreTest, BackupTest)
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>("BackupActor", persistAID, MetaStoreBackupOption{.enableSyncSysFunc=true}));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));

    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    const std::string frontendKey = "/sn/instance/business/yrk/tenant/0/function/0-system-faasfrontend/version/$latest/defaultaz/e49404726ebbbbfa00/2d8c9382-38bb-4c1c-8b7a-0b32a1243595";
    const std::string controllerKey = "/sn/instance/business/yrk/tenant/0/function/0-system-faascontroller/version/$latest/defaultaz/0-system-faascontroller-0/0-system-faascontroller-0";
    const std::string schedulerKey = "/sn/instance/business/yrk/tenant/0/function/0-system-faasscheduler/version/$latest/defaultaz/788a66fd50ce7a0700/6c5ace96-044b-446a-94ae-fca2ba282f83";
    const std::string managerKey = "/sn/instance/business/yrk/tenant/0/function/0-system-faasmanager/version/$latest/defaultaz/b1a0dc1d8077080a00/3e6dbea7-5e81-479c-8049-5e65291e34e0";
    const std::string frontendVal = "frontend-value";
    const std::string controllerVal = "controller-value";
    const std::string schedulerVal = "scheduler-value";
    const std::string managerVal = "manager-value";
    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
            EXPECT_TRUE(request->success_size() == 11);
            EXPECT_TRUE(request->compare_size() == 0);
            std::map<std::string, std::string> completeM;
            std::map<std::string, std::string> systemFuncM;
            for (int i = 0; i < 11; i++) {
                const auto &cmp = request->success(i);
                EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestPut);
                const auto &putRequest = cmp.request_put();
                const auto &val = putRequest.value();
                const auto &key = putRequest.key();

                if (key.find("/metastore/kv/") == 0) {
                    ::mvccpb::KeyValue kv;
                    EXPECT_TRUE(kv.ParseFromString(val));
                    EXPECT_TRUE(key == std::string("/metastore/kv/") + kv.key());
                    completeM.emplace(kv.key(), kv.value());
                } else {
                    systemFuncM.emplace(key, val);
                }
            }
            EXPECT_TRUE(completeM.size() == 7);
            EXPECT_TRUE(systemFuncM.size() == 4);
            int i = 1;
            for (const auto &[k, v] : completeM) {
                if (k.find("key") != std::string::npos) {
                    EXPECT_TRUE(k == (std::string("key") + std::to_string(i)));
                    EXPECT_TRUE(v == (std::string("value") + std::to_string(i++)));
                }
            }
            EXPECT_TRUE(systemFuncM.find(frontendKey) != systemFuncM.end());
            EXPECT_TRUE(systemFuncM.find(frontendKey)->second == frontendVal);
            EXPECT_TRUE(systemFuncM.find(controllerKey) != systemFuncM.end());
            EXPECT_TRUE(systemFuncM.find(controllerKey)->second == controllerVal);
            EXPECT_TRUE(systemFuncM.find(schedulerKey) != systemFuncM.end());
            EXPECT_TRUE(systemFuncM.find(schedulerKey)->second == schedulerVal);
            EXPECT_TRUE(systemFuncM.find(managerKey) != systemFuncM.end());
            EXPECT_TRUE(systemFuncM.find(managerKey)->second == managerVal);
            *response = ::etcdserverpb::TxnResponse{};
            response->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }))
        .WillOnce(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                             ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
            EXPECT_TRUE(request->compare_size() == 0);
            EXPECT_TRUE(request->success_size() == 11);
            std::set<std::string> completeSet;
            std::set<std::string> systemFuncSet;
            for (int i = 0; i < 11; i++) {
                const auto &cmp = request->success(i);
                EXPECT_TRUE(cmp.request_case() == etcdserverpb::RequestOp::kRequestDeleteRange);
                const auto &deleteRequest = cmp.request_delete_range();
                auto key = deleteRequest.key();
                if (key.find("/metastore/kv/") == 0) {
                    completeSet.emplace(key);
                } else {
                    systemFuncSet.emplace(key);
                }
                EXPECT_TRUE(deleteRequest.range_end().empty());
            }
            EXPECT_TRUE(completeSet.size() == 7);
            EXPECT_TRUE(systemFuncSet.size() == 4);
            int i = 1;
            for (const auto &x : completeSet) {
                if (x.find("key") != std::string::npos) {
                    EXPECT_TRUE(x == (std::string("/metastore/kv/key") + std::to_string(i++)));
                }
            }
            EXPECT_TRUE(systemFuncSet.count(frontendKey));
            EXPECT_TRUE(systemFuncSet.count(controllerKey));
            EXPECT_TRUE(systemFuncSet.count(schedulerKey));
            EXPECT_TRUE(systemFuncSet.count(managerKey));
            *response = ::etcdserverpb::TxnResponse{};
            response->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }));

    {
        auto transaction = client.BeginTransaction();
        transaction->Then(TxnOperation::Create("key1", "value1", PutOption{ .asyncBackup = false }));
        transaction->Then(TxnOperation::Create("key2", "value2", PutOption{}));
        transaction->Then(TxnOperation::Create("key3", "value3", PutOption{}));
        transaction->Then(TxnOperation::Create(frontendKey, frontendVal, PutOption{}));
        transaction->Then(TxnOperation::Create(controllerKey, controllerVal, PutOption{}));
        transaction->Then(TxnOperation::Create(schedulerKey, schedulerVal, PutOption{}));
        transaction->Then(TxnOperation::Create(managerKey, managerVal, PutOption{}));
        transaction->Commit().Get();
    }
    {
        auto transaction = client.BeginTransaction();
        transaction->Then(TxnOperation::Create("key", DeleteOption{ .prefix = true, .asyncBackup = false }));
        transaction->Then(TxnOperation::Create("/sn/instance", DeleteOption{ .prefix = true, .asyncBackup = false }));
        transaction->Commit().Get();
    }


    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);

    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);

    litebus::Terminate(backupAID);
    litebus::Await(backupAID);

    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, SlowBackupTest)
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>("BackupActor", persistAID));
    litebus::AID kvAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));
    litebus::AID kvAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillRepeatedly(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                                   ::etcdserverpb::TxnResponse *resp) -> ::grpc::Status {
            TimeLoop(1);
            *resp = ::etcdserverpb::TxnResponse{};
            resp->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }));

    std::vector<litebus::Future<std::shared_ptr<TxnResponse>>> futures;
    for (int i = 0; i < 10; i++) {
        auto txn = client.BeginTransaction();
        txn->Then(TxnOperation::Create("key" + std::to_string(i), "value" + std::to_string(i),
                                       PutOption{ .asyncBackup = false }));
        futures.emplace_back(txn->Commit());
    }

    for (auto &fut : futures) {
        ASSERT_AWAIT_READY(fut);
    }

    litebus::Terminate(kvAccessorAID);
    litebus::Await(kvAccessorAID);
    litebus::Terminate(kvAID);
    litebus::Await(kvAID);

    litebus::Terminate(backupAID);
    litebus::Await(backupAID);
    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, BackupFlushBelowMaxConcurrency)
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>(
        "BackupActor", persistAID,
        MetaStoreBackupOption{ .metaStoreMaxFlushConcurrency = 10, .metaStoreMaxFlushBatchSize = 1 }));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));

    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillRepeatedly(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                                   ::etcdserverpb::TxnResponse *txnResponse) -> ::grpc::Status {
            TimeLoop(1);
            *txnResponse = ::etcdserverpb::TxnResponse{};
            txnResponse->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }));
    auto start = std::chrono::steady_clock::now();
    std::vector<litebus::Future<std::shared_ptr<TxnResponse>>> futures;
    for (int i = 0; i < 10; i++) {
        auto transaction = client.BeginTransaction();
        transaction->Then(TxnOperation::Create("key" + std::to_string(i), "value1" + std::to_string(i), PutOption{ .asyncBackup = false }));
        futures.emplace_back(transaction->Commit());
    }
    for (auto &fut : futures) {
        ASSERT_AWAIT_READY(fut);
    }
    auto end = std::chrono::steady_clock::now();
    EXPECT_TRUE(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() <= 10);
    litebus::Terminate(kvServerAccessorAID);
    litebus::Terminate(kvServerAID);
    litebus::Terminate(backupAID);
    litebus::Terminate(persistAID);

    litebus::Await(kvServerAccessorAID);
    litebus::Await(kvServerAID);
    litebus::Await(backupAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, BackupFlushAboveMaxConcurrency)
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>(
        "BackupActor", persistAID,
        MetaStoreBackupOption{ .metaStoreMaxFlushConcurrency = 2, .metaStoreMaxFlushBatchSize = 1 }));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));

    litebus::AID accessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = accessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillRepeatedly(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                                   ::etcdserverpb::TxnResponse *rsp) -> ::grpc::Status {
            TimeLoop(1);
            *rsp = ::etcdserverpb::TxnResponse{};
            rsp->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }));
    auto start = std::chrono::steady_clock::now();
    std::vector<litebus::Future<std::shared_ptr<TxnResponse>>> futures;
    for (int i = 0; i < 10; i++) {
        auto transaction = client.BeginTransaction();
        transaction->Then(TxnOperation::Create("key" + std::to_string(i), "value" + std::to_string(i),
                                               PutOption{ .asyncBackup = false }));
        futures.emplace_back(transaction->Commit());
    }
    for (auto &fut : futures) {
        ASSERT_AWAIT_READY(fut);
    }
    auto end = std::chrono::steady_clock::now();
    EXPECT_TRUE(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() >= 4);
    litebus::Terminate(accessorAID);
    litebus::Await(accessorAID);
    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);

    litebus::Terminate(backupAID);
    litebus::Await(backupAID);
    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, BackupFlushAsyncBack)
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>(
        "BackupActor", persistAID,
        MetaStoreBackupOption{ .metaStoreMaxFlushConcurrency = 5, .metaStoreMaxFlushBatchSize = 2 }));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));

    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillRepeatedly(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                                   ::etcdserverpb::TxnResponse *response) -> ::grpc::Status {
            TimeLoop(1);
            *response = ::etcdserverpb::TxnResponse{};
            response->mutable_header()->set_revision(1);
            return ::grpc::Status::OK;
        }));
    std::vector<litebus::Future<std::shared_ptr<TxnResponse>>> futures;
    for (int i = 0; i < 10; i++) {
        auto transaction = client.BeginTransaction();
        transaction->Then(TxnOperation::Create("key" + std::to_string(i), "value" + std::to_string(i),
                                               PutOption{ .asyncBackup = true }));
        transaction->Then(TxnOperation::Create("key1" + std::to_string(i), "value" + std::to_string(i),
                                               PutOption{ .asyncBackup = true }));
        transaction->Then(TxnOperation::Create("key2" + std::to_string(i), "value" + std::to_string(i),
                                               PutOption{ .asyncBackup = true }));
        futures.emplace_back(transaction->Commit());
        auto transaction1 = client.BeginTransaction();
        transaction1->Then(
            TxnOperation::Create("key" + std::to_string(i), DeleteOption{ .prevKv = false, .prefix = false }));
        transaction1->Then(
            TxnOperation::Create("key1" + std::to_string(i), DeleteOption{ .prevKv = false, .prefix = false }));
        transaction1->Then(
            TxnOperation::Create("key2" + std::to_string(i), DeleteOption{ .prevKv = false, .prefix = false }));
        futures.emplace_back(transaction->Commit());
    }
    for (auto &fut : futures) {
        ASSERT_AWAIT_READY(fut);
    }
    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);
    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);

    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
    litebus::Terminate(backupAID);
    litebus::Await(backupAID);
}

TEST_F(MetaStoreTest, BackupFlushRequestWithError)
{
    auto persistAID =
        litebus::Spawn(std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{}));
    auto backupActor = std::make_shared<BackupActor>(
        "BackupActor", persistAID,
        MetaStoreBackupOption{ .metaStoreMaxFlushConcurrency = 10, .metaStoreMaxFlushBatchSize = 1 });
    auto backupAID = litebus::Spawn(backupActor);
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));

    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();
    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillRepeatedly(
            Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                       ::etcdserverpb::TxnResponse *response) -> ::grpc::Status { return ::grpc::Status::OK; }));
    std::vector<litebus::Future<std::shared_ptr<TxnResponse>>> futures;
    for (int i = 0; i < 10; i++) {
        auto transaction = client.BeginTransaction();
        transaction->Then(TxnOperation::Create("key" + std::to_string(i), "value" + std::to_string(i),
                                               PutOption{ .asyncBackup = false }));
        futures.emplace_back(transaction->Commit());
    }
    for (auto &fut : futures) {
        ASSERT_AWAIT_READY(fut);
    }
    EXPECT_TRUE(backupActor->currentFlushThreshold_ < 10);
    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);
    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);

    litebus::Terminate(backupAID);
    litebus::Await(backupAID);
    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, BackupFailTest)
{
    auto persistAID = litebus::Spawn(std::make_shared<EtcdKvClientStrategy>(
        "Persist", etcdAddress_, MetaStoreTimeoutOption{ .operationRetryTimes = 1, .grpcTimeout = 0 }));
    auto backupAID = litebus::Spawn(std::make_shared<BackupActor>("BackupActor", persistAID));
    litebus::AID kvServerAID = litebus::Spawn(std::make_shared<KvServiceActor>(backupAID));

    litebus::AID kvServerAccessorAID = litebus::Spawn(std::make_shared<KvServiceAccessorActor>(kvServerAID));
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = etcdAddress_,
                                            .metaStoreAddress = kvServerAccessorAID.Url(),
                                            .enableMetaStore = true },
                           {}, MetaStoreTimeoutOption{});
    client.Init();

    EXPECT_CALL(*etcdKvService_, Txn)  // test persistence
        .WillRepeatedly(Invoke([&](::grpc::ServerContext *, const ::etcdserverpb::TxnRequest *request,
                                   ::etcdserverpb::TxnResponse *) -> ::grpc::Status {
            return ::grpc::Status(::grpc::StatusCode::DEADLINE_EXCEEDED, "failed");
        }));

    {
        auto transaction = client.BeginTransaction();
        transaction->Then(TxnOperation::Create("key1", "value1", PutOption{ .asyncBackup = false }));
        transaction->Commit().Get();
    }

    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);
    litebus::Terminate(kvServerAID);
    litebus::Await(kvServerAID);

    litebus::Terminate(backupAID);
    litebus::Await(backupAID);
    litebus::Terminate(persistAID);
    litebus::Await(persistAID);
}

TEST_F(MetaStoreTest, KvRecoverTest)
{
    auto persistActor = std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{});
    litebus::Spawn(persistActor);
    auto backupActor = std::make_shared<BackupActor>("BackupActor", persistActor->GetAID());
    litebus::Spawn(backupActor);
    auto kvServiceActor = std::make_shared<KvServiceActor>();
    litebus::Spawn(kvServiceActor);
    kvServiceActor->backupActor_ = backupActor->GetAID();

    ::mvccpb::KeyValue kv1;
    kv1.set_key("123");
    kv1.set_value("123");
    kv1.set_mod_revision(1);

    ::mvccpb::KeyValue kv2;
    kv2.set_key("1234");
    kv2.set_value("1234");
    kv2.set_mod_revision(2);

    ::etcdserverpb::RangeResponse response;
    response.mutable_header()->set_revision(100);
    response.set_count(2);
    auto *kv = response.mutable_kvs()->Add();
    kv->set_key("/metastore/kv/123");
    kv->set_value(kv1.SerializeAsString());

    kv = response.mutable_kvs()->Add();
    kv->set_key("/metastore/kv/1234");
    kv->set_value(kv2.SerializeAsString());

    EXPECT_CALL(*etcdKvService_, Range)
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(response), testing::Return(::grpc::Status::OK)));
    auto ok = litebus::Async(kvServiceActor->GetAID(), &KvServiceActor::Recover);
    ASSERT_AWAIT_READY(ok);

    EXPECT_EQ(kvServiceActor->cache_.size(), 2);
    EXPECT_NE(kvServiceActor->cache_.find("123"), kvServiceActor->cache_.end());
    EXPECT_EQ(kvServiceActor->cache_.at("123").value(), "123");
    EXPECT_NE(kvServiceActor->cache_.find("1234"), kvServiceActor->cache_.end());
    EXPECT_EQ(kvServiceActor->cache_.at("1234").value(), "1234");
    EXPECT_EQ(kvServiceActor->modRevision_, 2);

    litebus::Terminate(kvServiceActor->GetAID());
    litebus::Await(kvServiceActor);
    litebus::Terminate(backupActor->GetAID());
    litebus::Await(backupActor);
    litebus::Terminate(persistActor->GetAID());
    litebus::Await(persistActor);
}

TEST_F(MetaStoreTest, LeaseRecoverTest)
{
    auto persistActor = std::make_shared<EtcdKvClientStrategy>("Persist", etcdAddress_, MetaStoreTimeoutOption{});
    litebus::Spawn(persistActor);
    auto backupActor = std::make_shared<BackupActor>("BackupActor", persistActor->GetAID());
    litebus::Spawn(backupActor);
    auto leaseServiceActor = std::make_shared<LeaseServiceActor>(litebus::AID());
    litebus::Spawn(leaseServiceActor);
    leaseServiceActor->backupActor_ = backupActor->GetAID();

    ::messages::Lease lease1;
    lease1.set_id(1);
    lease1.set_ttl(100);
    lease1.add_items("123");
    lease1.add_items("1234");
    ::messages::Lease lease2;
    lease2.set_id(2);
    lease2.set_ttl(1);
    lease2.add_items("12345");

    ::etcdserverpb::RangeResponse response;
    response.mutable_header()->set_revision(100);
    response.set_count(2);
    auto *kv = response.mutable_kvs()->Add();
    kv->set_key("/metastore/lease/1");
    kv->set_value(lease1.SerializeAsString());

    kv = response.mutable_kvs()->Add();
    kv->set_key("/metastore/lease/2");
    kv->set_value(lease2.SerializeAsString());

    EXPECT_CALL(*etcdKvService_, Range)
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(response), testing::Return(::grpc::Status::OK)));
    EXPECT_FALSE(leaseServiceActor->running_);

    ::etcdserverpb::DeleteRangeResponse deleteRangeResponse;
    deleteRangeResponse.mutable_header()->set_revision(101);
    KeyValue delKv;
    delKv.set_key("/metastore/lease/2");
    delKv.set_value(lease2.SerializeAsString());
    deleteRangeResponse.mutable_prev_kvs()->Add(std::move(delKv));
    litebus::Future<bool> isDelete;
    EXPECT_CALL(*etcdKvService_, DeleteRange).WillOnce(testing::DoAll(testing::SetArgPointee<2>(deleteRangeResponse), testing::Assign(&isDelete, true), testing::Return(::grpc::Status::OK)));


    auto ok = litebus::Async(leaseServiceActor->GetAID(), &LeaseServiceActor::Start);
    ASSERT_AWAIT_READY(ok);
    ASSERT_AWAIT_TRUE([&]() { return leaseServiceActor->running_; });

    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    EXPECT_GE(leaseServiceActor->leases_.size(), 1);
    EXPECT_NE(leaseServiceActor->leases_.find(1), leaseServiceActor->leases_.end());
    EXPECT_EQ(leaseServiceActor->leases_.at(1).id(), 1);
    EXPECT_EQ(leaseServiceActor->leases_.at(1).ttl(), 100);
    EXPECT_GT(leaseServiceActor->leases_.at(1).expiry(), milliseconds + 100);
    EXPECT_EQ(leaseServiceActor->leases_.at(1).items().size(), 2);
    ASSERT_AWAIT_READY(isDelete);
    litebus::Terminate(leaseServiceActor->GetAID());
    litebus::Await(leaseServiceActor);
    litebus::Terminate(backupActor->GetAID());
    litebus::Await(backupActor);
    litebus::Terminate(persistActor->GetAID());
    litebus::Await(persistActor);
}

TEST_F(MetaStoreTest, WatchServiceActorCancelTest)  // NOLINT
{
    auto wsActor = std::make_shared<meta_store::WatchServiceActor>("wsActor");
    auto observer1 = std::make_shared<meta_store::WatchServiceActor::Observer>();
    observer1->clientInfo =
        std::make_shared<meta_store::WatchServiceActor::WatchClientInfo>(litebus::AID("client1", "127.0.0.1"), 0);
    auto observer2 = std::make_shared<meta_store::WatchServiceActor::Observer>();
    observer2->clientInfo =
        std::make_shared<meta_store::WatchServiceActor::WatchClientInfo>(litebus::AID("client2", "127.0.0.1"), 1);

    wsActor->strictObserversByKey_["key"].emplace_back(observer1);
    wsActor->strictObserversByKey_["key"].emplace_back(observer2);

    wsActor->RemoveStrictObserverById("key", 1);

    EXPECT_EQ(wsActor->strictObserversByKey_["key"].size(), static_cast<uint32_t>(1));
    EXPECT_EQ(wsActor->strictObserversByKey_["key"].at(0)->clientInfo->second, 0);
}

TEST_F(MetaStoreTest, MetaStoreDriverest)  // NOLINT
{
    // start with local
    auto metaStoreDriver = std::make_shared<meta_store::MetaStoreDriver>();
    metaStoreDriver->Start();

    EXPECT_TRUE(metaStoreDriver->Stop().IsOk());
    metaStoreDriver->Await();

    // start with persist
    metaStoreDriver = std::make_shared<meta_store::MetaStoreDriver>();
    metaStoreDriver->Start({localAddress_});

    EXPECT_TRUE(metaStoreDriver->Stop().IsOk());
    metaStoreDriver->Await();
    auto monitor = MetaStoreMonitorFactory::GetInstance().GetMonitor(localAddress_);
    litebus::Future<bool> fut = monitor->ClearObservers();
    ASSERT_AWAIT_READY(fut);

    // start with passthrough
    metaStoreDriver = std::make_shared<meta_store::MetaStoreDriver>();

    EXPECT_TRUE(metaStoreDriver->Stop().IsOk());
    metaStoreDriver->Await();
}

TEST_F(MetaStoreTest, KvServiceActorDuplicateOngoingTest)
{
    auto kvActor = std::make_shared<meta_store::KvServiceActor>();

    // Put
    {
        etcdserverpb::PutRequest request;
        request.set_key("key");
        request.set_value("1");
        etcdserverpb::PutResponse response;

        // check
        EXPECT_TRUE(kvActor->CheckUniqueOngoingValid(request.key()));
        // try run
        auto puts = kvActor->TryPut(&request, &response);
        auto kv = puts.first;
        auto preKv = puts.second;
        EXPECT_EQ(kv.key(), request.key());
        EXPECT_EQ(kv.value(), request.value());
        // insert
        kvActor->InsertUniqueOngoing(etcdserverpb::RequestOp::RequestCase::kRequestPut, kv, preKv);
        // check failed: duplicate key
        EXPECT_FALSE(kvActor->CheckUniqueOngoingValid(request.key()));
        // callback
        kvActor->OnTryPutCache(request.key(), Status::OK());
        // check
        EXPECT_TRUE(kvActor->CheckUniqueOngoingValid(request.key()));
    }

    // Delete
    {
        etcdserverpb::PutRequest putRequest;
        putRequest.set_key("key2");
        putRequest.set_value("2");
        etcdserverpb::PutResponse getResponse;
        kvActor->Put(&putRequest, &getResponse);

        etcdserverpb::DeleteRangeRequest request;
        request.set_key("key2");
        etcdserverpb::DeleteRangeResponse response;
        // try run
        auto deletes = kvActor->TryDeleteRange(&request, &response);
        EXPECT_EQ(deletes->size(), static_cast<uint32_t>(1));
        EXPECT_EQ(deletes->front().key(), request.key());

        // check
        EXPECT_TRUE(kvActor->CheckDeletesOngoingValid(deletes));
        // insert
        kvActor->InsertDeleteOngoing(deletes);
        // check failed: duplicate ongoing
        EXPECT_FALSE(kvActor->CheckUniqueOngoingValid(deletes->front().key()));
        // callbak
        kvActor->OnTryDeleteRange(deletes, Status::OK());
        // check
        EXPECT_TRUE(kvActor->CheckDeletesOngoingValid(deletes));
    }
}
}  // namespace functionsystem::meta_store::test