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

#include "async/async.hpp"
#include "common/utils/actor_worker.h"
#include "function_proxy/common/posix_client/shared_client/shared_client.h"
#include "function_proxy/common/posix_client/shared_client/shared_client_manager.h"
#include "function_proxy/common/posix_client/shared_client/posix_stream_manager_proxy.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "litebus.hpp"
#include "mocks/mock_posix_control_stream_client.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using namespace ::testing;

class MockerProxy : public ControlInterfaceClientManagerProxy, public DataInterfaceClientManagerProxy {
    explicit MockerProxy(const litebus::AID &aid)
    : ControlInterfaceClientManagerProxy(aid),
      DataInterfaceClientManagerProxy(aid),
      aid_(aid)
    {
    }
    ~MockerProxy() override = default;

    litebus::Future<std::shared_ptr<DataInterfacePosixClient>> NewDataInterfacePosixClient(
        const std::string &instanceID, const std::string &, const std::string &)
    {
        // The creation of a shared client is initiated by the control plane. The data plane supports only client
        // acquisition.
        return litebus::Async(aid_, &SharedClientManager::GetReadyClient, instanceID)
            .Then([](const std::shared_ptr<BaseClient> &client)
                      -> litebus::Future<std::shared_ptr<DataInterfacePosixClient>> {
                if (client == nullptr) {
                    return nullptr;
                }
                return std::dynamic_pointer_cast<DataInterfacePosixClient>(client);
            });
    }

    litebus::Future<std::shared_ptr<grpc::ControlClient>> AsyncInitPosixClient(
        const std::shared_ptr<grpc::PosixControlWrapper> &posixControlWrapper, const NewClientInfo &newClientInfo)
    {
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<grpc::ControlClient>>>();
        auto handler = [promise, posixControlWrapper, newClientInfo]() {
            grpc::ControlClientConfig config{ .target = newClientInfo.address,
                                              .creds = ::grpc::InsecureChannelCredentials(),
                                              .timeoutSec = newClientInfo.timeoutSec,
                                              .maxGrpcSize = newClientInfo.maxGrpcSize };
            auto posix = posixControlWrapper->InitPosixStream(newClientInfo.instanceID, newClientInfo.runtimeID, config);
            promise->SetValue(posix);
        };

        auto actor = std::make_shared<ActorWorker>();
        (void)actor->AsyncWork(handler).OnComplete([actor](const litebus::Future<Status> &) { actor->Terminate(); });
        return promise->GetFuture();
    }

    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> NewControlInterfacePosixClient(
        const std::string &instanceID, const std::string &runtimeID, const std::string &address,
        std::function<void()> closedCb, int64_t timeoutSec, int32_t maxGrpcSize)
    {
        NewClientInfo newClientInfo{ .instanceID = instanceID,
                                     .runtimeID = runtimeID,
                                     .address = address,
                                     .timeoutSec = timeoutSec,
                                     .maxGrpcSize = maxGrpcSize };
        return litebus::Async(aid_, &SharedClientManager::GetClient, instanceID)
            .Then([this, aid(aid_), newClientInfo, closedCb,
                   posixControlWrapper(posixControlWrapper_)](const std::shared_ptr<BaseClient> &client)
                      -> litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> {
                if (client != nullptr) {
                    if (!client->IsDone()) {
                        return std::dynamic_pointer_cast<ControlInterfacePosixClient>(client);
                    }
                    YRLOG_WARN("runtime({}) client for instance({}), address {} has been closed. try to reconnect",
                               newClientInfo.runtimeID, newClientInfo.instanceID, newClientInfo.address);
                }

                YRLOG_INFO("new runtime({}) client for instance({}), address {}", newClientInfo.runtimeID,
                           newClientInfo.instanceID, newClientInfo.address);
                // The connection is created and executed in the caller thread to avoid blocking the client query.
                return AsyncInitPosixClient(posixControlWrapper, newClientInfo)
                    .Then([aid, newClientInfo, closedCb](const std::shared_ptr<grpc::ControlClient> &posix)
                              -> litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> {
                        if (posix == nullptr || posix->IsDone()) {
                            return nullptr;
                        }
                        YRLOG_INFO("runtime({}) client for instance({}), address {} connect successfully",
                                   newClientInfo.runtimeID, newClientInfo.instanceID, newClientInfo.address);
                        posix->RegisterUserCallback(closedCb);
                        return litebus::Async(aid, &SharedClientManager::UpdateClient, newClientInfo, posix);
                    });
            });
    }

private:
    litebus::AID aid_;
};

