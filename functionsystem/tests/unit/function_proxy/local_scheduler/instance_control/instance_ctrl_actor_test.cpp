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
#include "constants.h"
#include "logs/logging.h"
#include "function_proxy/local_scheduler/instance_control/instance_ctrl_actor.h"

#include <gtest/gtest.h>

#include "common/constants/signal.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "metrics/metrics_adapter.h"
#include "http/http_server.h"
#include "rpc/server/common_grpc_server.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"
#include "files.h"
#include "common/utils/struct_transfer.h"
#include "hex/hex.h"
#include "function_proxy/common/posix_client/shared_client/posix_stream_manager_proxy.h"
#include "function_proxy/common/posix_client/shared_client/shared_client_manager.h"
#include "httpd/http.hpp"
#include "mocks/mock_function_agent_mgr.h"
#include "mocks/mock_instance_control_view.h"
#include "mocks/mock_instance_state_machine.h"
#include "mocks/mock_observer.h"
#include "mocks/mock_runtime_client.h"
#include "mocks/mock_cloud_api_gateway.h"
#include "mocks/mock_scheduler.h"
#include "mocks/mock_local_sched_srv.h"
#include "mocks/mock_instance_control_view.h"
#include "mocks/mock_shared_client.h"
#include "mocks/mock_shared_client_manager_proxy.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace functionsystem::local_scheduler;
using namespace functionsystem::grpc;
using namespace functionsystem::function_proxy;
using namespace testing;
namespace {
const std::string RESOURCE_DIR = "/home/lwy/sn/resource";
const std::string A_TXT_CONTENT =
    "f48f9d5a9706088947ac438ebe005aa26c9370579f2231c538b28894a315562182da0eb18002c86728c4cdc0df5efb19e1c2060e93370fd891d4f3d9e5b2b61376643f86d0210ce996446a985759b15112037a5a2f6463cf5fd6afc7ff30fe814bf960eb0c16c5059407c74d6a93a8b3110405cbc935dff672da3b648d62e0d5cecd91bc7063211e6b33210afb6899e8322eabffe167318a5ac5d591aa7579efd37e9e4c7fcf390e97c1151b7c1bf00b4a18764a1a0cac1fda1ea6389b39d755127f0e5bc072e6d5936738be1585535dc63b71ad58686f71c821325009de36bdbac31c1c044845bd1bb41230ec9815695ef3f9e7143a16410113ff3286147a76";
const std::string B_TXT_CONTENT =
    "5d3da9f432be72b34951c737053eb2c816aaccae2b390d092046288aa5ce2cc5b16529f8197de316303735fbc0c041ccc3885b9be5fef4933b6806febb940b6bb609b3bf1d1501110e3ba62c6d8b2cf4388a08a8e123a3cea96daec619fbca177bdf092461f5701b02e5af83ddf0f6ce40deb279cda3ec7d6805237d229e26e30555f3dd890b7306b42bdef0ca1f963dbe25cd00d75018ab3216fcd3b7002b8a493d015306bf264cca12718890ef11c8d9e54721ebd6bdecab6c7084442f45611f249d9b5d703414770a46380d0b97c0187185241e9b6187c8168414370649fe6e7afef83a0df645424c4b6c0631dc3ef50c30af37eda905a1886ca12474c68a";
const std::string D_TXT_CONTENT =
    "37a1b37efbb9bb6beadb4446f40aa2c4bcaeb298192fa390ed03ee65bfcd54e55da39bae9961b9fa0d4b89591e41eed835ed01cca315eab75ebaf8a9e7b02287a468ec6d0c61f9f8e4d58dad90fb8a6a13bee7fe4685dbb535bfdb7e76b328d66b4d4bc7aa48791b205d1d2f2ef176f2b5b80a8ddc34ed9514372130eb896bc18745facf059a7fa37ef5e2ef413d0030f5bca581055eb3b3565dca642651cb802530e2e4964ab3c8a37370adfd65c80483398a1a8668caed455deabae0dbae7fb2bcdeeee4c2a2d9431ed93c6527985ef684127691904c799e13f37daeb1cb7ebfb0904d61796362514e521ac0fed682fd952ca3e9ce9a7a4407aaaa44f8aab6";
const std::string E_TXT_CONTENT =
    "43b0d158d9dcf4ffd416eb4e6a89d1b7a66d595c43329bb5c1c66d5befe33c37f31da53aaf539e43238457c46e1f28339cb9dda461c71c0ea2dba3dc8006684ff0d8d59ee2192582983c155e400d5b7cadcb65bbe682e61d175af54549796e447f3174b95f1f50998ae7785b5c0c359746e1ee6eeb989284fbe9e0f801ce5a7267285afbab7694c0e8434d6b86991298a46039de4d1fbfd824b8337b11c2d0b2f30ed4d46312e315cd9042abddc09ea73169f9e1f5baa496d44ed5cac9659cab076212499ef09a56db69e7444d665195a0562a7c82d176d027b0ecc7f4a26215e003fd463bf3911633baf85ee98f9187357a65ee2869b3d93a3871d830b4034e";

const std::string TEST_TENANT_ID = "TEST_TENANT_ID";
const std::string TEST_TENANT_ID_2 = "TEST_TENANT_ID_2";
const std::string TEST_USER_ID = "TEST_USER_ID";
const std::string TEST_USER_ID_2 = "TEST_USER_ID_2";
const std::string TEST_INSTANCE_ID = "TEST_INSTANCE_ID";
const std::string TEST_INSTANCE_ID_2 = "TEST_INSTANCE_ID_2";
const std::string TEST_REQUEST_ID = "TEST_REQUEST_ID";
const std::string TEST_RUNTIME_ID = "TEST_RUNTIME_ID";
const std::string TEST_NODE_ID = "TEST_NODE_ID";
const std::string GRPC_SERVER_IP = "127.0.0.1";

const std::string HTTP_SERVER_NAME = "v3.0";

// OS path
const std::string OIDC_TOKEN_DIR = "/var/run/secrets/tokens/";
const std::string OIDC_TOKEN_PATH = "/var/run/secrets/tokens/oidc-token";
const std::string MOCK_OIDC_TOKEN_CONTENT = "test_oidc_token";
const std::string MOCK_IAM_TOKEN = "mock-iam-token";

// sub-pub
const std::string SUBSCRIBER_ID = "subscriber";
const std::string PUBLISHER_ID = "publisher";

}

