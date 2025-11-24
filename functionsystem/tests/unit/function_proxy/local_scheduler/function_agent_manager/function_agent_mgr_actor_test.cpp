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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/proto/pb/message_pb.h"
#include "common/types/instance_state.h"
#include "function_agent/code_deployer/s3_deployer.h"
#include "kv_service_accessor_actor.h"
#include "kv_service_actor.h"
#include "lease_service_actor.h"
#include "function_proxy/local_scheduler/function_agent_manager/function_agent_mgr.h"
#include "function_proxy/local_scheduler/function_agent_manager/function_agent_mgr_actor.h"
#include "mocks/mock_function_agent.h"
#include "mocks/mock_meta_store_client.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace local_scheduler;
using namespace ::testing;

const local_scheduler::FunctionAgentMgrActor::Param PARAM = {
    .retryTimes = 3,
    .retryCycleMs = 100,
    .pingTimes = 3,
    .pingCycleMs = 500,
    .enableTenantAffinity = true,
    .tenantPodReuseTimeWindow = 3,
    .enableIpv4TenantIsolation = true,
    .getAgentInfoRetryMs = 100,
    .invalidAgentGCInterval = 100,
};

const string TENANT_ID1 = "tenant1";
const string TENANT_ID2 = "tenant2";
const string FUNC_PROXY_ID1 = "node1";
const string FUNC_PROXY_ID2 = "node2";
const string FUNC_AGENT_ID1 = "agent1";
const string FUNC_AGENT_ID2 = "agent2";
const string FUNC_INSTANCE_ID1 = "instance1";
const string FUNC_INSTANCE_ID2 = "instance2";

class MockAgentActor : public litebus::ActorBase {
public:
    MockAgentActor() : litebus::ActorBase("mock-agent")
    {
    }

    MOCK_METHOD(void, SetNetworkIsolationRequest, (const litebus::AID &from, std::string &&name, std::string &&msg));

protected:
    void Init() override
    {
        Receive("SetNetworkIsolationRequest", &MockAgentActor::SetNetworkIsolationRequest);
    }
};

class FuncAgentMgrActorHelper : public FunctionAgentMgrActor {
public:
    FuncAgentMgrActorHelper(const std::string &metaStoreAddress)
        : FunctionAgentMgrActor("funcAgentMgr", PARAM, "nodeID",
                                std::make_shared<MockMetaStoreClient>(metaStoreAddress))
    {
    }

    litebus::Future<Status> SyncInstancesWithEmptyUnit()
    {
        std::shared_ptr<resource_view::ResourceUnit> resourceUnit = std::make_shared<resource_view::ResourceUnit>();
        resourceUnit->set_id("funcAgentMgr");
        return SyncInstances(resourceUnit);
    }
    litebus::Future<Status> SyncInstancesWithEmptyInstanceCtl()
    {
        std::shared_ptr<resource_view::ResourceUnit> resourceUnit = std::make_shared<resource_view::ResourceUnit>();
        resourceUnit->set_id("funcAgentMgr");
        auto instances = resourceUnit->mutable_instances();
        resource_view::InstanceInfo instanceInfo;
        instanceInfo.set_instanceid("funcAgentMgr_instance_id");
        instances->insert({ "funcAgentMgr_instance_id", instanceInfo });
        return SyncInstances(resourceUnit);
    }
};

class FuncAgentMgrActorTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        metaStoreAddress_ = "127.0.0.1:" + std::to_string(FindAvailablePort());
    }

    void SetUp() override
    {
        agentMgrActorHelper_ = make_shared<FuncAgentMgrActorHelper>(metaStoreAddress_);
    }

    inline static std::string metaStoreAddress_;
    shared_ptr<FuncAgentMgrActorHelper> agentMgrActorHelper_;
};

TEST_F(FuncAgentMgrActorTest, EmptyResourceUnit)
{
    auto status = agentMgrActorHelper_->SyncInstancesWithEmptyUnit();
    EXPECT_EQ(status.Get().StatusCode(), StatusCode::SUCCESS);
}

