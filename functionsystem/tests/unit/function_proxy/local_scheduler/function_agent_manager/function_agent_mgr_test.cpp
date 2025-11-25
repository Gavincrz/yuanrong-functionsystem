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

#include "function_proxy/local_scheduler/function_agent_manager/function_agent_mgr.h"

#include <gtest/gtest.h>

#include <async/async.hpp>
#include <async/future.hpp>
#include <chrono>
#include <iostream>
#include <thread>

#include "common/constants/signal.h"
#include "common/proto/pb/message_pb.h"
#include "function_agent/code_deployer/s3_deployer.h"
#include "function_agent_helper.h"
#include "utils/port_helper.h"
#include "mocks/mock_bundle_mgr.h"
#include "mocks/mock_function_agent.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_local_sched_srv.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_resource_view.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace local_scheduler;
using namespace ::testing;

namespace {
const string REQUEST_ID = "requestID";
const string INSTANCE_ID = "instanceID";
const string TRACE_ID = "TRACE_ID";
const string FUNCTION_NAME = "function";
const string STORAGE_TYPE = "s3";

const string REGISTER_SUCCESS_MESSAGE = "register successfully";
const string DEPLOY_SUCCESS_MESSAGE = "deploy success";
const string AGENT_EXITED_MESSAGE = "function agent exited";
const string AGENT_MAY_EXITED_MESSAGE = "function agent may already exited";
const string KILL_SUCCESS_MESSAGE = "kill success";
const string DEPLOY_RETRY_FAIL_MESSAGE = "deploy retry fail";
const string KILL_RETRY_FAIL_MESSAGE = "kill retry fail";

const uint32_t REQUEST_NUM = 500;
const uint32_t FUNC_AGENT_NUM = 10;
}  // namespace

const string TEST_FUNC_AGENT_NAME = "testFuncAgent";
const string TEST_LOCAL_SCHEDULER_AID = "testLocalScheduler_01-32379";

// set up function agent information
// agentAID formatted as a string "AgentServiceActor@127.0.0.1:58866"
// agentAIDName formatted as a string "AgentServiceActor"
// agent address formatted as string "127.0.0.1:58866"
const string SETUP_FUNC_AGENT_AID_NAME = "AgentServiceActor";
const string SETUP_LOCAL_SCHEDULER_AID = "setupLocalScheduler_01-32379";
const string SETUP_RUNTIME_MANAGER_AID = "setup-RuntimeManagerSrv";
const string SETUP_RUNTIME_MANAGER_RANDOM_ID = "setup-runtimemanager-random-id";
const string SETUP_INSTANCE_ID = "setup-instance-id";

const local_scheduler::FunctionAgentMgrActor::Param PARAM = {
    .retryTimes = 3,
    .retryCycleMs = 100,
    .pingTimes = 3,
    .pingCycleMs = 500,
    .enableTenantAffinity = true,
    .tenantPodReuseTimeWindow = 3,
    .enableIpv4TenantIsolation = true,
    .enableForceDeletePod = true,
    .getAgentInfoRetryMs = 100,
    .invalidAgentGCInterval = 100,
};

const RuntimeConfig runtimeConfig{ .runtimeHeartbeatEnable = "true",
                                   .runtimeMaxHeartbeatTimeoutTimes = 3,
                                   .runtimeHeartbeatTimeoutMS = 2000,
                                   .runtimeInitCallTimeoutMS = 3000,
                                   .runtimeShutdownTimeoutSeconds = 3};

class DISABLED_FuncAgentMgrTest : public ::testing::Test {
friend class FunctionAgentMgrActor;
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        metaStoreAddress_ = "127.0.0.1:" + std::to_string(FindAvailablePort());
        funcAgentAddress_ = metaStoreAddress_;
    }

    void SetUp() override
    {
        mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>(metaStoreAddress_);

        auto getResponse = std::make_shared<GetResponse>();
        EXPECT_CALL(*mockMetaStoreClient_, Get).WillRepeatedly(Return(getResponse));

        funcAgentRegisInfoInit_.set_agentaidname(SETUP_FUNC_AGENT_AID_NAME);
        funcAgentRegisInfoInit_.set_agentaddress(funcAgentAddress_);
        funcAgentRegisInfoInit_.set_runtimemgraid(SETUP_RUNTIME_MANAGER_AID);
        funcAgentRegisInfoInit_.set_runtimemgrid(SETUP_RUNTIME_MANAGER_RANDOM_ID);
        funcAgentRegisInfoInit_.set_statuscode(1);

        funcAgentMgr_ = make_shared<local_scheduler::FunctionAgentMgr>(
            make_shared<local_scheduler::FunctionAgentMgrActor>("funcAgentMgr", PARAM, "nodeID", mockMetaStoreClient_));
        funcAgentMgr_->SetNodeID("nodeID");
        funcAgentMgr_->SetRetrySendCleanStatusInterval(100);
        S3Config s3Config;
        messages::CodePackageThresholds codePackageThresholds;
        randomFuncAgentName_ = GenerateRandomFuncAgentName();
        funcAgent_ = make_shared<MockFunctionAgent>(TEST_FUNC_AGENT_NAME, randomFuncAgentName_,
                                                    SETUP_LOCAL_SCHEDULER_AID, s3Config, codePackageThresholds);
        InstanceCtrlConfig instanceCtrlConfig{};
        instanceCtrlConfig.runtimeConfig = runtimeConfig;
        instCtrl_ = make_shared<MockInstanceCtrl>(
            make_shared<local_scheduler::InstanceCtrlActor>("mockInstanceCtrl", "nodeID", instanceCtrlConfig));
        resourceView_ = MockResourceView::CreateMockResourceView();
        funcAgentHelper_ = make_shared<FunctionAgentHelper>();

        localSchedSrv_ = make_shared<MockLocalSchedSrv>();
        mockBundleMgr_ = make_shared<MockBundleMgr>();

        funcAgentMgr_->Start(instCtrl_, resourceView_);
        funcAgentMgr_->BindLocalSchedSrv(localSchedSrv_);
        funcAgentMgr_->BindBundleMgr(mockBundleMgr_);

        litebus::Spawn(funcAgent_);

        auto putResponse = std::make_shared<PutResponse>();
        EXPECT_CALL(*mockMetaStoreClient_, Put).WillRepeatedly(Return(putResponse));
        std::string jsonStr;
        (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfoInit_, &jsonStr);

        messages::Register registerMsg = GenRegister(randomFuncAgentName_, funcAgent_->GetAID().UnfixUrl(), jsonStr);
        auto resourceUnit = registerMsg.mutable_resource();
        resourceUnit->set_id(randomFuncAgentName_);
        auto instances = resourceUnit->mutable_instances();
        resource_view::InstanceInfo instanceInfo;
        instanceInfo.set_instanceid(SETUP_INSTANCE_ID);
        instances->insert({ SETUP_INSTANCE_ID, instanceInfo });

        EXPECT_CALL(*funcAgent_.get(), MockRegister).WillRepeatedly(testing::Return(registerMsg.SerializeAsString()));
        funcAgentMgr_->ClearFuncAgentsRegis();

        litebus::Future<string> registeredMsg;
        EXPECT_CALL(*funcAgent_.get(), MockRegistered(testing::_, testing::_, testing::_))
            .WillRepeatedly(testing::Return());

        EXPECT_CALL(*resourceView_.get(), AddResourceUnit(testing::_))
            .WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

        EXPECT_CALL(*instCtrl_.get(), SyncInstances(testing::_))
            .WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

        EXPECT_CALL(*resourceView_.get(), UpdateUnitStatus(_, _)).WillRepeatedly(Return(Status(StatusCode::SUCCESS)));
        EXPECT_CALL(*mockBundleMgr_, SyncBundles).WillRepeatedly(Return(Status::OK()));

        funcAgentMgr_->ToReady();
        ASSERT_AWAIT_TRUE([=]() -> bool {
            auto lambda = [=]() {
                litebus::Async(funcAgent_->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler,
                               funcAgentMgr_->GetActorAID());
            };
            return CheckIsRegister(funcAgentMgr_, randomFuncAgentName_, lambda);
            });
    }

    bool CheckIsRegister(const shared_ptr<local_scheduler::FunctionAgentMgr> &funcAgentMgr,
                         const std::string &agentName, std::function<void()> func)
    {
        auto isDone = std::make_shared<litebus::Promise<bool>>();
        auto isRegistered = funcAgentMgr->IsRegistered(agentName);
        (void)isRegistered.Then([isDone, func](const bool &value) -> litebus::Future<bool> {
            if (value) {
                isDone->SetValue(true);
                return true;
            }
            if (func != nullptr) {
                func();
            }
            isDone->SetValue(false);
            return false;
        });
        return isDone->GetFuture().Get();
    }

    void TearDown() override
    {
        litebus::Terminate(funcAgent_->GetAID());
        litebus::Await(funcAgent_);
        funcAgentMgr_->ClearFuncAgentsRegis();
        funcAgentMgr_ = nullptr;
        funcAgent_ = nullptr;
        instCtrl_ = nullptr;
        localSchedSrv_ = nullptr;
        mockBundleMgr_ = nullptr;
        resourceView_ = nullptr;
        funcAgentHelper_ = nullptr;
        mockMetaStoreClient_ = nullptr;
    }

    std::vector<litebus::Future<string>> RegisterFuncAgents(std::string testName,
                                                            std::vector<shared_ptr<MockFunctionAgent>> &funcAgents)
    {
        auto funcAgentNum(funcAgents.size());
        for (size_t i = 0; i < funcAgentNum; ++i) {
            S3Config s3Config;
            messages::CodePackageThresholds codePackageThresholds;
            funcAgents[i] = make_shared<MockFunctionAgent>(testName + "_agent_AID" + std::to_string(i),
                                                           testName + "_agent_AID" + std::to_string(i),
                                                           SETUP_LOCAL_SCHEDULER_AID, s3Config, codePackageThresholds);
            litebus::Spawn(funcAgents[i]);
        }

        EXPECT_CALL(*resourceView_.get(), AddResourceUnit(testing::_))
            .WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

        EXPECT_CALL(*instCtrl_.get(), SyncInstances(testing::_))
            .WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

        EXPECT_CALL(*resourceView_.get(), UpdateUnitStatus(_, _)).WillRepeatedly(Return(Status(StatusCode::SUCCESS)));
        EXPECT_CALL(*mockBundleMgr_, UpdateBundlesStatus).WillRepeatedly(Return());

        std::vector<litebus::Future<string>> msgs(funcAgentNum);
        for (size_t i = 0; i < funcAgents.size(); ++i) {
            messages::FuncAgentRegisInfo funcAgentRegisInfoInit;
            funcAgentRegisInfoInit.set_agentaidname("agent_aid_name_" + std::to_string(i));
            funcAgentRegisInfoInit.set_agentaddress("agent_address_" + std::to_string(i));
            funcAgentRegisInfoInit.set_runtimemgraid("runtime_manager_aid_" + std::to_string(i));
            funcAgentRegisInfoInit.set_runtimemgrid("runtime_manager_random_id" + std::to_string(i));
            funcAgentRegisInfoInit.set_statuscode(1);

            std::string jsonStr;
            (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfoInit, &jsonStr);

            auto registerMsg =
                GenRegister(testName + "_agent_AID" + std::to_string(i), funcAgents[i]->GetAID().UnfixUrl(), jsonStr);
            auto resourceUnit = registerMsg.mutable_resource();
            resourceUnit->set_id(testName + "_agent_AID" + std::to_string(i));
            auto instances = resourceUnit->mutable_instances();
            resource_view::InstanceInfo instanceInfo;
            instanceInfo.set_instanceid(INSTANCE_ID + std::to_string(i));
            instances->insert({ INSTANCE_ID + std::to_string(i), instanceInfo });

            EXPECT_CALL(*funcAgents[i].get(), MockRegister).WillOnce(testing::Return(registerMsg.SerializeAsString()));
            EXPECT_CALL(*funcAgents[i].get(), MockRegistered(testing::_, testing::_, testing::_))
                .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&msgs[i])));
            litebus::Async(funcAgents[i]->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler,
                           funcAgentMgr_->GetActorAID());
        }

        for (size_t i = 0; i < funcAgentNum; i++) {
            msgs[i].Get(1000);
        }

        return msgs;
    }

    void TerminateFcAgents(std::vector<shared_ptr<MockFunctionAgent>> &funcAgents)
    {
        for (auto funcAgent : funcAgents) {
            litebus::Terminate(funcAgent->GetAID());
            litebus::Await(funcAgent);
        }
    }

    std::string FuncAgentRegisToCollectionStrHelper(
        std::unordered_map<std::string, messages::FuncAgentRegisInfo> &funcAgentRegisInfos)
    {
        messages::FuncAgentRegisInfoCollection regisInfoStrCollection;
        auto map = regisInfoStrCollection.mutable_funcagentregisinfomap();
        for (auto &info : funcAgentRegisInfos) {
            map->insert({ info.first, info.second });
        }

        std::string jsonStr;
        if (!google::protobuf::util::MessageToJsonString(regisInfoStrCollection, &jsonStr).ok()) {
            YRLOG_ERROR("failed to trans to json string from FuncAgentRegisInfoCollection");
        }
        return jsonStr;
    }

    inline std::string GenerateRandomFuncAgentName()
    {
        return GenerateRandomName("randomFuncAgent");
    }

    shared_ptr<local_scheduler::FunctionAgentMgr> funcAgentMgr_;
    shared_ptr<MockFunctionAgent> funcAgent_;
    shared_ptr<MockInstanceCtrl> instCtrl_;
    shared_ptr<MockLocalSchedSrv> localSchedSrv_;
    shared_ptr<MockBundleMgr> mockBundleMgr_;
    shared_ptr<MockResourceView> resourceView_;
    shared_ptr<FunctionAgentHelper> funcAgentHelper_;
    shared_ptr<MockMetaStoreClient> mockMetaStoreClient_;
    messages::FuncAgentRegisInfo funcAgentRegisInfoInit_;
    string randomFuncAgentName_;
    inline static std::string funcAgentAddress_;
    inline static std::string metaStoreAddress_;
};