class MockUtilClass {
public:
    MOCK_METHOD(void, MockUserCallback, ());

    void FakeUserCallback()
    {
        std::cout << "FakeUserCallback" << std::endl;
        MockUserCallback();
    }
};

class InstanceCtrlActorTest : public ::testing::Test {
public:
    inline static std::string metaStoreServerHost_;
    inline static uint16_t grpcServerPort_;
    static void SetUpTestCase()
    {
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        grpcServerPort_ = functionsystem::test::FindAvailablePort();
    }

    void SetUp() override
    {
        auto applePath = RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION + "/" + APPLE;
        auto boyPath = RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION + "/" + BOY;
        auto dogPath = RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION + "/" + DOG;
        auto eggPath = RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION + "/" + EGG;
        if (FileExists(applePath) && FileExists(boyPath) && FileExists(dogPath) && FileExists(eggPath)) {
            isResourceExisted_ = true;
        }
        if (!isResourceExisted_) {
            EXPECT_TRUE(litebus::os::Mkdir(applePath).IsNone());
            TouchFile(litebus::os::Join(applePath, A_TXT));
            EXPECT_TRUE(Write(litebus::os::Join(applePath, A_TXT), A_TXT_CONTENT));

            EXPECT_TRUE(litebus::os::Mkdir(boyPath).IsNone());
            TouchFile(litebus::os::Join(boyPath, B_TXT));
            EXPECT_TRUE(Write(litebus::os::Join(boyPath, B_TXT), B_TXT_CONTENT));

            EXPECT_TRUE(litebus::os::Mkdir(dogPath).IsNone());
            TouchFile(litebus::os::Join(dogPath, D_TXT));
            EXPECT_TRUE(Write(litebus::os::Join(dogPath, D_TXT), D_TXT_CONTENT));

            EXPECT_TRUE(litebus::os::Mkdir(eggPath).IsNone());
            TouchFile(litebus::os::Join(eggPath, E_TXT));
            EXPECT_TRUE(Write(litebus::os::Join(eggPath, E_TXT), E_TXT_CONTENT));
        }

        httpServer_ = std::make_shared<HttpServer>(HTTP_SERVER_NAME);
        cloudApiGateway_ = std::make_shared<MockCloudApiGateway>("mock-iam");
        httpServer_->RegisterRoute(cloudApiGateway_);
        litebus::Spawn(cloudApiGateway_);
        litebus::Spawn(httpServer_);

        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        auto metaStorageAccessor = std::make_shared<MetaStorageAccessor>(metaStoreClient_);

        CommonGrpcServerConfig serverConfig;
        serverConfig.ip = GRPC_SERVER_IP;
        serverConfig.listenPort= std::to_string(grpcServerPort_);
        serverConfig.creds = ::grpc::InsecureServerCredentials();
        server_ = std::make_shared<CommonGrpcServer>(serverConfig);
        posixService_ = std::make_shared<PosixService>();
        server_->RegisterService(posixService_);
        server_->Start();
        ASSERT_TRUE(server_->WaitServerReady());

        sharedClientManager_ = std::make_shared<SharedClientManager>("SharedClientManager");
        litebus::Spawn(sharedClientManager_);
        posixStreamManagerProxy_ = std::make_shared<PosixStreamManagerProxy>(sharedClientManager_->GetAID());
        posixService_->RegisterUpdatePosixClientCallback(std::bind(
            &PosixStreamManagerProxy::UpdateControlInterfacePosixClient, posixStreamManagerProxy_,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        mockFunctionAgentMgr_ = std::make_shared<MockFunctionAgentMgr>("MOCK_FUNCTION_AGENT_MGR", metaStoreClient_);
        mockObserver_ = std::make_shared<MockObserver>();
        mockInstanceCtrlView_ = std::make_shared<MockInstanceControlView>("TEST_NODE_ID");

        RuntimeConfig runtimeConfig;
        runtimeConfig.runtimeHeartbeatEnable = "true";
        runtimeConfig.runtimeMaxHeartbeatTimeoutTimes = 3;
        runtimeConfig.runtimeHeartbeatTimeoutMS = 2000;
        runtimeConfig.runtimeInitCallTimeoutMS = 3000;
        runtimeConfig.runtimeDsAuthEnable = true;
        runtimeConfig.runtimeDsEncryptEnable = true;
        runtimeConfig.dataSystemAccessKey = "Test-DataSystem-AccessKey";
        runtimeConfig.dataSystemSecurityKey = "Test-DataSystem-SecurityKey";
        runtimeConfig.runtimeDsServerPublicKey = "testDsServerPublicKey";
        runtimeConfig.runtimeDsClientPrivateKey = "testDsClientPrivateKey";
        runtimeConfig.runtimeDsClientPublicKey = "testDsClientPublicKey";

        instanceCtrlConfig_.runtimeConfig = runtimeConfig;

        instanceCtrlActor_ = std::make_shared<InstanceCtrlActor>("TEST_INSTANCE_CTRL_ACTOR_NAME", "TEST_NODE_ID",
                                                                 instanceCtrlConfig_);

        instanceCtrlActor_->BindMetaStoreClient(metaStoreClient_);
        instanceCtrlActor_->BindFunctionAgentMgr(mockFunctionAgentMgr_);
        instanceCtrlActor_->BindObserver(mockObserver_);
        instanceCtrlActor_->BindInstanceControlView(mockInstanceCtrlView_);
        instanceCtrlActor_->BindControlInterfaceClientManager(posixStreamManagerProxy_);
        litebus::Spawn(instanceCtrlActor_);

    }

    void TearDown() override
    {
        std::cout << "TearDown" << std::endl;
        server_ = nullptr;
        posixService_ = nullptr;
        litebus::Terminate(instanceCtrlActor_->GetAID());
        litebus::Await(instanceCtrlActor_);
        instanceCtrlActor_= nullptr;
        litebus::Terminate(sharedClientManager_->GetAID());
        litebus::Await(sharedClientManager_);
        sharedClientManager_ = nullptr;
        if (!isResourceExisted_) {
            EXPECT_TRUE(litebus::os::Rmdir(RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION).IsNone());
        }
        metaStoreClient_ = nullptr;
        posixStreamManagerProxy_ = nullptr;
        mockInstanceCtrlView_ = nullptr;
        mockObserver_ = nullptr;
        mockFunctionAgentMgr_ = nullptr;
        etcdSrvDriver_->StopServer();
        etcdSrvDriver_ = nullptr;

        litebus::Terminate(cloudApiGateway_->GetAID());
        litebus::Await(cloudApiGateway_);
        litebus::Terminate(httpServer_->GetAID());
        litebus::Await(httpServer_);
        httpServer_ = nullptr;
        cloudApiGateway_ = nullptr;
    }

    std::shared_ptr<MockRuntimeClient> CreateRuntimeClient(const std::string &instanceID, const std::string &runtimeID,
                                                           const std::string &token)
    {
        std::shared_ptr<::grpc::ChannelCredentials> creds = ::grpc::InsecureChannelCredentials();
        RuntimeClientConfig config;
        config.serverAddress = GRPC_SERVER_IP + ":" + std::to_string(grpcServerPort_);
        config.runtimeID = runtimeID;
        config.instanceID = instanceID;
        config.token = token;
        config.creds = creds;
        auto client = std::make_shared<MockRuntimeClient>(config);
        client->Start();
        return client;
    }

protected:
    InstanceCtrlConfig instanceCtrlConfig_;
    bool isResourceExisted_{ false };
    std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    std::shared_ptr<InstanceCtrlActor> instanceCtrlActor_;
    std::shared_ptr<PosixStreamManagerProxy> posixStreamManagerProxy_;
    std::shared_ptr<SharedClientManager> sharedClientManager_;
    std::shared_ptr<CommonGrpcServer> server_;
    std::shared_ptr<PosixService> posixService_;
    std::shared_ptr<MockFunctionAgentMgr> mockFunctionAgentMgr_;
    std::shared_ptr<MockObserver> mockObserver_;
    std::shared_ptr<MockInstanceControlView> mockInstanceCtrlView_;
    std::shared_ptr<MockCloudApiGateway> cloudApiGateway_;
    std::shared_ptr<HttpServer> httpServer_;
};

TEST_F(InstanceCtrlActorTest, DeployAppDriver)
{
    auto request = std::make_shared<messages::ScheduleRequest>();
    request->mutable_instance()->set_instanceid(TEST_INSTANCE_ID);
    request->mutable_instance()->mutable_createoptions()->insert({ APP_ENTRYPOINT, "runtimeEnv.pip" });
    request->mutable_instance()->mutable_createoptions()->insert({ "POST_START_EXEC", "pythons script.py" });
    request->mutable_instance()->mutable_createoptions()->insert({ "DELEGATE_ENV_VAR", "runtimeEnv.env_vars" });
    request->mutable_instance()->mutable_createoptions()->insert({ "USER_PROVIDED_METADATA", "{\"task_id\":\"taskId1\",\"task\":\"task\"}" });
    request->mutable_instance()->mutable_createoptions()->insert({ "DELEGATE_DOWNLOAD", "{\"storage_type\":\"working_dir\",\"code_path\":\"file:///home/xxx/xxx.zip\"}" });

    messages::DeployInstanceResponse deployResp;
    deployResp.set_code(0);
    deployResp.set_pid(33333);

    // state machine is nullptr
    auto status = instanceCtrlActor_->UpdateInstance(deployResp, request, 0);
    EXPECT_EQ(status.Get().StatusCode(), StatusCode::ERR_INNER_SYSTEM_ERROR);

    auto mockInstanceStateMachine = std::make_shared<MockInstanceStateMachine>("TEST_PROXY_ID");
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(mockInstanceStateMachine));

    InstanceInfo ins;
    ins.set_instanceid(TEST_INSTANCE_ID);
    ins.mutable_instancestatus()->set_code(1);
    EXPECT_CALL(*mockInstanceStateMachine, GetInstanceInfo).WillRepeatedly(Return(ins));
    EXPECT_CALL(*mockInstanceStateMachine, GetCancelFuture).WillRepeatedly(Return(litebus::Future<std::string>()));

    status = instanceCtrlActor_->UpdateInstance(deployResp, request, 0);
    EXPECT_TRUE(request->instance().extensions().find(PID) != request->instance().extensions().end());
    EXPECT_EQ(request->instance().extensions().find(PID)->second, "33333");
    EXPECT_TRUE(instanceCtrlActor_->runtimeHeartbeatTimers_.find(TEST_INSTANCE_ID) == instanceCtrlActor_->runtimeHeartbeatTimers_.end());
    EXPECT_TRUE(instanceCtrlActor_->concernedInstance_.find(TEST_INSTANCE_ID) != instanceCtrlActor_->concernedInstance_.end());
}

TEST_F(InstanceCtrlActorTest, StopAppDriver)
{
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(3);
    scheduleReq->mutable_instance()->set_function(function);
    auto context = std::make_shared<InstanceContext>(scheduleReq);
    auto killContext = std::make_shared<KillContext>();
    killContext->instanceContext = context;
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(APP_STOP_SIGNAL);
    killContext->killRequest = killReq;

    // if signalRoute failed
    killContext->killRsp.set_code(common::ErrorCode::ERR_PARAM_INVALID);
    EXPECT_EQ(instanceCtrlActor_->StopAppDriver(killContext).Get().code(), common::ErrorCode::ERR_PARAM_INVALID);

    // if instance in remote node, will call ForwardKillToInstanceManager
    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    instanceCtrlActor_->localSchedSrv_ = localSchedSrv;
    messages::ForwardKillResponse forwardKillResponse;
    forwardKillResponse.set_code(common::ErrorCode::ERR_NONE);
    EXPECT_CALL(*localSchedSrv, ForwardKillToInstanceManager).WillOnce(DoAll(Invoke([&](
        const std::shared_ptr<messages::ForwardKillRequest> &req) {
        EXPECT_EQ(req->req().signal(), APP_STOP_SIGNAL);
        return forwardKillResponse;
    })));
    EXPECT_CALL(*mockObserver_, DelInstance).WillRepeatedly(Return(Status::OK()));
    killContext->killRsp.set_code(common::ErrorCode::ERR_NONE);
    killContext->isLocal = false;
    instanceCtrlActor_->StopAppDriver(killContext);

    // if instance is local, will call SetInstanceFatal
    killContext->isLocal = true;
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillOnce(Return(nullptr));
    instanceCtrlActor_->StopAppDriver(killContext);
}

TEST_F(InstanceCtrlActorTest, SetTenantAffinityOpt_instance)
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    auto instanceInfo = scheduleReq->mutable_instance();
    instanceInfo->set_tenantid("testTenant");
    auto instanceAffinity = instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_instance();

