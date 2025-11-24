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

#include "function_master/system_function_loader/bootstrap_actor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/inotify.h>


#include "common/etcd_service/etcd_service_driver.h"
#include "common/utils/meta_store_kv_operation.h"
#include "httpd/http_actor.hpp"
#include "mocks/mock_instance_manager.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/os_utils.hpp"
#include "utils/port_helper.h"

namespace functionsystem::system_function_loader::test {
using namespace functionsystem::test;

const std::string SYSTEM_FUNC_CONFIG_PATH = "/home/sn/function/config";
const std::string SYSTEM_FUNC_CONFIG_FILE = "system-function-config.json";
const std::string SYSTEM_FUNC_PAYLOAD_PATH = "/home/sn/function/payload";
const std::string SYSTEM_FUNC_META_PATH = "/home/sn/function/system-function-meta";

const std::unordered_map<std::string, std::string> payloadContent = {
    { "faas-scheduler-config.json",
      R"({"0-system-faasscheduler":{"systemFunctionName":"0-system-faascontroller","signal":65,"payload":{"version":"v1"}}})" },
    { "faas-frontend-config.json",
      R"({"0-system-faasfrontend":{"systemFunctionName":"0-system-faascontroller","signal":66,"payload":{"version":"v1","instanceNum":2}}})" },
};

const std::string controllerV0 =
    R"({"funcMetaData":{"layers":[],"name":"faascontroller","description":"","version":"v0","functionUrn":"sn:cn:yrk:0:function:faascontroller","functionVersionUrn":"sn:cn:yrk:0:function:faascontroller:v0","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"0","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0}}})";
const std::string controllerV1 =
    R"({"funcMetaData":{"layers":[],"name":"faascontroller","description":"","version":"v1","functionUrn":"sn:cn:yrk:0:function:faascontroller","functionVersionUrn":"sn:cn:yrk:0:function:faascontroller:v1","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"0","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0}}})";
const std::string schedulerV1 =
    R"({"funcMetaData":{"layers":[],"name":"faasscheduler","description":"","version":"v1","functionUrn":"sn:cn:yrk:0:function:faasscheduler","functionVersionUrn":"sn:cn:yrk:0:function:faasscheduler:v1","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"0","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0}}})";
const std::string frontendV1 =
    R"({"funcMetaData":{"layers":[],"name":"faasfrontend","description":"","version":"v1","functionUrn":"sn:cn:yrk:0:function:faasfrontend","functionVersionUrn":"sn:cn:yrk:0:function:faasfrontend:v1","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"0","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0}}})";
const std::string controllerV2 =
    R"({"funcMetaData":{"layers":[],"name":"faascontroller","description":"","version":"v2","functionUrn":"sn:cn:yrk:0:function:faascontroller","functionVersionUrn":"sn:cn:yrk:0:function:faascontroller:v2","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"0","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0}}})";

const std::unordered_map<std::string, std::string> metaContent = {
    { "faas-controller-meta.json", controllerV1 },
    { "faas-frontend-meta.json", frontendV1 },
    { "faas-scheduler-meta.json", schedulerV1 },
};

void GenSystemFunctionConfigFile(const std::string &content)
{
    if (!litebus::os::ExistPath(SYSTEM_FUNC_CONFIG_PATH)) {
        litebus::os::Mkdir(SYSTEM_FUNC_CONFIG_PATH);
    }
    auto filePath = SYSTEM_FUNC_CONFIG_PATH + "/" + SYSTEM_FUNC_CONFIG_FILE;

    std::ofstream outfile;
    outfile.open(filePath.c_str());
    outfile << content << std::endl;
    outfile.close();
}

void GenSystemFunctionPayloadFiles(const std::unordered_map<std::string, std::string> &content)
{
    if (!litebus::os::ExistPath(SYSTEM_FUNC_PAYLOAD_PATH)) {
        litebus::os::Mkdir(SYSTEM_FUNC_PAYLOAD_PATH);
    }
    for (auto iter = content.begin(); iter != content.end(); ++iter) {
        auto filePath = SYSTEM_FUNC_PAYLOAD_PATH + "/" + iter->first;
        std::ofstream outfile;
        outfile.open(filePath.c_str());
        outfile << iter->second << std::endl;
        outfile.close();
    }
}

void GenSystemFunctionMetaFiles(const std::unordered_map<std::string, std::string> &content)
{
    if (!litebus::os::ExistPath(SYSTEM_FUNC_META_PATH)) {
        litebus::os::Mkdir(SYSTEM_FUNC_META_PATH);
    }
    for (auto iter = content.begin(); iter != content.end(); ++iter) {
        auto filePath = SYSTEM_FUNC_META_PATH + "/" + iter->first;
        std::ofstream outfile;
        outfile.open(filePath.c_str());
        outfile << iter->second << std::endl;
        outfile.close();
    }
}

class GlobalScheduler : public functionsystem::global_scheduler::GlobalSched {
public:
    MOCK_METHOD(litebus::Future<Status>, Schedule, (const std::shared_ptr<messages::ScheduleRequest> &), (override));
    MOCK_METHOD(litebus::Future<litebus::Option<std::string>>, GetLocalAddress, (const std::string &), (override));
};

class MockLocalActor : public litebus::ActorBase {
public:
    MockLocalActor() : litebus::ActorBase("mock-LocalSchedInstanceCtrlActor")
    {
    }
    MOCK_METHOD(void, ForwardCustomSignalRequest, (const litebus::AID &, std::string &&, std::string &&));

    void SendForwardCustomSignalResponse(const litebus::AID &to, const std::string &requestID)
    {
        messages::ForwardKillResponse rsp;
        rsp.set_requestid(requestID);
        rsp.set_code(StatusCode::SUCCESS);
        (void)Send(to, "ForwardCustomSignalResponse", rsp.SerializeAsString());
    }

protected:
    void Init() override
    {
        Receive("ForwardCustomSignalRequest", &MockLocalActor::ForwardCustomSignalRequest);
    }
};

class BootstrapWrapper : public BootstrapActor {
public:
    BootstrapWrapper(const std::shared_ptr<MetaStoreClient> &metaClient,
                     std::shared_ptr<global_scheduler::GlobalSched> globalSched, uint32_t sysFuncRetryPeriod,
                     bool enableFrontendPool = false)
        : BootstrapActor(metaClient, globalSched, sysFuncRetryPeriod, "instanceManagerAddress", enableFrontendPool)
    {
    }

    litebus::Future<bool> LoadBootstrapConfigWrapper(const std::string &customArgs)
    {
        LoadBootstrapConfig(customArgs);
        return true;
    }

    litebus::Future<bool> UpdateConfigWrapper()
    {
        UpdateConfigHandler();
        return true;
    }

    litebus::Future<bool> UpdatePayloadWrapper()
    {
        UpdatePayloadHandler();
        return true;
    }

    litebus::Future<bool> UpdateMetaWrapper()
    {
        UpdateMetaHandler();
        return true;
    }
};

class BootstrapActorTest : public ::testing::Test {
protected:
    std::shared_ptr<MockInstanceManager> mockInstanceMgr_ = nullptr;
    std::shared_ptr<BootstrapWrapper> bootstrapActor_ = nullptr;
    std::shared_ptr<GlobalScheduler> globalSched_ = nullptr;
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::shared_ptr<MetaStorageAccessor> metaStoreAccessor_;
    inline static std::string metaStoreServerHost_;
    inline static std::string localAddress_;

    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
        uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
        localAddress_ = "127.0.0.1:" + std::to_string(port);

        auto client = std::make_unique<MetaStoreClient>(
            MetaStoreConfig{ .etcdAddress = metaStoreServerHost_ });
        client->Init();
        metaStoreAccessor_ = std::make_shared<MetaStorageAccessor>(std::move(client));
        litebus::os::Rm("/home/sn/function/");
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        metaStoreAccessor_ = nullptr;
        etcdSrvDriver_->StopServer();
        etcdSrvDriver_ = nullptr;
    }

    void SetUp() override
    {
        globalSched_ = std::make_shared<GlobalScheduler>();
        auto metaClient =
            MetaStoreClient::Create(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_ });
        bootstrapActor_ = std::make_shared<BootstrapWrapper>(metaClient, globalSched_, 900, true);
        mockInstanceMgr_ = std::make_shared<MockInstanceManager>();
        litebus::Spawn(bootstrapActor_);

        litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::BindInstanceManager, mockInstanceMgr_);
        litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::UpdateLeaderInfo,
                       GetLeaderInfo(bootstrapActor_->GetAID()));

        litebus::os::Rm("/home/sn/function/");
    }

    void TearDown() override
    {
        auto result = metaStoreAccessor_->Delete("/faas/system-function/config", true);
        EXPECT_AWAIT_READY(result);
        result = metaStoreAccessor_->Delete(INSTANCE_PATH_PREFIX, true);
        EXPECT_AWAIT_READY(result);
        result = metaStoreAccessor_->Delete(FUNC_META_PATH_PREFIX, true);
        EXPECT_AWAIT_READY(result);

        litebus::os::Rm("/home/sn/function/");

        litebus::Terminate(bootstrapActor_->GetAID());
        litebus::Await(bootstrapActor_->GetAID());
        bootstrapActor_ = nullptr;
        globalSched_ = nullptr;
        mockInstanceMgr_ = nullptr;
    }

    void LoadBootstrapConfig(const std::string &customArgs)
    {
        auto status =
            litebus::Async(bootstrapActor_->GetAID(), &BootstrapWrapper::LoadBootstrapConfigWrapper, customArgs);
        EXPECT_AWAIT_READY(status);
    }

    bool CheckInstanceExist(const FuncInstanceParams &funcInstanceParams)
    {
        auto status =
            litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::CheckInstanceExist, funcInstanceParams);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }

    Status LoadFunctionConfigs()
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::LoadFunctionConfigs);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }

    Status LoadSysFuncCustomArgs(const std::string &args)
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::LoadSysFuncCustomArgs, args);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }

    int GetFunctionConfigSize()
    {
        auto size = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::GetFunctionConfigSize);
        EXPECT_AWAIT_READY(size);
        return size.Get();
    }

    litebus::Option<FunctionConfig> GetFunctionConfig(const std::string &funcName)
    {
        auto config = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::GetFunctionConfig, funcName);
        EXPECT_AWAIT_READY(config);
        return config.Get();
    }

    litebus::Option<nlohmann::json> GetSysFuncCustomArgs(const std::string &funcName)
    {
        auto json = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::GetSysFuncCustomArgs, funcName);
        EXPECT_AWAIT_READY(json);
        return json.Get();
    }

    Status LoadCurrentFunctionConfigs()
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::LoadCurrentFunctionConfigs);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }

    Status LoadSysFuncPayloads()
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::LoadSysFuncPayloads);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }

    Status LoadSysFuncMetas()
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::LoadSysFuncMetas);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }

    Status LoadCurrentSysFuncMetas()
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::LoadCurrentSysFuncMetas);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }

    litebus::Option<FunctionConfig> GetCurrFunctionConfig(const std::string &funcName)
    {
        auto json = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::GetCurrFunctionConfig, funcName);
        EXPECT_AWAIT_READY(json);
        return json.Get();
    }

    litebus::Option<FunctionPayload> GetFunctionPayload(const std::string &funcName)
    {
        auto payload = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::GetFunctionPayload, funcName);
        EXPECT_AWAIT_READY(payload);
        return payload.Get();
    }

    litebus::Option<std::unordered_map<std::string, std::vector<std::pair<std::string, FunctionMeta>>>>
    GetFunctionMetaQueue()
    {
        auto queue = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::GetFunctionMetaQueue);
        EXPECT_AWAIT_READY(queue);
        return queue.Get();
    }
    void UpdateConfigHandler()
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapWrapper::UpdateConfigWrapper);
        EXPECT_AWAIT_READY(status);
    }

    void UpdatePayloadHandler()
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapWrapper::UpdatePayloadWrapper);
        EXPECT_AWAIT_READY(status);
    }

    void UpdateMetaHandler()
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapWrapper::UpdateMetaWrapper);
        EXPECT_AWAIT_READY(status);
    }

    Status SendUpdateArgsSignal(const std::string &functionName, const FunctionPayload &functionPayload, int retryTimes)
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::SendUpdateArgsSignal, functionName,
                                     functionPayload, retryTimes);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }

    Status SendUpgradeFunctionSignal(const std::string &functionName, const FunctionConfig &newConfig,
                                     const FunctionConfig &currConfig, int retryTimes)
    {
        auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::SendUpgradeFunctionSignal,
                                     functionName, newConfig, currConfig, retryTimes);
        EXPECT_AWAIT_READY(status);
        return status.Get();
    }
};