class SharedPosixClientTest : public Test {
public:
    void SetUp() override
    {
        sharedClientManager_ = std::make_shared<SharedClientManager>("SharedClientManager");
        litebus::Spawn(sharedClientManager_);
        sharedClientMgrProxy_ = std::make_shared<MockerProxy>(sharedClientManager_->GetAID());
        mockClientWrapper_ = std::make_shared<MockPosixControlWrapper>();
        sharedClientMgrProxy_->BindPosixWrapper(mockClientWrapper_);
    }

    void TearDown() override
    {
        litebus::Terminate(sharedClientManager_->GetAID());
        litebus::Await(sharedClientManager_);
        sharedClientManager_ = nullptr;
        sharedClientMgrProxy_ = nullptr;
        mockClientWrapper_ = nullptr;
    }

    std::shared_ptr<DataInterfaceClientManagerProxy> GetDataMgr()
    {
        return sharedClientMgrProxy_;
    }

    std::shared_ptr<ControlInterfaceClientManagerProxy> GetControlMgr()
    {
        return sharedClientMgrProxy_;
    }

    void Prepare(std::string instanceID, const std::shared_ptr<MockControlClient> &mockControlClient)
    {
        std::string runtimeID = "runtime-A";
        std::string address = "127.0.0.1:123";
        EXPECT_CALL(*mockClientWrapper_, InitPosixStream(instanceID, runtimeID, _))
            .WillOnce(Return(mockControlClient));
        EXPECT_CALL(*mockControlClient, Start).WillOnce(Return());
        EXPECT_CALL(*mockControlClient, Stop).WillOnce(Return());
        auto controlInterface = GetControlMgr();
        auto created = controlInterface->NewControlInterfacePosixClient(instanceID, runtimeID, address, nullptr);
        ASSERT_AWAIT_READY(created);
        EXPECT_NE(created.Get(), nullptr);
    }

protected:
    std::shared_ptr<SharedClientManager> sharedClientManager_;
    std::shared_ptr<MockerProxy> sharedClientMgrProxy_;
    std::shared_ptr<MockPosixControlWrapper> mockClientWrapper_;
};

/**
 * Description: Shared Client Manager Test
 * Step:
 * 1. empty client util the client insert
 * 2. Get already existed client
 * 3. Delete client
 * Expectation:
 */
TEST_F(SharedPosixClientTest, SharedClientManagerTest)
{
    std::string instanceID = "instanceID-A";
    std::string instanceNoExist = "instanceID-B";
    std::string runtimeID = "runtime-A";
    std::string address = "127.0.0.1:123";
    auto dataInterface = GetDataMgr();
    auto future = dataInterface->NewDataInterfacePosixClient(instanceID, runtimeID, address);

    auto controlInterface = GetControlMgr();
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockClientWrapper_, InitPosixStream(instanceID, runtimeID, _))
        .WillOnce(Return(mockControlClient));
    EXPECT_CALL(*mockControlClient, Start).WillOnce(Return());
    EXPECT_CALL(*mockControlClient, IsDone).WillOnce(Return(false));
    EXPECT_CALL(*mockControlClient, Stop).WillOnce(Return());
    auto created = controlInterface->NewControlInterfacePosixClient(instanceID, runtimeID, address, nullptr);
    ASSERT_AWAIT_READY(created);
    EXPECT_NE(created.Get(), nullptr);

    ASSERT_AWAIT_READY(future);
    EXPECT_NE(future.Get(), nullptr);

    auto dataCreated = dataInterface->GetDataInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(dataCreated);
    EXPECT_NE(dataCreated.Get(), nullptr);

    auto dataNoExist = dataInterface->GetDataInterfacePosixClient(instanceNoExist);
    ASSERT_AWAIT_READY(dataNoExist);
    EXPECT_NE(dataNoExist, nullptr);

    auto controlCreated = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(controlCreated);
    EXPECT_NE(controlCreated.Get(), nullptr);

    (void)dataInterface->DeleteClient(instanceID);
    // duplicated delete
    (void)controlInterface->DeleteClient(instanceID);
    // get deleted client
    auto deleted = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(deleted);
    EXPECT_EQ(deleted.Get(), nullptr);
}

/**
 * Description: client Call Test
 * Steps:
 * 1. call success
 * 2. call failed
 */