    // Case0: without "RequiredAntiAffinity" labels then add
    instanceCtrlActor_->SetTenantAffinityOpt(scheduleReq);
    EXPECT_TRUE(scheduleReq->instance().scheduleoption().affinity().inner().tenant().has_preferredaffinity());
    EXPECT_TRUE(scheduleReq->instance().scheduleoption().affinity().inner().tenant().has_requiredantiaffinity());

    // Case1: user affinity
    instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_instance()->clear_requiredantiaffinity();
    instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_instance()->clear_preferredaffinity();
    (*instanceAffinity->mutable_requiredantiaffinity()) = Selector(false, { { Exist("key1") } });
    (*instanceAffinity->mutable_preferredaffinity()) = Selector(true, { { Exist("key1") } });

    instanceCtrlActor_->SetTenantAffinityOpt(scheduleReq);
    auto requiredTenantAntiAffinity = instanceInfo->scheduleoption().affinity().inner().tenant().requiredantiaffinity();
    EXPECT_EQ(requiredTenantAntiAffinity.condition().subconditions(0).expressions(0).key(), TENANT_ID);
    EXPECT_EQ(requiredTenantAntiAffinity.condition().subconditions(0).expressions(0).op().notin().values(0), "testTenant");
    EXPECT_EQ(requiredTenantAntiAffinity.condition().subconditions(0).expressions(1).key(), TENANT_ID);
    auto requiredAntiAffinity = instanceAffinity->requiredantiaffinity();
    EXPECT_EQ(requiredAntiAffinity.condition().subconditions(0).expressions(0).key(), "key1");