/**
 * Feature: BootstrapActorTest LoadBootstrapConfigInvalidTest
 * Description: Try to load invalid config, failed
 * Steps:
 * 1. File not exist
 * 2. Invalid json
 * 3. Empty function name
 *
 * Expectation:
 * 1-3. Load failed
 */
TEST_F(BootstrapActorTest, LoadBootstrapConfigInvalidTest)
{
    (void)litebus::os::Rmdir(SYSTEM_FUNC_CONFIG_PATH);
    // file not exist
    auto status = LoadFunctionConfigs();
    EXPECT_TRUE(status.IsError());
    EXPECT_THAT(status.ToString(), testing::HasSubstr("bootstrap config file not exist"));
    EXPECT_EQ(GetFunctionConfigSize(), 0);

    // invalid json
    std::string content = "fake json";
    GenSystemFunctionConfigFile(content);
    status = LoadFunctionConfigs();
    EXPECT_TRUE(status.IsError());
    EXPECT_THAT(status.ToString(), testing::HasSubstr("parse json failed"));
    EXPECT_EQ(GetFunctionConfigSize(), 0);

    // empty function name
    content =
        R"({"":{"tenantID":"0","version":"$latest","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}},"":{"tenantID":"0","version":"$latest","memory":100,"cpu":100,"createOptions":{"concurrentNum":"1"},"instanceNum":2,"schedulingOps": {"extension": {"schedule_policy": "shared"}},"args":{"xxx":4,"xxx2":{"xxx":2000}}}})";
    GenSystemFunctionConfigFile(content);
    status = LoadFunctionConfigs();
    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(GetFunctionConfigSize(), 0);
}

TEST_F(BootstrapActorTest, LoadBootstrapConfigTest)
{
    const std::string content =
        R"({"0-system-faascontroller":{"tenantID":"0","version":"$latest","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx2":{"xxx":1000}}},"0-system-faasfrontend":{"tenantID":"0","version":"$latest","memory":100,"cpu":100,"createOptions":{"concurrentNum":"1"},"instanceNum":2,"schedulingOps": {"extension": {"schedule_policy": "shared"}},"args":{"xxx":4,"xxx2":{"xxx":2000}}}})";
    GenSystemFunctionConfigFile(content);

    auto status = LoadFunctionConfigs();
    EXPECT_TRUE(status.IsOk());

    auto controllerConfig = GetFunctionConfig("0-system-faascontroller");
    EXPECT_TRUE(controllerConfig.IsSome());
    EXPECT_EQ(controllerConfig.Get().tenantID, "0");
    EXPECT_EQ(controllerConfig.Get().version, "$latest");
    EXPECT_EQ(controllerConfig.Get().cpu, 500);
    EXPECT_EQ(controllerConfig.Get().memory, 500);
    EXPECT_EQ(controllerConfig.Get().createOptions.size(), 1u);
    EXPECT_EQ(controllerConfig.Get().createOptions.find("concurrentNum")->second, "10");
    EXPECT_EQ(controllerConfig.Get().instanceNum, 1u);
    EXPECT_EQ(controllerConfig.Get().extension.at("schedule_policy"), "monopoly");
    EXPECT_EQ(controllerConfig.Get().args.dump(), "{\"xxx\":2,\"xxx2\":{\"xxx\":1000}}");

    auto frontConfig = GetFunctionConfig("0-system-faasfrontend");
    EXPECT_TRUE(frontConfig.IsSome());
    EXPECT_EQ(frontConfig.Get().tenantID, "0");
    EXPECT_EQ(frontConfig.Get().version, "$latest");
    EXPECT_EQ(frontConfig.Get().cpu, 100);
    EXPECT_EQ(frontConfig.Get().memory, 100);
    EXPECT_EQ(controllerConfig.Get().createOptions.size(), 1u);
    EXPECT_EQ(frontConfig.Get().createOptions.find("concurrentNum")->second, "1");
    EXPECT_EQ(frontConfig.Get().instanceNum, 2u);
    EXPECT_EQ(frontConfig.Get().extension.at("schedule_policy"), "shared");
    EXPECT_EQ(frontConfig.Get().args.dump(), "{\"xxx\":4,\"xxx2\":{\"xxx\":2000}}");
}

/**
 * Feature: BootstrapActorTest LoadBootstrapConfigTest
 * Description: Load bootstrap config
 * Steps:
 * 1. Write config file
 * 2. LoadFunctionConfigs
 *
 * Expectation:
 * 2. config load success
 */
TEST_F(BootstrapActorTest, LoadBootstrapConfigTestCompatible)
{
    const std::string content =
        R"({"0-system-faascontroller":{"tenantID":"0","version":"$latest","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension":{"schedule_policy":"monopoly"}},"args":{"xxx":2,"xxx2":{"xxx":1000}}},"0-system-faasfrontend":{"tenantID":"0","version":"$latest","memory":100,"cpu":100,"createOptions":{"concurrentNum":"1"},"instanceNum":2,"schedulingOps": {"extension":{"schedule_policy":"shared"}},"args":{"xxx":4,"xxx2":{"xxx":2000}}}})";
    GenSystemFunctionConfigFile(content);

    auto status = LoadFunctionConfigs();
    EXPECT_TRUE(status.IsOk());

    auto controllerConfig = GetFunctionConfig("0-system-faascontroller");
    EXPECT_TRUE(controllerConfig.IsSome());
    EXPECT_EQ(controllerConfig.Get().tenantID, "0");
    EXPECT_EQ(controllerConfig.Get().version, "$latest");
    EXPECT_EQ(controllerConfig.Get().cpu, 500);
    EXPECT_EQ(controllerConfig.Get().memory, 500);
    EXPECT_EQ(controllerConfig.Get().createOptions.size(), 1u);
    EXPECT_EQ(controllerConfig.Get().createOptions.find("concurrentNum")->second, "10");
    EXPECT_EQ(controllerConfig.Get().instanceNum, 1u);
    EXPECT_EQ(controllerConfig.Get().extension.at("schedule_policy"), "monopoly");
    EXPECT_EQ(controllerConfig.Get().args.dump(), "{\"xxx\":2,\"xxx2\":{\"xxx\":1000}}");

    auto frontConfig = GetFunctionConfig("0-system-faasfrontend");
    EXPECT_TRUE(frontConfig.IsSome());
    EXPECT_EQ(frontConfig.Get().tenantID, "0");
    EXPECT_EQ(frontConfig.Get().version, "$latest");
    EXPECT_EQ(frontConfig.Get().cpu, 100);
    EXPECT_EQ(frontConfig.Get().memory, 100);
    EXPECT_EQ(controllerConfig.Get().createOptions.size(), 1u);
    EXPECT_EQ(frontConfig.Get().createOptions.find("concurrentNum")->second, "1");
    EXPECT_EQ(frontConfig.Get().instanceNum, 2u);
    EXPECT_EQ(frontConfig.Get().extension.at("schedule_policy"), "shared");
    EXPECT_EQ(frontConfig.Get().args.dump(), "{\"xxx\":4,\"xxx2\":{\"xxx\":2000}}");
}

/**
 * Feature: BootstrapActorTest LoadSysFuncCustomArgsTest
 * Description: Load system function custom args from flags
 * Steps:
 * 1. Empty config
 * 2. Invalid config json
 * 3. Correct json
 * 4. Load bootstrap config
 *
 * Expectation:
 * 1. Load failed
 * 2. Load failed
 * 3. Load success
 * 4. Args are combined
 */
TEST_F(BootstrapActorTest, LoadSysFuncCustomArgsTest)
{
    std::string content = "";
    auto status = LoadSysFuncCustomArgs(content);
    EXPECT_TRUE(status.IsError());
    EXPECT_THAT(status.ToString(), testing::HasSubstr("sysFuncCustomArgs is empty"));

    content = "fake json";
    status = LoadSysFuncCustomArgs(content);
    EXPECT_TRUE(status.IsError());
    EXPECT_THAT(status.ToString(), testing::HasSubstr("parse arg json failed"));

    content =
        R"({"0-system-faascontroller":{"memory":500,"cpu":500},"0-system-faasfrontend":{"memory":100,"cpu":100}})";
    status = LoadSysFuncCustomArgs(content);
    EXPECT_TRUE(status.IsOk());

    auto controllerArgs = GetSysFuncCustomArgs("0-system-faascontroller");
    EXPECT_TRUE(controllerArgs.IsSome());
    EXPECT_EQ(controllerArgs.Get().at("cpu"), 500);
    EXPECT_EQ(controllerArgs.Get().at("memory"), 500);

    auto frontEndArgs = GetSysFuncCustomArgs("0-system-faasfrontend");
    EXPECT_TRUE(frontEndArgs.IsSome());
    EXPECT_EQ(frontEndArgs.Get().at("cpu"), 100);
    EXPECT_EQ(frontEndArgs.Get().at("memory"), 100);

    // combine args
    content =
        R"({"0-system-faascontroller":{"tenantID":"0","version":"$latest","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx1":2,"xxx2":{"xxx":1000}}},"0-system-faasfrontend":{"tenantID":"0","version":"$latest","memory":100,"cpu":100,"createOptions":{"concurrentNum":"1"},"instanceNum":2,"schedulingOps": {"extension": {"schedule_policy": "shared"}},"args":{"xxx1":4,"xxx2":{"xxx":2000}}}})";
    GenSystemFunctionConfigFile(content);
    status = LoadFunctionConfigs();
    EXPECT_TRUE(status.IsOk());

    auto controllerConfig = GetFunctionConfig("0-system-faascontroller");
    EXPECT_TRUE(controllerConfig.IsSome());
    EXPECT_EQ(controllerConfig.Get().args.dump(), "{\"cpu\":500,\"memory\":500,\"xxx1\":2,\"xxx2\":{\"xxx\":1000}}");

    auto frontEndConfig = GetFunctionConfig("0-system-faasfrontend");
    EXPECT_TRUE(frontEndConfig.IsSome());
    EXPECT_EQ(frontEndConfig.Get().args.dump(), "{\"cpu\":100,\"memory\":100,\"xxx1\":4,\"xxx2\":{\"xxx\":2000}}");
}

