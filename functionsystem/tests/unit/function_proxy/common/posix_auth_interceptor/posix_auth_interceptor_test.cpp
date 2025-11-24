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
#include "function_proxy/common/posix_auth_interceptor/posix_auth_interceptor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/posix_service/posix_service.h"
#include "common/rpc/server/common_grpc_server.h"
#include "common/utils/files.h"
#include "function_proxy/common/iam/internal_iam.h"
#include "mocks/mock_internal_iam.h"
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
}  // namespace

class MockClientProxy {
public:
    MOCK_METHOD(void, MockUpdatePosixClient,
                (const std::string &instanceID, const std::string &runtimeID,
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

class PosixAuthInterceptorTest : public ::testing::Test {
public:
    inline static uint16_t grpcServerPort_;
    [[maybe_unused]] static void SetUpTestSuite()
    {
        grpcServerPort_ = functionsystem::test::FindAvailablePort();
    }

    void SetUp() override
    {
        InternalIAM::Param param;
        param.isEnableIAM = true;
        param.credType = IAMCredType::AK_SK;
        internalIAM_ = std::make_shared<MockInternalIAM>(param);
        std::shared_ptr<::grpc::ServerCredentials> creds = ::grpc::InsecureServerCredentials();

        CommonGrpcServerConfig serverConfig;
        serverConfig.ip = GRPC_SERVER_IP;
        serverConfig.listenPort = std::to_string(grpcServerPort_);
        serverConfig.creds = creds;
        server_ = std::make_shared<CommonGrpcServer>(serverConfig);
        posixService_ = std::make_shared<PosixService>();
        posixService_->BindInternalIAM(internalIAM_);
        server_->RegisterService(posixService_);
        server_->Start();
        ASSERT_TRUE(server_->WaitServerReady());

        mockProxy_ = std::make_shared<MockClientProxy>();
        posixService_->RegisterUpdatePosixClientCallback(std::bind(&MockClientProxy::FakeUpdatePosixClient, mockProxy_,
                                                                   std::placeholders::_1, std::placeholders::_2,
                                                                   std::placeholders::_3));
    }

    void TearDown() override
    {
        server_ = nullptr;
        posixService_ = nullptr;
        internalIAM_ = nullptr;
        PosixService::DeleteClient(TEST_INSTANCE_ID);
    }

    std::shared_ptr<MockRuntimeClient> CreateRuntimeClient(const std::string &instanceID, const std::string &runtimeID)
    {
        RuntimeClientConfig config;
        config.serverAddress = GRPC_SERVER_IP + ":" + std::to_string(grpcServerPort_);
        config.serverName = "server";
        config.runtimeID = runtimeID;
        config.instanceID = instanceID;
        auto client = std::make_shared<MockRuntimeClient>(config);
        client->Start();
        return client;
    }

protected:
    std::shared_ptr<CommonGrpcServer> server_{ nullptr };
    std::shared_ptr<PosixService> posixService_{ nullptr };
    std::shared_ptr<MockInternalIAM> internalIAM_{ nullptr };
    std::shared_ptr<MockClientProxy> mockProxy_{ nullptr };
};

TEST_F(PosixAuthInterceptorTest, VerifyAKSKTest)
{
    KillRequest request;
    auto killMsg = std::make_shared<StreamingMessage>();
    *killMsg->mutable_killreq() = request;
    killMsg->set_messageid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    internalIAM_->Insert(TEST_INSTANCE_ID);
    EXPECT_CALL(*internalIAM_, IsIAMEnabled).WillRepeatedly(Return(true));
    EXPECT_CALL(*internalIAM_, GetCredType).WillRepeatedly(Return(IAMCredType::AK_SK));

    EXPECT_CALL(*mockProxy_, MockUpdatePosixClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID, testing::_)).Times(1);
    litebus::Future<std::shared_ptr<AKSKContent>> akSkFuture;
    auto akSkContent = std::make_shared<AKSKContent>();
    akSkContent->tenantID = "tenantID";
    akSkContent->accessKey = "ak";
    akSkContent->secretKey = "sk";
    akSkContent->dataKey = "dk";
    auto now = static_cast<uint64_t>(std::time(nullptr));
    akSkContent->expiredTimeStamp = now + 10000;
    akSkFuture.SetValue(akSkContent);
    EXPECT_CALL(*internalIAM_, RequireCredentialByAK)
        .WillOnce(Return(akSkFuture))
        .WillOnce(Return(akSkFuture))
        .WillOnce(Return(akSkFuture));

    auto client = CreateRuntimeClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID);
    EXPECT_CALL(*client, MockClientClosedCallback).Times(1);

    auto recvFuture = litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>>();
    EXPECT_CALL(*client, MockReceiver).WillOnce(DoAll(FutureArg<0>(&recvFuture)));
    ASSERT_TRUE(client->Send(killMsg).Get(3000).IsSome());

    ASSERT_AWAIT_READY(recvFuture);
    ASSERT_TRUE(recvFuture.IsOK());
    EXPECT_EQ(recvFuture.Get()->body_case(), StreamingMessage::kKillRsp);
    EXPECT_EQ(recvFuture.Get()->killrsp().code(), common::ERR_LOCAL_SCHEDULER_ABNORMAL);

    EXPECT_EQ(mockProxy_->clients.size(), static_cast<uint32_t>(1));

    client->Stop();
}

TEST_F(PosixAuthInterceptorTest, VerifyAKSKFailedTest)
{
    KillRequest request;
    auto killMsg = std::make_shared<StreamingMessage>();
    *killMsg->mutable_killreq() = request;
    killMsg->set_messageid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    internalIAM_->Insert(TEST_INSTANCE_ID);
    EXPECT_CALL(*internalIAM_, IsIAMEnabled).WillRepeatedly(Return(true));
    EXPECT_CALL(*internalIAM_, GetCredType).WillRepeatedly(Return(IAMCredType::AK_SK));

    EXPECT_CALL(*mockProxy_, MockUpdatePosixClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID, testing::_)).Times(1);
    litebus::Future<std::shared_ptr<AKSKContent>> akSkFuture;
    auto akSkContent = std::make_shared<AKSKContent>();
    akSkContent->tenantID = "tenantID";
    akSkContent->accessKey = "ak";
    akSkContent->secretKey = "sk";
    akSkContent->dataKey = "dk";
    auto now = static_cast<uint64_t>(std::time(nullptr));
    akSkContent->expiredTimeStamp = now + 10000;
    akSkFuture.SetValue(akSkContent);

