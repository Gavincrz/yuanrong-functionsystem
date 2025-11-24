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

#include "common/rpc/client/grpc_client.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#include "async/future.hpp"
#include "common/common_flags/common_flags.h"
#include "common/logs/logging.h"
#include "common/utils/files.h"
#include "common/hex/hex.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "etcd/server/etcdserver/api/v3election/v3electionpb/v3election.grpc.pb.h"
#include "utils/port_helper.h"

using namespace etcdserverpb;

namespace functionsystem::test {

std::string serverAddress_ = std::string("127.0.0.1:50001");
constexpr uint64_t GRPC_TEST_RSP_HEADER_CLUSTER_ID = 123456;
constexpr int GRPC_TEST_WATCH_ID = 1234567;

class TestEtcdWatchService final : public etcdserverpb::Watch::Service {
public:
    explicit TestEtcdWatchService()
    {
    }

    grpc::Status Watch(grpc::ServerContext *context,
                       grpc::ServerReaderWriter<WatchResponse, WatchRequest> *stream) override
    {
        WatchRequest req;
        WatchResponse rsp;

        while (stream->Read(&req)) {
            rsp.set_watch_id(req.create_request().watch_id());
            stream->Write(rsp);
        }

        return grpc::Status::OK;
    }
};

class TestEtcdKvService final : public etcdserverpb::KV::Service {
public:
    explicit TestEtcdKvService()
    {
    }

    grpc::Status Range(grpc::ServerContext *context, const etcdserverpb::RangeRequest *req,
                       etcdserverpb::RangeResponse *rsp) override
    {
        return grpc::Status::OK;
    }
    grpc::Status Put(grpc::ServerContext *context, const etcdserverpb::PutRequest *req,
                     etcdserverpb::PutResponse *rsp) override
    {
        YRLOG_DEBUG("TestEtcdKvService recv, key = {}", req->key());
        auto header = rsp->mutable_header();
        header->set_cluster_id(GRPC_TEST_RSP_HEADER_CLUSTER_ID);
        return grpc::Status::OK;
    }
    grpc::Status DeleteRange(grpc::ServerContext *context, const etcdserverpb::DeleteRangeRequest *req,
                             etcdserverpb::DeleteRangeResponse *rsp) override
    {
        return grpc::Status::OK;
    }
    grpc::Status Txn(grpc::ServerContext *context, const etcdserverpb::TxnRequest *req,
                     etcdserverpb::TxnResponse *rsp) override
    {
        return grpc::Status::OK;
    }
    grpc::Status Compact(grpc::ServerContext *context, const etcdserverpb::CompactionRequest *req,
                         etcdserverpb::CompactionResponse *rsp) override
    {
        return grpc::Status::OK;
    }
};



static int MakeTmpDir(const std::string &path = "tmp")
{
    std::string cmd = "mkdir -p " + path;
    int r = system(cmd.c_str());
    return r;
}

grpc::Server *g_server = nullptr;
class GrpcClientTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        serverAddress_ = "127.0.0.1:" + std::to_string(FindAvailablePort());
        YRLOG_INFO("Start GRPC Server on net port: {}", serverAddress_);

        auto p = std::make_shared<litebus::Promise<bool>>();
        YRLOG_DEBUG("start test grpc server {}", "test");
        auto t = std::thread(RunServer, p, &g_server);
        t.detach();
        p->GetFuture().Get();
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        if (g_server != nullptr) {
            g_server->Shutdown();
            g_server = nullptr;
        }
    }

    void SetUp() override
    {
    }

    void TearDown() override
    {
    }

    static void RunServer(std::shared_ptr<litebus::Promise<bool>> p, grpc::Server **s)
    {
        std::string serverAddr(serverAddress_);
        TestEtcdKvService kvService;
        TestEtcdWatchService watchService;

        grpc::ServerBuilder builder;
        builder.AddListeningPort(serverAddr, grpc::InsecureServerCredentials());
        builder.RegisterService(&kvService);
        builder.RegisterService(&watchService);
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        YRLOG_DEBUG("server listening on {}", serverAddr);
        *s = server.get();
        p->SetValue(true);
        server->Wait();
    }
    
    inline static std::string serverAddress_;

private:
};

TEST_F(GrpcClientTest, CreateGrpcClientFailed2)
{
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    auto c = GrpcClient<KV>::CreateGrpcClient("127.0.0.1:" + std::to_string(port));
    EXPECT_TRUE(c);

    c = GrpcClient<KV>::CreateGrpcClient("fake_address");
    EXPECT_TRUE(c);
}

TEST_F(GrpcClientTest, GrpcEtcdKvPutFailed)
{
    PutRequest req;
    req.set_key("test_key");

    PutResponse rsp;
    auto c = GrpcClient<KV>::CreateGrpcClient("127.0.0.1:8080");
    ASSERT_TRUE(c);

    auto s = c->Call("Test::test etcd kv put ", req, rsp, &etcdserverpb::KV::Stub::Put, 1);
    EXPECT_FALSE(s.IsOk());
}

TEST_F(GrpcClientTest, GrpcEtcdKvPutFailed2)
{
    PutRequest req;
    req.set_key("test_key");

    PutResponse rsp;
    auto c = GrpcClient<KV>::CreateGrpcClient("abcdefg");
    ASSERT_TRUE(c);

    auto s = c->Call("Test::test etcd kv put ", req, rsp, &etcdserverpb::KV::Stub::Put, 1);

    EXPECT_FALSE(s.IsOk());
}