TEST_F(SharedPosixClientTest, CallTest)
{
    auto callreq = std::make_shared<runtime_rpc::StreamingMessage>();
    auto nullClient = BaseClient(nullptr);
    auto nullFuture = nullClient.Call(callreq);
    ASSERT_AWAIT_READY(nullFuture);
    EXPECT_EQ(nullFuture.IsOK(), true);
    EXPECT_EQ(nullFuture.Get()->mutable_callrsp()->code(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);

    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillOnce(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    ASSERT_NE(client, nullptr);

    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_callrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));

    callreq->mutable_callreq()->set_requestid("testRequestID");
    auto future = client->Call(callreq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);

    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    // in test case it would be processed without multiple thread because of mock
    client->Call(callreq).OnComplete([](const litebus::Future<SharedStreamMsg> &ret) -> void {
        EXPECT_EQ(ret.IsError(), false);
        EXPECT_EQ(ret.Get()->mutable_callrsp()->code(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);
    });
    (void)controlInterface->DeleteClient(instanceID);
}

/**
 * Description: client Call Test
 * Steps:
 * 1. InitCall success
 * 2. InitCall failed
 */
TEST_F(SharedPosixClientTest, InitCallTest)
{
    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillRepeatedly(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(!client->IsDone());
    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_callrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));

    auto callreq = std::make_shared<runtime::CallRequest>();
    auto future = client->InitCall(callreq, 5000);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);

    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    // in test case it would be processed without multiple thread because of mock
    client->InitCall(callreq, 5000).OnComplete([](const litebus::Future<runtime::CallResponse> &ret) -> void {
        EXPECT_EQ(ret.IsError(), true);
        EXPECT_EQ(ret.GetErrorCode(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);
    });
    (void)controlInterface->DeleteClient(instanceID);
}

/**
 * Description: client Send Test
 * Steps:
 * 1. send success
 * 2. send failed, reach max retry.
 * 3. send failed
 */
TEST_F(SharedPosixClientTest, SendTest)
{
    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillRepeatedly(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    ASSERT_NE(client, nullptr);

    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_callrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    auto callreq = std::make_shared<runtime_rpc::StreamingMessage>();
    auto future = client->Send(callreq, 0, 5000);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);

    future =
        client->Send(callreq, 16, 5000).OnComplete([](const litebus::Future<runtime_rpc::StreamingMessage> &ret) {
            EXPECT_EQ(ret.IsError(), true);
            EXPECT_EQ(ret.GetErrorCode(), StatusCode::REQUEST_TIME_OUT);
        });
    ASSERT_AWAIT_TRUE([&future]() -> bool { return future.IsError(); });

    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    future = client->Send(callreq, 0, 5000).OnComplete([](const litebus::Future<runtime_rpc::StreamingMessage> &ret) {
        EXPECT_EQ(ret.IsError(), true);
    });
    ASSERT_AWAIT_TRUE([&future]() -> bool { return future.IsError(); });

    (void)controlInterface->DeleteClient(instanceID);
}

/**
 * Description: client NotifyResult Test
 * Steps:
 * 1. NotifyResult success
 * 2. NotifyResult failed
 */
TEST_F(SharedPosixClientTest, NotifyResultTest)
{
    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillRepeatedly(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    EXPECT_NE(client, nullptr);
    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_notifyrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    auto req = runtime::NotifyRequest();
    auto future = client->NotifyResult(std::move(req));
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);

    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    req = runtime::NotifyRequest();
    // in test case it would be processed without multiple thread because of mock
    client->NotifyResult(std::move(req)).OnComplete([](const litebus::Future<runtime::NotifyResponse> &ret) -> void {
        EXPECT_EQ(ret.IsError(), true);
    });
    (void)controlInterface->DeleteClient(instanceID);
}

/**
 * Description: client Heartbeat Test
 * Steps:
 * 1. Heartbeat success
 * 2. Heartbeat failed
 */
TEST_F(SharedPosixClientTest, HeartbeatTest)
{
    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillRepeatedly(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    EXPECT_NE(client, nullptr);
    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_heartbeatrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    auto future = client->Heartbeat(2000);
    ASSERT_AWAIT_READY(future);
    EXPECT_TRUE(future.IsOK());

    msg.mutable_heartbeatrsp()->set_code(common::HealthCheckCode::HEALTHY);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    future = client->Heartbeat(2000);
    ASSERT_AWAIT_READY(future);
    EXPECT_TRUE(future.IsOK());
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);

    msg.mutable_heartbeatrsp()->set_code(common::HealthCheckCode::HEALTH_CHECK_FAILED);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    bool isFinish = false;
    client->Heartbeat(2000).OnComplete([&isFinish](const litebus::Future<Status> &status) {
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.GetErrorCode(), static_cast<int32_t>(StatusCode::INSTANCE_HEALTH_CHECK_ERROR));
        isFinish = true;
    });
    ASSERT_AWAIT_TRUE([&isFinish]() { return isFinish; });

    msg.mutable_heartbeatrsp()->set_code(common::HealthCheckCode::SUB_HEALTH);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    future = client->Heartbeat(2000);
    ASSERT_AWAIT_READY(future);
    EXPECT_TRUE(future.IsOK());
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::INSTANCE_SUB_HEALTH);

    isFinish = false;
    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    // in test case it would be processed without multiple thread because of mock
    client->Heartbeat(2000).OnComplete([&isFinish](const litebus::Future<Status> &status) {
        EXPECT_TRUE(status.IsError());
        EXPECT_EQ(status.GetErrorCode(), static_cast<int32_t>(StatusCode::INSTANCE_HEARTBEAT_LOST));
        isFinish = true;
    });
    ASSERT_AWAIT_TRUE([&isFinish]() { return isFinish; });
    (void)controlInterface->DeleteClient(instanceID);
}

