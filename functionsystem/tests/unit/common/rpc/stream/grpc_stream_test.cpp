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

#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include <iostream>
#include <thread>

#include "utils/port_helper.h"
#include "common/rpc/stream/posix/control_client.h"
#include "common/rpc/stream/posix/control_server.h"
#include "common/status/status.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using namespace functionsystem::grpc;
using namespace runtime_rpc;

litebus::Future<std::shared_ptr<StreamingMessage>> InvokeHandler(const std::string &from,
                                                                 const std::shared_ptr<StreamingMessage> &request)
{
    EXPECT_EQ(from, "tmpInstance");
    auto msg = std::make_shared<StreamingMessage>();
    EXPECT_TRUE(request->has_invokereq());
    if (request->invokereq().requestid() == "server_call_invoke") {
        msg->mutable_invokersp()->set_code(common::ErrorCode::ERR_NONE);
    } else {
        msg->mutable_invokersp()->set_code(common::ErrorCode::ERR_PARAM_INVALID);
    }
    return msg;
}

litebus::Future<std::shared_ptr<StreamingMessage>> CallResultHandler(const std::string &from,
                                                                     const std::shared_ptr<StreamingMessage> &request)
{
    EXPECT_EQ(from, "tmpInstance");
    auto msg = std::make_shared<StreamingMessage>();
    EXPECT_TRUE(request->has_callresultreq());
    if (request->callresultreq().requestid() == "server_call_result") {
        msg->mutable_callresultack()->set_code(common::ErrorCode::ERR_NONE);
    } else {
        msg->mutable_callresultack()->set_code(common::ErrorCode::ERR_PARAM_INVALID);
    }
    return msg;
}

litebus::Future<std::shared_ptr<StreamingMessage>> CallServerHandler(const std::shared_ptr<StreamingMessage> &request)
{
    auto msg = std::make_shared<StreamingMessage>();
    EXPECT_TRUE(request->has_callreq());
    if (request->callreq().senderid() == "ut_client") {
        msg->mutable_callrsp()->set_code(common::ErrorCode::ERR_NONE);
    } else {
        msg->mutable_callrsp()->set_code(common::ErrorCode::ERR_PARAM_INVALID);
    }
    return msg;
}

litebus::Future<std::shared_ptr<StreamingMessage>> NotifyServerHandler(const std::shared_ptr<StreamingMessage> &request)
{
    auto msg = std::make_shared<StreamingMessage>();
    EXPECT_TRUE(request->has_notifyreq());
    EXPECT_EQ(request->notifyreq().requestid(), "request_id");
    msg->mutable_notifyrsp();
    return msg;
}

class StreamTest : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        REGISTER_FUNCTION_SYS_POSIX_CONTROL_HANDLER(StreamingMessage::kInvokeReq, &InvokeHandler);
        REGISTER_FUNCTION_SYS_POSIX_CONTROL_HANDLER(StreamingMessage::kCallResultReq, &CallResultHandler);

        REGISTER_RUNTIME_CONTROL_POSIX_HANDLER(StreamingMessage::kCallReq, &CallServerHandler);
        REGISTER_RUNTIME_CONTROL_POSIX_HANDLER(StreamingMessage::kNotifyReq, &NotifyServerHandler);

        serverAddress_ = "127.0.0.1:" + std::to_string(FindAvailablePort());
        StartServerAndClient();
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        ShutdownServerAndClient();
    }

    static void StartServerAndClient()
    {
        service_ = std::make_shared<ControlServer>();
        litebus::Promise<bool> promise;
        thr_ = std::thread([promise]() {
            ::grpc::ServerBuilder builder;
            YRLOG_INFO("Server listening on {}", serverAddress_);
            builder.AddListeningPort(serverAddress_, ::grpc::InsecureServerCredentials());
            builder.RegisterService(service_.get());
            server_ = std::move(builder.BuildAndStart());
            promise.SetValue(true);
            server_->Wait();
            std::cout << "Server exit." << std::endl;
        });

        promise.GetFuture().Get();
        grpc::ControlClientConfig config{
            .target = serverAddress_,
            .creds = ::grpc::InsecureChannelCredentials(),
            .timeoutSec = 30,
            .maxGrpcSize = 5
        };
        client_ = std::make_shared<ControlClient>("tmpInstance", "runtimeID", config);
        client_->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // wait for client to connect to server
    }

    static void ShutdownServerAndClient()
    {
        service_->Finish();
        server_->Shutdown();
        if (thr_.joinable()) {
            printf("wait server finished\n");
            thr_.join();
        }
        server_ = nullptr;
        client_->Stop();
    }

    static void Restart()
    {
        ShutdownServerAndClient();
        StartServerAndClient();
    }