TEST_F(FuncAgentMgrActorTest, EmptyInstanceCtl)
{
    auto status = agentMgrActorHelper_->SyncInstancesWithEmptyInstanceCtl();
    EXPECT_EQ(status.GetErrorCode(), StatusCode::LS_SYNC_INSTANCE_FAIL);
}

TEST_F(FuncAgentMgrActorTest, AddFuncAgentFailed)
{
    auto mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>("111111");
    auto funcAgentMgr =
        make_shared<local_scheduler::FunctionAgentMgr>(make_shared<local_scheduler::FunctionAgentMgrActor>(
            "RecoverHeartBeatSuccessActor", PARAM, "nodeID", mockMetaStoreClient_));
    auto r = std::make_shared<resource_view::ResourceUnit>();
    auto r2 = std::make_shared<resource_view::ResourceUnit>();
    litebus::Promise<std::shared_ptr<resource_view::ResourceUnit>> p;
    funcAgentMgr->actor_->funcAgentResUpdatedMap_["id1"] = p;
    funcAgentMgr->SetFuncAgentUpdateMapPromise("id1", r);
    auto actor = make_shared<local_scheduler::FunctionAgentMgrActor>("RecoverHeartBeatSuccessActor", PARAM, "nodeID",
                                                                     mockMetaStoreClient_);
    auto res = actor->AddFuncAgent(Status(StatusCode::SUCCESS), "", r2);
    EXPECT_EQ(res.Get().IsError(), false);
}

TEST_F(FuncAgentMgrActorTest, TimeoutEventTest)
{
    auto mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>("111111");
    auto actor = make_shared<local_scheduler::FunctionAgentMgrActor>("RecoverHeartBeatSuccessActor", PARAM, "nodeID",
                                                                     mockMetaStoreClient_);
    actor->TimeoutEvent("id1");
    EXPECT_EQ(actor->funcAgentTable_.count("id1"), size_t(0));

    actor->funcAgentTable_["id1"] = {
        .isEnable =  false,
        .isInit =  false,
        .recoverPromise =  std::make_shared<litebus::Promise<bool>>(),
        .aid =  "aid1",
        .instanceIDs =  {}
    };
    EXPECT_EQ(actor->funcAgentTable_.count("id1"), size_t(1));
    actor->TimeoutEvent("id1");
    EXPECT_EQ(actor->funcAgentTable_.count("id1"), size_t(0));
}

/**
 * Test query instance status info
 */
TEST_F(FuncAgentMgrActorTest, DoAddFuncAgent)
{
    auto mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>("111111");
    auto actor = make_shared<local_scheduler::FunctionAgentMgrActor>("RecoverHeartBeatSuccessActor", PARAM, "nodeID",
                                                                     mockMetaStoreClient_);

    auto future = actor->DoAddFuncAgent(Status::OK(), "mock-agent-id");
}

/**
 * Test query instance status info
 */
TEST_F(FuncAgentMgrActorTest, QueryInstanceStatusInfo)
{
    auto mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>("111111");
    auto actor = make_shared<local_scheduler::FunctionAgentMgrActor>("RecoverHeartBeatSuccessActor", PARAM, "nodeID",
                                                                     mockMetaStoreClient_);

    auto future = actor->QueryInstanceStatusInfo("mock-agent-name", "mock-instance-id", "mock-runtime-id");

    messages::QueryInstanceStatusResponse rsp;
    actor->QueryInstanceStatusInfoResponse("mock-agent-name", "", rsp.SerializeAsString());
}