/**
 * Feature: BootstrapActorTest CheckInstanceExistTest
 * Description: Load system function custom args from flags
 * Steps:
 * 1. Put instance into meta_store
 * 2. Check existed instance0
 * 3. Check non-existed instance1
 * 4. Check invalid instance2
 *
 * Expectation:
 * 2. Error already existed
 * 3. Not exist
 * 4. Error invalid instance key
 */
TEST_F(BootstrapActorTest, CheckInstanceExistTest)
{
    auto funcKey = "0/0-system-faascontroller/$latest";
    auto instanceID0 = "0-system-faascontroller-0";
    auto instanceID1 = "0-system-faascontroller-1";

    FuncInstanceParams params0 = {
        .traceID = "", .instanceID = instanceID0, .requestID = "requestid0", .functionKey = funcKey
    };
    FuncInstanceParams params1 = { .traceID = "", .instanceID = instanceID1, .requestID = "", .functionKey = funcKey };
    FuncInstanceParams params2 = { .traceID = "", .instanceID = "", .requestID = "", .functionKey = "" };

    litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::BindInstanceManager, nullptr);
    EXPECT_TRUE(!CheckInstanceExist(params0));

    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise0;
    promise0.SetValue({ "", nullptr });

    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise1;
    promise1.SetValue({ "key", nullptr });

    auto instance1 = std::make_shared<resource_view::InstanceInfo>();
    instance1->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::FATAL));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise2;
    promise2.SetValue({ "key", instance1 });

    auto instance2 = std::make_shared<resource_view::InstanceInfo>();
    instance2->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::CREATING));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise3;
    promise3.SetValue({ "key", instance2 });

    auto instance3 = std::make_shared<resource_view::InstanceInfo>();
    instance3->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise4;
    promise4.SetValue({ "key", instance3 });
    EXPECT_CALL(*mockInstanceMgr_, GetInstanceInfoByInstanceID)
        .WillOnce(testing::Return(promise0.GetFuture()))
        .WillOnce(testing::Return(promise1.GetFuture()))
        .WillOnce(testing::Return(promise2.GetFuture()))
        .WillOnce(testing::Return(promise3.GetFuture()))
        .WillOnce(testing::Return(promise4.GetFuture()));

    litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::BindInstanceManager, mockInstanceMgr_);
    // empty instance
    EXPECT_FALSE(CheckInstanceExist(params0));
    EXPECT_FALSE(CheckInstanceExist(params0));

    // unrecoverable instance
    EXPECT_TRUE(CheckInstanceExist(params2));

    // waiting instance
    EXPECT_TRUE(CheckInstanceExist(params2));

    // waiting running
    EXPECT_TRUE(CheckInstanceExist(params2));
}

TEST_F(BootstrapActorTest, CheckInstanceExistReachMaxWaitTimesTest)
{
    auto funcKey = "0/0-system-faascontroller/$latest";
    auto instanceID0 = "0-system-faascontroller-0";
    auto instanceKey = GenInstanceKey(funcKey, instanceID0, "requestid0").Get();
    FuncInstanceParams params0 = {
        .traceID = "", .instanceID = instanceID0, .requestID = "requestid0", .functionKey = funcKey
    };
    litebus::Future<litebus::Option<std::string>> address;
    address.SetValue(litebus::Option<std::string>(localAddress_));
    auto mockLocal = std::make_shared<MockLocalActor>();
    litebus::Spawn(mockLocal);
    EXPECT_CALL(*globalSched_, GetLocalAddress).WillRepeatedly(testing::Return(address));
    EXPECT_CALL(*mockLocal, ForwardCustomSignalRequest)
        .WillRepeatedly(testing::Invoke([aid(mockLocal->GetAID())](const litebus::AID &from, std::string &&,
                                                                   std::string &&msg) {
            internal::ForwardKillRequest forwardKillRequest;
            if (msg.empty() || !forwardKillRequest.ParseFromString(msg)) {
                YRLOG_WARN("Failed to parse requestID from forwardKillRequest.");
                return;
            }
            litebus::Async(aid, &MockLocalActor::SendForwardCustomSignalResponse, from, forwardKillRequest.requestid());
        }));

    auto instance = std::make_shared<resource_view::InstanceInfo>();
    instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::CREATING));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise;
    promise.SetValue({ instanceKey, instance });
    EXPECT_CALL(*mockInstanceMgr_, GetInstanceInfoByInstanceID).WillRepeatedly(testing::Return(promise.GetFuture()));

    for (int i = 0; i < 59; i++) {
        EXPECT_TRUE(CheckInstanceExist(params0));
    }
    auto waitMap = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::GetInstanceWaitingStateTimesMap).Get();
    EXPECT_EQ(static_cast<uint32_t>(59), waitMap[instanceKey]);
    EXPECT_TRUE(CheckInstanceExist(params0));
    waitMap = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::GetInstanceWaitingStateTimesMap).Get();
    EXPECT_EQ(static_cast<uint32_t>(0), waitMap[instanceKey]);

    litebus::Terminate(bootstrapActor_->GetAID());
    litebus::Await(bootstrapActor_->GetAID());
    litebus::Terminate(mockLocal->GetAID());
    litebus::Await(mockLocal->GetAID());
}

/**
 * Feature: BootstrapActorTest BuildScheduleRequestTest
 * Description: Build schedule request
 * Steps:
 * 1. Build schedule request
 *
 * Expectation:
 * 1. Schedule request has correct parma
 */
TEST_F(BootstrapActorTest, BuildScheduleRequestTest)
{
    nlohmann::json customConfig{};
    customConfig["xxx"] = "1";
    customConfig["xxx2"] = 2;
    FunctionConfig config{ .tenantID = "tenantID",
                           .version = "version",
                           .memory = 101,
                           .cpu = 102,
                           .instanceNum = 100,
                           .args = customConfig };
    config.extension["schedule_policy"] = "monopoly";
    config.createOptions["concurrentNum"] = "10";

    FuncInstanceParams params{
        .traceID = "traceID", .instanceID = "instanceID", .requestID = "requestID", .functionKey = "functionKey"
    };

    auto request = bootstrapActor_->BuildScheduleRequest(config, params);

    EXPECT_EQ(request->traceid(), params.traceID);
    EXPECT_EQ(request->requestid(), params.requestID);
    EXPECT_EQ(request->instance().parentid(), "");

    runtime::CallRequest callRequestGet;
    callRequestGet.ParseFromString(request->initrequest());
    EXPECT_EQ(callRequestGet.traceid(), params.traceID);
    EXPECT_EQ(callRequestGet.requestid(), params.requestID);
    EXPECT_EQ(callRequestGet.function(), params.functionKey);
    EXPECT_EQ(callRequestGet.iscreate(), true);
    EXPECT_EQ(callRequestGet.createoptions().at("concurrentNum"), config.createOptions["concurrentNum"]);
    EXPECT_EQ(callRequestGet.createoptions().at(RESOURCE_OWNER_KEY), SYSTEM_OWNER_VALUE);
    EXPECT_EQ(callRequestGet.args().size(), 1);

    auto instance = request->instance();
    EXPECT_EQ(instance.instanceid(), params.instanceID);
    EXPECT_EQ(instance.requestid(), params.requestID);
    EXPECT_EQ(instance.function(), params.functionKey);
    EXPECT_EQ(instance.createoptions().at("concurrentNum"), config.createOptions["concurrentNum"]);
    EXPECT_EQ(instance.createoptions().at(RESOURCE_OWNER_KEY), SYSTEM_OWNER_VALUE);
    EXPECT_EQ(instance.scheduleoption().schedpolicyname(), "monopoly");
    EXPECT_EQ(instance.instancestatus().code(), static_cast<int32_t>(InstanceState::SCHEDULING));
    EXPECT_EQ(instance.instancestatus().msg(), "scheduling");
    EXPECT_EQ(instance.tenantid(), "tenantID");
    EXPECT_EQ(instance.resources().resources().find(CPU_RESOURCE_NAME)->second.scalar().value(), config.cpu);
    EXPECT_EQ(instance.resources().resources().find(MEMORY_RESOURCE_NAME)->second.scalar().value(), config.memory);
}

/**
 * Feature: BootstrapActorTest LoadSysFuncPayloadsTest
 * Description: load system-function payloads files
 * Steps:
 * 1. Load system-function payloads from files
 * Expectation:
 * 1. Local cache saved correct payloads
 */
TEST_F(BootstrapActorTest, LoadSysFuncPayloadsTest)
{
    (void)litebus::os::Rmdir(SYSTEM_FUNC_PAYLOAD_PATH);
    auto status = LoadSysFuncPayloads();
    EXPECT_EQ(status.StatusCode(), StatusCode::FAILED);

    GenSystemFunctionPayloadFiles(payloadContent);
    status = LoadSysFuncPayloads();
    auto payload = GetFunctionPayload("0-system-faasscheduler").Get();
    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(payload.sysFuncName, "0-system-faascontroller");
    EXPECT_EQ(payload.signal, 65);
    EXPECT_EQ(payload.payload.dump(), R"({"version":"v1"})");

    payload = GetFunctionPayload("0-system-faasfrontend").Get();
    EXPECT_EQ(payload.sysFuncName, "0-system-faascontroller");
    EXPECT_EQ(payload.signal, 66);
    EXPECT_EQ(payload.payload.at("instanceNum").get<int32_t>(), 0);
    bootstrapActor_->ScaleByFunctionName("0-system-faasfrontend", 3);
    payload = GetFunctionPayload("0-system-faasfrontend").Get();
    EXPECT_EQ(payload.payload.at("instanceNum").get<int32_t>(), 3);
    // boostrapActor change to slave
    explorer::LeaderInfo leaderInfo{.name = "newMaster", .address = "127.0.0.9:8080"};
    bootstrapActor_->UpdateLeaderInfo(leaderInfo);
    bootstrapActor_->ScaleByFunctionName("0-system-faasfrontend", 4);
    payload = GetFunctionPayload("0-system-faasfrontend").Get();
    EXPECT_EQ(payload.payload.at("instanceNum").get<int32_t>(), 4);
}

/**
 * Feature: BootstrapActorTest LoadCurrentFunctionConfigsTest
 * Description: Bootstrap load current system-function-configs from etcd
 * Steps:
 * 1. Load system-function configs from file
 * 2. Put configs into etcd
 * 3. Load system-function configs from etcd
 *
 * Expectation:
 * 1. All configs can be loaded from etcd
 * 2. All configs are consist with local cache
 */