    auto preferredTenantAffinity = instanceInfo->scheduleoption().affinity().inner().tenant().preferredaffinity();
    YRLOG_DEBUG("preferredTenantAffinity: {}", preferredTenantAffinity.DebugString());
    EXPECT_EQ(preferredTenantAffinity.condition().subconditions(0).expressions(0).key(), TENANT_ID);
    EXPECT_EQ(preferredTenantAffinity.condition().subconditions(0).expressions(0).op().in().values(0), "testTenant");
    EXPECT_EQ(preferredTenantAffinity.condition().subconditions(0).weight(), 100);
    auto preferredAffinity = instanceAffinity->preferredaffinity();
    EXPECT_EQ(preferredAffinity.condition().subconditions(0).expressions(0).key(), "key1");
    EXPECT_EQ(preferredAffinity.condition().subconditions(0).weight(), 100);

    // Case2: Conflicting tenant Labels from user
    instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_instance()->clear_requiredantiaffinity();
    instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_instance()->clear_preferredaffinity();
    (*instanceAffinity->mutable_requiredantiaffinity()) = Selector(false, { { NotExist(TENANT_ID) } });
    (*instanceAffinity->mutable_preferredaffinity()) = Selector(true, { { Exist(TENANT_ID) }, { In("key4", { "value4" }) } , { NotIn(TENANT_ID, { "value4" })}, { NotIn("key5", { "value5" })} });
    instanceCtrlActor_->SetTenantAffinityOpt(scheduleReq);

    requiredTenantAntiAffinity = instanceInfo->scheduleoption().affinity().inner().tenant().requiredantiaffinity();
    YRLOG_DEBUG("requiredTenantAntiAffinity2: {}", requiredTenantAntiAffinity.DebugString());
    EXPECT_EQ(requiredTenantAntiAffinity.condition().subconditions(0).expressions_size(), 2);
    EXPECT_EQ(requiredTenantAntiAffinity.condition().subconditions(0).expressions(0).key(), TENANT_ID);
    EXPECT_EQ(requiredTenantAntiAffinity.condition().subconditions(0).expressions(0).op().notin().values(0), "testTenant");
    EXPECT_EQ(requiredTenantAntiAffinity.condition().subconditions(0).expressions(1).key(), TENANT_ID);

    preferredTenantAffinity = instanceInfo->scheduleoption().affinity().inner().tenant().preferredaffinity();
    YRLOG_DEBUG("preferredAffinity2: {}", preferredAffinity.DebugString());
    EXPECT_EQ(preferredTenantAffinity.condition().subconditions_size(), 1);
    EXPECT_EQ(preferredTenantAffinity.condition().subconditions(0).expressions(0).key(), TENANT_ID);
    EXPECT_EQ(preferredTenantAffinity.condition().subconditions(0).expressions(0).op().in().values(0), "testTenant");
    EXPECT_EQ(preferredTenantAffinity.condition().subconditions(0).weight(), 100);

    preferredAffinity = instanceAffinity->preferredaffinity();
    EXPECT_EQ(preferredAffinity.condition().subconditions_size(), 2);
    EXPECT_EQ(preferredAffinity.condition().subconditions(0).expressions(0).key(), "key4");
    EXPECT_EQ(preferredAffinity.condition().subconditions(0).expressions(0).op().in().values(0), "value4");
    EXPECT_EQ(preferredAffinity.condition().subconditions(0).weight(), 100);
    EXPECT_EQ(preferredAffinity.condition().subconditions(1).expressions(0).key(), "key5");
    EXPECT_EQ(preferredAffinity.condition().subconditions(1).expressions(0).op().notin().values(0), "value5");
    EXPECT_EQ(preferredAffinity.condition().subconditions(1).weight(), 90);
}