    litebus::Future<std::shared_ptr<AKSKContent>> fakeAkSkFuture;
    auto fakeAkSkContent = std::make_shared<AKSKContent>();
    fakeAkSkContent->tenantID = "tenantID";
    fakeAkSkContent->accessKey = "ak";
    fakeAkSkContent->secretKey = "fake";
    fakeAkSkContent->dataKey = "dk";
    fakeAkSkContent->expiredTimeStamp = now + 10000;
    fakeAkSkFuture.SetValue(fakeAkSkContent);
    EXPECT_CALL(*internalIAM_, RequireCredentialByAK)
        .WillOnce(Return(akSkFuture))
        .WillOnce(Return(akSkFuture))
        .WillOnce(Return(akSkFuture))
        .WillOnce(Return(fakeAkSkFuture));

    auto client = CreateRuntimeClient(TEST_INSTANCE_ID, TEST_RUNTIME_ID);
    EXPECT_CALL(*client, MockClientClosedCallback).Times(1);

    auto recvFuture = litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>>();
    EXPECT_CALL(*client, MockReceiver).WillOnce(DoAll(FutureArg<0>(&recvFuture)));
    ASSERT_TRUE(client->Send(killMsg).Get(3000).IsSome());

    ASSERT_AWAIT_READY(recvFuture);
    ASSERT_TRUE(recvFuture.IsOK());
    EXPECT_EQ(recvFuture.Get()->body_case(), StreamingMessage::kKillRsp);
    EXPECT_EQ(recvFuture.Get()->killrsp().code(), common::ERR_LOCAL_SCHEDULER_ABNORMAL);
    EXPECT_EQ(mockProxy_->clients.size(), static_cast<uint32_t>(1));

    auto status = client->Send(killMsg);
    ASSERT_AWAIT_READY(status);

    client->Stop();
}
}  // namespace functionsystem::test