TEST_F(BootstrapActorTest, LoadCurrentFunctionConfigsTest)
{
    const std::string content =
        R"({"0-system-faascontroller":{"tenantID":"0","version":"$latest","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx2":{"xxx":1000}}},"0-system-faasfrontend":{"tenantID":"0","version":"$latest","memory":100,"cpu":100,"createOptions":{"concurrentNum":"1"},"instanceNum":2,"schedulingOps": {"extension": {"schedule_policy": "shared"}},"args":{"xxx":4,"xxx2":{"xxx":2000}}}})";
    GenSystemFunctionConfigFile(content);

    auto status = LoadFunctionConfigs();
    EXPECT_TRUE(status.IsOk());
    auto controllerConfig = GetFunctionConfig("0-system-faascontroller").Get();
    auto frontConfig = GetFunctionConfig("0-system-faasfrontend").Get();

    auto result =
        metaStoreAccessor_->Put("/faas/system-function/config/0-system-faascontroller", controllerConfig.jsonStr);
    EXPECT_AWAIT_READY(result);
    result = metaStoreAccessor_->Put("/faas/system-function/config/0-system-faasfrontend", frontConfig.jsonStr);
    EXPECT_AWAIT_READY(result);

    status = LoadCurrentFunctionConfigs();
    EXPECT_TRUE(status.IsOk());
    auto currControllerConfig = GetCurrFunctionConfig("0-system-faascontroller").Get();
    auto currFrontConfig = GetCurrFunctionConfig("0-system-faasfrontend").Get();

    EXPECT_EQ(controllerConfig.tenantID, currControllerConfig.tenantID);
    EXPECT_EQ(controllerConfig.version, currControllerConfig.version);
    EXPECT_EQ(controllerConfig.cpu, currControllerConfig.cpu);
    EXPECT_EQ(controllerConfig.memory, currControllerConfig.memory);
    EXPECT_EQ(controllerConfig.createOptions.size(), currControllerConfig.createOptions.size());
    EXPECT_EQ(controllerConfig.createOptions.find("concurrentNum")->second,
              currControllerConfig.createOptions.find("concurrentNum")->second);
    EXPECT_EQ(controllerConfig.instanceNum, currControllerConfig.instanceNum);
    EXPECT_EQ(controllerConfig.extension.at("schedule_policy"), currControllerConfig.extension.at("schedule_policy"));
    EXPECT_EQ(controllerConfig.args.dump(), currControllerConfig.args.dump());
    EXPECT_EQ(controllerConfig.jsonStr, currControllerConfig.jsonStr);

    EXPECT_EQ(frontConfig.tenantID, currFrontConfig.tenantID);
    EXPECT_EQ(frontConfig.version, currFrontConfig.version);
    EXPECT_EQ(frontConfig.cpu, currFrontConfig.cpu);
    EXPECT_EQ(frontConfig.memory, currFrontConfig.memory);
    EXPECT_EQ(frontConfig.createOptions.find("concurrentNum")->second,
              currFrontConfig.createOptions.find("concurrentNum")->second);
    EXPECT_EQ(frontConfig.instanceNum, currFrontConfig.instanceNum);
    EXPECT_EQ(frontConfig.extension.at("schedule_policy"), currFrontConfig.extension.at("schedule_policy"));
    EXPECT_EQ(frontConfig.args.dump(), currFrontConfig.args.dump());
    EXPECT_EQ(frontConfig.jsonStr, currFrontConfig.jsonStr);
}

/**
 * Feature: BootstrapActorTest UpdateSysFuncMetaTest
 * Description: load system-function meta files
 * Steps:
 * 1. Load system-function metaInfo from files
 * 2. reload new system-function metaInfo from files twice
 *
 * Expectation:
 * 1. Local cache saved two versions
 * 2. metaStore saved two versions
 */
TEST_F(BootstrapActorTest, UpdateSysFuncMetaTest)
{
    (void)litebus::os::Rmdir(SYSTEM_FUNC_META_PATH);
    auto status = LoadSysFuncMetas();
    EXPECT_EQ(status.StatusCode(), StatusCode::FAILED);

    std::unordered_map<std::string, std::string> content = {
        { "faas-controller-meta.json", controllerV2 },
        { "faas-scheduler-meta.json", schedulerV1 },
        { "faas-frontend-meta.json", frontendV1 },
    };

    auto funcMeta = GetFuncMetaFromJson(controllerV1);
    auto future =
        metaStoreAccessor_->Put(FUNC_META_PATH_PREFIX + "/0/function/faascontroller/version/pre1", controllerV0);
    EXPECT_AWAIT_READY(future);
    future = metaStoreAccessor_->Put(FUNC_META_PATH_PREFIX + "/0/function/faascontroller/version/pre2", controllerV0);
    EXPECT_AWAIT_READY(future);
    future = metaStoreAccessor_->Put(FUNC_META_PATH_PREFIX + "/0/function/faascontroller/version/pre3", controllerV0);
    EXPECT_AWAIT_READY(future);
    future = metaStoreAccessor_->Put(FUNC_META_PATH_PREFIX + "/0/function/faascontroller/version/pre4", controllerV0);
    EXPECT_AWAIT_READY(future);
    future = metaStoreAccessor_->Put(FUNC_META_PATH_PREFIX + "/0/function/faascontroller/version/pre5", controllerV0);
    EXPECT_AWAIT_READY(future);

    LoadCurrentSysFuncMetas();
    auto queue = GetFunctionMetaQueue().Get();
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_EQ(queue["faascontroller"].size(), 5u);

    GenSystemFunctionMetaFiles(metaContent);
    UpdateMetaHandler();
    queue = GetFunctionMetaQueue().Get();
    EXPECT_EQ(queue.size(), 3u);
    EXPECT_EQ(queue["faascontroller"].size(), 6u);
    EXPECT_EQ(queue["faasscheduler"].size(), 1u);
    EXPECT_EQ(queue["faasfrontend"].size(), 1u);

    GenSystemFunctionMetaFiles(content);
    UpdateMetaHandler();
    queue = GetFunctionMetaQueue().Get();
    EXPECT_EQ(queue.size(), 3u);
    EXPECT_EQ(queue["faascontroller"].size(), 6u);
    EXPECT_EQ(queue["faasscheduler"].size(), 1u);
    EXPECT_EQ(queue["faasfrontend"].size(), 1u);

    GenSystemFunctionMetaFiles(metaContent);
    UpdateMetaHandler();
    queue = GetFunctionMetaQueue().Get();
    EXPECT_EQ(queue["faascontroller"].at(0).first,
              "/yr/functions/business/yrk/tenant/0/function/faascontroller/version/pre2");
    EXPECT_EQ(queue["faascontroller"].at(4).first,
              "/yr/functions/business/yrk/tenant/0/function/faascontroller/version/v2");
    EXPECT_EQ(queue["faascontroller"].at(5).first,
              "/yr/functions/business/yrk/tenant/0/function/faascontroller/version/v1");
    ASSERT_AWAIT_TRUE([=]() {
        return metaStoreAccessor_->GetAllWithPrefix(FUNC_META_PATH_PREFIX + "/0/function/faascontroller").Get().size() == 6;
    });
    ASSERT_AWAIT_TRUE([=]() {
        return metaStoreAccessor_->GetAllWithPrefix(FUNC_META_PATH_PREFIX + "/0/function/faasscheduler").Get().size() == 1;
    });
    ASSERT_AWAIT_TRUE([=]() {
        return metaStoreAccessor_->GetAllWithPrefix(FUNC_META_PATH_PREFIX + "/0/function/faasfrontend").Get().size() == 1;
    });
}

/**
 * Feature: BootstrapActorTest ScheduleTest
 * Description: Send schedule request to global scheduler
 * Steps:
 * 1. Put instance into meta_store
 * 2. Write config file
 * 3. LoadBootstrapConfig
 *
 * Expectation:
 * 3. Send 3 schedule request
 */
TEST_F(BootstrapActorTest, ScheduleTest)
{
    const std::string content =
        R"({"0-system-faascontroller":{"tenantID":"0","version":"$latest","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}},"0-system-actorcontroller":{"tenantID":"0","version":"$latest","memory":100,"cpu":100,"createOptions":{"concurrentNum":"1"},"instanceNum":2,"schedulingOps": {"extension": {"schedule_policy": "shared"}},"args":{"xxx":4,"xxx2":{"xxx":2000}}}})";
    bootstrapActor_->retryTimeoutMs_ = 100;
    GenSystemFunctionPayloadFiles(payloadContent);
    GenSystemFunctionMetaFiles(metaContent);
    GenSystemFunctionConfigFile(content);

    auto expectedCall = std::make_shared<litebus::Promise<bool>>();
    EXPECT_CALL(*globalSched_, Schedule)
        .WillOnce(testing::Return(Status(StatusCode::FAILED)))
        // retry.WillOnce(testing::Return(Status(StatusCode::SUCCESS)))
        .WillOnce(testing::Return(Status(StatusCode::SUCCESS)))
        .WillOnce(testing::DoAll(testing::Invoke([expectedCall](const std::shared_ptr<messages::ScheduleRequest> &) {
                                     expectedCall->SetValue(true);
                                 }),
                                 testing::Return(Status(StatusCode::SUCCESS))));

    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise;
    promise.SetValue({ "", nullptr });

    auto instance = std::make_shared<resource_view::InstanceInfo>();
    instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise2;
    promise2.SetValue({ "key", instance });
    EXPECT_CALL(*mockInstanceMgr_, GetInstanceInfoByInstanceID)
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillRepeatedly(testing::Return(promise2.GetFuture()));

    LoadBootstrapConfig("");
    EXPECT_AWAIT_READY(expectedCall->GetFuture());
    auto currControllerConfig = GetCurrFunctionConfig("0-system-faascontroller");
    EXPECT_EQ(currControllerConfig.IsSome(), true);
}

/**
 * Feature: BootstrapActorTest KillInstancesTest
 * Description: Kill system function instances
 * Steps:
 * 1. Put function accessor instance into meta_store
 * 2. Write config file, load config
 * 3. Kill instances
 *
 * Expectation:
 * 2. Call global scheduler 3 times
 * 3. Call function accessor 3 times
 */