TEST_F(InstanceCtrlActorTest, SetTenantAffinityOpt_resource)
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    auto instanceInfo = scheduleReq->mutable_instance();
    instanceInfo->set_tenantid("testTenant");
    auto resourceAffinity = instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_resource();

    // Case0: "without "RequiredAntiAffinity" labels
    instanceCtrlActor_->SetTenantAffinityOpt(scheduleReq);
    EXPECT_FALSE(scheduleReq->instance().scheduleoption().affinity().resource().has_requiredantiaffinity());
    EXPECT_FALSE(scheduleReq->instance().scheduleoption().affinity().resource().has_requiredaffinity());

    // Case1: user affinity
    instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_resource()->clear_preferredaffinity();
    instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_resource()->clear_requiredantiaffinity();
    (*resourceAffinity->mutable_requiredantiaffinity()) = Selector(false, { {Exist("key1")} });
    (*resourceAffinity->mutable_preferredaffinity()) = Selector(true, {{Exist("key1")}});

    instanceCtrlActor_->SetTenantAffinityOpt(scheduleReq);

    auto requiredAntiAffinity = resourceAffinity->requiredantiaffinity();
    YRLOG_DEBUG("requiredAntiAffinity: {}", requiredAntiAffinity.DebugString());
    for (auto subcondition : requiredAntiAffinity.condition().subconditions()) {
        for (auto expression : subcondition.expressions()) {
            EXPECT_NE(expression.key(), TENANT_ID);
        }
    }

    auto preferredAffinity = resourceAffinity->preferredaffinity();
    YRLOG_DEBUG("preferredAffinity: {}", preferredAffinity.DebugString());
    for (auto subcondition : preferredAffinity.condition().subconditions()) {
        for (auto expression : subcondition.expressions()) {
            EXPECT_NE(expression.key(), TENANT_ID);
        }
    }

    // Case2: Conflicting tenant Labels from user
    instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_resource()->clear_preferredaffinity();
    instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_resource()->clear_requiredantiaffinity();
    (*resourceAffinity->mutable_requiredantiaffinity()) = Selector(false, { {NotExist(TENANT_ID)} });
    (*resourceAffinity->mutable_preferredaffinity()) = Selector(true,
        {{Exist(TENANT_ID)}, {In("key4", {"value4"})}, {NotIn(TENANT_ID, {"value4"})}, {NotIn("key5", {"value5"})}});

    instanceCtrlActor_->SetTenantAffinityOpt(scheduleReq);

    requiredAntiAffinity = resourceAffinity->requiredantiaffinity();
    YRLOG_DEBUG("requiredAntiAffinity2: {}", requiredAntiAffinity.DebugString());
    for (auto subcondition : requiredAntiAffinity.condition().subconditions()) {
        for (auto expression : subcondition.expressions()) {
            EXPECT_NE(expression.key(), TENANT_ID);
        }
    }

    preferredAffinity = resourceAffinity->preferredaffinity();
    YRLOG_DEBUG("preferredAffinity2: {}", preferredAffinity.DebugString());
    for (auto subcondition : preferredAffinity.condition().subconditions()) {
        for (auto expression : subcondition.expressions()) {
            EXPECT_NE(expression.key(), TENANT_ID);
        }
    }
}

TEST_F(InstanceCtrlActorTest, SetTenantAffinityOpt_label)
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    auto instanceInfo = scheduleReq->mutable_instance();
    instanceInfo->set_tenantid("testTenant");
    auto labels = instanceInfo->mutable_labels();

    instanceInfo->mutable_labels()->Add("label-1");
    instanceInfo->mutable_labels()->Add(TENANT_ID + ":tenantA");
    instanceInfo->mutable_labels()->Add("label-2");

    instanceCtrlActor_->SetTenantAffinityOpt(scheduleReq);

    for (auto label = labels->begin(); label != labels->end(); ++label) {
        EXPECT_NE(*label, TENANT_ID);
    }
}


TEST_F(InstanceCtrlActorTest, SetInstanceBillingContext)
{
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetEnabledInstruments({functionsystem::metrics::YRInstrument::YR_INSTANCE_RUNNING_DURATION});
    std::string endPointKey = "endpoint";
    InstanceInfo ins1;
    ins1.set_instanceid(TEST_INSTANCE_ID);
    (*ins1.mutable_scheduleoption()->mutable_extension())["YR_Metrics"] = "{\"app_name\":\"app name 001\",\"endpoint\":\"127.0.0.1\",\"project_id\":\"project 001\",\"app_instance_id\":\"app instance 001\"}";
    instanceCtrlActor_->SetInstanceBillingContext(ins1);
    auto billingInstanceMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInstanceMap();
    auto it = billingInstanceMap.find(TEST_INSTANCE_ID);
    EXPECT_TRUE(it != billingInstanceMap.end());
    EXPECT_TRUE(it->second.customCreateOption.find("app_name")->second == "app name 001");
    EXPECT_TRUE(it->second.customCreateOption.find("endpoint")->second == "127.0.0.1");
    EXPECT_TRUE(it->second.customCreateOption.find("project_id")->second == "project 001");
    EXPECT_TRUE(it->second.customCreateOption.find("app_instance_id")->second == "app instance 001");
}

/**
 * SetScheduleReqConfigSuccess
 * Test Set ScheduleReq config successfully
 * Steps:
 * 1. execute SetScheduleReqFunctionAgentIDAndHeteroConfig and set ScheduleReq
 *
 * Expectations:
 * 1. set ScheduleReq successfully
 */
