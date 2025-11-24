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

#include "async/future.hpp"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "grpcpp/server_builder.h"
#include "mocks/mock_etcd_kv_service.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;

class KvClientTest : public ::testing::Test {
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
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        if (etcdServer_ != nullptr) {
            etcdServer_->Shutdown();
            etcdServer_ = nullptr;
        }
    }

public:
    inline static std::string etcdAddress_;
    inline static std::shared_ptr<grpc::Server> etcdServer_ = nullptr;
    inline static std::shared_ptr<MockEtcdKvService> etcdKvService_ = nullptr;
};

TEST_F(KvClientTest, CommitRawTest)  // NOLINT
{
    auto aid = litebus::Spawn(std::make_shared<meta_store::EtcdKvClientStrategy>(
        "EtcdKvClientStrategy", etcdAddress_, MetaStoreTimeoutOption{}, GrpcSslConfig{}));

    ::etcdserverpb::TxnResponse response;

    response.mutable_header()->set_cluster_id(100);
    response.mutable_header()->set_revision(100);
    response.set_succeeded(true);

    auto *item = response.mutable_responses()->Add();
    auto *raw = item->mutable_response_delete_range();
    raw->mutable_header()->set_cluster_id(100);
    raw->mutable_header()->set_revision(100);
    raw->set_deleted(1);
    auto *kv = raw->mutable_prev_kvs()->Add();
    kv->set_key("mock-key");
    kv->set_key("mock-value");

    EXPECT_CALL(*etcdKvService_, Txn)
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(response), testing::Return(::grpc::Status::OK)));

    ::etcdserverpb::TxnRequest request;
    auto future = litebus::Async(aid, &EtcdKvClientStrategy::CommitRaw, request);
    EXPECT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get()->succeeded());
    EXPECT_EQ(future.Get()->responses().size(), 1);

    litebus::Terminate(aid);
    litebus::Await(aid);
}
}  // namespace functionsystem::meta_store::test