TEST_F(BootstrapActorTest, KillInstancesTest)
{
    auto mockLocal = std::make_shared<MockLocalActor>();
    litebus::Spawn(mockLocal);

    auto funcKey = "0/0-system-faascontroller/$latest";
    auto funcKey2 = "0/0-system-faasfrontend/$latest";
    auto key = "0-system-faascontroller-0";
    auto key2 = "0-system-faasfrontend-0";
    auto key3 = "0-system-faasfrontend-1";
    auto instanceInfoStr =
        R"({"instanceID":"0-system-faascontroller-0","requestID":"0-system-faascontroller-0","runtimeID":"runtime-ca040000-0000-4000-80df-0a1e4dc86433","runtimeAddress":"10.42.2.214:21006","functionAgentID":"function_agent_10.42.2.214-58866","functionProxyID":"mock","function":"0/0-system-faascontroller/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":666}},"CPU":{"name":"CPU","scalar":{"value":666}}}},"scheduleOption":{"schedPolicyName":"monopoly","affinity":{"instanceAffinity":{}},"resourceSelector":{"resource.owner":"0-system-faascontroller-0"}},"createOptions":{"DELEGATE_ENCRYPT":"{\"metaEtcdPwd\":\"\"}","resource.owner":"system","NO_USE_DELEGATE_VOLUME_MOUNTS":"[{\"name\":\"sts-config\",\"mountPath\":\"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/apple/a\",\"subPath\":\"a\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/boy/b\",\"subPath\":\"b\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/cat/c\",\"subPath\":\"c\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/dog/d\",\"subPath\":\"d\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker.ini\",\"subPath\":\"HMSCaaSYuanRongWorker.ini\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker.sts.p12\",\"subPath\":\"HMSCaaSYuanRongWorker.sts.p12\"},{\"name\":\"certs-volume\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/\"}]","concurrentNum":"10"},"instanceStatus":{"code":3,"msg":"running"},"storageType":"local","scheduleTimes":1,"deployTimes":1,"args":[{"value":"eyJmYWFzZnJvbnRlbmRDb25maWciOnsiYXV0aGVudGljYXRpb25FbmFibGUiOmZhbHNlLCJidXNpbmVzc1R5cGUiOjEsImNsdXN0ZXJJRCI6ImNsdXN0ZXIwMDEiLCJjcHUiOjEyMDAsImRhdGFTeXN0ZW1Db25maWciOnsiZXhlY3V0ZVRUTFNlYyI6MTgwMCwiZXhlY3V0ZVdyaXRlTW9kZSI6Ik5vbmVMMkNhY2hlIiwidGltZW91dE1zIjo2MDAwMCwidXBsb2FkVFRMU2VjIjo4NjQwMCwidXBsb2FkV3JpdGVNb2RlIjoiTm9uZUwyQ2FjaGUifSwiZnVuY3Rpb25DYXBhYmlsaXR5IjoxLCJodHRwIjp7Im1heFJlcXVlc3RCb2R5U2l6ZSI6NiwicmVzcHRpbWVvdXQiOjUsIndvcmtlckluc3RhbmNlUmVhZFRpbWVPdXQiOjV9LCJtZW1vcnkiOjQwOTYsInNsYVF1b3RhIjoxMDAwLCJ0cmFmZmljTGltaXREaXNhYmxlIjp0cnVlLCJ1cnBjQ29uZmlnIjp7ImVuYWJsZWQiOmZhbHNlLCJwb2xsaW5nTnVtIjowLCJwb29sU2l6ZSI6MjAwLCJwb3J0IjoxOTk5Niwid29ya2VyTnVtIjoxMH19LCJmYWFzc2NoZWR1bGVyQ29uZmlnIjp7ImJ1cnN0U2NhbGVOdW0iOjEwMDAsImNwdSI6MTAwMCwiZG9ja2VyUm9vdFBhdGgiOiIvaG9tZS9kaXNrL2RvY2tlciIsImxlYXNlU3BhbiI6MTAwMCwibWVtb3J5Ijo0MDk2LCJzY2FsZURvd25UaW1lIjo2MDAwMCwic2xhUXVvdGEiOjEwMDB9LCJmcm9udGVuZEluc3RhbmNlTnVtIjoxLCJtZXRhRXRjZCI6eyJwYXNzd29yZCI6IiIsInNlcnZlcnMiOlsiNy4yMTguMjEuMjQ6MzIzODAiXSwic3NsRW5hYmxlIjpmYWxzZSwidXNlciI6IiJ9LCJyYXdTdHNDb25maWciOnsic2VydmVyQ29uZmlnIjp7ImRvbWFpbiI6IiR7U1RTX0RPTUFJTl9TRVJWRVJ9IiwicGF0aCI6Ii9vcHQvaHVhd2VpL2NlcnRzL0hNU0NsaWVudENsb3VkQWNjZWxlcmF0ZVNlcnZpY2UvSE1TQ2FhU1l1YW5Sb25nV29ya2VyL0hNU0NhYXNZdWFuUm9uZ1dvcmtlci5pbmkifSwic3RzRW5hYmxlIjpmYWxzZX0sInJvdXRlckV0Y2QiOnsicGFzc3dvcmQiOiIiLCJzZXJ2ZXJzIjpbIjcuMjE4LjIxLjI0OjMyMzc5Il0sInNzbEVuYWJsZSI6ZmFsc2UsInVzZXIiOiIifSwic2NoZWR1bGVySW5zdGFuY2VOdW0iOjEsInRsc0NvbmZpZyI6eyJjYUNvbnRlbnQiOiIke0NBX0NPTlRFTlR9IiwiY2VydENvbnRlbnQiOiIke0NFUlRfQ09OVEVOVH0iLCJrZXlDb250ZW50IjoiJHtLRVlfQ09OVEVOVH0ifX0="}],"version":"3","dataSystemHost":"10.244.158.229"})";
    const std::string content =
        R"({"0-system-faascontroller":{"memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}},"0-system-faasfrontend":{"memory":100,"cpu":100,"createOptions":{"concurrentNum":"1"},"instanceNum":2,"schedulingOps": {"extension": {"schedule_policy": "shared"}},"args":{"xxx":4,"xxx2":{"xxx":2000}}}})";

    GenSystemFunctionPayloadFiles({});
    GenSystemFunctionMetaFiles(metaContent);
    GenSystemFunctionConfigFile(content);
    auto expectedCall = std::make_shared<litebus::Promise<bool>>();
    EXPECT_CALL(*globalSched_, Schedule)
        .WillOnce(testing::DoAll(
            testing::Invoke([funcKey2, key2, instanceInfoStr](const std::shared_ptr<messages::ScheduleRequest> &) {
                auto status = metaStoreAccessor_->Put(GenInstanceKey(funcKey2, key2, key2).Get(), instanceInfoStr);
                EXPECT_AWAIT_READY(status);
            }),
            testing::Return(Status(StatusCode::SUCCESS))))
        .WillOnce(testing::DoAll(
            testing::Invoke([funcKey2, key3, instanceInfoStr](const std::shared_ptr<messages::ScheduleRequest> &) {
                auto status = metaStoreAccessor_->Put(GenInstanceKey(funcKey2, key3, key3).Get(), instanceInfoStr);
                EXPECT_AWAIT_READY(status);
            }),
            testing::Return(Status(StatusCode::SUCCESS))))
        .WillOnce(testing::DoAll(testing::Invoke([funcKey, key, instanceInfoStr,
                                                  expectedCall](const std::shared_ptr<messages::ScheduleRequest> &) {
                                     auto status = metaStoreAccessor_->Put(GenInstanceKey(funcKey, key, key).Get(),
                                                                           instanceInfoStr);
                                     EXPECT_AWAIT_READY(status);
                                     expectedCall->SetValue(true);
                                 }),
                                 testing::Return(Status(StatusCode::SUCCESS))));

    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise;
    promise.SetValue({ "", nullptr });

    auto instance = std::make_shared<resource_view::InstanceInfo>();
    instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise2;
    promise2.SetValue({ "key", instance });
    EXPECT_CALL(*mockInstanceMgr_, GetInstanceInfoByInstanceID)
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillRepeatedly(testing::Return(promise2.GetFuture()));

    LoadBootstrapConfig("");
    EXPECT_AWAIT_READY(expectedCall->GetFuture());

    litebus::Future<litebus::Option<std::string>> address;
    address.SetValue(litebus::Option<std::string>(localAddress_));

    EXPECT_CALL(*globalSched_, GetLocalAddress).WillRepeatedly(testing::Return(address));
    EXPECT_CALL(*mockLocal, ForwardCustomSignalRequest)
        .Times(3)
        .WillRepeatedly(testing::Invoke([aid(mockLocal->GetAID())](const litebus::AID &from, std::string &&,
                                                                   std::string &&msg) {
            internal::ForwardKillRequest forwardKillRequest;
            if (msg.empty() || !forwardKillRequest.ParseFromString(msg)) {
                YRLOG_WARN("Failed to parse requestID from forwardKillRequest.");
                return;
            }
            litebus::Async(aid, &MockLocalActor::SendForwardCustomSignalResponse, from, forwardKillRequest.requestid());
        }));

    auto status = litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::KillSystemFuncInstances);
    EXPECT_AWAIT_READY(status);
    EXPECT_TRUE(status.IsOK());

    litebus::Terminate(mockLocal->GetAID());
    litebus::Await(mockLocal->GetAID());
}

/**
 * Feature: BootstrapActorTest KeepAliveTest
 * Description: Bootstrap keep system function alive
 * Steps:
 * 1. Start system function
 * 2. Delete instance key in meta store
 * 3. Wait for check, and start function again
 *
 * Expectation:
 * 1. Invoke schedule once
 * 3. Invoke schedule again
 */
TEST_F(BootstrapActorTest, KeepAliveTest)
{
    const std::string content =
        R"({"0-system-faascontroller":{"tenantID":"0","version":"$latest","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";

    GenSystemFunctionPayloadFiles(payloadContent);
    GenSystemFunctionMetaFiles(metaContent);
    GenSystemFunctionConfigFile(content);

    auto expectedCall1 = std::make_shared<litebus::Promise<bool>>();
    auto expectedCall2 = std::make_shared<litebus::Promise<bool>>();
    EXPECT_CALL(*globalSched_, Schedule)
        .WillOnce(testing::DoAll(
            testing::Invoke([expectedCall1](const std::shared_ptr<messages::ScheduleRequest> &) {
                expectedCall1->SetValue(true);
            }),
            testing::Return(Status(StatusCode::SUCCESS))))
        .WillOnce(testing::DoAll(
            testing::Invoke([expectedCall2](const std::shared_ptr<messages::ScheduleRequest> &) {
                expectedCall2->SetValue(true);
            }),
            testing::Return(Status(StatusCode::SUCCESS))));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise;
    promise.SetValue({ "", nullptr });

    auto instance = std::make_shared<resource_view::InstanceInfo>();
    instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise2;
    promise2.SetValue({ "key", instance });
    EXPECT_CALL(*mockInstanceMgr_, GetInstanceInfoByInstanceID)
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillOnce(testing::Return(promise2.GetFuture()))
        .WillOnce(testing::Return(promise2.GetFuture()))
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillRepeatedly(testing::Return(promise2.GetFuture()));

    LoadBootstrapConfig("");
    EXPECT_AWAIT_READY(expectedCall1->GetFuture());
    EXPECT_AWAIT_READY(expectedCall2->GetFuture());

    litebus::Terminate(bootstrapActor_->GetAID());
    litebus::Await(bootstrapActor_->GetAID());
}