TEST_F(InstanceCtrlActorTest, SetScheduleReqConfigSuccess)
{
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(0);
    scheduleReq->mutable_instance()->set_function(function);
    resource_view::Resource resource;
    resource.set_name(resource_view::NPU_RESOURCE_NAME + "/"
                      + DEFAULT_NPU_PRODUCT + "/" + resource_view::HETEROGENEOUS_MEM_KEY);
    resource.set_type(resources::Value_Type_SCALAR);
    (*(scheduleReq->mutable_instance()->mutable_resources()->mutable_resources()))
        [resource_view::NPU_RESOURCE_NAME + "/"
         + DEFAULT_NPU_PRODUCT + "/" + resource_view::HETEROGENEOUS_MEM_KEY] = resource;


    ScheduleResult result;
    result.id = "agent-id-0";
    result.realIDs = {1,2,5,11};
    auto name = resource_view::NPU_RESOURCE_NAME + "/310";
    auto &vectors = result.allocatedVectors[name];
    auto &cg = (*vectors.mutable_values())[resource_view::HETEROGENEOUS_MEM_KEY];
    for (int i = 0; i< 8; i++) {
        (*cg.mutable_vectors())["uuid"].add_values(1010);
    }

    SetScheduleReqFunctionAgentIDAndHeteroConfig(scheduleReq, result);
    ASSERT_TRUE(scheduleReq->mutable_instance()->functionagentid() == "agent-id-0");
    EXPECT_EQ(scheduleReq->instance().schedulerchain().size(), 1);
    EXPECT_EQ(scheduleReq->instance().schedulerchain().Get(0), "agent-id-0");

    auto resources = scheduleReq->instance().resources().resources();
    EXPECT_EQ(resources.at(name).type(), resources::Value_Type::Value_Type_VECTORS);
    EXPECT_EQ(resources.at(name).name(), name);
    EXPECT_EQ(resources.at(name).vectors().values().at(resource_view::HETEROGENEOUS_MEM_KEY)
                  .vectors().at("uuid").values().at(0), 1010);

    ASSERT_TRUE(scheduleReq->instance().createoptions().at("func-NPU-DEVICE-IDS") == "1,2,5,11");
}

/**
 * ShutdownWithNoInstanceClient
 * Test when instance without client enters into shutdown
 * Expectations:
 * Terminate billing
 */
TEST_F(InstanceCtrlActorTest, ShutdownWithNoInstanceClient)
{
    resource_view::InstanceInfo inst;
    auto id = "Test_InstID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto id2 = "Test_ReqID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    inst.set_instanceid(id);
    inst.set_requestid(id2);

    metrics::MetricsAdapter::GetInstance().GetMetricsContext().InitBillingInstance(id, std::map<std::string, std::string>{});
    auto res = instanceCtrlActor_->ShutDownInstance(inst, 10);
    EXPECT_EQ(res.Get(), Status::OK());
    auto endTime = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInstance(id).endTimeMillis;
    YRLOG_DEBUG("EndTime is: {}", endTime);
    EXPECT_TRUE(endTime != 0);
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().EraseBillingInstance();
}

/**
 * RetryForwardSchedule
 * Test is transition version is incorrect, and retry RetryForwardSchedule
 * Expectations:
 * scheduleRep code is version wrong or others
 */
TEST_F(InstanceCtrlActorTest, RetryForwardSchedule)
{
    auto scheduleRequest = std::make_shared<messages::ScheduleRequest>();
    messages::ScheduleResponse resp;

    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    instanceCtrlActor_->localSchedSrv_ = localSchedSrv;

    messages::ScheduleResponse wrongVersionResponse;
    wrongVersionResponse.set_requestid("requestID");
    wrongVersionResponse.set_message("version is incorrect");
    wrongVersionResponse.set_code(StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);

    messages::ScheduleResponse otherResponse;
    otherResponse.set_requestid("requestID");
    otherResponse.set_message("good");
    otherResponse.set_code(StatusCode::SUCCESS);

    auto mockInstanceStateMachine = std::make_shared<MockInstanceStateMachine>("machine1");
    EXPECT_CALL(*mockInstanceStateMachine, GetCancelFuture).WillRepeatedly(Return(litebus::Future<std::string>()));
    // test other StatusCode
    EXPECT_CALL(*localSchedSrv, ForwardSchedule).WillOnce(Return(otherResponse));
    auto future = instanceCtrlActor_->RetryForwardSchedule(scheduleRequest, resp, 0, mockInstanceStateMachine);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);

    // test StatusCode is INSTANCE_TRANSACTION_WRONG_VERSION but statemachine is not exist
    auto instanceControlView = std::make_shared<MockInstanceControlView>("nodeID");
    instanceCtrlActor_->instanceControlView_ = instanceControlView;
    EXPECT_CALL(*instanceControlView, GetInstance).WillOnce(Return(nullptr));
    EXPECT_CALL(*localSchedSrv, ForwardSchedule).WillOnce(Return(wrongVersionResponse));
    future = instanceCtrlActor_->RetryForwardSchedule(scheduleRequest, resp, 0, mockInstanceStateMachine);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);

    // test StatusCode is INSTANCE_TRANSACTION_WRONG_VERSION but statemachine is existed, and retry successfully
    EXPECT_CALL(*mockInstanceStateMachine, GetVersion()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mockInstanceStateMachine, GetInstanceState()).WillRepeatedly(Return(InstanceState::SCHEDULING));
    EXPECT_CALL(*instanceControlView, GetInstance).WillOnce(Return(mockInstanceStateMachine));
    EXPECT_CALL(*localSchedSrv, ForwardSchedule).WillOnce(Return(wrongVersionResponse)).WillOnce(Return(otherResponse));
    future = instanceCtrlActor_->RetryForwardSchedule(scheduleRequest, resp, 0, mockInstanceStateMachine);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);
    EXPECT_EQ(scheduleRequest->instance().version(), 1);

    // test StatusCode is INSTANCE_TRANSACTION_WRONG_VERSION but statemachine is existed, and state is not Scheduling
    mockInstanceStateMachine = std::make_shared<MockInstanceStateMachine>("machine1");
    EXPECT_CALL(*mockInstanceStateMachine, GetVersion()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mockInstanceStateMachine, GetInstanceState()).WillRepeatedly(Return(InstanceState::CREATING));
    EXPECT_CALL(*instanceControlView, GetInstance).WillOnce(Return(mockInstanceStateMachine));
    EXPECT_CALL(*localSchedSrv, ForwardSchedule).WillOnce(Return(wrongVersionResponse));
    future = instanceCtrlActor_->RetryForwardSchedule(scheduleRequest, resp, 0, mockInstanceStateMachine);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);
    EXPECT_EQ(scheduleRequest->instance().version(), 1);

    // test StatusCode is INSTANCE_TRANSACTION_WRONG_VERSION and statemachine is existed, and retry 3 time
    EXPECT_CALL(*mockInstanceStateMachine, GetVersion()).WillRepeatedly(Return(2));
    EXPECT_CALL(*instanceControlView, GetInstance).WillRepeatedly(Return(mockInstanceStateMachine));
    EXPECT_CALL(*mockInstanceStateMachine, GetInstanceState()).WillRepeatedly(Return(InstanceState::SCHEDULING));
    EXPECT_CALL(*localSchedSrv, ForwardSchedule).Times(3).WillRepeatedly(Return(wrongVersionResponse));
    future = instanceCtrlActor_->RetryForwardSchedule(scheduleRequest, resp, 0, mockInstanceStateMachine);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);
    EXPECT_EQ(scheduleRequest->instance().version(), 2);
}