TEST_F(DISABLED_FuncAgentMgrTest, CreateSuccess)
{
    auto funcAgentMgr = local_scheduler::FunctionAgentMgr::Create("NodeID", PARAM, mockMetaStoreClient_);
    EXPECT_NE(funcAgentMgr.get(), nullptr);
}

// test for FunctionAgentMgr::Register
// receive register request from function agent and register successfully
TEST_F(DISABLED_FuncAgentMgrTest, RegisterSuccess)
{
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;
    auto funcAgent = make_shared<MockFunctionAgent>(randomFuncAgentName_, randomFuncAgentName_,
                                                    TEST_LOCAL_SCHEDULER_AID, s3Config, codePackageThresholds);
    litebus::Spawn(funcAgent);

    std::string jsonStr;
    (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfoInit_, &jsonStr);

    messages::Register registerMsg = GenRegister(TEST_FUNC_AGENT_NAME, funcAgent->GetAID().UnfixUrl(), jsonStr);
    auto resourceUnit = registerMsg.mutable_resource();
    resourceUnit->set_id(TEST_FUNC_AGENT_NAME);

    auto instances = resourceUnit->mutable_instances();
    resource_view::InstanceInfo instanceInfo;
    instanceInfo.set_instanceid(INSTANCE_ID);
    instances->insert({ INSTANCE_ID, instanceInfo });

    EXPECT_CALL(*funcAgent.get(), MockRegister).WillOnce(testing::Return(registerMsg.SerializeAsString()));

    litebus::Future<string> registeredMsg;
    EXPECT_CALL(*funcAgent.get(), MockRegistered(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&registeredMsg)));

    litebus::Future<resource_view::ResourceUnit> addResourceUnitMsg;
    EXPECT_CALL(*resourceView_.get(), AddResourceUnit(testing::_))
        .WillOnce(testing::DoAll(FutureArg<0>(&addResourceUnitMsg), testing::Return(Status(StatusCode::SUCCESS))));

    litebus::Future<shared_ptr<resource_view::ResourceUnit>> syncResourceUnitMsg;
    EXPECT_CALL(*instCtrl_.get(), SyncInstances(testing::_))
        .WillOnce(testing::DoAll(
            testing::DoAll(FutureArg<0>(&syncResourceUnitMsg), testing::Return(Status(StatusCode::SUCCESS)))));

    EXPECT_CALL(*resourceView_.get(), UpdateUnitStatus(_, _)).WillOnce(Return(Status(StatusCode::SUCCESS)));
    EXPECT_CALL(*mockBundleMgr_, UpdateBundlesStatus).WillOnce(Return());

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    auto registerVal = registeredMsg.Get(100);
    ASSERT_TRUE(registerVal.IsSome());
    EXPECT_FALSE(registerVal.Get().empty());

    messages::Registered registered;
    auto parseRet = registered.ParseFromString(registerVal.Get());
    EXPECT_TRUE(parseRet);
    EXPECT_EQ(registered.code(), StatusCode::SUCCESS);
    auto expectRegisteredMsg = TEST_FUNC_AGENT_NAME + " " + REGISTER_SUCCESS_MESSAGE;
    EXPECT_STREQ(registered.message().c_str(), expectRegisteredMsg.c_str());

    auto syncResourceUnitVal = syncResourceUnitMsg.Get(100);
    ASSERT_TRUE(syncResourceUnitVal.IsSome());
    EXPECT_STREQ(syncResourceUnitVal.Get()->id().c_str(), TEST_FUNC_AGENT_NAME.c_str());
    EXPECT_STREQ(syncResourceUnitVal.Get()->instances().at(INSTANCE_ID).instanceid().c_str(), INSTANCE_ID.c_str());

    auto resourceUnitVal = addResourceUnitMsg.Get(100);
    ASSERT_TRUE(resourceUnitVal.IsSome());
    EXPECT_STREQ(resourceUnitVal.Get().id().c_str(), TEST_FUNC_AGENT_NAME.c_str());

    ASSERT_AWAIT_TRUE([=]() -> bool { return CheckIsRegister(funcAgentMgr_, TEST_FUNC_AGENT_NAME, nullptr); });

    std::cout << funcAgentMgr_->Dump() << std::endl;

    litebus::Terminate(funcAgent->GetAID());
    litebus::Await(funcAgent);

    // clean
    registerMsg.clear_resource();
}

// test for FunctionAgentMgr::Register
// receive register request from function agent, first success but retry failed
TEST_F(DISABLED_FuncAgentMgrTest, AgentRegisterFailed)
{
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;
    auto funcAgent = make_shared<MockFunctionAgent>(
        "AgentRegisterFailed_func_agent_AID", "AgentRegisterFailed_func_agent_AID",
        "AgentRegisterFailed_local_scheduler_AID", s3Config, codePackageThresholds);
    litebus::Spawn(funcAgent);

    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    info.set_runtimemgrid("runtime_manager_randomid");
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::FAILED));
    funcAgentsRegis["agent_id"] = info;
    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);

    messages::FuncAgentRegisInfo funcAgentRegisInfo, funcAgentRegisInfo2;
    funcAgentRegisInfo.set_runtimemgrid("runtime_manager_randomid");
    funcAgentRegisInfo2.set_runtimemgrid("runtime_manager_randomid_2");
    std::string jsonStr, jsonStr2;
    (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfo, &jsonStr);
    (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfo2, &jsonStr2);
    messages::Register registerMsg, registerMsg2;
    registerMsg = GenRegister("agent_id", funcAgent->GetAID().UnfixUrl(), jsonStr);
    registerMsg2 = GenRegister("agent_id", funcAgent->GetAID().UnfixUrl(), jsonStr2);
    EXPECT_CALL(*funcAgent.get(), MockRegister).WillOnce(testing::Return(registerMsg.SerializeAsString()))
    .WillOnce(testing::Return(registerMsg2.SerializeAsString()));

    litebus::Future<string> registeredMsg;
    EXPECT_CALL(*funcAgent.get(), MockRegistered(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&registeredMsg)));

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    auto registerVal = registeredMsg.Get(100);
    ASSERT_TRUE(registerVal.IsSome());
    messages::Registered registered;
    auto parseRet = registered.ParseFromString(registerVal.Get());
    EXPECT_TRUE(parseRet);
    EXPECT_EQ(registered.code(), StatusCode::FAILED);
    EXPECT_STREQ(registered.message().c_str(), "agent_id retry register failed");

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    ASSERT_AWAIT_TRUE([=]() -> bool { return CheckIsRegister(funcAgentMgr_, "agent_id", nullptr);});

    litebus::Terminate(funcAgent->GetAID());
    litebus::Await(funcAgent);
}

TEST_F(DISABLED_FuncAgentMgrTest, AgentRegisterEvicted)
{
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;
    auto funcAgent = make_shared<MockFunctionAgent>(
        "AgentRegisterFailed_func_agent_AID", "AgentRegisterFailed_func_agent_AID",
        "AgentRegisterFailed_local_scheduler_AID", s3Config, codePackageThresholds);
    litebus::Spawn(funcAgent);

    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    info.set_runtimemgrid("runtime_manager_randomid");
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::EVICTED));
    funcAgentsRegis["agent_id"] = info;
    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);

    messages::FuncAgentRegisInfo funcAgentRegisInfo;
    funcAgentRegisInfo.set_runtimemgrid("runtime_manager_randomid");
    std::string jsonStr;
    (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfo, &jsonStr);
    messages::Register registerMsg;
    registerMsg = GenRegister("agent_id", funcAgent->GetAID().UnfixUrl(), jsonStr);
    EXPECT_CALL(*funcAgent.get(), MockRegister).WillOnce(testing::Return(registerMsg.SerializeAsString()));

    litebus::Future<string> registeredMsg;
    EXPECT_CALL(*funcAgent.get(), MockRegistered(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&registeredMsg)));

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    ASSERT_AWAIT_READY(registeredMsg);
    messages::Registered registered;
    EXPECT_TRUE(registered.ParseFromString(registeredMsg.Get()));
    EXPECT_EQ(registered.code(), StatusCode::LS_AGENT_EVICTED);
    EXPECT_STREQ(registered.message().c_str(), "agent_id failed to register, has been evicted");

    litebus::Terminate(funcAgent->GetAID());
    litebus::Await(funcAgent);
}

// test for FunctionAgentMgr::Register
// receive register request from function agent but parse message fail
TEST_F(DISABLED_FuncAgentMgrTest, RegisterParseFail)
{
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;
    auto funcAgent = make_shared<MockFunctionAgent>(randomFuncAgentName_, randomFuncAgentName_,
                                                    SETUP_LOCAL_SCHEDULER_AID, s3Config, codePackageThresholds);
    litebus::Spawn(funcAgent);

    EXPECT_CALL(*funcAgent.get(), MockRegister).WillOnce(testing::Return("testFuncAgent@127.0.0.1:8080"));

    litebus::Future<string> registeredMsg;
    EXPECT_CALL(*funcAgent.get(), MockRegistered(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&registeredMsg)));

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    auto registerVal = registeredMsg.Get(100);
    ASSERT_TRUE(registerVal.IsSome());

    messages::Registered registered;
    auto parseRet = registered.ParseFromString(registerVal.Get());
    EXPECT_TRUE(parseRet);
    EXPECT_EQ(registered.code(), StatusCode::PARAMETER_ERROR);
    EXPECT_STREQ(registered.message().c_str(), "invalid request body");

    std::cout << funcAgentMgr_->Dump() << std::endl;

    litebus::Terminate(funcAgent->GetAID());
    litebus::Await(funcAgent);
}