TEST_F(GrpcClientTest, GrpcEtcdKvPutSuccess)
{
    PutRequest req;
    req.set_key("test_key");

    PutResponse rsp;
    auto c = GrpcClient<KV>::CreateGrpcClient(serverAddress_);
    ASSERT_TRUE(c);

    auto s = c->Call("Test::test etcd kv put ", req, rsp, &etcdserverpb::KV::Stub::Put, 10);

    EXPECT_TRUE(s.IsOk());
    ASSERT_TRUE(rsp.has_header());
    const auto &h = rsp.header();
    auto clusterID = h.cluster_id();
    EXPECT_EQ(clusterID, GRPC_TEST_RSP_HEADER_CLUSTER_ID);
}

TEST_F(GrpcClientTest, GrpcEtcdKvPutSuccess2)
{
    PutRequest req;
    req.set_key("test_key");

    PutResponse rsp;
    auto c = GrpcClient<KV>::CreateGrpcClient(serverAddress_);
    ASSERT_TRUE(c);

    auto s = c->CallAsync("Test::test etcd kv async put", req, rsp, &etcdserverpb::KV::Stub::AsyncPut, 10);

    EXPECT_TRUE(s.IsOk());
    ASSERT_TRUE(rsp.has_header());
    const auto &h = rsp.header();
    auto clusterID = h.cluster_id();
    EXPECT_EQ(clusterID, GRPC_TEST_RSP_HEADER_CLUSTER_ID);
}

TEST_F(GrpcClientTest, GrpcEtcdKvPutSuccess3)
{
    PutRequest req;
    req.set_key("test_key");

    PutResponse rsp;
    auto c = GrpcClient<KV>::CreateGrpcClient(serverAddress_);
    ASSERT_TRUE(c);

    litebus::Promise<Status> donePromise;

    auto doneCB = [&donePromise](const Status &s) {
        YRLOG_DEBUG("test grpc call done");
        donePromise.SetValue(s);
    };

    bool r = c->CallAsync("Test::test etcd kv async put", req, rsp, &etcdserverpb::KV::Stub::AsyncPut, doneCB, 10);
    EXPECT_TRUE(r);

    auto s = donePromise.GetFuture().Get();

    EXPECT_TRUE(s.IsOk());
    ASSERT_TRUE(rsp.has_header());
    const auto &h = rsp.header();
    auto clusterID = h.cluster_id();
    EXPECT_EQ(clusterID, GRPC_TEST_RSP_HEADER_CLUSTER_ID);
}

TEST_F(GrpcClientTest, GrpcEtcdWatchCallFailed)
{
    auto c = GrpcClient<Watch>::CreateGrpcClient(serverAddress_);
    ASSERT_TRUE(c);

    using StubFunc =
        std::unique_ptr<grpc::ClientReaderWriter<WatchRequest, WatchResponse>> (Watch::Stub::*)(grpc::ClientContext *);
    auto s = c->CallReadWriteStream<WatchRequest, WatchResponse, StubFunc>(&etcdserverpb::Watch::Stub::Watch, nullptr);
    ASSERT_FALSE(s);
}

TEST_F(GrpcClientTest, GrpcEtcdWatchWriteSuccess)
{
    WatchRequest req;
    WatchResponse rsp;
    auto cReq = req.mutable_create_request();
    cReq->set_watch_id(GRPC_TEST_WATCH_ID);

    auto c = GrpcClient<Watch>::CreateGrpcClient(serverAddress_);
    ASSERT_TRUE(c);

    grpc::ClientContext context;
    using StubFunc =
        std::unique_ptr<grpc::ClientReaderWriter<WatchRequest, WatchResponse>> (Watch::Stub::*)(grpc::ClientContext *);
    auto s = c->CallReadWriteStream<WatchRequest, WatchResponse, StubFunc>(&etcdserverpb::Watch::Stub::Watch, &context);
    ASSERT_TRUE(s);

    s->Write(req);
    s->WritesDone();

    s->Read(&rsp);
    EXPECT_EQ(rsp.watch_id(), GRPC_TEST_WATCH_ID);
    auto status = s->Finish();
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcClientTest, GetConfig)
{
    const char *argv[] = { "./domain_scheduler", "--etcd_auth_type=TLS" };
    CommonFlags flags;
    flags.ParseFlags(2, argv);
    auto config = GetGrpcSSLConfig(flags);
    EXPECT_TRUE(config.targetName.empty());
}

TEST_F(GrpcClientTest, GetNoauthConfig)
{
    const char *argv[] = { "./domain_scheduler", "--etcd_auth_type=Noauth" };
    CommonFlags flags;
    flags.ParseFlags(2, argv);
    auto config = GetGrpcSSLConfig(flags);
    EXPECT_TRUE(config.targetName.empty());
}

TEST_F(GrpcClientTest, GetSTSConfig)
{
    const char *argv[] = { "./domain_scheduler", "--etcd_auth_type=STS" };
    CommonFlags flags;
    flags.ParseFlags(2, argv);
    auto config = GetGrpcSSLConfig(flags);
    EXPECT_TRUE(config.targetName.empty());
}

}  // namespace functionsystem::test