/**
 * TryDispatchOnLocal
 * Test is transition version is incorrect
 * Expectations:
 * scheduleRep code is version wrong or success
 */
TEST_F(InstanceCtrlActorTest, TryDispatchOnLocal)
{
    auto mockInstanceStateMachine = std::make_shared<MockInstanceStateMachine>("machine1");
    auto mockScheduler = std::make_shared<MockScheduler>();
    instanceCtrlActor_->scheduler_ = mockScheduler;

    auto scheduleRequest = std::make_shared<messages::ScheduleRequest>();
    ScheduleResult scheduleResult;
    auto status = Status::OK();

    resource_view::InstanceInfo instanceInfoSaved;
    instanceInfoSaved.set_functionproxyid("proxy1");
    TransitionResult result;
    result.status = Status(StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION, "version is incorrect");
    result.savedInfo = instanceInfoSaved;
    EXPECT_CALL(*mockInstanceStateMachine, TransitionToImpl(_, _, _, _, _)).WillRepeatedly(Return(result));
    bool isCalled = false;
    EXPECT_CALL(*mockScheduler, ScheduleConfirm).WillOnce(Return(Status::OK()))
        .WillOnce(Return(Status::OK())).WillOnce(DoAll(Assign(&isCalled, true), Return(Status::OK()))); // mock schedule successfully

    // test instance parentfunctionproxyaid is empty
    auto future = instanceCtrlActor_->TryDispatchOnLocal(status, scheduleRequest, scheduleResult, InstanceState::SCHEDULING, mockInstanceStateMachine);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);

    // test instance parentfunctionproxyaid is same as instance owner
    scheduleRequest->mutable_instance()->set_parentfunctionproxyaid("proxy1-LocalSchedInstanceCtrlActor@127.0.0.1:22772");
    future = instanceCtrlActor_->TryDispatchOnLocal(status, scheduleRequest, scheduleResult, InstanceState::SCHEDULING, mockInstanceStateMachine);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);

    // test instance parentfunctionproxyaid is different from instance owner
    scheduleRequest->mutable_instance()->set_parentfunctionproxyaid("proxy2-LocalSchedInstanceCtrlActor@127.0.0.1:22772");
    future = instanceCtrlActor_->TryDispatchOnLocal(status, scheduleRequest, scheduleResult, InstanceState::SCHEDULING, mockInstanceStateMachine);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);
    ASSERT_AWAIT_TRUE([&]() { return isCalled; });
}

/**
 * server mode driver heartbeat lost
 */
TEST_F(InstanceCtrlActorTest, DriverLostOnServerMode)
{
    std::string instanceID = "driver-job_123456";
    // get state machine
    InstanceInfo ins;
    ins.set_instanceid(instanceID);
    ins.set_jobid("job_123456");
    auto mockInstanceStateMachine = std::make_shared<MockInstanceStateMachine>("TEST_PROXY_ID");
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(mockInstanceStateMachine));
    EXPECT_CALL(*mockInstanceStateMachine, GetInstanceInfo).WillRepeatedly(Return(ins));

    // heartbeat not stopped by kernel
    instanceCtrlActor_->runtimeHeartbeatTimers_[instanceID] = litebus::Timer();

    // Delete client
    // observer_ delete instance
    EXPECT_CALL(*mockObserver_, DelInstance).WillRepeatedly(Return(Status::OK()));

    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    instanceCtrlActor_->BindLocalSchedSrv(localSchedSrv);
    messages::ForwardKillResponse response;
    response.set_code(common::ErrorCode::ERR_NONE);
    auto promise = std::make_shared<litebus::Promise<bool>>();
    EXPECT_CALL(*localSchedSrv, ForwardKillToInstanceManager)
        .WillOnce(DoAll(Invoke([&](const std::shared_ptr<messages::ForwardKillRequest> &req) {
            EXPECT_EQ(req->req().instanceid(), "job_123456");
            promise->SetValue(true);
            return response;
        })));
    litebus::Async(instanceCtrlActor_->GetAID(), &InstanceCtrlActor::HandleRuntimeHeartbeatLost, instanceID,
                   "runtimeID");
    ASSERT_AWAIT_READY(promise->GetFuture());
}

/**
 * duplicate driver event
 */
TEST_F(InstanceCtrlActorTest, DuplicateDriverEvent)
{
    auto mockSharedClientManagerProxy_ = std::make_shared<MockSharedClientManagerProxy>();
    instanceCtrlActor_->BindControlInterfaceClientManager(mockSharedClientManagerProxy_);
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    auto promise = std::make_shared<litebus::Promise<bool>>();
    EXPECT_CALL(*mockSharedClientManagerProxy_, NewControlInterfacePosixClient(_, _, _, _, _, _))
        .WillOnce(DoAll([&](const std::string &instanceID, const std::string &runtimeID, const std::string &address,
                            std::function<void()> closeCb, int64_t timeoutSec, int32_t maxGrpcSize) {
            promise->SetValue(true);
            return mockSharedClient;
        }));

    std::string instanceID = "driver-job_123456";
    // get state machine
    InstanceInfo ins;
    ins.set_instanceid(instanceID);
    ins.set_jobid("job_123456");
    litebus::Async(instanceCtrlActor_->GetAID(), &InstanceCtrlActor::OnDriverEvent, ins);
    litebus::Async(instanceCtrlActor_->GetAID(), &InstanceCtrlActor::OnDriverEvent, ins);
    ASSERT_AWAIT_READY(promise->GetFuture());
}