// test for FunctionAgentMgr::Register
// receive register request from function agent but heartbeat link fail
TEST_F(DISABLED_FuncAgentMgrTest, RegisterBuildLinkFail)
{
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;
    auto funcAgent = make_shared<MockFunctionAgent>(randomFuncAgentName_, randomFuncAgentName_,
                                                    SETUP_LOCAL_SCHEDULER_AID, s3Config, codePackageThresholds);
    litebus::Spawn(funcAgent);

    std::string jsonStr;
    (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfoInit_, &jsonStr);

    messages::Register registerMsg = GenRegister(TEST_FUNC_AGENT_NAME, funcAgent->GetAID().UnfixUrl(), jsonStr);
    auto resourceUnit = registerMsg.mutable_resource();
    resourceUnit->set_id(TEST_FUNC_AGENT_NAME);

    EXPECT_CALL(*funcAgent.get(), MockRegister).WillOnce(testing::Return(registerMsg.SerializeAsString()));

    litebus::Future<string> registeredMsg;
    EXPECT_CALL(*funcAgent.get(), MockRegistered(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&registeredMsg)));

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    auto registerVal = registeredMsg.Get(100);
    ASSERT_TRUE(registerVal.IsSome());

    std::cout << funcAgentMgr_->Dump() << std::endl;

    litebus::Terminate(funcAgent->GetAID());
    litebus::Await(funcAgent);

    // clean
    registerMsg.clear_resource();
}