TEST_F(BootstrapActorTest, UpdateSysFunctionPayloadTest)
{
    bootstrapActor_->waitKillInstanceMs_ = 300;
    bootstrapActor_->waitStartInstanceMs_ = 1000;
    bootstrapActor_->waitUpdateConfigMapMs_ = 100;
    bootstrapActor_->retryTimeoutMs_ = 100;

    auto mockLocal = std::make_shared<MockLocalActor>();
    litebus::Spawn(mockLocal);

    auto funcKey = "0/0-system-faascontroller/$latest";
    auto key = "0-system-faascontroller-0";
    const std::string content =
        R"({"0-system-faascontroller":{"tenantID":"0","version":"$latest","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}},"0-system-faasfrontend":{"tenantID":"0","version":"$latest","memory":100,"cpu":100,"createOptions":{"concurrentNum":"1"},"instanceNum":2,"schedulingOps": {"extension": {"schedule_policy": "shared"}},"args":{"xxx":4,"xxx2":{"xxx":2000}}}})";
    auto instanceInfoStr =
        R"({"instanceID":"0-system-faascontroller-0","requestID":"0-system-faascontroller-0","runtimeID":"runtime-ca040000-0000-4000-80df-0a1e4dc86433","runtimeAddress":"10.42.2.214:21006","functionAgentID":"function_agent_10.42.2.214-58866","functionProxyID":"mock","function":"0/0-system-faascontroller/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":666}},"CPU":{"name":"CPU","scalar":{"value":666}}}},"scheduleOption":{"schedPolicyName":"monopoly","affinity":{"instanceAffinity":{}},"resourceSelector":{"resource.owner":"0-system-faascontroller-0"}},"createOptions":{"DELEGATE_ENCRYPT":"{\"metaEtcdPwd\":\"\"}","resource.owner":"system","NO_USE_DELEGATE_VOLUME_MOUNTS":"[{\"name\":\"sts-config\",\"mountPath\":\"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/apple/a\",\"subPath\":\"a\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/boy/b\",\"subPath\":\"b\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/cat/c\",\"subPath\":\"c\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/dog/d\",\"subPath\":\"d\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker.ini\",\"subPath\":\"HMSCaaSYuanRongWorker.ini\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker.sts.p12\",\"subPath\":\"HMSCaaSYuanRongWorker.sts.p12\"},{\"name\":\"certs-volume\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/\"}]","concurrentNum":"10"},"instanceStatus":{"code":3,"msg":"running"},"storageType":"local","scheduleTimes":1,"deployTimes":1,"args":[{"value":"eyJmYWFzZnJvbnRlbmRDb25maWciOnsiYXV0aGVudGljYXRpb25FbmFibGUiOmZhbHNlLCJidXNpbmVzc1R5cGUiOjEsImNsdXN0ZXJJRCI6ImNsdXN0ZXIwMDEiLCJjcHUiOjEyMDAsImRhdGFTeXN0ZW1Db25maWciOnsiZXhlY3V0ZVRUTFNlYyI6MTgwMCwiZXhlY3V0ZVdyaXRlTW9kZSI6Ik5vbmVMMkNhY2hlIiwidGltZW91dE1zIjo2MDAwMCwidXBsb2FkVFRMU2VjIjo4NjQwMCwidXBsb2FkV3JpdGVNb2RlIjoiTm9uZUwyQ2FjaGUifSwiZnVuY3Rpb25DYXBhYmlsaXR5IjoxLCJodHRwIjp7Im1heFJlcXVlc3RCb2R5U2l6ZSI6NiwicmVzcHRpbWVvdXQiOjUsIndvcmtlckluc3RhbmNlUmVhZFRpbWVPdXQiOjV9LCJtZW1vcnkiOjQwOTYsInNsYVF1b3RhIjoxMDAwLCJ0cmFmZmljTGltaXREaXNhYmxlIjp0cnVlLCJ1cnBjQ29uZmlnIjp7ImVuYWJsZWQiOmZhbHNlLCJwb2xsaW5nTnVtIjowLCJwb29sU2l6ZSI6MjAwLCJwb3J0IjoxOTk5Niwid29ya2VyTnVtIjoxMH19LCJmYWFzc2NoZWR1bGVyQ29uZmlnIjp7ImJ1cnN0U2NhbGVOdW0iOjEwMDAsImNwdSI6MTAwMCwiZG9ja2VyUm9vdFBhdGgiOiIvaG9tZS9kaXNrL2RvY2tlciIsImxlYXNlU3BhbiI6MTAwMCwibWVtb3J5Ijo0MDk2LCJzY2FsZURvd25UaW1lIjo2MDAwMCwic2xhUXVvdGEiOjEwMDB9LCJmcm9udGVuZEluc3RhbmNlTnVtIjoxLCJtZXRhRXRjZCI6eyJwYXNzd29yZCI6IiIsInNlcnZlcnMiOlsiNy4yMTguMjEuMjQ6MzIzODAiXSwic3NsRW5hYmxlIjpmYWxzZSwidXNlciI6IiJ9LCJyYXdTdHNDb25maWciOnsic2VydmVyQ29uZmlnIjp7ImRvbWFpbiI6IiR7U1RTX0RPTUFJTl9TRVJWRVJ9IiwicGF0aCI6Ii9vcHQvaHVhd2VpL2NlcnRzL0hNU0NsaWVudENsb3VkQWNjZWxlcmF0ZVNlcnZpY2UvSE1TQ2FhU1l1YW5Sb25nV29ya2VyL0hNU0NhYXNZdWFuUm9uZ1dvcmtlci5pbmkifSwic3RzRW5hYmxlIjpmYWxzZX0sInJvdXRlckV0Y2QiOnsicGFzc3dvcmQiOiIiLCJzZXJ2ZXJzIjpbIjcuMjE4LjIxLjI0OjMyMzc5Il0sInNzbEVuYWJsZSI6ZmFsc2UsInVzZXIiOiIifSwic2NoZWR1bGVySW5zdGFuY2VOdW0iOjEsInRsc0NvbmZpZyI6eyJjYUNvbnRlbnQiOiIke0NBX0NPTlRFTlR9IiwiY2VydENvbnRlbnQiOiIke0NFUlRfQ09OVEVOVH0iLCJrZXlDb250ZW50IjoiJHtLRVlfQ09OVEVOVH0ifX0="}],"version":"3","dataSystemHost":"10.244.158.229"})";

    const std::unordered_map<std::string, std::string> newPayloadContent = {
        { "faas-scheduler-config.json",
          R"({"0-system-faasscheduler":{"systemFunctionName":"0-system-faascontroller","signal":65,"payload":{"version":"v2"}}})" },
        { "faas-frontend-config.json",
          R"({"0-system-faasfrontend":{"systemFunctionName":"0-system-faascontroller","signal":66,"payload":{"version":"v1"}}})" },
    };

    GenSystemFunctionMetaFiles(metaContent);
    GenSystemFunctionConfigFile(content);

    EXPECT_CALL(*globalSched_, Schedule)
        .WillRepeatedly(testing::DoAll(
            testing::Invoke(
                [funcKey, key, instanceInfoStr, content](const std::shared_ptr<messages::ScheduleRequest> &) {
                    auto status = metaStoreAccessor_->Put(GenInstanceKey(funcKey, key, key).Get(), instanceInfoStr);
                    EXPECT_AWAIT_READY(status);
                    status = metaStoreAccessor_->Put("/faas/system-function/config/0-system-faascontroller", content);
                    EXPECT_AWAIT_READY(status);
                    YRLOG_INFO("Success schedule {}", funcKey);
                }),
            testing::Return(Status(StatusCode::SUCCESS))));
    litebus::Future<litebus::Option<std::string>> address;
    address.SetValue(litebus::Option<std::string>(localAddress_));
    EXPECT_CALL(*globalSched_, GetLocalAddress).WillRepeatedly(testing::Return(address));

    auto firstCallEnd = std::make_shared<litebus::Promise<bool>>();
    auto secondCallEnd = std::make_shared<litebus::Promise<bool>>();
    EXPECT_CALL(*mockLocal, ForwardCustomSignalRequest)
        .WillOnce(testing::Invoke([aid(mockLocal->GetAID())](const litebus::AID &from, std::string &&,
                                                             std::string &&msg) {
            internal::ForwardKillRequest forwardKillRequest;
            if (msg.empty() || !forwardKillRequest.ParseFromString(msg)) {
                YRLOG_WARN("Failed to parse requestID from forwardKillRequest.");
                return;
            }
            litebus::Async(aid, &MockLocalActor::SendForwardCustomSignalResponse, from, forwardKillRequest.requestid());
        }))
        .WillOnce(testing::Invoke([firstCallEnd, aid(mockLocal->GetAID())](const litebus::AID &from, std::string &&,
                                                                           std::string &&msg) {
            internal::ForwardKillRequest forwardKillRequest;
            if (msg.empty() || !forwardKillRequest.ParseFromString(msg)) {
                YRLOG_WARN("Failed to parse requestID from forwardKillRequest.");
                return;
            }
            litebus::Async(aid, &MockLocalActor::SendForwardCustomSignalResponse, from, forwardKillRequest.requestid());
            firstCallEnd->SetValue(true);
        }))
        .WillOnce(testing::Invoke([aid(mockLocal->GetAID())](const litebus::AID &from, std::string &&,
                                                             std::string &&msg) {
            internal::ForwardKillRequest forwardKillRequest;
            if (msg.empty() || !forwardKillRequest.ParseFromString(msg)) {
                YRLOG_WARN("Failed to parse requestID from forwardKillRequest.");
                return;
            }
            litebus::Async(aid, &MockLocalActor::SendForwardCustomSignalResponse, from, forwardKillRequest.requestid());
        }))
        .WillOnce(testing::Invoke([secondCallEnd, aid(mockLocal->GetAID())](const litebus::AID &from, std::string &&,
                                                                            std::string &&msg) {
            internal::ForwardKillRequest forwardKillRequest;
            if (msg.empty() || !forwardKillRequest.ParseFromString(msg)) {
                YRLOG_WARN("Failed to parse requestID from forwardKillRequest.");
                return;
            }
            litebus::Async(aid, &MockLocalActor::SendForwardCustomSignalResponse, from, forwardKillRequest.requestid());
            secondCallEnd->SetValue(true);
        }));

    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise;
    promise.SetValue({ "", nullptr });

    auto instance = std::make_shared<resource_view::InstanceInfo>();
    instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise2;
    promise2.SetValue({ "key", instance });
    EXPECT_CALL(*mockInstanceMgr_, GetInstanceInfoByInstanceID)
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillOnce(testing::Return(promise.GetFuture()))
        .WillRepeatedly(testing::Return(promise2.GetFuture()));

    GenSystemFunctionPayloadFiles(payloadContent);
    UpdatePayloadHandler();
    auto config = GetFunctionPayload("0-system-faasscheduler").Get();
    auto status = SendUpdateArgsSignal("0-system-faasscheduler", config, 0);
    EXPECT_FALSE(status.IsOk());
    status = SendUpdateArgsSignal("0-system-faasscheduler", config, 6);
    EXPECT_FALSE(status.IsOk());

    YRLOG_INFO("Start first update");
    LoadBootstrapConfig("");
    EXPECT_AWAIT_READY(firstCallEnd->GetFuture());

    YRLOG_INFO("Start second update");
    GenSystemFunctionPayloadFiles(newPayloadContent);
    UpdatePayloadHandler();
    EXPECT_AWAIT_READY(secondCallEnd->GetFuture());

    litebus::Terminate(mockLocal->GetAID());
    litebus::Await(mockLocal->GetAID());
}