protected:
    inline static std::thread thr_;
    inline static std::string serverAddress_;
    inline static std::unique_ptr<::grpc::Server> server_;
    inline static std::shared_ptr<ControlServer> service_;
    inline static std::shared_ptr<ControlClient> client_;
};

TEST_F(StreamTest, PosixCallServerValidTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("call0");
    request->mutable_callreq()->set_requestid("hello");
    request->mutable_callreq()->set_senderid("ut_client");
    auto future = client_->Send(request);
    const auto &result = future.Get();
    EXPECT_EQ(result.has_callrsp(), true);
    EXPECT_EQ(result.callrsp().code(), common::ErrorCode::ERR_NONE);
}

TEST_F(StreamTest, PosixCallServerInvalidTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("call1");
    request->mutable_callreq()->set_requestid("hello");
    request->mutable_callreq()->set_senderid("invalid");
    auto future = client_->Send(request);
    const auto &result = future.Get();
    EXPECT_EQ(result.has_callrsp(), true);
    EXPECT_EQ(result.callrsp().code(), common::ErrorCode::ERR_PARAM_INVALID);
}

TEST_F(StreamTest, PosixNotifyServerTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("notify");
    request->mutable_notifyreq()->set_requestid("request_id");
    auto future = client_->Send(request);
    const auto &result = future.Get();
    EXPECT_EQ(result.has_notifyrsp(), true);
}

TEST_F(StreamTest, PosixInvokeClientValidTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("invoke0");
    request->mutable_invokereq()->set_requestid("server_call_invoke");
    auto future = service_->Send(request);
    const auto &result = future.Get();
    EXPECT_EQ(result.has_invokersp(), true);
    EXPECT_EQ(result.invokersp().code(), common::ErrorCode::ERR_NONE);
}

TEST_F(StreamTest, ServerFinishTest)
{
    service_->TryFinish();

    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("invoke0");
    request->mutable_invokereq()->set_requestid("server_call_invoke");
    auto future = service_->Send(request);
    EXPECT_TRUE(future.IsError());

    Restart(); // restart server and client
}

TEST_F(StreamTest, PosixInvokeClientInValidTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("invoke1");
    request->mutable_invokereq()->set_requestid("invalid");
    auto future = service_->Send(request);
    const auto &result = future.Get();
    EXPECT_EQ(result.has_invokersp(), true);
    EXPECT_EQ(result.invokersp().code(), common::ErrorCode::ERR_PARAM_INVALID);
}

TEST_F(StreamTest, PosixCallResultClientValidTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("callresult0");
    request->mutable_callresultreq()->set_requestid("server_call_result");
    auto future = service_->Send(request);
    const auto &result = future.Get();
    EXPECT_EQ(result.has_callresultack(), true);
    EXPECT_EQ(result.callresultack().code(), common::ErrorCode::ERR_NONE);
}

TEST_F(StreamTest, PosixCallResultClientInValidTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("callresult1");
    request->mutable_callresultreq()->set_requestid("invalid");
    auto future = service_->Send(request);
    const auto &result = future.Get();
    EXPECT_EQ(result.has_callresultack(), true);
    EXPECT_EQ(result.callresultack().code(), common::ErrorCode::ERR_PARAM_INVALID);
}

TEST_F(StreamTest, PosixInValidCallServerTest)
{
    grpc::ControlClientConfig config{
        .target = "127.5.3.1:50000", .creds = ::grpc::InsecureChannelCredentials(), .timeoutSec = 1, .maxGrpcSize = 4
    };
    auto client = std::make_shared<ControlClient>("tmpInstance", "runtimeID", config);
    client->Start();
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("call0");
    request->mutable_callreq()->set_requestid("hello");
    request->mutable_callreq()->set_senderid("ut_client");
    auto future = client->Send(request);
    EXPECT_EQ(future.GetErrorCode(), StatusCode::GRPC_STREAM_CALL_ERROR);
    // send again
    request = std::make_shared<StreamingMessage>();
    request->set_messageid("call1");
    request->mutable_callreq()->set_requestid("hello");
    request->mutable_callreq()->set_senderid("ut_client");
    future = client->Send(request);
    EXPECT_EQ(future.GetErrorCode(), StatusCode::GRPC_STREAM_CALL_ERROR);
    client->Stop();
}