// test for FunctionAgentMgr::Register
// receive register request from function agent but sync instance info fail
TEST_F(DISABLED_FuncAgentMgrTest, RegisterSyncInstanceFail)
{
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;
    auto funcAgent = make_shared<MockFunctionAgent>(
        "RegisterSyncInstanceFail_func_agent_AID", "RegisterSyncInstanceFail_func_agent_AID",
        "RegisterSyncInstanceFail_local_scheduler_AID", s3Config, codePackageThresholds);
    litebus::Spawn(funcAgent);

    std::string jsonStr;
    (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfoInit_, &jsonStr);

    messages::Register registerMsg =
        GenRegister("RegisterSyncInstanceFail_func_agent_AID", funcAgent->GetAID().UnfixUrl(), jsonStr);
    auto resourceUnit = registerMsg.mutable_resource();
    resourceUnit->set_id("RegisterSyncInstanceFail_func_agent_AID");

    auto instances = resourceUnit->mutable_instances();
    resource_view::InstanceInfo instanceInfo;
    instanceInfo.set_instanceid("RegisterSyncInstanceFail_instance_AID");
    instances->insert({ "RegisterSyncInstanceFail_instance_AID", instanceInfo });

    messages::Register registerMsg2 = registerMsg;
    registerMsg2.set_name("RegisterSyncInstanceFail_func_agent_AID_2");
    EXPECT_CALL(*funcAgent.get(), MockRegister)
        .WillOnce(testing::Return(registerMsg.SerializeAsString()))
        .WillOnce(testing::Return(registerMsg2.SerializeAsString()));

    messages::CleanStatusResponse mockResp;
    EXPECT_CALL(*funcAgent.get(), MockCleanStatusResponse(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::Return(std::pair<bool, string>(true, mockResp.SerializeAsString())));

    EXPECT_CALL(*funcAgent.get(), MockRegistered(testing::_, testing::_, testing::_)).WillRepeatedly(testing::Return());

    EXPECT_CALL(*resourceView_.get(), AddResourceUnit(testing::_))
        .WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

    litebus::Promise<Status> testRet1;
    testRet1.SetFailed(StatusCode::LS_SYNC_KILL_INSTANCE_FAIL);
    EXPECT_CALL(*instCtrl_.get(), SyncInstances(testing::_))
        .Times(2)
        .WillOnce(testing::Return(testRet1.GetFuture()))
        .WillOnce(testing::Return(Status(StatusCode::FAILED)));

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    std::cout << funcAgentMgr_->Dump() << std::endl;

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    litebus::AID heartBeatAID =
        litebus::AID(HEARTBEAT_OBSERVER_BASENAME + FUNCTION_AGENT_AGENT_MGR_ACTOR_NAME, funcAgentMgr_->GetActorAID().Url());
    HeartbeatClientDriver pingPongDriver("RegisterSyncInstanceFail_func_agent_AID_2",
                                  [](const litebus::AID &) {});
    (void)pingPongDriver.Start(heartBeatAID);

    ASSERT_AWAIT_TRUE([=]() -> bool {
        return CheckIsRegister(funcAgentMgr_, "RegisterSyncInstanceFail_func_agent_AID_2", nullptr);
    });

    litebus::Terminate(funcAgent->GetAID());
    litebus::Await(funcAgent);

    // clean
    registerMsg.clear_resource();
}

// test for FunctionAgentMgr::Register
// receive many register request from function agent and register successfully
TEST_F(DISABLED_FuncAgentMgrTest, RegisterParallel)
{
    const uint32_t FUNC_AGENT_NUM = 10;

    std::vector<shared_ptr<MockFunctionAgent>> funcAgents(FUNC_AGENT_NUM);
    auto msgs = RegisterFuncAgents("RegisterParallel", funcAgents);

    for (size_t i = 0; i < FUNC_AGENT_NUM; ++i) {
        auto val = msgs[i].Get(1000);
        ASSERT_EQ(val.IsSome(), true);

        EXPECT_FALSE(val.Get().empty());
        messages::Registered registeredMsg;
        auto parseRet = registeredMsg.ParseFromString(val.Get());
        EXPECT_TRUE(parseRet);
        EXPECT_EQ(registeredMsg.code(), StatusCode::SUCCESS);
        auto expectRegisteredMsg = "RegisterParallel_agent_AID" + std::to_string(i) + " " + REGISTER_SUCCESS_MESSAGE;
        EXPECT_STREQ(registeredMsg.message().c_str(), expectRegisteredMsg.c_str());
    }

    for (size_t i = 0; i < FUNC_AGENT_NUM; ++i) {
        auto funcAgentID = "RegisterParallel_agent_AID" + std::to_string(i);
        ASSERT_AWAIT_TRUE([=]() -> bool { return CheckIsRegister(funcAgentMgr_, funcAgentID, nullptr); });
    }

    std::cout << funcAgentMgr_->Dump() << std::endl;

    // clean
    funcAgents.clear();
    TerminateFcAgents(funcAgents);
}

inline std::shared_ptr<messages::DeployInstanceRequest> GenDeployInstanceRequest(const std::string &requestID,
                                                                                 const std::string &instanceID,
                                                                                 const std::string &traceID)
{
    auto req = std::make_shared<messages::DeployInstanceRequest>();
    req->set_requestid(requestID);
    req->set_instanceid(instanceID);
    req->set_traceid(traceID);

    auto spec = req->mutable_funcdeployspec();
    spec->set_accesskey("mock_accesskey");
    spec->set_secretaccesskey("mock_secretaccesskey");
    spec->set_token("mock_token");
    return req;
}

inline std::shared_ptr<messages::StaticFunctionChangeRequest> GenStaticFunctionChangeRequest(
    const std::string &requestID, const std::string &instanceID, int32_t status)
{
    auto staticFunctionChangeRequest = std::make_shared<messages::StaticFunctionChangeRequest>();
    staticFunctionChangeRequest->set_instanceid(instanceID);
    staticFunctionChangeRequest->set_requestid(requestID);
    staticFunctionChangeRequest->set_status(status);
    return staticFunctionChangeRequest;
}

TEST_F(DISABLED_FuncAgentMgrTest, DeployInstanceSuccess)
{
    litebus::Future<std::string> mockMsg;

    messages::DeployInstanceResponse mockResp =
        GenDeployInstanceResponse(StatusCode::SUCCESS, DEPLOY_SUCCESS_MESSAGE, REQUEST_ID);
    EXPECT_CALL(*funcAgent_.get(), MockDeployInstance(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&mockMsg),
                                       testing::Return(std::pair<bool, string>(true, mockResp.SerializeAsString()))));

    auto req = GenDeployInstanceRequest(REQUEST_ID, INSTANCE_ID, TRACE_ID);
    auto ret = funcAgentMgr_->DeployInstance(req, randomFuncAgentName_);

    auto resp = ret.Get(1000);
    ASSERT_TRUE(resp.IsSome());
    EXPECT_STREQ(resp.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(resp.Get().code(), StatusCode::SUCCESS);
    EXPECT_STREQ(resp.Get().message().c_str(), DEPLOY_SUCCESS_MESSAGE.c_str());

    auto msg = mockMsg.Get(1000);
    ASSERT_TRUE(msg.IsSome());
    ASSERT_FALSE(msg.Get().empty());

    messages::DeployInstanceRequest testReq;
    auto parseRet = testReq.ParseFromString(msg.Get());
    ASSERT_TRUE(parseRet);
    EXPECT_TRUE(testReq.requestid() == req->requestid());
    // reveive rotation token
    EXPECT_TRUE(testReq.funcdeployspec().accesskey() == "mock_accesskey");
    EXPECT_TRUE(testReq.funcdeployspec().secretaccesskey() == "mock_secretaccesskey");
    EXPECT_TRUE(testReq.funcdeployspec().token() == "mock_token");
}

TEST_F(DISABLED_FuncAgentMgrTest, DeployInstanceAgentExit)
{
    litebus::Future<std::string> mockMsg;

    messages::DeployInstanceResponse mockResp =
        GenDeployInstanceResponse(StatusCode::SUCCESS, DEPLOY_SUCCESS_MESSAGE, REQUEST_ID);
    EXPECT_CALL(*funcAgent_.get(), MockDeployInstance(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&mockMsg),
                                       testing::Return(std::pair<bool, string>(false, mockResp.SerializeAsString()))));
    EXPECT_CALL(*resourceView_.get(), DeleteResourceUnit(testing::_))
        .WillOnce(testing::Return(Status(StatusCode::SUCCESS)));
    EXPECT_CALL(*instCtrl_.get(), PutFailedInstanceStatusByAgentId(testing::_)).WillOnce(testing::Return());

    auto req = GenDeployInstanceRequest(REQUEST_ID, INSTANCE_ID, TRACE_ID);
    auto ret = funcAgentMgr_->DeployInstance(req, randomFuncAgentName_);
    litebus::Async(funcAgentMgr_->GetActorAID(), &FunctionAgentMgrActor::TimeoutEvent, randomFuncAgentName_);

    ASSERT_AWAIT_READY(ret);
    EXPECT_STREQ(ret.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(ret.Get().code(), StatusCode::ERR_INNER_COMMUNICATION);
    EXPECT_STREQ(ret.Get().message().c_str(), AGENT_EXITED_MESSAGE.c_str());
}

TEST_F(DISABLED_FuncAgentMgrTest, DeployInstanceParallel)
{
    const uint32_t REQUEST_NUM = 500;
    const uint32_t FUNC_AGENT_NUM = 10;

    std::vector<shared_ptr<MockFunctionAgent>> funcAgents(FUNC_AGENT_NUM);
    RegisterFuncAgents("DeployInstanceParallel", funcAgents);

    std::vector<shared_ptr<FunctionAgentHelper>> funcAgentHelpers(FUNC_AGENT_NUM);
    for (size_t i = 0; i < FUNC_AGENT_NUM; ++i) {
        funcAgentHelpers[i] = make_shared<FunctionAgentHelper>();
        EXPECT_CALL(*funcAgents[i].get(), MockDeployInstance(testing::_, testing::_, testing::_))
            .WillRepeatedly(testing::Invoke(funcAgentHelpers[i].get(), &FunctionAgentHelper::MockDeployInstance));
    }

    std::vector<litebus::Future<messages::DeployInstanceResponse>> rets;
    for (size_t i = 0; i < REQUEST_NUM; ++i) {
        auto req = GenDeployInstanceRequest(REQUEST_ID + std::to_string(i), INSTANCE_ID, TRACE_ID);
        rets.emplace_back(funcAgentMgr_->DeployInstance(
            req, "DeployInstanceParallel_agent_AID" + std::to_string(i % FUNC_AGENT_NUM)));
    }

    for (size_t i = 0; i < REQUEST_NUM; ++i) {
        auto resp = rets[i].Get(15000);
        ASSERT_TRUE(resp.IsSome());
        auto expectRequestID = REQUEST_ID + std::to_string(i);
        EXPECT_STREQ(resp.Get().requestid().c_str(), expectRequestID.c_str());
        EXPECT_EQ(resp.Get().code(), StatusCode::SUCCESS);
        EXPECT_STREQ(resp.Get().message().c_str(), DEPLOY_SUCCESS_MESSAGE.c_str());
    }

    // clean
    funcAgentMgr_->ClearFuncAgentsRegis();
    TerminateFcAgents(funcAgents);
    funcAgents.clear();
}

TEST_F(DISABLED_FuncAgentMgrTest, DeployInstanceRetrySuccess)
{
    messages::DeployInstanceResponse mockResp =
        GenDeployInstanceResponse(StatusCode::SUCCESS, DEPLOY_SUCCESS_MESSAGE, REQUEST_ID);
    EXPECT_CALL(*funcAgent_.get(), MockDeployInstance(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(std::pair<bool, string>{ false, "" }))
        .WillOnce(testing::Return(std::pair<bool, string>{ false, "" }))
        .WillRepeatedly(testing::Return(std::pair<bool, string>{ true, mockResp.SerializeAsString() }));

    auto req = GenDeployInstanceRequest(REQUEST_ID, INSTANCE_ID, TRACE_ID);

    auto ret = funcAgentMgr_->DeployInstance(req, randomFuncAgentName_);

    auto resp = ret.Get(1000);
    ASSERT_EQ(resp.IsSome(), true);
    EXPECT_STREQ(resp.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(resp.Get().code(), StatusCode::SUCCESS);
    EXPECT_STREQ(resp.Get().message().c_str(), DEPLOY_SUCCESS_MESSAGE.c_str());
}

/**
 * Feature: DeployInstance
 * Description: deploy instance fail
 * Steps:
 * 1. deploy instance to an unregistered agent
 * 2. retry deploy instance to an registered agent
 * Expectation: deploy instance response is error
 */
TEST_F(DISABLED_FuncAgentMgrTest, DeployInstanceFail)
{
    auto req = GenDeployInstanceRequest(REQUEST_ID, INSTANCE_ID, TRACE_ID);
    auto ret = funcAgentMgr_->DeployInstance(req, "setupFuncAgent_01-58866123");
    auto resp = ret.Get(1000);
    ASSERT_EQ(resp.IsSome(), true);
    EXPECT_STREQ(resp.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(resp.Get().code(), StatusCode::ERR_INNER_COMMUNICATION);
    EXPECT_STREQ(resp.Get().message().c_str(), "function agent is not register");

    EXPECT_CALL(*funcAgent_.get(), MockDeployInstance(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::Return(std::pair<bool, string>{ false, "" }));
    req = GenDeployInstanceRequest(REQUEST_ID, INSTANCE_ID, TRACE_ID);
    ret = funcAgentMgr_->DeployInstance(req, randomFuncAgentName_);
    resp = ret.Get(1000);
    ASSERT_EQ(resp.IsSome(), true);
    EXPECT_STREQ(resp.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(resp.Get().code(), StatusCode::ERR_INNER_COMMUNICATION);
    EXPECT_STREQ(resp.Get().message().c_str(), DEPLOY_RETRY_FAIL_MESSAGE.c_str());
}

TEST_F(DISABLED_FuncAgentMgrTest, KillInstanceSuccess)
{
    litebus::Future<std::string> mockMsg;

    messages::KillInstanceResponse mockResp =
        GenKillInstanceResponse(StatusCode::SUCCESS, KILL_SUCCESS_MESSAGE, REQUEST_ID);
    EXPECT_CALL(*funcAgent_.get(), MockKillInstance(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&mockMsg),
                                       testing::Return(std::pair<bool, string>(true, mockResp.SerializeAsString()))));

    auto req = GenKillInstanceRequest(REQUEST_ID, FUNCTION_NAME, TRACE_ID, STORAGE_TYPE);

    auto ret = funcAgentMgr_->KillInstance(req, randomFuncAgentName_);
    auto resp = ret.Get(1000);
    ASSERT_TRUE(resp.IsSome());
    EXPECT_STREQ(resp.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(resp.Get().code(), StatusCode::SUCCESS);
    EXPECT_STREQ(resp.Get().message().c_str(), KILL_SUCCESS_MESSAGE.c_str());

    auto msg = mockMsg.Get(1000);
    ASSERT_TRUE(msg.IsSome());
    ASSERT_FALSE(msg.Get().empty());

    messages::DeployInstanceRequest testReq;
    auto parseRet = testReq.ParseFromString(msg.Get());
    ASSERT_TRUE(parseRet);
    EXPECT_TRUE(testReq.requestid() == req->requestid());
}

TEST_F(DISABLED_FuncAgentMgrTest, KillnstanceAgentExit)
{
    litebus::Future<std::string> mockMsg;

    messages::KillInstanceResponse mockResp =
        GenKillInstanceResponse(StatusCode::SUCCESS, KILL_SUCCESS_MESSAGE, REQUEST_ID);
    EXPECT_CALL(*funcAgent_.get(), MockKillInstance(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&mockMsg),
                                       testing::Return(std::pair<bool, string>(false, mockResp.SerializeAsString()))));
    EXPECT_CALL(*resourceView_.get(), DeleteResourceUnit(testing::_))
        .WillOnce(testing::Return(Status(StatusCode::SUCCESS)));
    EXPECT_CALL(*instCtrl_.get(), PutFailedInstanceStatusByAgentId(testing::_)).WillOnce(testing::Return());

    auto req = GenKillInstanceRequest(REQUEST_ID, FUNCTION_NAME, TRACE_ID, STORAGE_TYPE);
    auto ret = funcAgentMgr_->KillInstance(req, randomFuncAgentName_);
    litebus::Async(funcAgentMgr_->GetActorAID(), &FunctionAgentMgrActor::TimeoutEvent, randomFuncAgentName_);

    ASSERT_AWAIT_READY(ret);
    EXPECT_STREQ(ret.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(ret.Get().code(), StatusCode::SUCCESS);
    EXPECT_STREQ(ret.Get().message().c_str(), AGENT_MAY_EXITED_MESSAGE.c_str());
}

TEST_F(DISABLED_FuncAgentMgrTest, KillInstanceParallel)
{
    std::vector<shared_ptr<MockFunctionAgent>> funcAgents(FUNC_AGENT_NUM);
    RegisterFuncAgents("KillInstanceParallel", funcAgents);

    std::vector<shared_ptr<FunctionAgentHelper>> funcAgentHelpers(FUNC_AGENT_NUM);
    for (size_t i = 0; i < FUNC_AGENT_NUM; ++i) {
        funcAgentHelpers[i] = make_shared<FunctionAgentHelper>();
        EXPECT_CALL(*funcAgents[i].get(), MockKillInstance(testing::_, testing::_, testing::_))
            .WillRepeatedly(testing::Invoke(funcAgentHelpers[i].get(), &FunctionAgentHelper::MockKillInstance));
    }

    funcAgentMgr_->EnableAgents();
    std::vector<litebus::Future<messages::KillInstanceResponse>> rets;
    for (size_t i = 0; i < REQUEST_NUM; ++i) {
        auto req = GenKillInstanceRequest(REQUEST_ID + std::to_string(i), INSTANCE_ID, TRACE_ID, STORAGE_TYPE);
        rets.emplace_back(
            funcAgentMgr_->KillInstance(req, "KillInstanceParallel_agent_AID" + std::to_string(i % FUNC_AGENT_NUM)));
    }

    for (size_t i = 0; i < REQUEST_NUM; ++i) {
        auto resp = rets[i].Get(15000);
        ASSERT_TRUE(resp.IsSome());
        auto expectRequestID = REQUEST_ID + std::to_string(i);
        EXPECT_STREQ(resp.Get().requestid().c_str(), expectRequestID.c_str());
        EXPECT_EQ(resp.Get().code(), StatusCode::SUCCESS);
        EXPECT_STREQ(resp.Get().message().c_str(), KILL_SUCCESS_MESSAGE.c_str());
    }

    // clean
    funcAgentMgr_->ClearFuncAgentsRegis();
    TerminateFcAgents(funcAgents);
    funcAgents.clear();
    funcAgentHelpers.clear();
    rets.clear();
}

TEST_F(DISABLED_FuncAgentMgrTest, KillInstanceRetrySuccess)
{
    messages::KillInstanceResponse mockResp =
        GenKillInstanceResponse(StatusCode::SUCCESS, KILL_SUCCESS_MESSAGE, REQUEST_ID);
    EXPECT_CALL(*funcAgent_.get(), MockKillInstance(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(std::pair<bool, string>{ false, "" }))
        .WillOnce(testing::Return(std::pair<bool, string>{ false, "" }))
        .WillRepeatedly(testing::Return(std::pair<bool, string>{ true, mockResp.SerializeAsString() }));

    auto req = GenKillInstanceRequest(REQUEST_ID, FUNCTION_NAME, TRACE_ID, STORAGE_TYPE);
    auto ret = funcAgentMgr_->KillInstance(req, randomFuncAgentName_);

    auto resp = ret.Get(1000);
    ASSERT_TRUE(resp.IsSome());
    EXPECT_TRUE(resp.Get().requestid() == REQUEST_ID);
    EXPECT_EQ(resp.Get().code(), StatusCode::SUCCESS);
    EXPECT_STREQ(resp.Get().message().c_str(), KILL_SUCCESS_MESSAGE.c_str());
}

/**
 * Feature: KillInstance
 * Description: kill instance fail
 * Steps:
 * 1. kill instance to an unregistered agent
 * 2. retry kill instance to an registered agent
 * Expectation: kill instance response is error
 */
TEST_F(DISABLED_FuncAgentMgrTest, KillInstanceFail)
{
    auto req = GenKillInstanceRequest(REQUEST_ID, FUNCTION_NAME, TRACE_ID, STORAGE_TYPE);
    auto ret = funcAgentMgr_->KillInstance(req, "setupFuncAgent_01-58866123");
    auto resp = ret.Get(1000);
    ASSERT_TRUE(resp.IsSome());
    EXPECT_TRUE(resp.Get().requestid() == REQUEST_ID);
    EXPECT_EQ(resp.Get().code(), StatusCode::ERR_INNER_COMMUNICATION);
    EXPECT_STREQ(resp.Get().message().c_str(), "function agent not register");

    EXPECT_CALL(*funcAgent_.get(), MockKillInstance(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::Return(std::pair<bool, string>{ false, "" }));
    req = GenKillInstanceRequest(REQUEST_ID, FUNCTION_NAME, TRACE_ID, STORAGE_TYPE);
    ret = funcAgentMgr_->KillInstance(req, randomFuncAgentName_);
    resp = ret.Get(1000);
    ASSERT_TRUE(resp.IsSome());
    EXPECT_TRUE(resp.Get().requestid() == REQUEST_ID);
    EXPECT_EQ(resp.Get().code(), StatusCode::ERR_INNER_COMMUNICATION);
    EXPECT_STREQ(resp.Get().message().c_str(), KILL_RETRY_FAIL_MESSAGE.c_str());
}

TEST_F(DISABLED_FuncAgentMgrTest, UpdateResourcesInitAlready)
{
    messages::UpdateResourcesRequest resourceViewReq;

    auto resourceUnit = resourceViewReq.mutable_resourceunit();
    resourceUnit->set_id(randomFuncAgentName_);
    auto capacity = resourceUnit->mutable_capacity();
    auto resources = capacity->mutable_resources();

    resource_view::Resource resource;
    resource.set_name("CPU");
    resource.set_type(resource_view::ValueType::Value_Type_SCALAR);

    auto scalar = resource.mutable_scalar();
    scalar->set_limit(100);
    scalar->set_value(50);

    resources->insert({ "CPU", resource });

    litebus::Future<std::shared_ptr<resource_view::ResourceUnit>> futureView;
    EXPECT_CALL(*resourceView_.get(), UpdateResourceUnit(testing::_, testing::_))
        .WillOnce(testing::DoAll(FutureArg<0>(&futureView), testing::Return(Status(StatusCode::SUCCESS))));
    litebus::Async(funcAgent_->GetAID(), &MockFunctionAgent::UpdateResources, funcAgentMgr_->GetActorAID(),
                   resourceViewReq);

    auto ret = futureView.Get(1000);

    ASSERT_TRUE(ret.IsSome());

    auto resourceUnitActual = ret.Get();
    EXPECT_STREQ(resourceUnitActual->id().c_str(), randomFuncAgentName_.c_str());
    auto capacityActual = resourceUnitActual->capacity();
    auto resourcesActual = capacityActual.resources();
    EXPECT_EQ(resourcesActual["CPU"].scalar().limit(), 100);
    EXPECT_EQ(resourcesActual["CPU"].scalar().value(), 50);

    // clean
    resourceViewReq.clear_resourceunit();
}

/**
 * Feature: function agent manager.
 * Description: function agent manager update resource successfully when agent isn't initialized.
 * Steps:
 * 1. Mock function agent.
 * 2. Mock heartbeat add to return SUCCESS.
 * 3. Mock sync instance to return LS_SYNC_INSTANCE_COMPLETE.
 * 4. Mock AddResourceUnit to return SUCCESS.
 * 5. send request of update resource.
 * Expectation: function agent don't init and add resource of function agent when updating resource.
 */
TEST_F(DISABLED_FuncAgentMgrTest, UpdateResourcesNoInit)
{
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;
    auto funcAgent = make_shared<MockFunctionAgent>(randomFuncAgentName_, randomFuncAgentName_,
                                                    SETUP_LOCAL_SCHEDULER_AID, s3Config, codePackageThresholds);
    litebus::Spawn(funcAgent);

    std::string jsonStr;
    (void)google::protobuf::util::MessageToJsonString(funcAgentRegisInfoInit_, &jsonStr);

    messages::Register registerMsg = GenRegister(TEST_FUNC_AGENT_NAME, funcAgent->GetAID().UnfixUrl(), jsonStr);
    auto resourceUnit = registerMsg.mutable_resource();
    resourceUnit->set_id(TEST_FUNC_AGENT_NAME);
    auto instances = resourceUnit->mutable_instances();

    resource_view::InstanceInfo instanceInfo;
    instanceInfo.set_instanceid(INSTANCE_ID);
    instances->insert({ INSTANCE_ID, instanceInfo });

    EXPECT_CALL(*funcAgent.get(), MockRegister).WillOnce(testing::Return(registerMsg.SerializeAsString()));

    litebus::Future<string> registeredMsg;
    EXPECT_CALL(*funcAgent.get(), MockRegistered(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&registeredMsg)));

    EXPECT_CALL(*resourceView_.get(), AddResourceUnit(testing::_))
        .WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

    litebus::Future<shared_ptr<resource_view::ResourceUnit>> syncResourceUnitMsg;
    EXPECT_CALL(*instCtrl_.get(), SyncInstances(testing::_))
        .WillOnce(testing::DoAll(testing::DoAll(FutureArg<0>(&syncResourceUnitMsg),
                                                testing::Return(Status(StatusCode::LS_SYNC_INSTANCE_COMPLETE)))));

    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::RegisterToLocalScheduler, funcAgentMgr_->GetActorAID());

    ASSERT_AWAIT_TRUE([=]() -> bool { return CheckIsRegister(funcAgentMgr_, TEST_FUNC_AGENT_NAME, nullptr);});

    messages::UpdateResourcesRequest resourceViewReq;

    resourceUnit = resourceViewReq.mutable_resourceunit();
    resourceUnit->set_id(randomFuncAgentName_);
    auto capacity = resourceUnit->mutable_capacity();
    auto resources = capacity->mutable_resources();

    resource_view::Resource resource;
    resource.set_name(resource_view::CPU_RESOURCE_NAME);
    resource.set_type(resource_view::ValueType::Value_Type_SCALAR);

    auto scalar = resource.mutable_scalar();
    scalar->set_limit(100);
    scalar->set_value(50);

    resources->insert({ resource_view::CPU_RESOURCE_NAME, resource });

    litebus::Future<resource_view::ResourceUnit> futureView;
    EXPECT_CALL(*resourceView_.get(), AddResourceUnit(testing::_))
        .WillOnce(testing::DoAll(FutureArg<0>(&futureView), testing::Return(Status(StatusCode::SUCCESS))));
    litebus::Async(funcAgent->GetAID(), &MockFunctionAgent::UpdateResources, funcAgentMgr_->GetActorAID(),
                   resourceViewReq);

    ASSERT_AWAIT_READY(futureView);

    auto resourceUnitActual = futureView.Get();
    EXPECT_STREQ(resourceUnitActual.id().c_str(), randomFuncAgentName_.c_str());
    auto capacityActual = resourceUnitActual.capacity();
    auto resourcesActual = capacityActual.resources();
    EXPECT_EQ(resourcesActual[resource_view::CPU_RESOURCE_NAME].scalar().limit(), 100);
    EXPECT_EQ(resourcesActual[resource_view::CPU_RESOURCE_NAME].scalar().value(), 50);

    resourceUnit->clear_instances();
    registerMsg.clear_resource();
    resourceViewReq.clear_resourceunit();
}

TEST_F(DISABLED_FuncAgentMgrTest, UpdateInstanceStatus)
{
    messages::UpdateInstanceStatusRequest infoReq;
    auto info = infoReq.mutable_instancestatusinfo();
    info->set_requestid(REQUEST_ID);
    info->set_instanceid(INSTANCE_ID);
    info->set_instancemsg("instance is failed");
    info->set_status(15);

    litebus::Future<shared_ptr<InstanceExitStatus>> futureInfo;
    EXPECT_CALL(*instCtrl_.get(), UpdateInstanceStatus(testing::_))
        .WillOnce(testing::DoAll(FutureArg<0>(&futureInfo), testing::Return(Status(StatusCode::SUCCESS))));

    litebus::Future<string> resp;
    EXPECT_CALL(*funcAgent_.get(), MockUpdateInstanceStatusResponse(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(testing::DoAll(FutureArg<2>(&resp))));
    litebus::Async(funcAgent_->GetAID(), &MockFunctionAgent::UpdateInstanceStatus, funcAgentMgr_->GetActorAID(),
                   infoReq);

    auto ret = futureInfo.Get(1000);

    ASSERT_TRUE(ret.IsSome());
    EXPECT_TRUE(ret.Get()->instanceID == INSTANCE_ID);
    EXPECT_TRUE(ret.Get()->errCode == static_cast<int32_t>(common::ErrorCode::ERR_INSTANCE_EXITED));
    EXPECT_TRUE(ret.Get()->statusMsg == "instance is failed");

    auto respStr = resp.Get(1000);
    ASSERT_TRUE(respStr.IsSome());

    messages::UpdateInstanceStatusResponse respVal;
    auto parseVal = respVal.ParseFromString(respStr.Get());

    EXPECT_TRUE(parseVal);
    EXPECT_STREQ(respVal.requestid().c_str(), REQUEST_ID.c_str());
}

TEST_F(DISABLED_FuncAgentMgrTest, UpdateInstanceDiskUsageExceedLimitStatus)
{
    messages::UpdateInstanceStatusRequest infoReq;
    auto info = infoReq.mutable_instancestatusinfo();
    info->set_requestid(REQUEST_ID);
    info->set_instanceid(INSTANCE_ID);
    info->set_instancemsg("disk usage exceed limit");
    info->set_status(15);
    info->set_type(static_cast<int32_t>(EXIT_TYPE::EXCEPTION_INFO));

    litebus::Future<shared_ptr<InstanceExitStatus>> futureInfo;
    EXPECT_CALL(*instCtrl_.get(), UpdateInstanceStatus(testing::_))
        .WillOnce(testing::DoAll(FutureArg<0>(&futureInfo), testing::Return(Status(StatusCode::SUCCESS))));

    litebus::Future<string> resp;
    EXPECT_CALL(*funcAgent_.get(), MockUpdateInstanceStatusResponse(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(testing::DoAll(FutureArg<2>(&resp))));
    litebus::Async(funcAgent_->GetAID(), &MockFunctionAgent::UpdateInstanceStatus, funcAgentMgr_->GetActorAID(),
                   infoReq);

    ASSERT_AWAIT_READY(futureInfo);
    ASSERT_TRUE(futureInfo.IsOK());
    EXPECT_TRUE(futureInfo.Get()->instanceID == INSTANCE_ID);
    EXPECT_TRUE(futureInfo.Get()->errCode == static_cast<int32_t>(common::ErrorCode::ERR_USER_FUNCTION_EXCEPTION));
    EXPECT_TRUE(futureInfo.Get()->statusMsg == "disk usage exceed limit");

    ASSERT_AWAIT_READY(resp);
    ASSERT_TRUE(resp.IsOK());

    messages::UpdateInstanceStatusResponse respVal;
    auto parseVal = respVal.ParseFromString(resp.Get());

    EXPECT_TRUE(parseVal);
    EXPECT_STREQ(respVal.requestid().c_str(), REQUEST_ID.c_str());
}

TEST_F(DISABLED_FuncAgentMgrTest, UpdateDiskUsageLimit)
{
    litebus::Future<string> futureResult;
    EXPECT_CALL(*localSchedSrv_, DeletePod(_, _, _)).WillOnce(DoAll(FutureArg<1>(&futureResult), Return()));

    messages::UpdateAgentStatusRequest request;
    request.set_requestid("testRequestID");
    request.set_status(RUNTIME_MANAGER_DISK_USAGE_EXCEED_LIMIT);
    funcAgent_->UpdateAgentStatus(funcAgentMgr_->GetActorAID(), request);
    EXPECT_AWAIT_TRUE([&]() -> bool { return futureResult.Get() == "testRequestID"; });
}

/**
 * Feature: function agent manager.
 * Description: UpdateAgent.
 * Steps:
 * 1. Deploy Instance
 * 2. Update Agent Status
 * Expectation:
 * 1. Get Kill Request
 * 2. Get Update Agent status response
 */
TEST_F(DISABLED_FuncAgentMgrTest, UpdateAgent)
{
    messages::DeployInstanceResponse mockDeployResp =
        GenDeployInstanceResponse(StatusCode::SUCCESS, DEPLOY_SUCCESS_MESSAGE, REQUEST_ID);
    EXPECT_CALL(*funcAgent_.get(), MockDeployInstance(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::Return(std::pair<bool, string>(true, mockDeployResp.SerializeAsString())));

    auto req = GenDeployInstanceRequest(REQUEST_ID, INSTANCE_ID, TRACE_ID);
    auto ret = funcAgentMgr_->DeployInstance(req, randomFuncAgentName_);
}

TEST_F(DISABLED_FuncAgentMgrTest, UpdateAgentExit)
{
    litebus::Future<string> resp;
    messages::UpdateAgentStatusRequest request;
    request.set_requestid("testRequestID");
    request.set_status(FUNC_AGENT_EXITED);
    EXPECT_CALL(*instCtrl_.get(), PutFailedInstanceStatusByAgentId(testing::_)).WillOnce(testing::Return());
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse)).WillOnce(Return(putResponse));
    litebus::Future<string> futureResult;
    EXPECT_CALL(*localSchedSrv_, DeletePod(_, _, _))
        .WillOnce(DoAll(FutureArg<1>(&futureResult), Return()));
    funcAgent_->UpdateAgentStatus(funcAgentMgr_->GetActorAID(), request);
    EXPECT_TRUE(futureResult.Get() == "testRequestID");
}

TEST_F(DISABLED_FuncAgentMgrTest, DiskUsageExceedLimit_UpdateAgentExit)
{
    litebus::Future<string> resp;
    messages::UpdateAgentStatusRequest request;
    request.set_requestid("testRequestID");
    request.set_status(RUNTIME_MANAGER_DISK_USAGE_EXCEED_LIMIT);
    EXPECT_CALL(*instCtrl_.get(), PutFailedInstanceStatusByAgentId(testing::_)).WillOnce(testing::Return());
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse)).WillOnce(Return(putResponse));
    litebus::Future<string> futureResult;
    EXPECT_CALL(*localSchedSrv_, DeletePod(_, _, _))
        .WillOnce(DoAll(FutureArg<1>(&futureResult), Return()));
    funcAgent_->UpdateAgentStatus(funcAgentMgr_->GetActorAID(), request);
    EXPECT_TRUE(futureResult.Get() == "testRequestID");
}

TEST_F(DISABLED_FuncAgentMgrTest, PutAgentRegisInfoWithProxyNodeIDSuccess)
{
    /* *json string:
     {"funcAgentRegisInfoMap":
        {"function_agent_127.0.0.1-58866":
            {
                "agentAID":"AgentServiceActor@127.0.0.1:58866",
                "agentID":"function_agent_127.0.0.1-58866",
                "agentAddress":"127.0.0.1:58866",
                "runtimeMgrAID":"dggphicprd30662-RuntimeManagerSrv",
                "runtimeMgrID":"c86f4404-0000-4000-8000-00347ac832c2",
                "statusCode":2
            },
         "function_agent_127.0.0.1-58866":
            {
                "agentAID":"AgentServiceActor@127.0.0.1:58866",
                "agentID":"function_agent_127.0.0.1-58866",
                "agentAddress":"127.0.0.1:58866",
                "runtimeMgrAID":"dggphicprd30662-RuntimeManagerSrv",
                "runtimeMgrID":"34040000-0000-4000-80bd-f25604551989",
                "statusCode":1
             }
        }
    }
    */
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    for (int i = 0; i < 5; i++) {
        messages::FuncAgentRegisInfo info;
        info.set_agentaidname("agent_aid_name_" + std::to_string(i));
        info.set_agentaddress("agent_address_" + std::to_string(i));
        info.set_runtimemgraid("runtime_manager_aid_" + std::to_string(i));
        info.set_runtimemgrid("runtime_manager_randomid_" + std::to_string(i));
        info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
        funcAgentsRegis["agent_aid_" + std::to_string(i)] = info;
    }

    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));
    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    litebus::Future<Status> future = funcAgentMgr_->PutAgentRegisInfoWithProxyNodeID();
    EXPECT_TRUE(future.Get().IsOk());
    funcAgentMgr_->ClearFuncAgentsRegis();

    // clean
    funcAgentsRegis.clear();
}

TEST_F(DISABLED_FuncAgentMgrTest, PutAgentRegisInfoWithProxyNodeIDFailed)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    for (int i = 0; i < 5; i++) {
        messages::FuncAgentRegisInfo info;
        info.set_agentaidname("agent_aid_name_" + std::to_string(i));
        info.set_agentaddress("agent_address_" + std::to_string(i));
        info.set_runtimemgraid("runtime_manager_aid_" + std::to_string(i));
        info.set_runtimemgrid("runtime_manager_randomid_" + std::to_string(i));
        info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
        funcAgentsRegis["agent_id_" + std::to_string(i)] = info;
    }

    auto putResponse = std::make_shared<PutResponse>();
    putResponse->status = Status(StatusCode::BP_META_STORAGE_PUT_ERROR, "error");
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));
    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    litebus::Future<Status> future = funcAgentMgr_->PutAgentRegisInfoWithProxyNodeID();
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::BP_META_STORAGE_PUT_ERROR);
    funcAgentMgr_->ClearFuncAgentsRegis();

    // clean
    funcAgentsRegis.clear();
}