TEST_F(BootstrapActorTest, UpdateSysFunctionConfigTest)
{
    bootstrapActor_->waitKillInstanceMs_ = 100;
    bootstrapActor_->waitStartInstanceMs_ = 100;
    bootstrapActor_->waitUpdateConfigMapMs_ = 100;
    bootstrapActor_->retryTimeoutMs_ = 100;

    auto mockLocal = std::make_shared<MockLocalActor>();
    litebus::Spawn(mockLocal);

    auto funcKey = "0/0-system-faascontroller/v1";
    auto funcKey2 = "0/0-system-faascontroller/v2";
    auto key = "0-system-faascontroller-0";
    auto instanceInfoStr =
        R"({"instanceID":"0-system-faascontroller-0","requestID":"0-system-faascontroller-0","runtimeID":"runtime-ca040000-0000-4000-80df-0a1e4dc86433","runtimeAddress":"10.42.2.214:21006","functionAgentID":"function_agent_10.42.2.214-58866","functionProxyID":"mock","function":"0/0-system-faascontroller/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":666}},"CPU":{"name":"CPU","scalar":{"value":666}}}},"scheduleOption":{"schedPolicyName":"monopoly","affinity":{"instanceAffinity":{}},"resourceSelector":{"resource.owner":"0-system-faascontroller-0"}},"createOptions":{"DELEGATE_ENCRYPT":"{\"metaEtcdPwd\":\"\"}","resource.owner":"system","NO_USE_DELEGATE_VOLUME_MOUNTS":"[{\"name\":\"sts-config\",\"mountPath\":\"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/apple/a\",\"subPath\":\"a\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/boy/b\",\"subPath\":\"b\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/cat/c\",\"subPath\":\"c\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/dog/d\",\"subPath\":\"d\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker.ini\",\"subPath\":\"HMSCaaSYuanRongWorker.ini\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker.sts.p12\",\"subPath\":\"HMSCaaSYuanRongWorker.sts.p12\"},{\"name\":\"certs-volume\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/\"}]","concurrentNum":"10"},"instanceStatus":{"code":3,"msg":"running"},"storageType":"local","scheduleTimes":1,"deployTimes":1,"args":[{"value":"eyJmYWFzZnJvbnRlbmRDb25maWciOnsiYXV0aGVudGljYXRpb25FbmFibGUiOmZhbHNlLCJidXNpbmVzc1R5cGUiOjEsImNsdXN0ZXJJRCI6ImNsdXN0ZXIwMDEiLCJjcHUiOjEyMDAsImRhdGFTeXN0ZW1Db25maWciOnsiZXhlY3V0ZVRUTFNlYyI6MTgwMCwiZXhlY3V0ZVdyaXRlTW9kZSI6Ik5vbmVMMkNhY2hlIiwidGltZW91dE1zIjo2MDAwMCwidXBsb2FkVFRMU2VjIjo4NjQwMCwidXBsb2FkV3JpdGVNb2RlIjoiTm9uZUwyQ2FjaGUifSwiZnVuY3Rpb25DYXBhYmlsaXR5IjoxLCJodHRwIjp7Im1heFJlcXVlc3RCb2R5U2l6ZSI6NiwicmVzcHRpbWVvdXQiOjUsIndvcmtlckluc3RhbmNlUmVhZFRpbWVPdXQiOjV9LCJtZW1vcnkiOjQwOTYsInNsYVF1b3RhIjoxMDAwLCJ0cmFmZmljTGltaXREaXNhYmxlIjp0cnVlLCJ1cnBjQ29uZmlnIjp7ImVuYWJsZWQiOmZhbHNlLCJwb2xsaW5nTnVtIjowLCJwb29sU2l6ZSI6MjAwLCJwb3J0IjoxOTk5Niwid29ya2VyTnVtIjoxMH19LCJmYWFzc2NoZWR1bGVyQ29uZmlnIjp7ImJ1cnN0U2NhbGVOdW0iOjEwMDAsImNwdSI6MTAwMCwiZG9ja2VyUm9vdFBhdGgiOiIvaG9tZS9kaXNrL2RvY2tlciIsImxlYXNlU3BhbiI6MTAwMCwibWVtb3J5Ijo0MDk2LCJzY2FsZURvd25UaW1lIjo2MDAwMCwic2xhUXVvdGEiOjEwMDB9LCJmcm9udGVuZEluc3RhbmNlTnVtIjoxLCJtZXRhRXRjZCI6eyJwYXNzd29yZCI6IiIsInNlcnZlcnMiOlsiNy4yMTguMjEuMjQ6MzIzODAiXSwic3NsRW5hYmxlIjpmYWxzZSwidXNlciI6IiJ9LCJyYXdTdHNDb25maWciOnsic2VydmVyQ29uZmlnIjp7ImRvbWFpbiI6IiR7U1RTX0RPTUFJTl9TRVJWRVJ9IiwicGF0aCI6Ii9vcHQvaHVhd2VpL2NlcnRzL0hNU0NsaWVudENsb3VkQWNjZWxlcmF0ZVNlcnZpY2UvSE1TQ2FhU1l1YW5Sb25nV29ya2VyL0hNU0NhYXNZdWFuUm9uZ1dvcmtlci5pbmkifSwic3RzRW5hYmxlIjpmYWxzZX0sInJvdXRlckV0Y2QiOnsicGFzc3dvcmQiOiIiLCJzZXJ2ZXJzIjpbIjcuMjE4LjIxLjI0OjMyMzc5Il0sInNzbEVuYWJsZSI6ZmFsc2UsInVzZXIiOiIifSwic2NoZWR1bGVySW5zdGFuY2VOdW0iOjEsInRsc0NvbmZpZyI6eyJjYUNvbnRlbnQiOiIke0NBX0NPTlRFTlR9IiwiY2VydENvbnRlbnQiOiIke0NFUlRfQ09OVEVOVH0iLCJrZXlDb250ZW50IjoiJHtLRVlfQ09OVEVOVH0ifX0="}],"version":"3","dataSystemHost":"10.244.158.229"})";

    const std::string content1 =
        R"({"0-system-faascontroller":{"version":"v1","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v1\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";

    GenSystemFunctionPayloadFiles(payloadContent);
    GenSystemFunctionMetaFiles(metaContent);

    EXPECT_CALL(*globalSched_, GetLocalAddress).WillRepeatedly(testing::Return(litebus::Option<std::string>(localAddress_)));

    // start test
    auto expectedCall = std::make_shared<litebus::Promise<bool>>();
    bool isEmpty = true;
    EXPECT_CALL(*globalSched_, Schedule)
        .WillOnce(testing::DoAll(testing::Invoke([funcKey](const std::shared_ptr<messages::ScheduleRequest> &) {
                                     YRLOG_INFO("Failed schedule {}", funcKey);
                                 }),
                                 testing::Return(Status(StatusCode::FAILED))))
        .WillOnce(testing::DoAll(testing::Invoke([expectedCall, funcKey, key, instanceInfoStr,
                                                  &isEmpty](const std::shared_ptr<messages::ScheduleRequest> &) {
                                     auto status = metaStoreAccessor_->Put(GenInstanceKey(funcKey, key, key).Get(),
                                                                           instanceInfoStr);
                                     EXPECT_AWAIT_READY(status);
                                     expectedCall->SetValue(true);
                                     isEmpty = false;
                                 }),
                                 testing::Return(Status(StatusCode::SUCCESS))));

    auto getFuture = [&isEmpty]() {
        litebus::Promise<instance_manager::InstanceKeyInfoPair> promise;
        if (isEmpty) {
            promise.SetValue({ "", nullptr });
            return promise.GetFuture();
        }

        auto instance = std::make_shared<resource_view::InstanceInfo>();
        instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
        promise.SetValue({ "key", instance });
        return promise.GetFuture();
    };
    EXPECT_CALL(*mockInstanceMgr_, GetInstanceInfoByInstanceID).WillRepeatedly(testing::Return(getFuture()));

    YRLOG_INFO("Start first update");
    GenSystemFunctionConfigFile(content1);
    LoadBootstrapConfig("");
    EXPECT_AWAIT_READY(expectedCall->GetFuture());

    auto updateConfigTestHandler = [&](const std::string &content, const std::string &killKey,
                                       const std::string &putKey) {
        auto expectedCall = std::make_shared<litebus::Promise<bool>>();
        EXPECT_CALL(*mockLocal, ForwardCustomSignalRequest)
            .WillRepeatedly(testing::Invoke([killKey, key, aid(mockLocal->GetAID()), &isEmpty](
                                                const litebus::AID &from, std::string &&, std::string &&msg) {
                internal::ForwardKillRequest forwardKillRequest;
                if (msg.empty() || !forwardKillRequest.ParseFromString(msg)) {
                    YRLOG_WARN("Failed to parse requestID from forwardKillRequest.");
                    return;
                }
                EXPECT_FALSE(forwardKillRequest.req().requestid().empty());
                litebus::Async(aid, &MockLocalActor::SendForwardCustomSignalResponse, from,
                               forwardKillRequest.requestid());
                auto status = metaStoreAccessor_->Delete(GenInstanceKey(killKey, key, key).Get());
                EXPECT_AWAIT_READY(status);
                isEmpty = true;
            }));
        EXPECT_CALL(*globalSched_, Schedule)
            .WillRepeatedly(testing::DoAll(testing::Invoke([expectedCall, putKey, key, instanceInfoStr, &isEmpty](
                                                               const std::shared_ptr<messages::ScheduleRequest> &) {
                                               auto status = metaStoreAccessor_->Put(
                                                   GenInstanceKey(putKey, key, key).Get(), instanceInfoStr);
                                               EXPECT_AWAIT_READY(status);
                                               expectedCall->SetValue(true);
                                               isEmpty = false;
                                           }),
                                           testing::Return(Status(StatusCode::SUCCESS))));
        YRLOG_INFO("Start update {}", content);
        GenSystemFunctionConfigFile(content);
        UpdateConfigHandler();
        EXPECT_AWAIT_READY_FOR(expectedCall->GetFuture(), 20000);
    };

    const std::string content2 =
        R"({"0-system-faascontroller":{"version":"v2","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v2\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";
    updateConfigTestHandler(content2, funcKey, funcKey2);

    const std::string memoryDiff =
        R"({"0-system-faascontroller":{"version":"v2","memory":600,"cpu":500,"createOptions":{"concurrentNum":"10","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v2\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";
    updateConfigTestHandler(memoryDiff, funcKey2, funcKey2);

    const std::string cpuDiff =
        R"({"0-system-faascontroller":{"version":"v2","memory":600,"cpu":600,"createOptions":{"concurrentNum":"10","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v2\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";
    updateConfigTestHandler(cpuDiff, funcKey2, funcKey2);

    const std::string createOptDiff =
        R"({"0-system-faascontroller":{"version":"v2","memory":600,"cpu":600,"createOptions":{"concurrentNum":"11","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v2\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";
    updateConfigTestHandler(createOptDiff, funcKey2, funcKey2);

    const std::string createOptDiff2 =
        R"({"0-system-faascontroller":{"version":"v2","memory":600,"cpu":600,"createOptions":{"DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v2\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";
    updateConfigTestHandler(createOptDiff2, funcKey2, funcKey2);

    const std::string argsDiff =
        R"({"0-system-faascontroller":{"version":"v2","memory":600,"cpu":600,"createOptions":{"DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v2\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":3,"xxx":{"xxx":1000}}}})";
    updateConfigTestHandler(argsDiff, funcKey2, funcKey2);

    litebus::Terminate(mockLocal->GetAID());
    litebus::Await(mockLocal->GetAID());
}

