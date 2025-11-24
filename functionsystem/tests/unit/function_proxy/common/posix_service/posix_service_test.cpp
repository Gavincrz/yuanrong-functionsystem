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
#include "function_proxy/common/posix_service/posix_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/rpc/server/common_grpc_server.h"
#include "common/utils/files.h"
#include "function_proxy/common/iam/internal_iam.h"
#include "mocks/mock_runtime_client.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::grpc;
using namespace runtime_rpc;
using namespace function_proxy;

namespace {
const std::string GRPC_SERVER_IP = "127.0.0.1";
const std::string TEST_INSTANCE_ID = "TEST_INSTANCE_ID";
const std::string TEST_RUNTIME_ID = "TEST_RUNTIME_ID";
}

class MockClientProxy {
public:
    MOCK_METHOD(void, MockUpdatePosixClient, (const std::string &instanceID, const std::string &runtimeID,
                                              const std::shared_ptr<grpc::PosixClient> &posixClient));

    void FakeUpdatePosixClient(const std::string &instanceID, const std::string &runtimeID,
                         const std::shared_ptr<grpc::PosixClient> &posixClient)
    {
        std::cout << "instance id = " << instanceID << std::endl;
        std::cout << "runtime id = " << runtimeID << std::endl;
        clients.emplace(instanceID, posixClient);
        MockUpdatePosixClient(instanceID, runtimeID, posixClient);
    }

    std::unordered_map<std::string, std::shared_ptr<grpc::PosixClient>> clients;

};

class PosixServiceTest : public ::testing::Test {
public:
    inline static uint16_t grpcServerPort_;
    [[maybe_unused]] static void SetUpTestSuite()
    {
        grpcServerPort_ = functionsystem::test::FindAvailablePort();
    }

    void SetUp() override
    {
        InternalIAM::Param param;
        param.isEnableIAM = false;
        internalIAM_ = std::make_shared<InternalIAM>(param);
        std::shared_ptr<::grpc::ServerCredentials> creds = ::grpc::InsecureServerCredentials();

        CommonGrpcServerConfig serverConfig;
        serverConfig.ip = GRPC_SERVER_IP;
        serverConfig.listenPort= std::to_string(grpcServerPort_);
        serverConfig.creds = creds;
        server_ = std::make_shared<CommonGrpcServer>(serverConfig);
        posixService_ = std::make_shared<PosixService>();
        posixService_->BindInternalIAM(internalIAM_);
        server_->RegisterService(posixService_);
        server_->Start();
        ASSERT_TRUE(server_->WaitServerReady());

        mockProxy_ = std::make_shared<MockClientProxy>();
        posixService_->RegisterUpdatePosixClientCallback(
            std::bind(&MockClientProxy::FakeUpdatePosixClient, mockProxy_,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void TearDown() override
    {
        server_ = nullptr;
        posixService_ = nullptr;
    }

    std::shared_ptr<MockRuntimeClient> CreateRuntimeClient(const std::string &instanceID, const std::string &runtimeID)
    {
        RuntimeClientConfig config;
        config.serverAddress = GRPC_SERVER_IP + ":" + std::to_string(grpcServerPort_);
        config.serverName = "daylight";
        config.runtimeID = runtimeID;
        config.instanceID = instanceID;
        auto client = std::make_shared<MockRuntimeClient>(config);
        client->Start();
        return client;
    }

protected:
    std::shared_ptr<CommonGrpcServer> server_{ nullptr };
    std::shared_ptr<PosixService> posixService_{ nullptr };
    std::shared_ptr<InternalIAM> internalIAM_{ nullptr };
    std::shared_ptr<MockClientProxy> mockProxy_{ nullptr };
};

/**
 * Feature: PosixServiceTest--ClientConnectTest
 * Description: test runtime grpc client connect to grpc server in proxy
 * Steps:
 * 1. runtime client connect to server without RuntimeID or InstanceID will be reject
 * 2. runtime client connect to server success, test whether message send and receive are ok
 * 3. test runtime client connect to server with credentials
 */
TEST_F(PosixServiceTest, ClientConnectTest)
{
    KillRequest request;
    auto killMsg = std::make_shared<StreamingMessage>();
    *killMsg->mutable_killreq() = request;
    killMsg->set_messageid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    auto client1 = CreateRuntimeClient(TEST_INSTANCE_ID, "");
    EXPECT_EQ(mockProxy_->clients.size(), static_cast<uint32_t>(0));
    EXPECT_CALL(*client1, MockClientClosedCallback).Times(1);

    auto client11 = CreateRuntimeClient(TEST_INSTANCE_ID + "1", TEST_RUNTIME_ID);
    EXPECT_EQ(mockProxy_->clients.size(), static_cast<uint32_t>(0));
    litebus::Future<bool> isCalled11;
    EXPECT_CALL(*client11, MockClientClosedCallback).Times(1).WillOnce(Assign(&isCalled11, true));

    EXPECT_CALL(*mockProxy_, MockUpdatePosixClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID, testing::_)).Times(1);
    internalIAM_->Insert(TEST_INSTANCE_ID);
    auto client2 = CreateRuntimeClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID);
    litebus::Future<bool> isCalled2;
    EXPECT_CALL(*client2, MockClientClosedCallback).Times(1).WillOnce(Assign(&isCalled2, true));

    auto recvFuture = litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>>();
    EXPECT_CALL(*client2, MockReceiver).WillOnce(DoAll(FutureArg<0>(&recvFuture)));
    ASSERT_TRUE(client2->Send(killMsg).Get(3000).IsSome());

    auto recv = recvFuture.Get(3000);
    ASSERT_TRUE(recv.IsSome());
    EXPECT_EQ(recv.Get()->body_case(), StreamingMessage::kKillRsp);
    EXPECT_EQ(recv.Get()->killrsp().code(), common::ERR_LOCAL_SCHEDULER_ABNORMAL);

    EXPECT_EQ(mockProxy_->clients.size(), static_cast<uint32_t>(1));

    client1->Stop();
    client2->Stop();
    ASSERT_AWAIT_READY(isCalled11);
    ASSERT_AWAIT_READY(isCalled2);
}

/**
 * Feature: PosixServiceTest--UpdatePosixClientTest
 * Description: test UpdateClient Callback of PosixService
 * Steps:
 * 1. runtime grpc client connect to grpc server in proxy success and will update posix to ClientProxy
 */
TEST_F(PosixServiceTest, UpdatePosixClientTest)
{
    EXPECT_CALL(*mockProxy_, MockUpdatePosixClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID, testing::_)).Times(1);
    internalIAM_->Insert(TEST_INSTANCE_ID);
    auto client = CreateRuntimeClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID);
    EXPECT_CALL(*client, MockClientClosedCallback).Times(1);

    KillRequest request;
    auto killMsg = std::make_shared<StreamingMessage>();
    *killMsg->mutable_killreq() = request;
    killMsg->set_messageid("test_message_id");
    ASSERT_AWAIT_TRUE([&]() { return mockProxy_->clients.find(TEST_INSTANCE_ID) != mockProxy_->clients.end(); });

    auto recvFuture = litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>>();
    EXPECT_CALL(*client, MockReceiver).WillOnce(DoAll(FutureArg<0>(&recvFuture)));

    auto sendRet = client->Send(killMsg).Get(3000);
    ASSERT_TRUE(sendRet.IsSome());

    auto recv = recvFuture.Get(3000);
    ASSERT_TRUE(recv.IsSome());

    EXPECT_EQ(recv.Get()->body_case(), StreamingMessage::kKillRsp);
    EXPECT_EQ(recv.Get()->messageid(), "test_message_id");
    EXPECT_EQ(recv.Get()->killrsp().code(), common::ERR_LOCAL_SCHEDULER_ABNORMAL);

    client->Stop();
}