TEST_F(StreamTest, PosixInvokeClientMsgSizeTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("invoke0");
    request->mutable_invokereq()->set_requestid("server_call_invoke");
    std::string bigStr(4 * 1024 * 1024, 'a');
    common::Arg arg{};
    arg.set_type(::common::Arg_ArgType_VALUE);
    arg.set_value(bigStr);
    request->mutable_invokereq()->mutable_args()->Add(std::move(arg));
    auto future = service_->Send(request);
    auto result = future.Get();
    EXPECT_EQ(result.has_invokersp(), true);
    EXPECT_EQ(result.invokersp().code(), common::ErrorCode::ERR_NONE);

    std::string bigStr2(5 * 1024 * 1024, 'a');
    common::Arg arg2{};
    arg2.set_type(::common::Arg_ArgType_VALUE);
    arg2.set_value(bigStr);
    request->mutable_invokereq()->mutable_args()->Add(std::move(arg2));
    future = service_->Send(request);
    ASSERT_AWAIT_SET(future);
    EXPECT_EQ(future.IsError(), true);

    Restart(); // restart server and client
}

class InvocationService : public runtime_rpc::RuntimeRPC::Service {
public:
    InvocationService() = default;
    ~InvocationService() = default;
    ::grpc::Status MessageStream(::grpc::ServerContext *context,
                                 ::grpc::ServerReaderWriter<StreamingMessage, StreamingMessage> *stream) override
    {
        StreamingMessage recv;
        stream->Read(&recv);
        EXPECT_EQ(recv.body_case(), StreamingMessage::kCallReq);
        StreamingMessage send;
        send.set_messageid(recv.messageid());
        send.mutable_callrsp()->set_code(common::ErrorCode::ERR_NONE);
        stream->Write(send);
        std::cout << "stream finished " << std::endl;
        return ::grpc::Status();
    }
};

class StreamTestV2 : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        serverAddress_ = "127.0.0.1:" + std::to_string(FindAvailablePort());
        YRLOG_INFO("Start GRPC Server on net port: {}", serverAddress_);

        litebus::Promise<bool> promise;
        auto thr = std::thread([promise]() {
            std::string serverAddress(serverAddress_);
            ::grpc::ServerBuilder builder;
            builder.AddListeningPort(serverAddress, ::grpc::InsecureServerCredentials());
            builder.RegisterService(&service_);
            server_ = std::move(builder.BuildAndStart());
            std::cout << "Server listening on " << serverAddress << std::endl;
            promise.SetValue(true);
            server_->Wait();
            std::cout << "Server exit." << std::endl;
        });
        thr.detach();
        promise.GetFuture().Get();
        grpc::ControlClientConfig config{ .target = serverAddress_,
                                          .creds = ::grpc::InsecureChannelCredentials(),
                                          .timeoutSec = 30,
                                          .maxGrpcSize = 4 };
        client_ = std::make_shared<ControlClient>("tmpInstance", "runtimeID", config);
        client_->Start();
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        server_->Shutdown();
        client_->Stop();
    }

    void Invoke()
    {
        grpc::ControlClientConfig config{ .target = serverAddress_,
                                          .creds = ::grpc::InsecureChannelCredentials(),
                                          .timeoutSec = 30,
                                          .maxGrpcSize = 4 };
        auto client = std::make_shared<ControlClient>("tmpInstance", "runtimeID", config);
        client->Start();
        auto request = std::make_shared<StreamingMessage>();
        request->set_messageid("call0");
        request->mutable_callreq()->set_requestid("hello");
        request->mutable_callreq()->set_senderid("ut_client");
        auto future = client->Send(request);
        const auto &result = future.Get();
        EXPECT_EQ(result.has_callrsp(), true);
        EXPECT_EQ(result.callrsp().code(), common::ErrorCode::ERR_NONE);
        client->Stop();
        YRLOG_INFO("------INVOKE DONE-------------");
    }

protected:
    inline static std::unique_ptr<::grpc::Server> server_;
    inline static InvocationService service_;
    inline static std::shared_ptr<ControlClient> client_;
    inline static std::string serverAddress_;
};

TEST_F(StreamTestV2, PosixCallServerValidTest)
{
    auto request = std::make_shared<StreamingMessage>();
    request->set_messageid("call0");
    request->mutable_callreq()->set_requestid("hello");
    request->mutable_callreq()->set_senderid("ut_client");
    auto future = client_->Send(request);
    const auto &result = future.Get();
    EXPECT_EQ(result.has_callrsp(), true);
    EXPECT_EQ(result.callrsp().code(), common::ErrorCode::ERR_NONE);
}
} // namespace functionsystem::test