/**
 * Description: client Shutdown Test
 * Steps:
 * 1. Shutdown success
 * 2. Shutdown failed
 */
TEST_F(SharedPosixClientTest, ShutdownTest)
{
    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillRepeatedly(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    EXPECT_NE(client, nullptr);
    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_shutdownrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    auto req = runtime::ShutdownRequest();
    auto future = client->Shutdown(std::move(req));
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);

    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    req = runtime::ShutdownRequest();
    // in test case it would be processed without multiple thread because of mock
    client->Shutdown(std::move(req)).OnComplete([](const litebus::Future<runtime::ShutdownResponse> &ret) -> void {
        EXPECT_EQ(ret.IsOK(), true);
        EXPECT_EQ(ret.Get().code(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);
    });
    (void)controlInterface->DeleteClient(instanceID);
}

/**
 * Description: client Signal Test
 * Steps:
 * 1. Signal success
 * 2. Signal failed
 */
TEST_F(SharedPosixClientTest, SignalTest)
{
    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillRepeatedly(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    EXPECT_NE(client, nullptr);
    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_signalrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    auto req = runtime::SignalRequest();
    auto future = client->Signal(std::move(req));
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);

    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    req = runtime::SignalRequest();
    // in test case it would be processed without multiple thread because of mock
    client->Signal(std::move(req)).OnComplete([](const litebus::Future<runtime::SignalResponse> &ret) -> void {
        EXPECT_EQ(ret.IsOK(), true);
        EXPECT_EQ(ret.Get().code(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);
    });
    (void)controlInterface->DeleteClient(instanceID);
}

/**
 * Description: client Checkpoint Test
 * Steps:
 * 1. Checkpoint success
 * 2. Checkpoint failed
 */
TEST_F(SharedPosixClientTest, CheckpointTest)
{
    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillRepeatedly(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    EXPECT_NE(client, nullptr);
    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_checkpointrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    auto req = runtime::CheckpointRequest();
    auto future = client->Checkpoint(std::move(req));
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);

    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    req = runtime::CheckpointRequest();
    // in test case it would be processed without multiple thread because of mock
    client->Checkpoint(std::move(req)).OnComplete([](const litebus::Future<runtime::CheckpointResponse> &ret) -> void {
        EXPECT_EQ(ret.IsOK(), true);
        EXPECT_EQ(ret.Get().code(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);
    });
    (void)controlInterface->DeleteClient(instanceID);
}

/**
 * Description: client Recover Test
 * Steps:
 * 1. Recover success
 * 2. Recover failed
 */
TEST_F(SharedPosixClientTest, RecoverTest)
{
    std::string instanceID = "instanceID-A";
    auto mockControlClient = std::make_shared<MockControlClient>();
    EXPECT_CALL(*mockControlClient, IsDone).WillRepeatedly(Return(false));
    Prepare(instanceID, mockControlClient);
    auto controlInterface = GetControlMgr();
    auto created = controlInterface->GetControlInterfacePosixClient(instanceID);
    ASSERT_AWAIT_READY(created);
    auto client = created.Get();
    EXPECT_NE(client, nullptr);
    runtime_rpc::StreamingMessage msg;
    (void)msg.mutable_recoverrsp();
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(msg));
    auto req = runtime::RecoverRequest();
    auto future = client->Recover(std::move(req));
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);

    // recover timeout
    litebus::Promise<runtime_rpc::StreamingMessage> promise;
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    req = runtime::RecoverRequest();
    auto ret = client->Recover(std::move(req), 100);
    ASSERT_AWAIT_READY(ret);
    EXPECT_EQ(ret.IsOK(), true);
    EXPECT_EQ(ret.Get().code(), common::ERR_USER_FUNCTION_EXCEPTION);
    EXPECT_THAT(ret.Get().message(), HasSubstr("timeout to call recover"));

    // recover failed
    promise.SetFailed(-1);
    EXPECT_CALL(*mockControlClient, Send).WillOnce(Return(promise.GetFuture()));
    req = runtime::RecoverRequest();
    // in test case it would be processed without multiple thread because of mock
    ret = client->Recover(std::move(req));
    ASSERT_AWAIT_READY(ret);
    EXPECT_EQ(ret.IsOK(), true);
    EXPECT_EQ(ret.Get().code(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);

    (void)controlInterface->DeleteClient(instanceID);
}
}  // namespace functionsystem::test