TEST_F(FuncAgentMgrActorTest, QueryDebugInstanceInfos)
{
    // start no mock metastore service
    auto kvServiceActor = std::make_shared<functionsystem::meta_store::KvServiceActor>();
    litebus::Spawn(kvServiceActor);
    litebus::AID kvServerAccessorAID =
        litebus::Spawn(std::make_shared<meta_store::KvServiceAccessorActor>(kvServiceActor->GetAID()));
    auto leaseServiceActor = std::make_shared<functionsystem::meta_store::LeaseServiceActor>(kvServiceActor->GetAID());
    litebus::Spawn(leaseServiceActor);
    leaseServiceActor->Start();
    kvServiceActor->AddLeaseServiceActor(leaseServiceActor->GetAID());
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    std::string addr = "127.0.0.1:" + std::to_string(port);
    functionsystem::MetaStoreConfig metaStoreConfig{ .etcdAddress = addr,
                                                     .metaStoreAddress = addr,
                                                     .enableMetaStore = true };
    auto metaStoreClient = std::make_shared<functionsystem::MetaStoreClient>(
        metaStoreConfig, functionsystem::GrpcSslConfig{}, functionsystem::MetaStoreTimeoutOption());
    metaStoreClient->Init();

    auto funcAgentMgrActor = make_shared<local_scheduler::FunctionAgentMgrActor>("functionAgentMgrActor", PARAM, "nodeID",
                                                                     metaStoreClient);
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;
    auto agentServiceActor = make_shared<MockFunctionAgent>("agentName", "agentID",
                                                    "testLocalScheduler_01-32379", s3Config, codePackageThresholds);
    funcAgentMgrActor->funcAgentTable_["agentID"] = {
        .isEnable = true,
        .isInit = false,
        .recoverPromise = std::make_shared<litebus::Promise<bool>>(),
        .aid = agentServiceActor->GetAID(),
        .instanceIDs = {}
    };
    litebus::Spawn(funcAgentMgrActor);
    litebus::Spawn(agentServiceActor);


    messages::QueryDebugInstanceInfosResponse rsp;
    rsp.set_code(0);
    auto insInfo1 = rsp.add_debuginstanceinfos();
    insInfo1->set_instanceid("test_instID1");
    insInfo1->set_pid(100);
    insInfo1->set_debugserver("test_gdbserverAddr");
    insInfo1->set_status("S");

    EXPECT_CALL(*agentServiceActor.get(), MockQueryDebugInstanceInfos).WillOnce(Return(rsp));
    auto future = funcAgentMgrActor->QueryDebugInstanceInfos();
    EXPECT_EQ(future.Get().StatusCode(),StatusCode::SUCCESS);
    auto response = metaStoreClient->Get("/yr/debug/",{.prefix = true}).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(response->kvs[0].key(), "/yr/debug/test_instID1");
    messages::DebugInstanceInfo info;
    (void)google::protobuf::util::JsonStringToMessage(response->kvs[0].value(),&info);
    EXPECT_EQ(info.instanceid(),"test_instID1");
    EXPECT_EQ(info.debugserver(),"test_gdbserverAddr");

    litebus::Terminate(funcAgentMgrActor->GetAID());
    litebus::Await(funcAgentMgrActor);
    litebus::Terminate(agentServiceActor->GetAID());
    litebus::Await(agentServiceActor);
    litebus::Terminate(kvServerAccessorAID);
    litebus::Await(kvServerAccessorAID);
    litebus::Terminate(kvServiceActor->GetAID());
    litebus::Await(kvServiceActor);
    litebus::Terminate(leaseServiceActor->GetAID());
    litebus::Await(leaseServiceActor);

}

