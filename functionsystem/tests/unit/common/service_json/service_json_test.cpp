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

#include <gtest/gtest.h>

#include "common/service_json/service_json.h"
#include "common/service_json/service_metadata.h"
#include "common/service_json/service_handler.h"

using namespace functionsystem::service_json;

namespace functionsystem::test {
class ServiceJsonTest : public ::testing::Test {};
/*
TEST_F(ServiceJsonTest, ParseCodePathTest)
{
    // absulote code path
    std::string codePath = "/home/";
    std::string yamlDir = "/home/";
    std::string yamlPath = yamlDir + "xx.yaml";

    auto ret = ParseCodePath(codePath, yamlPath);
    EXPECT_EQ(ret, codePath);

    // relative code path
    codePath = "relative/";

    ret = ParseCodePath(codePath, yamlPath);
    EXPECT_EQ(ret, litebus::os::Join(yamlDir, codePath));
}

TEST_F(ServiceJsonTest, ExtendedTimeoutTest)
{
    YrLibBuilder yrLibBuilder("test");
    FunctionConfig functionConfig;
    EXPECT_TRUE(yrLibBuilder.ExtendedTimeout(functionConfig).empty());
    EXPECT_EQ(yrLibBuilder.Handler(), "fusion_computation_handler.fusion_computation_handler");

    std::string ret = yrLibBuilder.GetDefaultHandler("", "yrlib_handler.call");
    EXPECT_EQ(ret, "yrlib_handler.call");

    std::string ret1 = yrLibBuilder.GetDefaultHandler("yrlib_handler.init", "yrlib_handler.call");
    EXPECT_EQ(ret1, "yrlib_handler.init");

    ServiceInfo serviceInfo;

    auto meta = functionsystem::service_json::BuildFunctionMeta(serviceInfo, functionConfig, "functiontest", "/temp");
}

TEST_F(ServiceJsonTest, CheckNameTest)
{
    bool retEmpty = CheckName("", ".*", 0, 100);
    EXPECT_FALSE(retEmpty);

    bool retNameNotMatch = CheckName("1", "0", 0, 100);
    EXPECT_FALSE(retNameNotMatch);

    bool retWrongSize = CheckName("1", "1", 1, 0);
    EXPECT_FALSE(retWrongSize);
}

TEST_F(ServiceJsonTest, CheckServiceNameTest)
{
    bool retEmpty = CheckServiceName("");
    EXPECT_FALSE(retEmpty);

    bool ret = CheckServiceName("11");
    EXPECT_TRUE(ret);
}

TEST_F(ServiceJsonTest, CheckKindTest)
{
    bool retEmpty = CheckKind("");
    EXPECT_FALSE(retEmpty);

    bool ret = CheckKind("faas");
    EXPECT_TRUE(ret);
}

TEST_F(ServiceJsonTest, CheckFunctionNameTest)
{
    bool retEmpty = CheckFunctionName("");
    EXPECT_FALSE(retEmpty);

    bool ret = CheckFunctionName("faas");
    EXPECT_TRUE(ret);
}

TEST_F(ServiceJsonTest, CheckRuntimeTest)
{
    bool retEmpty = CheckRuntime("");
    EXPECT_FALSE(retEmpty);

    bool ret = CheckRuntime("cpp11");
    EXPECT_TRUE(ret);
}

TEST_F(ServiceJsonTest, CheckCPUAndMemorySize)
{
    bool retCpuFailed = CheckCPUAndMemorySize(0, 500);
    EXPECT_TRUE(retCpuFailed);

    retCpuFailed = CheckCPUAndMemorySize(-1, 500);
    EXPECT_FALSE(retCpuFailed);

    bool retMemFailed = CheckCPUAndMemorySize(500, 16001);
    EXPECT_FALSE(retMemFailed);

    bool ret = CheckCPUAndMemorySize(600, 600);
    EXPECT_TRUE(ret);
}

TEST_F(ServiceJsonTest, CheckEnvTest)
{
    std::unordered_map<std::string, std::string> envs = { { "FAAS_FUNCTION_NAME", "" } };
    bool ret = CheckEnv(envs);
    EXPECT_FALSE(ret);

    std::unordered_map<std::string, std::string> envs1 = { { "test", "" } };
    bool ret1 = CheckEnv(envs1);
    EXPECT_TRUE(ret1);
    std::string longStr;
    for (int i = 0; i < functionsystem::service_json::ENV_LENGTH_LIMIT; i++) {
        longStr += "a";
    }
    std::unordered_map<std::string, std::string> envs2 = { { "USER_DEFINE", longStr } };
    bool ret2 = CheckEnv(envs2);
    EXPECT_FALSE(ret2);
}

TEST_F(ServiceJsonTest, CheckLayerNameTest)
{
    bool ret = CheckLayerName("");
    EXPECT_FALSE(ret);


    bool ret1 = CheckLayerName("test_+&^");
    EXPECT_FALSE(ret1);

    bool ret2 = CheckLayerName("test");
    EXPECT_TRUE(ret2);
}

TEST_F(ServiceJsonTest, ParseAndCheckLayerVersionTest)
{
    bool ret = ParseAndCheckLayerVersion("");
    EXPECT_FALSE(ret);


    bool ret1 = ParseAndCheckLayerVersion("-1");
    EXPECT_FALSE(ret1);

    bool ret2 = ParseAndCheckLayerVersion("1");
    EXPECT_TRUE(ret2);
}

TEST_F(ServiceJsonTest, CheckFunctionRefLayerTest)
{
    bool ret = CheckFunctionRefLayer("");
    EXPECT_FALSE(ret);


    bool ret1 = CheckFunctionRefLayer(":-1");
    EXPECT_FALSE(ret1);

    bool ret2 = CheckFunctionRefLayer("test:1");
    EXPECT_TRUE(ret2);
}

TEST_F(ServiceJsonTest, CheckFunctionLayersTest)
{
    std::vector<std::string> layers;
    bool ret = CheckFunctionLayers(layers);
    EXPECT_TRUE(ret);
    for (int i = 0; i < 6; i++) {
        layers.emplace_back("1");
    }

    bool ret1 = CheckFunctionLayers(layers);
    EXPECT_FALSE(ret1);

    std::vector<std::string> layers1 = { "test:1" };
    bool ret2 = CheckFunctionLayers(layers1);
    EXPECT_TRUE(ret2);
}

TEST_F(ServiceJsonTest, CheckMinMaxInstanceTest)
{
    bool ret1 = CheckMinInstance(-1);
    EXPECT_FALSE(ret1);

    bool ret2 = CheckMinInstance(1);
    EXPECT_TRUE(ret2);

    bool ret3 = CheckMaxInstance(0);
    EXPECT_FALSE(ret3);

    bool ret4 = CheckMaxInstance(1001);
    EXPECT_FALSE(ret4);

    bool ret5 = CheckMinInstance(100);
    EXPECT_TRUE(ret5);

    bool ret6 = IsMinInstanceLargeThanMaxInstance(2, 1);
    EXPECT_FALSE(ret6);

    bool ret7 = IsMinInstanceLargeThanMaxInstance(1, 2);
    EXPECT_TRUE(ret7);

    bool ret8 = CheckConcurrentNum(0);
    EXPECT_FALSE(ret8);

    bool ret9 = CheckConcurrentNum(101);
    EXPECT_FALSE(ret9);

    bool ret10 = CheckConcurrentNum(11);
    EXPECT_TRUE(ret10);
}

TEST_F(ServiceJsonTest, CheckWorkerConfigTest)
{
    FunctionConfig config{ 1, 1, 100, "handler", "description" };
    bool ret1 = CheckWorkerConfig(config);
    EXPECT_TRUE(ret1);
}

TEST_F(ServiceJsonTest, PackHookHandlerTest)
{
    FunctionHookHandlerConfig config{ "init", "call", "checkpoint", "recover", "shutdown", "signal" };
    auto result = PackHookHandler(config);
    EXPECT_EQ(result["init"], "init");
}

TEST_F(ServiceJsonTest, CheckHookHandlerRegularizationTest)
{
    std::string longStr;
    for (int i = 0; i < 300; i++) {
        longStr += "a";
    }
    bool ret1 = CheckHookHandlerRegularization(longStr, "cpp11");
    EXPECT_FALSE(ret1);

    bool ret2 = CheckHookHandlerRegularization(longStr, "");
    EXPECT_FALSE(ret2);

    bool ret3 = CheckHookHandlerRegularization("test", "java1.8");
    EXPECT_TRUE(ret3);
}

TEST_F(ServiceJsonTest, CheckHookHandlerTest)
{
    FunctionHookHandlerConfig config{ "init", "call", "checkpoint", "recover", "shutdown", "signal" };
    auto result = CheckHookHandler(config, "cpp11");
    EXPECT_FALSE(result);

    FunctionHookHandlerConfig config2{ "init",     "call",  "checkpointrecoversame", "checkpointrecoversame",
        "shutdown", "signal" };

    auto result2 = CheckHookHandler(config2, "cpp11");
    EXPECT_FALSE(result2);
}

TEST_F(ServiceJsonTest, CheckFunctionConfigTest)
{
    FunctionConfig config{ 1, 1, 100, "handler", "description" };
    bool ret1 = CheckFunctionConfig(config);
    EXPECT_FALSE(ret1);
}

TEST_F(ServiceJsonTest, CheckFunctionTest)
{
    FunctionConfig config{ 1, 1, 100, "handler", "description" };
    bool ret1 = CheckFunction("function", config);
    EXPECT_FALSE(ret1);
}

TEST_F(ServiceJsonTest, GetFuncMetaFromServiceYamlTest)
{
    auto functionMeta = GetFuncMetaFromServiceYaml("/faketemp", "/temp");
    EXPECT_FALSE(functionMeta.IsSome());
    functionMeta = GetFuncMetaFromServiceYaml("/tmp/abc.yaml", "/tmp/libyaml_tool.so");
    EXPECT_FALSE(functionMeta.IsSome());
    functionMeta = GetFuncMetaFromServiceYaml("/tmp/services.yaml", "..../tmp/lib.so");
    EXPECT_FALSE(functionMeta.IsSome());
    functionMeta = GetFuncMetaFromServiceYaml("/tmp/services.yaml", "/tmp/libyaml_tool_xxxx.so");
    EXPECT_FALSE(functionMeta.IsSome());
    functionMeta = GetFuncMetaFromServiceYaml("/tmp/services.yaml", "/tmp/libyaml_tool.so");
    EXPECT_EQ(functionMeta.Get().size(), static_cast<uint32_t>(4)) << "Actual: " << functionMeta.Get().size();
}

TEST_F(ServiceJsonTest, CheckServiceInfosTest)
{
    service_json::ServiceInfo serviceInfo;
    serviceInfo.service = "service";
    serviceInfo.kind = "kind";
    serviceInfo.description = "description";
    FunctionConfig config{ 1, 1, 100, "handler", "description" };
    (void)serviceInfo.functions.emplace("funckey", config);

    std::vector<service_json::ServiceInfo> vec;
    vec.push_back(serviceInfo);

    EXPECT_FALSE(CheckServiceInfos(vec));
}

TEST_F(ServiceJsonTest, ParseFunctionHookHandlerConfigTest)
{
    FunctionHookHandlerConfig config;

    nlohmann::json configJson;
    configJson["initHandler"] = "initHandler";
    configJson["callHandler"] = "callHandler";
    configJson["checkpointHandler"] = "checkpointHandler";
    configJson["recoverHandler"] = "recoverHandler";
    configJson["shutdownHandler"] = "shutdownHandler";
    configJson["signalHandler"] = "signalHandler";
    configJson["healthHandler"] = "healthHandler";

    ParseFunctionHookHandlerConfig(config, configJson);

    EXPECT_EQ(config.healthHandler, "healthHandler");
    EXPECT_EQ(config.signalHandler, "signalHandler");
    EXPECT_EQ(config.callHandler, "callHandler");
    EXPECT_EQ(config.checkpointHandler, "checkpointHandler");
    EXPECT_EQ(config.recoverHandler, "recoverHandler");
    EXPECT_EQ(config.shutdownHandler, "shutdownHandler");
    EXPECT_EQ(config.initHandler, "initHandler");
}

TEST_F(ServiceJsonTest, ParseCodeMetaTest)
{
    std::vector<std::string> layers = { "numpy", "pandas" };
    FunctionConfig functionConfig;

    nlohmann::json configJson = { { "layers", layers }, { "storageType", "s3" }, { "codePath", "/temp" } };

    ParseCodeMeta(functionConfig, configJson);
    EXPECT_EQ(functionConfig.storageType, "s3");
    EXPECT_EQ(functionConfig.codePath, "/temp");
}

TEST_F(ServiceJsonTest, ParseEnvMetaTest)
{
    std::unordered_map<std::string, std::string> environment = {
        { "tenantID", "id" },
        { "version", "version" },
    };
    FunctionConfig functionConfig;
    nlohmann::json configJson = { { "encryptedEnvStr", "1222324b3jdjghdfghjert90965" },
                                  { "environment", environment } };

    ParseEnvMeta(functionConfig, configJson);
    EXPECT_EQ(functionConfig.encryptedEnvStr, "1222324b3jdjghdfghjert90965");
    EXPECT_EQ(functionConfig.environment["tenantID"], "id");
}

TEST_F(ServiceJsonTest, ParseInstMetaTest)
{
    FunctionConfig functionConfig;
    nlohmann::json configJson;
    configJson["minInstance"] = "2";
    configJson["maxInstance"] = "2";
    configJson["concurrentNum"] = "100";
    configJson["cacheInstance"] = "100";

    ParseInstMeta(functionConfig, configJson);
    EXPECT_EQ(functionConfig.minInstance, 2);
    EXPECT_EQ(functionConfig.maxInstance, 2);
    EXPECT_EQ(functionConfig.concurrentNum, 100);
    EXPECT_EQ(functionConfig.cacheInstance, 100);
}

TEST_F(ServiceJsonTest, ParseResTest)
{
    std::unordered_map<std::string, std::string> customResources = {
        { "gpu", "10" },
        { "npu", "10" },
    };
    FunctionConfig functionConfig;
    nlohmann::json configJson;
    configJson["cpu"] = "1000";
    configJson["memory"] = "1500";
    configJson["customResources"] = customResources;
    ParseRes(functionConfig, configJson);

    EXPECT_EQ(functionConfig.cpu, 1000);
    EXPECT_EQ(functionConfig.memory, 1500);
}

TEST_F(ServiceJsonTest, ParsefunctionTest)
{
    FunctionConfig functionConfig;
    nlohmann::json configJson;
    configJson["minInstance"] = "2";
    configJson["maxInstance"] = "2";
    configJson["concurrentNum"] = "100";
    configJson["cacheInstance"] = "100";
    configJson["handler"] = "handler";
    configJson["initializer"] = "initializer";
    configJson["initializerTimeout"] = "0";
    configJson["description"] = "description";
    configJson["runtime"] = "cpp11";
    configJson["timeout"] = "900";
    configJson["preStopHandler"] = "prestop";
    configJson["preStopTimeout"] = "3";

    Parsefunction(functionConfig, configJson);
    EXPECT_EQ(functionConfig.minInstance, 2);
    EXPECT_EQ(functionConfig.maxInstance, 2);
    EXPECT_EQ(functionConfig.initializer, "initializer");
    EXPECT_EQ(functionConfig.handler, "handler");
    EXPECT_EQ(functionConfig.prestop, "prestop");
    EXPECT_EQ(functionConfig.preStopTimeout, 3);
}

TEST_F(ServiceJsonTest, ParseDeviceInfo)
{
    DeviceMetaData deviceMetaData;
    nlohmann::json device, h;
    h["model"] = "ascend";
    h["hbm"] = "100";
    h["count"] = "1";
    h["stream"] = "1";
    h["latency"] = "200";
    h["type"] = "NPU";
    device["device"] = h;

    ParseDeviceInfo(deviceMetaData, device);
    EXPECT_EQ(deviceMetaData.model, "ascend");
    EXPECT_EQ(deviceMetaData.hbm, 100);
    EXPECT_EQ(deviceMetaData.count, static_cast<uint32_t>(1));
    EXPECT_EQ(deviceMetaData.stream, static_cast<long unsigned int>(1));
    EXPECT_EQ(deviceMetaData.latency, 200);
    EXPECT_EQ(deviceMetaData.type, "NPU");
}

TEST_F(ServiceJsonTest, ParseServiceInfoTest)
{
    std::vector<service_json::ServiceInfo> vec;
    FunctionConfig config{ 1, 1, 100, "handler", "description" };
    std::unordered_map<std::string, FunctionConfig> functions = { { "id", config } };
    nlohmann::json configJson;
    configJson["service"] = "service_test";
    configJson["kind"] = "kind_test";
    configJson["description"] = "description_test";

    nlohmann::json configJsonArray = nlohmann::json::array();
    configJsonArray.push_back(configJson);

    service_json::ServiceInfo serviceInfo;

    ParseServiceInfo(vec, configJsonArray);
    EXPECT_EQ(vec.size(), 1u);
    EXPECT_EQ(vec[0].service, "service_test");
    EXPECT_EQ(vec[0].kind, "kind_test");
    EXPECT_EQ(vec[0].description, "description_test");
}

TEST_F(ServiceJsonTest, NameMatchTest)
{
    auto result = NameMatch("test123", "test123");
    EXPECT_TRUE(result);

    auto result2 = NameMatch("test123", "test123567");
    EXPECT_FALSE(result2);
}*/
} // namespace functionsystem::test