TEST_F(DISABLED_FuncAgentMgrTest, RetrieveAgentRegisInfoSuccess)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    for (int i = 0; i < 5; i++) {
        messages::FuncAgentRegisInfo info;
        info.set_agentaidname("RetrieveAgentRegisInfoSuccess_agent_aid_name_" + std::to_string(i));
        info.set_agentaddress("RetrieveAgentRegisInfoSuccess_agent_address_" + std::to_string(i));
        info.set_runtimemgraid("RetrieveAgentRegisInfoSuccess_runtime_manager_aid_" + std::to_string(i));
        info.set_runtimemgrid("RetrieveAgentRegisInfoSuccess_runtime_manager_randomid_" + std::to_string(i));
        info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
        funcAgentsRegis["RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i)] = info;
    }

    funcAgentMgr_->ClearFuncAgentsRegis();

    std::string jsonStr = FuncAgentRegisToCollectionStrHelper(funcAgentsRegis);
    std::string nodeID = funcAgentMgr_->GetNodeID();
    KeyValue kv;
    kv.set_key("funcAgentRegisInfos");
    kv.set_value(jsonStr);

    auto getResponse = std::make_shared<GetResponse>();
    getResponse->kvs.push_back(kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillRepeatedly(Return(getResponse));

    // set funcAgentResUpdatedMap_ in case recover stuck
    for (int i = 0; i < 5; i++) {
        std::shared_ptr<resource_view::ResourceUnit> resourceUnit = std::make_shared<resource_view::ResourceUnit>();
        resourceUnit->set_id("RetrieveAgentRegisInfoSuccess_agent_aid_" + std::to_string(i));
        auto instances = resourceUnit->mutable_instances();
        resource_view::InstanceInfo instanceInfo;
        instanceInfo.set_instanceid("RetrieveAgentRegisInfoSuccess_instance_id_" + std::to_string(i));
        instances->insert({ "RetrieveAgentRegisInfoSuccess_instance_id_" + std::to_string(i), instanceInfo });
        funcAgentMgr_->SetFuncAgentUpdateMapPromise("RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i),
                                                    resourceUnit);
    }

    // mock call
    auto future = funcAgentMgr_->Sync();
    ASSERT_AWAIT_READY(future);
    auto regisInfo = funcAgentMgr_->GetFuncAgentsRegis();
    EXPECT_EQ(regisInfo.size(), static_cast<size_t>(5));
    for (int i = 0; i < 5; i++) {
        if (regisInfo.find("RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i)) == regisInfo.end()) {
            auto iter = regisInfo.find("RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i));
            EXPECT_TRUE(iter != regisInfo.end());
            messages::FuncAgentRegisInfo msg = iter->second;
            EXPECT_EQ(msg.agentaidname(), "RetrieveAgentRegisInfoSuccess_agent_aid_name_" + std::to_string(i));
            EXPECT_EQ(msg.agentaddress(), "RetrieveAgentRegisInfoSuccess_agent_address_" + std::to_string(i));
            EXPECT_EQ(msg.runtimemgraid(), "RetrieveAgentRegisInfoSuccess_runtime_manager_aid_" + std::to_string(i));
            EXPECT_EQ(msg.runtimemgrid(),
                      "RetrieveAgentRegisInfoSuccess_runtime_manager_randomid_" + std::to_string(i));
            EXPECT_EQ(msg.statuscode(), 1);
        }
    }
    // clean
    funcAgentsRegis.clear();
}

/**
 * Tests when get from etcd failed, retry 3 times, and print error message
 */
TEST_F(DISABLED_FuncAgentMgrTest, RetrieveAgentRegisInfoFailed)
{
    funcAgentMgr_->ClearFuncAgentsRegis();
    auto getResponse = std::make_shared<GetResponse>();
    getResponse->status = Status(StatusCode::LS_META_STORAGE_GET_ERROR, "Get failed");
    EXPECT_CALL(*mockMetaStoreClient_, Get)
        .WillOnce(Return(getResponse));
    auto promise = std::make_shared<litebus::Promise<std::unordered_map<std::string, messages::FuncAgentRegisInfo>>>();
    auto future = funcAgentMgr_->Sync();
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsError(), true);
    funcAgentMgr_->ClearFuncAgentsRegis();
}

TEST_F(DISABLED_FuncAgentMgrTest, RetrieveAgentRegisInfoWithFailedStatusSuccess)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    for (int i = 0; i < 5; i++) {
        messages::FuncAgentRegisInfo info;
        info.set_agentaidname("RetrieveAgentRegisInfoSuccess_agent_aid_name_" + std::to_string(i));
        info.set_agentaddress("RetrieveAgentRegisInfoSuccess_agent_address_" + std::to_string(i));
        info.set_runtimemgraid("RetrieveAgentRegisInfoSuccess_runtime_manager_aid_" + std::to_string(i));
        info.set_runtimemgrid("RetrieveAgentRegisInfoSuccess_runtime_manager_randomid_" + std::to_string(i));
        if (i < 3) {
            info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
        } else {
            info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::FAILED));
        }

        funcAgentsRegis["RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i)] = info;
    }
    funcAgentMgr_->ClearFuncAgentsRegis();

    std::string jsonStr = FuncAgentRegisToCollectionStrHelper(funcAgentsRegis);
    std::string nodeID = funcAgentMgr_->GetNodeID();
    KeyValue kv;
    kv.set_key("funcAgentRegisInfos");
    kv.set_value(jsonStr);

    auto getResponse = std::make_shared<GetResponse>();
    getResponse->kvs.push_back(std::move(kv));
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillRepeatedly(Return(getResponse));

    // set funcAgentResUpdatedMap_ in case recover stuck
    for (int i = 0; i < 5; i++) {
        std::shared_ptr<resource_view::ResourceUnit> resourceUnit = std::make_shared<resource_view::ResourceUnit>();
        resourceUnit->set_id("RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i));
        auto instances = resourceUnit->mutable_instances();
        resource_view::InstanceInfo instanceInfo;
        instanceInfo.set_instanceid("RetrieveAgentRegisInfoSuccess_instance_id_" + std::to_string(i));
        instances->insert({ "RetrieveAgentRegisInfoSuccess_instance_id_" + std::to_string(i), instanceInfo });
        funcAgentMgr_->SetFuncAgentUpdateMapPromise("RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i),
                                                    resourceUnit);
    }

    // mock call
    auto future = funcAgentMgr_->Sync();
    ASSERT_AWAIT_READY(future);
    auto regisInfo = funcAgentMgr_->GetFuncAgentsRegis();
    EXPECT_EQ(regisInfo.size(), static_cast<size_t>(5));
    for (int i = 0; i < 5; i++) {
        if (regisInfo.find("RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i)) == regisInfo.end()) {
            auto iter = regisInfo.find("RetrieveAgentRegisInfoSuccess_agent_id_" + std::to_string(i));
            EXPECT_TRUE(iter != regisInfo.end());
            messages::FuncAgentRegisInfo msg = iter->second;
            EXPECT_EQ(msg.agentaidname(), "RetrieveAgentRegisInfoSuccess_agent_aid_name_" + std::to_string(i));
            EXPECT_EQ(msg.agentaddress(), "RetrieveAgentRegisInfoSuccess_agent_address_" + std::to_string(i));
            EXPECT_EQ(msg.runtimemgraid(), "RetrieveAgentRegisInfoSuccess_runtime_manager_aid_" + std::to_string(i));
            EXPECT_EQ(msg.runtimemgrid(),
                      "RetrieveAgentRegisInfoSuccess_runtime_manager_randomid_" + std::to_string(i));
            EXPECT_EQ(msg.statuscode(), 1);
        }
    }
    // clean
    funcAgentsRegis.clear();
}

TEST_F(DISABLED_FuncAgentMgrTest, StaticFunctionScheduleRequest)
{
    auto funcAgentMgr =
        make_shared<local_scheduler::FunctionAgentMgr>(make_shared<local_scheduler::FunctionAgentMgrActor>(
            "RecoverHeartBeatSuccessActor", PARAM, "nodeID", mockMetaStoreClient_));
    funcAgentMgr->SetNodeID("nodeID");
    funcAgentMgr->ToReady();

    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info1;
    info1.set_agentaidname("RecoverHeartBeatSuccess_agent_aid_name");
    info1.set_agentaddress("RecoverHeartBeatSuccess_agent_address");
    info1.set_runtimemgraid("RecoverHeartBeatSuccess_runtime_manager_aid");
    info1.set_runtimemgrid("RecoverHeartBeatSuccess_runtime_manager_randomid");
    info1.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::FAILED));
    funcAgentsRegis["RecoverHeartBeatSuccess_agent_id"] = info1;

    messages::FuncAgentRegisInfo info2;
    info2.set_agentaidname("RecoverHeartBeatSuccessActor");
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    info2.set_agentaddress("127.0.0.1:" + std::to_string(port));
    info2.set_runtimemgraid("funcAgentMgr_runtime_manager_aid");
    info2.set_runtimemgrid("funcAgentMgr_runtime_manager_randomid");
    info2.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
    funcAgentsRegis["funcAgentMgr"] = info2;

    std::string jsonStr = FuncAgentRegisToCollectionStrHelper(funcAgentsRegis);
    std::cout << "serialized jsonStr:" << jsonStr << std::endl;
    std::string nodeID = funcAgentMgr->GetNodeID();
    KeyValue kv;
    kv.set_key("funcAgentRegisInfos");
    kv.set_value(jsonStr);

    auto getResponse = std::make_shared<GetResponse>();
    getResponse->kvs.push_back(std::move(kv));
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));

    EXPECT_CALL(*localSchedSrv_, NotifyEvictResult(_)).WillOnce(Return());
    EXPECT_CALL(*mockBundleMgr_, SyncBundles).WillOnce(Return(Status::OK()));

    std::shared_ptr<resource_view::ResourceUnit> resourceUnit = std::make_shared<resource_view::ResourceUnit>();
    resourceUnit->set_id("funcAgentMgr");
    auto instances = resourceUnit->mutable_instances();
    resource_view::InstanceInfo instanceInfo;
    instanceInfo.set_instanceid("funcAgentMgr_instance_id");
    instances->insert({ "funcAgentMgr_instance_id", instanceInfo });

    messages::UpdateResourcesRequest resourceViewReq;
    auto resourceUnit2 = resourceViewReq.mutable_resourceunit();
    resourceUnit2->set_id("funcAgentMgr");
    auto capacity = resourceUnit2->mutable_capacity();
    auto resources = capacity->mutable_resources();

    auto instances2 = resourceUnit2->mutable_instances();
    resource_view::InstanceInfo instanceInfo2;
    instanceInfo2.set_instanceid("funcAgentMgr_instance_id");
    instances2->insert({ "funcAgentMgr_instance_id", instanceInfo2 });

    resource_view::Resource resource;
    resource.set_name("CPU");
    resource.set_type(resource_view::ValueType::Value_Type_SCALAR);

    auto scalar = resource.mutable_scalar();
    scalar->set_limit(100);
    scalar->set_value(50);
    resources->insert({ "CPU", resource });

    funcAgentMgr->Start(instCtrl_, resourceView_);
    funcAgentMgr->BindLocalSchedSrv(localSchedSrv_);
    funcAgentMgr->BindBundleMgr(mockBundleMgr_);
    auto future = funcAgentMgr->Sync();
    ASSERT_AWAIT_READY(future);
    future = funcAgentMgr->Recover();
    ASSERT_AWAIT_READY(future);

    ASSERT_AWAIT_TRUE([=]() -> bool {
        auto lambda = [=](){
            std::string name = "funcAgentMgr";
            funcAgentMgr->UpdateResources(funcAgentMgr->GetActorAID(), std::move(name),
                                          resourceViewReq.SerializeAsString());
        };
        return CheckIsRegister(funcAgentMgr, "funcAgentMgr", lambda);
    });

    // clean
    resourceViewReq.clear_resourceunit();
    funcAgentsRegis.clear();
}