/**
 * cancel schedule
 */
TEST_F(InstanceCtrlActorTest, CancelSchedule)
{
    auto mockInstanceStateMachine = std::make_shared<MockInstanceStateMachine>("TEST_PROXY_ID");
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(mockInstanceStateMachine));
    InstanceInfo info;
    info.set_instanceid(TEST_INSTANCE_ID);
    info.set_tenantid(TEST_TENANT_ID);
    info.set_function(TEST_REQUEST_ID);
    EXPECT_CALL(*mockInstanceStateMachine, GetInstanceInfo).WillRepeatedly(Return(info));
    auto cancelFuture = litebus::Future<std::string>();
    cancelFuture.SetValue("cancel");
    EXPECT_TRUE(cancelFuture.IsOK());
    EXPECT_CALL(*mockInstanceStateMachine, GetCancelFuture).WillRepeatedly(Return(cancelFuture));
    // cancel on RetryForwardSchedule
    {
        auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
        instanceCtrlActor_->localSchedSrv_ = localSchedSrv;
        messages::ScheduleResponse resp;
        auto request = std::make_shared<messages::ScheduleRequest>();
        auto future = instanceCtrlActor_->RetryForwardSchedule(request, resp, 0, mockInstanceStateMachine);
        EXPECT_EQ(future.Get().code(), StatusCode::ERR_SCHEDULE_CANCELED);
    }
    // cancel before DeployInstance
    {
        auto request = std::make_shared<messages::ScheduleRequest>();
        request->set_requestid(TEST_REQUEST_ID);
        request->mutable_instance()->CopyFrom(info);
        EXPECT_CALL(*mockInstanceStateMachine, GetScheduleRequest).WillRepeatedly(Return(request));
        InstanceState state;
        EXPECT_CALL(*mockInstanceStateMachine, TransitionToImpl)
            .WillOnce(DoAll(testing::SaveArg<0>(&state), Return(TransitionResult{ litebus::None(), info, info })));
        auto funcMeta = std::make_shared<FunctionMeta>();
        funcMeta->funcMetaData.tenantId = TEST_TENANT_ID;
        instanceCtrlActor_->funcMetaMap_.emplace(TEST_REQUEST_ID, *funcMeta);
        auto status = instanceCtrlActor_->DeployInstance(request, 1, litebus::None());
        EXPECT_EQ(status.Get().StatusCode(), StatusCode::ERR_SCHEDULE_CANCELED);
        EXPECT_EQ(state, InstanceState::FATAL);
    }
    // cancel before Readiness
    {
        auto request = std::make_shared<messages::ScheduleRequest>();
        request->mutable_instance()->CopyFrom(info);
        auto status = instanceCtrlActor_->CheckReadiness(nullptr, request, 0);
        EXPECT_EQ(status.Get().StatusCode(), StatusCode::ERR_SCHEDULE_CANCELED);
    }
    // cancel before SendInitRuntime
    {
        auto request = std::make_shared<messages::ScheduleRequest>();
        request->mutable_instance()->CopyFrom(info);
        auto status = instanceCtrlActor_->SendInitRuntime(nullptr, request);
        EXPECT_EQ(status.Get().StatusCode(), StatusCode::ERR_SCHEDULE_CANCELED);
    }
}

/**
 * Tests notification signal resend functionality with two scenarios:
 * 1. When all four retry attempts fail
 * 2. When the signal succeeds on the second attempt
 */
TEST_F(InstanceCtrlActorTest, RetryNotificationSignal) {
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::CREATING));
    scheduleReq->mutable_instance()->set_functionproxyid(TEST_NODE_ID);
    scheduleReq->mutable_instance()->set_instanceid(SUBSCRIBER_ID);
    scheduleReq->set_requestid("requestId");
    auto context = std::make_shared<InstanceContext>(scheduleReq);
    auto subscriber = std::make_shared<InstanceStateMachine>(TEST_NODE_ID, context, false);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(subscriber));

    EXPECT_CALL(*mockFunctionAgentMgr_, IsFuncAgentRecovering(testing::_))
        .WillRepeatedly(Return(true));

    auto mockSharedClientManagerProxy_ = std::make_shared<MockSharedClientManagerProxy>();
    instanceCtrlActor_->BindControlInterfaceClientManager(mockSharedClientManagerProxy_);

    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*mockSharedClientManagerProxy_, GetControlInterfacePosixClient(_))
        .WillRepeatedly(Return(mockSharedClient));

    // Test case 1: All four retries fail
    runtime::SignalResponse errorSignalRsp;
    errorSignalRsp.set_code(common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);
    EXPECT_CALL(*mockSharedClient, Signal)
        .Times(4)
        .WillRepeatedly(Return(errorSignalRsp));

    common::NotificationPayload notificationPayload;
    notificationPayload.mutable_instancetermination()->set_instanceid(SUBSCRIBER_ID);
    std::string serializedPayload;
    notificationPayload.SerializeToString(&serializedPayload);
    auto notifyReq = std::make_shared<KillRequest>();
    notifyReq->set_signal(NOTIFY_SIGNAL);
    notifyReq->set_instanceid(SUBSCRIBER_ID);
    notifyReq->set_payload(std::move(serializedPayload));
    auto response = instanceCtrlActor_->Kill(PUBLISHER_ID, notifyReq).Get();
    EXPECT_EQ(response.code(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS);

    // Test case 2: First attempt fails, second succeeds
    runtime::SignalResponse successSignalRsp;
    successSignalRsp.set_code(common::ErrorCode::ERR_NONE);
    EXPECT_CALL(*mockSharedClient, Signal)
        .WillOnce(Return(errorSignalRsp))
        .WillOnce(Return(successSignalRsp));

    response = instanceCtrlActor_->Kill(PUBLISHER_ID, notifyReq).Get();
    EXPECT_EQ(response.code(), common::ErrorCode::ERR_NONE);
}

}  // namespace functionsystem::test