TEST_F(BootstrapActorTest, UpdateConfigHandlerAtStartTest)
{
    bootstrapActor_->waitKillInstanceMs_ = 100;
    bootstrapActor_->waitStartInstanceMs_ = 100;
    bootstrapActor_->waitUpdateConfigMapMs_ = 100;

    auto mockLocal = std::make_shared<MockLocalActor>();
    litebus::Spawn(mockLocal);

    auto funcKey = "0/0-system-faascontroller/v2";
    auto key = "0-system-faascontroller-0";
    auto instanceInfoStr =
        R"({"instanceID":"0-system-faascontroller-0","requestID":"0-system-faascontroller-0","runtimeID":"runtime-ca040000-0000-4000-80df-0a1e4dc86433","runtimeAddress":"10.42.2.214:21006","functionAgentID":"function_agent_10.42.2.214-58866","functionProxyID":"mock","function":"0/0-system-faascontroller/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":666}},"CPU":{"name":"CPU","scalar":{"value":666}}}},"scheduleOption":{"schedPolicyName":"monopoly","affinity":{"instanceAffinity":{}},"resourceSelector":{"resource.owner":"0-system-faascontroller-0"}},"createOptions":{"DELEGATE_ENCRYPT":"{\"metaEtcdPwd\":\"\"}","resource.owner":"system","NO_USE_DELEGATE_VOLUME_MOUNTS":"[{\"name\":\"sts-config\",\"mountPath\":\"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/apple/a\",\"subPath\":\"a\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/boy/b\",\"subPath\":\"b\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/cat/c\",\"subPath\":\"c\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker/dog/d\",\"subPath\":\"d\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker.ini\",\"subPath\":\"HMSCaaSYuanRongWorker.ini\"},{\"name\":\"sts-config\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/HMSCaaSYuanRongWorker.sts.p12\",\"subPath\":\"HMSCaaSYuanRongWorker.sts.p12\"},{\"name\":\"certs-volume\",\"mountPath\": \"/opt/huawei/certs/HMSClientCloudAccelerateService/HMSCaaSYuanRongWorker/\"}]","concurrentNum":"10"},"instanceStatus":{"code":3,"msg":"running"},"storageType":"local","scheduleTimes":1,"deployTimes":1,"args":[{"value":"eyJmYWFzZnJvbnRlbmRDb25maWciOnsiYXV0aGVudGljYXRpb25FbmFibGUiOmZhbHNlLCJidXNpbmVzc1R5cGUiOjEsImNsdXN0ZXJJRCI6ImNsdXN0ZXIwMDEiLCJjcHUiOjEyMDAsImRhdGFTeXN0ZW1Db25maWciOnsiZXhlY3V0ZVRUTFNlYyI6MTgwMCwiZXhlY3V0ZVdyaXRlTW9kZSI6Ik5vbmVMMkNhY2hlIiwidGltZW91dE1zIjo2MDAwMCwidXBsb2FkVFRMU2VjIjo4NjQwMCwidXBsb2FkV3JpdGVNb2RlIjoiTm9uZUwyQ2FjaGUifSwiZnVuY3Rpb25DYXBhYmlsaXR5IjoxLCJodHRwIjp7Im1heFJlcXVlc3RCb2R5U2l6ZSI6NiwicmVzcHRpbWVvdXQiOjUsIndvcmtlckluc3RhbmNlUmVhZFRpbWVPdXQiOjV9LCJtZW1vcnkiOjQwOTYsInNsYVF1b3RhIjoxMDAwLCJ0cmFmZmljTGltaXREaXNhYmxlIjp0cnVlLCJ1cnBjQ29uZmlnIjp7ImVuYWJsZWQiOmZhbHNlLCJwb2xsaW5nTnVtIjowLCJwb29sU2l6ZSI6MjAwLCJwb3J0IjoxOTk5Niwid29ya2VyTnVtIjoxMH19LCJmYWFzc2NoZWR1bGVyQ29uZmlnIjp7ImJ1cnN0U2NhbGVOdW0iOjEwMDAsImNwdSI6MTAwMCwiZG9ja2VyUm9vdFBhdGgiOiIvaG9tZS9kaXNrL2RvY2tlciIsImxlYXNlU3BhbiI6MTAwMCwibWVtb3J5Ijo0MDk2LCJzY2FsZURvd25UaW1lIjo2MDAwMCwic2xhUXVvdGEiOjEwMDB9LCJmcm9udGVuZEluc3RhbmNlTnVtIjoxLCJtZXRhRXRjZCI6eyJwYXNzd29yZCI6IiIsInNlcnZlcnMiOlsiNy4yMTguMjEuMjQ6MzIzODAiXSwic3NsRW5hYmxlIjpmYWxzZSwidXNlciI6IiJ9LCJyYXdTdHNDb25maWciOnsic2VydmVyQ29uZmlnIjp7ImRvbWFpbiI6IiR7U1RTX0RPTUFJTl9TRVJWRVJ9IiwicGF0aCI6Ii9vcHQvaHVhd2VpL2NlcnRzL0hNU0NsaWVudENsb3VkQWNjZWxlcmF0ZVNlcnZpY2UvSE1TQ2FhU1l1YW5Sb25nV29ya2VyL0hNU0NhYXNZdWFuUm9uZ1dvcmtlci5pbmkifSwic3RzRW5hYmxlIjpmYWxzZX0sInJvdXRlckV0Y2QiOnsicGFzc3dvcmQiOiIiLCJzZXJ2ZXJzIjpbIjcuMjE4LjIxLjI0OjMyMzc5Il0sInNzbEVuYWJsZSI6ZmFsc2UsInVzZXIiOiIifSwic2NoZWR1bGVySW5zdGFuY2VOdW0iOjEsInRsc0NvbmZpZyI6eyJjYUNvbnRlbnQiOiIke0NBX0NPTlRFTlR9IiwiY2VydENvbnRlbnQiOiIke0NFUlRfQ09OVEVOVH0iLCJrZXlDb250ZW50IjoiJHtLRVlfQ09OVEVOVH0ifX0="}],"version":"3","dataSystemHost":"10.244.158.229"})";

    const std::string content1 =
        R"({"0-system-faascontroller":{"version":"v1","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v1\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";
    const std::string content2 =
        R"({"0-system-faascontroller":{"version":"v2","memory":600,"cpu":600,"createOptions":{"concurrentNum":"10","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v2\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}}})";
    const std::string jsonStr1 =
        R"({"version":"v1","memory":500,"cpu":500,"createOptions":{"concurrentNum":"10","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v1\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}})";
    const std::string jsonStr2 =
        R"({"version":"v2","memory":600,"cpu":600,"createOptions":{"concurrentNum":"10","DELEGATE_RUNTIME_MANAGER":"{\"image\":\"v2\"}"},"instanceNum":1,"schedulingOps": {"extension": {"schedule_policy": "monopoly"}},"args":{"xxx":2,"xxx":{"xxx":1000}}})";

    auto status = metaStoreAccessor_->Put(GenInstanceKey(funcKey, key, key).Get(), instanceInfoStr);
    EXPECT_AWAIT_READY(status);
    status = metaStoreAccessor_->Put("/faas/system-function/config/0-system-faascontroller",
                            jsonStr2);  // etcd config version == 2
    EXPECT_AWAIT_READY(status);
    GenSystemFunctionPayloadFiles(payloadContent);
    GenSystemFunctionMetaFiles(metaContent);
    GenSystemFunctionConfigFile(content1); // local file version == v1

    litebus::Future<litebus::Option<std::string>> address;
    address.SetValue(litebus::Option<std::string>(localAddress_));
    EXPECT_CALL(*globalSched_, GetLocalAddress).WillRepeatedly(testing::Return(address));

    EXPECT_CALL(*mockLocal, ForwardCustomSignalRequest)
        .WillRepeatedly(testing::Invoke([aid(mockLocal->GetAID())](const litebus::AID &from, std::string &&,
                                                             std::string &&msg) {
            internal::ForwardKillRequest forwardKillRequest;
            if (msg.empty() || !forwardKillRequest.ParseFromString(msg)) {
                YRLOG_WARN("Failed to parse requestID from forwardKillRequest.");
                return;
            }
            litebus::Async(aid, &MockLocalActor::SendForwardCustomSignalResponse, from, forwardKillRequest.requestid());
        }));

    litebus::Promise<bool> promise;
    EXPECT_CALL(*globalSched_, Schedule)
        .WillOnce(testing::Return(Status(StatusCode::SUCCESS)))
        .WillRepeatedly(testing::DoAll(
            testing::Invoke(
                [funcKey, key, instanceInfoStr, jsonStr1, promise](const std::shared_ptr<messages::ScheduleRequest> &) {
                    auto status = metaStoreAccessor_->Put(GenInstanceKey(funcKey, key, key).Get(), instanceInfoStr);
                    EXPECT_AWAIT_READY(status);
                    status = metaStoreAccessor_->Put("/faas/system-function/config/0-system-faascontroller", jsonStr1);
                    EXPECT_AWAIT_READY(status);
                    YRLOG_INFO("Success schedule {}", funcKey);
                    promise.SetValue(true);
                }),
            testing::Return(Status(StatusCode::SUCCESS))));

    litebus::Promise<instance_manager::InstanceKeyInfoPair> p;
    p.SetValue({ "", nullptr });

    auto instance = std::make_shared<resource_view::InstanceInfo>();
    instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    litebus::Promise<instance_manager::InstanceKeyInfoPair> promise2;
    promise2.SetValue({ "key", instance });
    EXPECT_CALL(*mockInstanceMgr_, GetInstanceInfoByInstanceID)
        .WillOnce(testing::Return(p.GetFuture()))
        .WillRepeatedly(testing::Return(promise2.GetFuture()));

    LoadBootstrapConfig("");
    ASSERT_AWAIT_READY(promise.GetFuture());
    litebus::Terminate(mockLocal->GetAID());
    litebus::Await(mockLocal->GetAID());
}

// SlaveBusiness test cases
TEST_F(BootstrapActorTest, SlaveBusinessTest)  // NOLINT
{
    auto member = std::make_shared<BootstrapActor::Member>();
    auto bootstrapActor = std::make_shared<BootstrapActor>(nullptr, nullptr, 0, "");
    auto slaveBusiness = std::make_shared<BootstrapActor::SlaveBusiness>(bootstrapActor, member);
    slaveBusiness->SystemFunctionKeepAlive();
    slaveBusiness->KillSystemFuncInstances();
    slaveBusiness->ScaleByFunctionName("0-system-faasfrontend", 2, {});
}

}  // namespace functionsystem::system_function_loader::test