TEST_F(DISABLED_FuncAgentMgrTest, RecoverHeartBeatEmptySuccess)
{
    auto funcAgentMgr =
        make_shared<local_scheduler::FunctionAgentMgr>(make_shared<local_scheduler::FunctionAgentMgrActor>(
            "RecoverHeartBeatSuccessActor", PARAM, "nodeID", mockMetaStoreClient_));
    funcAgentMgr->SetNodeID("nodeID");
    funcAgentMgr->Start(instCtrl_, resourceView_);
    funcAgentMgr->BindLocalSchedSrv(localSchedSrv_);
    funcAgentMgr->BindBundleMgr(mockBundleMgr_);
    funcAgentMgr->ToReady();

    auto getResponse = std::make_shared<GetResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    bool isFinished = false;
    EXPECT_CALL(*instCtrl_, SyncAgent).WillOnce(DoAll(Assign(&isFinished, true), Return(Status::OK())));
    auto future = funcAgentMgr->Sync();
    ASSERT_AWAIT_READY(future);
    future = funcAgentMgr->Recover();
    ASSERT_AWAIT_READY(future);

    ASSERT_AWAIT_TRUE([&]() { return isFinished; });
}

/**
 * Tests evict agent which is not exist
 */
TEST_F(DISABLED_FuncAgentMgrTest, EvictInvalidAgent)
{
    auto req = std::make_shared<messages::EvictAgentRequest>();
    req->set_agentid("invalid");
    auto future = funcAgentMgr_->EvictAgent(req);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), false);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::PARAMETER_ERROR);
}

