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

#ifndef UT_MOCKS_MOCK_RUNTIME_CLIENT_H
#define UT_MOCKS_MOCK_RUNTIME_CLIENT_H

#include <grpcpp/security/credentials.h>

#include <gmock/gmock.h>
#include <functional>

#include "common/proto/pb/posix/runtime_rpc.grpc.pb.h"
#include "common/rpc/stream/posix_reactor.h"
#include "common/aksk/aksk_util.h"

namespace functionsystem::test {
using namespace functionsystem::grpc;
using namespace runtime_rpc;

struct RuntimeClientConfig {
    std::string serverAddress;
    std::string serverName;
    std::string runtimeID;
    std::string instanceID;
    std::string token;
    std::shared_ptr<::grpc::ChannelCredentials> creds;
};

class MockRuntimeClient {
public:
    ~MockRuntimeClient()
    {
        if (reactor_ != nullptr && !reactor_->IsDone()) {
            reactor_->TryStop(context_);
        }
        reactor_ = nullptr;
        stub_ = nullptr;
    }

    MOCK_METHOD(void, MockReceiver, (const std::shared_ptr<StreamingMessage> &));
    MOCK_METHOD(void, MockClientClosedCallback, ());

    MockRuntimeClient(const RuntimeClientConfig &config) : config_(config)
    {
        try {
            ::grpc::ChannelArguments args;
            auto channel = ::grpc::CreateCustomChannel(config.serverAddress, ::grpc::InsecureChannelCredentials(), args);
            auto tmout = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), { 30, 0, GPR_TIMESPAN });
            if (!channel->WaitForConnected(tmout)) {
                YRLOG_WARN("ControlClient WaitForConnected address:{} Failed, tv_sec is {}", config.serverAddress, tmout.tv_sec);
                reactor_ = nullptr;
                return;
            }
            YRLOG_WARN("ControlClient Connected {} address:{}", config.runtimeID, config.serverAddress);
            reactor_ = std::make_shared<ClientReactor>();
            reactor_->RegisterReceiver(std::bind(&MockRuntimeClient::Receiver, this, std::placeholders::_1));
            reactor_->RegisterClosedCallback(std::bind(&MockRuntimeClient::ClientClosedCallback, this));
            reactor_->SetID("MOCK_" + config.runtimeID);
            stub_ = RuntimeRPC::NewStub(channel);
            context_.AddMetadata("instance_id", config.instanceID);
            context_.AddMetadata("runtime_id", config.runtimeID);
            context_.AddMetadata("authorization", config.token);
            context_.AddMetadata("access_key", "ak");
            auto timestamp = litebus::time::GetCurrentUTCTime();
            context_.AddMetadata("timestamp", timestamp);
            SensitiveValue sk("sk");
            context_.AddMetadata("signature", SignTimestamp("ak", sk, timestamp));
            stub_->async()->MessageStream(&context_, reactor_.get());
        } catch (std::exception &e) {
            YRLOG_ERROR(
                "failed to establish grpc connection between LocalScheduler and instance({})-runtime({}), exception({})",
                config.instanceID, config.runtimeID, e.what());
            reactor_ = nullptr;
        }
    }

    void Start()
    {
        if (reactor_ == nullptr) {
            YRLOG_WARN("posix client is not created", config_.runtimeID);
            return;
        }
        reactor_->AddMultipleHolds(2);
        reactor_->Read();
        reactor_->StartCall();
    }

    void Stop()
    {
        if (reactor_ == nullptr || reactor_->IsDone()) {
            return;
        }
        reactor_->TryStop(context_);
    }

    litebus::Future<bool> Send(const std::shared_ptr<StreamingMessage> &request)
    {
        auto sendMsgID = request->messageid();
        auto bodyType = request->body_case();
        std::cout << "send msg id = " << sendMsgID << ", body type = " << bodyType << std::endl;
        SensitiveValue sk("sk");
        functionsystem::SignStreamingMessage("ak", sk, request);
        if (reactor_ == nullptr || reactor_->IsDone()) {
            return false;
        }
        return reactor_->Write(request, true);
    }

    void Receiver(const std::shared_ptr<StreamingMessage> &recv)
    {
        auto resp = std::make_shared<StreamingMessage>();
        resp->set_messageid(recv->messageid());
        switch (recv->body_case()) {
            case StreamingMessage::kCallReq : {
                std::cout << "receive call req";
                resp->mutable_callrsp()->set_message("call");
                break;
            }
            case StreamingMessage::kInvokeReq : {
                std::cout << "receive invoke req";
                resp->mutable_invokersp()->set_message("invoke");
                break;
            }
            case StreamingMessage::kHeartbeatReq : {
                std::cout << "receive heartbeat req";
                runtime::HeartbeatResponse heartbeatResponse;
                *resp->mutable_heartbeatrsp() = heartbeatResponse;
                break;
            }
            default:
                std::cout << "receive default req";
                resp->mutable_invokersp()->set_message("default");
                break;
        }
        std::cout << ", instance id = " << config_.instanceID << ", runtime id = " << config_.runtimeID <<
            ", message id = " << recv->messageid() << std::endl;
        SensitiveValue sk("sk");
        functionsystem::SignStreamingMessage("ak", sk, resp);
        Send(resp);
        MockReceiver(recv);
    }

    void ClientClosedCallback()
    {
        std::cout << "client closed, instance id = " << config_.instanceID << ", runtime id = " << config_.runtimeID
                  << std::endl;
        MockClientClosedCallback();
    }

protected:
    using ClientReactor =
        PosixReactor<ReactorType::CLIENT, StreamingMessage, StreamingMessage>;

    std::unique_ptr<RuntimeRPC::Stub> stub_{ nullptr };
    ::grpc::ClientContext context_;
    std::shared_ptr<ClientReactor> reactor_{ nullptr };
    RuntimeClientConfig config_;
};
} // namespace functionsystem::test

#endif // UT_MOCKS_MOCK_RUNTIME_CLIENT_H