TEST_F(FuncAgentMgrActorTest, TenantEventCase2)
{
    // same node
    TenantEvent event1 = {
        .tenantID = TENANT_ID1,
        .functionProxyID = "nodeID",
        .functionAgentID = FUNC_AGENT_ID1,
        .instanceID = FUNC_INSTANCE_ID1,
        .agentPodIp = "10.42.1.221",
        .code = static_cast<int32_t>(InstanceState::RUNNING),
    };
    agentMgrActorHelper_->OnTenantUpdateInstance(event1);

    // same tenant on other node
    TenantEvent event2 = {
        .tenantID = TENANT_ID1,
        .functionProxyID = FUNC_PROXY_ID2,
        .functionAgentID = FUNC_AGENT_ID2,
        .instanceID = FUNC_INSTANCE_ID2,
        .agentPodIp = "10.42.2.222",
        .code = static_cast<int32_t>(InstanceState::RUNNING),
    };
    agentMgrActorHelper_->OnTenantUpdateInstance(event2);

    // another instance but same pod in this node
    TenantEvent event3 = {
        .tenantID = TENANT_ID1,
        .functionProxyID = "nodeID",
        .functionAgentID = FUNC_AGENT_ID1,
        .instanceID = FUNC_INSTANCE_ID2,
        .agentPodIp = "10.42.1.221",
        .code = static_cast<int32_t>(InstanceState::RUNNING),
    };
    agentMgrActorHelper_->OnTenantUpdateInstance(event3);

    auto tenantCacheMap = agentMgrActorHelper_->GetTenantCacheMap();
    auto tenantCache = tenantCacheMap[event1.tenantID];
    EXPECT_EQ(tenantCache->podIps.size(), 2u);

    agentMgrActorHelper_->OnTenantDeleteInstance(event3);
    EXPECT_EQ(tenantCache->podIps.size(), 2u);

    agentMgrActorHelper_->OnTenantDeleteInstance(event1);
    EXPECT_EQ(tenantCache->podIps.size(), 1u);
    EXPECT_FALSE(tenantCache->functionAgentCacheMap[event1.functionAgentID].isAgentOnThisNode);
}

TEST_F(FuncAgentMgrActorTest, TenantEventCase3)
{
    auto mockAgent = std::make_shared<MockAgentActor>();
    litebus::Spawn(mockAgent);

    FuncAgentMgrActorHelper::FuncAgentInfo mockAgentInfo;
    mockAgentInfo.aid = mockAgent->GetAID();

    EXPECT_CALL(*mockAgent, SetNetworkIsolationRequest).WillOnce(Return()).WillOnce(Return()).WillOnce(Return());

    agentMgrActorHelper_->InsertAgent(FUNC_AGENT_ID1, mockAgentInfo);
    agentMgrActorHelper_->InsertAgent(FUNC_AGENT_ID2, mockAgentInfo);

    // same tenant on other node
    TenantEvent event2 = {
        .tenantID = TENANT_ID1,
        .functionProxyID = FUNC_PROXY_ID2,
        .functionAgentID = FUNC_AGENT_ID2,
        .instanceID = FUNC_INSTANCE_ID2,
        .agentPodIp = "10.42.2.222",
        .code = static_cast<int32_t>(InstanceState::RUNNING),
    };
    agentMgrActorHelper_->OnTenantUpdateInstance(event2);

    // same node
    TenantEvent event1 = {
        .tenantID = TENANT_ID1,
        .functionProxyID = "nodeID",
        .functionAgentID = FUNC_AGENT_ID1,
        .instanceID = FUNC_INSTANCE_ID1,
        .agentPodIp = "10.42.1.221",
        .code = static_cast<int32_t>(InstanceState::RUNNING),
    };
    agentMgrActorHelper_->OnTenantUpdateInstance(event1);

    TenantEvent event3 = {
        .tenantID = TENANT_ID1,
        .functionProxyID = "nodeID",
        .functionAgentID = "fake_agent_id",
        .instanceID = FUNC_INSTANCE_ID1,
        .agentPodIp = "10.42.1.221",
        .code = static_cast<int32_t>(InstanceState::RUNNING),
    };
    agentMgrActorHelper_->OnTenantUpdateInstance(event3);

    auto tenantCacheMap = agentMgrActorHelper_->GetTenantCacheMap();
    auto tenantCache = tenantCacheMap[event1.tenantID];
    EXPECT_EQ(tenantCache->podIps.size(), 2u);

    agentMgrActorHelper_->OnTenantDeleteInstance(event1);
    EXPECT_EQ(tenantCache->podIps.size(), 1u);
    EXPECT_FALSE(tenantCache->functionAgentCacheMap[event2.functionAgentID].isAgentOnThisNode);

    litebus::Terminate(mockAgent->GetAID());
    litebus::Await(mockAgent->GetAID());
}

}  // namespace functionsystem::test