/**
 * Tests evict agent which is evicting/failed/evicted
 */
TEST_F(DISABLED_FuncAgentMgrTest, EvictAgentInMuliStatus)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    std::string post = "evicting";
    info.set_agentaidname("agent_aid_name_" + post);
    info.set_agentaddress("agent_address_" + post);
    info.set_runtimemgraid("runtime_manager_aid_" + post);
    info.set_runtimemgrid("runtime_manager_randomid_" + post);
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::EVICTING));
    funcAgentsRegis["agent_id_" + post] = info;
    funcAgentMgr_->InsertAgent("agent_id_" + post);

    post = "evicted";
    info.set_agentaidname("agent_aid_name_" + post);
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::EVICTED));
    funcAgentsRegis["agent_id_" + post] = info;

    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    funcAgentMgr_->InsertAgent("agent_id_" + post);
    funcAgentMgr_->EnableAgents();

    auto req = std::make_shared<messages::EvictAgentRequest>();
    req->set_agentid("agent_id_evicting");
    auto future = funcAgentMgr_->EvictAgent(req);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    litebus::Future<std::shared_ptr<messages::EvictAgentResult>> futureResult;
    EXPECT_CALL(*localSchedSrv_, NotifyEvictResult(_))
        .WillOnce(DoAll(FutureArg<0>(&futureResult), Return()));
    req->set_agentid("agent_id_evicted");
    future = funcAgentMgr_->EvictAgent(req);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    EXPECT_AWAIT_READY(futureResult);
    EXPECT_EQ(futureResult.Get()->code(), StatusCode::SUCCESS);
    EXPECT_EQ(futureResult.Get()->agentid(), "agent_id_evicted");
    // clean
    funcAgentsRegis.clear();
    funcAgentMgr_->ClearFuncAgentsRegis();
}

/**
 * Tests evict agent failed to put agent status
 */
TEST_F(DISABLED_FuncAgentMgrTest, EvictAgentPutStautsFailure)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    info.set_agentaidname("agent_id");
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
    funcAgentsRegis["agent_id"] = info;

    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    funcAgentMgr_->InsertAgent("agent_id");
    funcAgentMgr_->EnableAgents();

    auto putResponse = std::make_shared<PutResponse>();
    putResponse->status = Status(StatusCode::BP_META_STORAGE_PUT_ERROR, "error");
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse)).WillOnce(Return(putResponse));
    auto req = std::make_shared<messages::EvictAgentRequest>();
    req->set_agentid("agent_id");
    auto future = funcAgentMgr_->EvictAgent(req);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), false);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::BP_META_STORAGE_PUT_ERROR);
    // clean
    funcAgentsRegis.clear();
    funcAgentMgr_->ClearFuncAgentsRegis();
}

/**
 * Tests evict agent failed to put agent status
 */
TEST_F(DISABLED_FuncAgentMgrTest, EvictAgentSuccessful)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    info.set_agentaidname("agent_id");
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
    funcAgentsRegis["agent_id"] = info;

    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    funcAgentMgr_->InsertAgent("agent_id");
    funcAgentMgr_->EnableAgents();

    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse)).WillOnce(Return(putResponse));

    EXPECT_CALL(*instCtrl_.get(), EvictInstanceOnAgent(_))
        .WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

    EXPECT_CALL(*resourceView_.get(), UpdateUnitStatus(_, _)).WillOnce(Return(Status(StatusCode::SUCCESS)));
    EXPECT_CALL(*mockBundleMgr_, UpdateBundlesStatus).WillOnce(Return());
    EXPECT_CALL(*resourceView_.get(), DeleteResourceUnit(_)).WillOnce(Return(Status(StatusCode::SUCCESS)));

    litebus::Future<std::shared_ptr<messages::EvictAgentResult>> futureResult;
    EXPECT_CALL(*localSchedSrv_, NotifyEvictResult(_))
        .WillOnce(DoAll(FutureArg<0>(&futureResult), Return()));

    auto req = std::make_shared<messages::EvictAgentRequest>();
    req->set_agentid("agent_id");
    auto future = funcAgentMgr_->EvictAgent(req);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);

    EXPECT_AWAIT_READY(futureResult);
    EXPECT_EQ(futureResult.Get()->code(), StatusCode::SUCCESS);
    EXPECT_EQ(futureResult.Get()->agentid(), "agent_id");

    // clean
    funcAgentsRegis.clear();
    funcAgentMgr_->ClearFuncAgentsRegis();
}

/**
 * Tests evict agent failed to put agent status
 */