/**
 * Feature: PosixServiceTest--duplicateClientConnect
 * Description: test duplicate client connected
 * Steps:
 * 1. runtime grpc client connect to grpc server in proxy success and will update posix to ClientProxy
 * 2. the same runtime client connection should be refused
 */
TEST_F(PosixServiceTest, DuplicateClientConnect)
{
    EXPECT_CALL(*mockProxy_, MockUpdatePosixClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID, testing::_)).Times(1);
    EXPECT_CALL(*mockProxy_, MockUpdatePosixClient(TEST_INSTANCE_ID, "TEST_RUNTIME_ID_ACCEPT", testing::_)).Times(1);
    internalIAM_->Insert(TEST_INSTANCE_ID);
    auto client = CreateRuntimeClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID);
    litebus::Future<bool> isCalled1;
    EXPECT_CALL(*client, MockClientClosedCallback).Times(1).WillOnce(Assign(&isCalled1, true));
    EXPECT_CALL(*client, MockReceiver).WillOnce(Return());

    KillRequest request;
    auto killMsg = std::make_shared<StreamingMessage>();
    *killMsg->mutable_killreq() = request;
    killMsg->set_messageid("test_message_id");
    auto sendRet = client->Send(killMsg);
    ASSERT_AWAIT_READY(sendRet);
    EXPECT_EQ(sendRet.Get(), true);

    auto clientDuplicate = CreateRuntimeClient(TEST_INSTANCE_ID, "TEST_RUNTIME_ID_DUPLICATE");
    litebus::Promise<bool> promise;
    EXPECT_CALL(*clientDuplicate, MockClientClosedCallback).WillOnce(DoAll(Invoke([promise]() {
        promise.SetValue(true);
    })));

    sendRet = clientDuplicate->Send(killMsg);
    ASSERT_AWAIT_READY(promise.GetFuture());
    EXPECT_EQ(promise.GetFuture().Get(), true);

    client->Stop();
    ASSERT_AWAIT_READY(isCalled1);
    auto clientAccept = CreateRuntimeClient(TEST_INSTANCE_ID, "TEST_RUNTIME_ID_ACCEPT");
    EXPECT_CALL(*clientAccept, MockClientClosedCallback).Times(1);
    litebus::Promise<bool> recv;
    EXPECT_CALL(*clientAccept, MockReceiver)
        .WillOnce(DoAll(Invoke([recv](const std::shared_ptr<StreamingMessage> &msg) {
            recv.SetValue(true);
            EXPECT_EQ(msg->body_case(), StreamingMessage::kKillRsp);
            EXPECT_EQ(msg->messageid(), "test_message_id");
            EXPECT_EQ(msg->killrsp().code(), common::ERR_LOCAL_SCHEDULER_ABNORMAL);
        })));
    sendRet = clientAccept->Send(killMsg);
    ASSERT_AWAIT_READY(recv.GetFuture());
    EXPECT_EQ(recv.GetFuture().Get(), true);

    clientAccept->Stop();
}

}  // namespace functionsystem::test