TEST_F(DISABLED_FuncAgentMgrTest, EvictAgentFailed)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    info.set_agentaidname("agent_id");
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
    funcAgentsRegis["agent_id"] = info;

    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    funcAgentMgr_->InsertAgent("agent_id");
    funcAgentMgr_->EnableAgents();

    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse)).WillOnce(Return(putResponse));;

    EXPECT_CALL(*resourceView_.get(), UpdateUnitStatus(_, _)).WillOnce(Return(Status(StatusCode::SUCCESS)));
    EXPECT_CALL(*mockBundleMgr_, UpdateBundlesStatus).WillOnce(Return());

    EXPECT_CALL(*instCtrl_.get(), EvictInstanceOnAgent(_))
        .WillRepeatedly(testing::Return(Status(StatusCode::FAILED)));

    litebus::Future<std::shared_ptr<messages::EvictAgentResult>> futureResult;
    EXPECT_CALL(*localSchedSrv_, NotifyEvictResult(_))
        .WillOnce(DoAll(FutureArg<0>(&futureResult), Return()));

    auto req = std::make_shared<messages::EvictAgentRequest>();
    req->set_agentid("agent_id");
    auto future = funcAgentMgr_->EvictAgent(req);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);

    EXPECT_AWAIT_READY(futureResult);
    EXPECT_EQ(futureResult.Get()->code(), StatusCode::FAILED);
    EXPECT_EQ(futureResult.Get()->agentid(), "agent_id");

    // clean
    funcAgentsRegis.clear();
    funcAgentMgr_->ClearFuncAgentsRegis();
}

/*
 * Test agent failed gc
 * */
TEST_F(DISABLED_FuncAgentMgrTest, InvalidAgentGC)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    info.set_agentaidname("agent_id");
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
    funcAgentsRegis["agent_id"] = info;

    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillRepeatedly(Return(putResponse));
    litebus::Async(funcAgentMgr_->GetActorAID(), &FunctionAgentMgrActor::StopHeartbeat, "agent_id");
    ASSERT_AWAIT_TRUE([=]() -> bool {
        auto info = litebus::Async(funcAgentMgr_->GetActorAID(), &FunctionAgentMgrActor::GetFuncAgentsRegis).Get();
        return info.find("agent_id") == info.end();
    });
    // clean
    funcAgentsRegis.clear();
    funcAgentMgr_->ClearFuncAgentsRegis();
}

/**
 * Tests evict agent which is recovering.
 */
TEST_F(DISABLED_FuncAgentMgrTest, EvictRecoveringAgent)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    info.set_agentaidname("agent_id");
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
    funcAgentsRegis["agent_id"] = info;

    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    funcAgentMgr_->InsertAgent("agent_id");

    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse)).WillOnce(Return(putResponse));

    EXPECT_CALL(*instCtrl_.get(), EvictInstanceOnAgent(_))
        .WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

    EXPECT_CALL(*resourceView_.get(), UpdateUnitStatus(_, _)).WillOnce(Return(Status(StatusCode::SUCCESS)));
    EXPECT_CALL(*mockBundleMgr_, UpdateBundlesStatus).WillOnce(Return());
    EXPECT_CALL(*resourceView_.get(), DeleteResourceUnit(_)).WillOnce(Return(Status(StatusCode::SUCCESS)));

    litebus::Future<std::shared_ptr<messages::EvictAgentResult>> futureResult;
    EXPECT_CALL(*localSchedSrv_, NotifyEvictResult(_))
        .WillOnce(DoAll(FutureArg<0>(&futureResult), Return()));

    auto req = std::make_shared<messages::EvictAgentRequest>();
    req->set_agentid("agent_id");
    auto future = funcAgentMgr_->EvictAgent(req);
    funcAgentMgr_->EnableAgents();
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);

    EXPECT_AWAIT_READY(futureResult);
    EXPECT_EQ(futureResult.Get()->code(), StatusCode::SUCCESS);
    EXPECT_EQ(futureResult.Get()->agentid(), "agent_id");

    // clean
    funcAgentsRegis.clear();
    funcAgentMgr_->ClearFuncAgentsRegis();

}

TEST_F(DISABLED_FuncAgentMgrTest, TenantEventCase1)
{
    // same node
    TenantEvent event = {
        .tenantID = "tenant1",
        .functionProxyID = "nodeID",
        .functionAgentID = "agent1",
        .instanceID = "instance1",
        .agentPodIp = "127.0.0.1",
        .code = static_cast<int32_t>(InstanceState::RUNNING),
    };
    funcAgentMgr_->OnTenantUpdateInstance(event);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto tenantCacheMap = funcAgentMgr_->GetTenantCacheMap();
    auto tenantCache = tenantCacheMap[event.tenantID];
    EXPECT_TRUE(tenantCache->functionAgentCacheMap[event.functionAgentID].isAgentOnThisNode);

    funcAgentMgr_->OnTenantDeleteInstance(event);
    ASSERT_AWAIT_TRUE([=]() -> bool {
       return tenantCache->functionAgentCacheMap.count(event.functionAgentID) == (size_t)0;
    });
}

/*
 * Test metastore fallback recover
 * */
TEST_F(DISABLED_FuncAgentMgrTest, OnHealthyStatusTest)
{
    auto funcAgentMgr = make_shared<local_scheduler::FunctionAgentMgr>(
        make_shared<local_scheduler::FunctionAgentMgrActor>("funcAgentMgr-OnHealthyStatusTest", PARAM, "nodeID", mockMetaStoreClient_));
    funcAgentMgr->Start(instCtrl_, resourceView_);

    Status status(StatusCode::FAILED);
    funcAgentMgr->OnHealthyStatus(status);
    funcAgentMgr->OnHealthyStatus(Status::OK());
    funcAgentMgr->ToReady();
    auto putResponse = std::make_shared<PutResponse>();
    litebus::Future<std::string> key;
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(DoAll(FutureArg<0>(&key), Return(putResponse)));
    funcAgentMgr->OnHealthyStatus(Status::OK());
    ASSERT_AWAIT_READY(key);
    EXPECT_EQ(key.Get(), "/yr/agentInfo/nodeID");
    funcAgentMgr->Stop();
    funcAgentMgr->Await();
}

/**
 * Tests graceful shutdown, which evict all agent
 */
TEST_F(DISABLED_FuncAgentMgrTest, GracefulShutdown)
{
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> funcAgentsRegis;
    messages::FuncAgentRegisInfo info;
    info.set_agentaidname("agent_id");
    info.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::SUCCESS));
    messages::FuncAgentRegisInfo infoFailed;
    infoFailed.set_agentaidname("failed_agent");
    infoFailed.set_statuscode(static_cast<int32_t>(FunctionAgentMgrActor::RegisStatus::FAILED));
    funcAgentsRegis["agent_id"] = info;
    funcAgentsRegis["failed_agent"] = infoFailed;

    funcAgentMgr_->SetFuncAgentsRegis(funcAgentsRegis);
    funcAgentMgr_->InsertAgent("agent_id");
    funcAgentMgr_->EnableAgents();

    auto deleteResponse = std::make_shared<DeleteResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Delete).WillRepeatedly(Return(deleteResponse));

    EXPECT_CALL(*instCtrl_.get(), EvictInstanceOnAgent(_)).WillRepeatedly(testing::Return(Status(StatusCode::SUCCESS)));

    EXPECT_CALL(*resourceView_.get(), UpdateUnitStatus(_, _)).WillRepeatedly(Return(Status(StatusCode::SUCCESS)));
    EXPECT_CALL(*mockBundleMgr_, UpdateBundlesStatus).WillRepeatedly(Return());

    funcAgentMgr_->actor_->persistingAgentInfo_ = std::make_shared<litebus::Promise<Status>>();
    auto future = funcAgentMgr_->GracefulShutdown();
    funcAgentMgr_->actor_->persistingAgentInfo_->SetValue(Status::OK());
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);

    funcAgentMgr_->actor_->persistingAgentInfo_ = nullptr;
    future = funcAgentMgr_->GracefulShutdown();
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::SUCCESS);
    auto regis = funcAgentMgr_->GetFuncAgentsRegis();
    EXPECT_EQ(regis.empty(), true);
}

TEST_F(DISABLED_FuncAgentMgrTest, CreateStaticFunctionInstance)
{
    messages::ScheduleResponse scheduleResponse = GenScheduleResponse(0, "", "", REQUEST_ID);
    litebus::Future<shared_ptr<messages::ScheduleRequest>> scheduleFuture;
    EXPECT_CALL(*instCtrl_.get(), Schedule)
        .WillOnce(testing::DoAll(FutureArg<0>(&scheduleFuture), testing::Return(scheduleResponse)));
    litebus::Future<string> resp;
    EXPECT_CALL(*funcAgent_.get(), MockStaticFunctionScheduleResponse(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(testing::DoAll(FutureArg<2>(&resp))));

    messages::ScheduleRequest scheduleReq;
    scheduleReq.set_requestid(REQUEST_ID);
    litebus::Async(funcAgent_->GetAID(), &MockFunctionAgent::StaticFunctionScheduleRequest, funcAgentMgr_->GetActorAID(),
                   scheduleReq);

    auto ret = scheduleFuture.Get(1000);
    ASSERT_TRUE(ret.IsSome());
    EXPECT_TRUE(ret.Get()->requestid() == REQUEST_ID);

    auto respStr = resp.Get(1000);
    ASSERT_TRUE(respStr.IsSome());
    messages::ScheduleResponse respVal;
    auto parseVal = respVal.ParseFromString(respStr.Get());
    EXPECT_TRUE(parseVal);
    EXPECT_STREQ(respVal.requestid().c_str(), REQUEST_ID.c_str());
}

TEST_F(DISABLED_FuncAgentMgrTest, NotifyFunctionStatusChangeSuccess)
{
    litebus::Future<std::string> mockMsg;

    messages::StaticFunctionChangeResponse mockResp =
        GenStaticFunctionChangeResponse(StatusCode::SUCCESS, "notify success", REQUEST_ID, INSTANCE_ID);
    EXPECT_CALL(*funcAgent_.get(), MockNotifyFunctionStatusChange(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<2>(&mockMsg),
                                       testing::Return(std::pair<bool, string>(true, mockResp.SerializeAsString()))));

    auto req = GenStaticFunctionChangeRequest(REQUEST_ID, INSTANCE_ID, 3);
    auto ret = funcAgentMgr_->NotifyFunctionStatusChange(req, randomFuncAgentName_);

    auto resp = ret.Get(1000);
    ASSERT_TRUE(resp.IsSome());
    EXPECT_STREQ(resp.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(resp.Get().code(), StatusCode::SUCCESS);

    auto msg = mockMsg.Get(1000);
    ASSERT_TRUE(msg.IsSome());
    ASSERT_FALSE(msg.Get().empty());

}

TEST_F(DISABLED_FuncAgentMgrTest, NotifyFunctionStatusChangeRetrySuccess)
{
    messages::StaticFunctionChangeResponse mockResp =
        GenStaticFunctionChangeResponse(StatusCode::SUCCESS, "notify success", REQUEST_ID, INSTANCE_ID);
    EXPECT_CALL(*funcAgent_.get(), MockNotifyFunctionStatusChange(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(std::pair<bool, string>{ false, "" }))
        .WillOnce(testing::Return(std::pair<bool, string>{ false, "" }))
        .WillRepeatedly(testing::Return(std::pair<bool, string>{ true, mockResp.SerializeAsString() }));

    auto req = GenStaticFunctionChangeRequest(REQUEST_ID, INSTANCE_ID, 3);

    auto ret = funcAgentMgr_->NotifyFunctionStatusChange(req, randomFuncAgentName_);

    auto resp = ret.Get(1000);
    ASSERT_EQ(resp.IsSome(), true);
    EXPECT_STREQ(resp.Get().requestid().c_str(), REQUEST_ID.c_str());
    EXPECT_EQ(resp.Get().code(), StatusCode::SUCCESS);
}

}  // namespace functionsystem::test