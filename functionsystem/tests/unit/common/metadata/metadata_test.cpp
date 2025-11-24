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

#include <fstream>

#include <gtest/gtest.h>
#include <utils/os_utils.hpp>

#include "common/metadata/metadata.h"
namespace functionsystem::test {
using namespace ::testing;

class LoaderTest : public ::testing::Test {
public:
    static void GenFunctionMetaFile(const std::string &path, const std::string &content)
    {
        if (!litebus::os::ExistPath(path)) {
            litebus::os::Mkdir(path);
        }
        auto filePath = path + "/" + "fasscontroller.json";

        std::ofstream outfile;
        outfile.open(filePath.c_str());
        outfile << content << std::endl;
        outfile.close();
    }

    static void DeleteFunctionMetaFile(const std::string &path)
    {
        auto filePath = path + "/" + "fasscontroller.json";
        litebus::os::Rm(filePath);
    }
};

/**
 * Feature: LoadFunctionWithDeviceSuccess
 * Description: Load System Function
 * Steps:
 * Expectation:
 * cache contain function metadata information
 */
TEST_F(LoaderTest, LoadFunctionWithDeviceSuccess)
{
    const std::string content =
            R"({"funcMetaData":{"layers":[],"name":"faascontroller","description":"","version":"$latest","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:faascontroller","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:faascontroller:$latest","codeSize":22029378,"codeSha256":"1211a06","codeSha512":"1211a07","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"12345678901234561234567890123456","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":"","cryptoAlgorithm":"GCM"},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0},"device": {"name": "saxpy", "entrypoint": "saxpy.cubin", "model": "cuda", "hbm": 1000, "type": "GPU", "count": 8, "stream": 100, "latency": 120}}})";
    LoaderTest::GenFunctionMetaFile("/tmp/meta", content);
    std::unordered_map<std::string, FunctionMeta> map;
    LoadLocalFuncMeta(map, "/tmp/meta");
    auto funcMeta = map["12345678901234561234567890123456/faascontroller/$latest"];

    DeviceMetaData nf {
        .hbm = 1000,
        .latency = 120,
        .stream = 100,
        .count = 8,
        .model = "cuda",
        .type = "GPU",
    };

    // check NamedFunctions
    EXPECT_TRUE(funcMeta.extendedMetaData.deviceMetaData.model == nf.model);
    EXPECT_TRUE(funcMeta.extendedMetaData.deviceMetaData.hbm == nf.hbm);
    EXPECT_TRUE(funcMeta.extendedMetaData.deviceMetaData.count == nf.count);
    EXPECT_TRUE(funcMeta.extendedMetaData.deviceMetaData.type == nf.type);
    EXPECT_TRUE(funcMeta.extendedMetaData.deviceMetaData.latency == nf.latency);
    EXPECT_TRUE(funcMeta.extendedMetaData.deviceMetaData.stream == nf.stream);
    EXPECT_TRUE(funcMeta.funcMetaData.codeSha256 == "1211a06");
    EXPECT_TRUE(funcMeta.funcMetaData.codeSha512 == "1211a07");
    LoaderTest::DeleteFunctionMetaFile("/tmp/meta");
}

/**
 * Feature: LoadFunctionSuccess
 * Description: Load System Function
 * Steps:
 * Expectation:
 * cache contain function metadata information
 */
TEST_F(LoaderTest, LoadFunctionSuccess)
{
    const std::string content =
        R"({"funcMetaData":{"layers":[],"name":"faascontroller","description":"","version":"$latest","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:faascontroller","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:faascontroller:$latest","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"12345678901234561234567890123456","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0}}})";
    LoaderTest::GenFunctionMetaFile("/tmp/meta", content);
    std::unordered_map<std::string, FunctionMeta> map;
    LoadLocalFuncMeta(map, "/tmp/meta");
    auto funcMeta = map["12345678901234561234567890123456/faascontroller/$latest"];

    // check FuncMataData
    EXPECT_TRUE(funcMeta.funcMetaData.urn ==
        "sn:cn:yrk:12345678901234561234567890123456:function:faascontroller:$latest");
    EXPECT_TRUE(funcMeta.funcMetaData.runtime == "java1.8");
    EXPECT_TRUE(funcMeta.funcMetaData.entryFile == "fusion_computation_handler.fusion_computation_handler");
    EXPECT_TRUE(funcMeta.funcMetaData.handler.empty());
    EXPECT_TRUE(funcMeta.funcMetaData.codeSha256 == "1211a06");
    EXPECT_TRUE(funcMeta.funcMetaData.hookHandler["call"] == "com.actorTaskCallHandler");
    EXPECT_TRUE(funcMeta.funcMetaData.name == "faascontroller");
    EXPECT_TRUE(funcMeta.funcMetaData.version == "$latest");
    EXPECT_TRUE(funcMeta.funcMetaData.tenantId == "12345678901234561234567890123456");

    // check CodeMetaData
    EXPECT_TRUE(funcMeta.codeMetaData.storageType == "local");
    EXPECT_TRUE(funcMeta.codeMetaData.deployDir == "/home/sn/function");

    // check EnvMetaData
    EXPECT_TRUE(funcMeta.envMetaData.envKey == "1d34ef");
    EXPECT_TRUE(funcMeta.envMetaData.envInfo == "e819e3");
    EXPECT_TRUE(funcMeta.envMetaData.encryptedUserData.empty());

    // check ExtendedMetaData
    EXPECT_TRUE(funcMeta.extendedMetaData.instanceMetaData.maxInstance == 100);
    EXPECT_TRUE(funcMeta.extendedMetaData.instanceMetaData.minInstance == 0);
    EXPECT_TRUE(funcMeta.extendedMetaData.instanceMetaData.concurrentNum == 10);
    EXPECT_TRUE(funcMeta.extendedMetaData.instanceMetaData.cacheInstance == 0);

    LoaderTest::DeleteFunctionMetaFile("/tmp/meta");
}

/**
 * Feature: LoadFunctionFailedWhenMetadataInvalid
 * Description: Load System Function failed
 * Steps:
 * function metadata json format is invalid
 * Expectation:
 * cache does not contain function metadata information
 */
TEST_F(LoaderTest, LoadFunctionFailedWhenMetadataInvalid)
{
    const std::string content =
        R"({"funcMetaData":"layers":[],"name":"faascontroller","description":"","version":"$latest","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:faascontroller","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:faascontroller:$latest","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"12345678901234561234567890123456","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0}}})";
    LoaderTest::GenFunctionMetaFile("/tmp/meta", content);
    std::unordered_map<std::string, FunctionMeta> map;
    LoadLocalFuncMeta(map, "/tmp/meta");

    EXPECT_TRUE(map.find("12345678901234561234567890123456/faascontroller/$latest") == map.end());

    LoaderTest::DeleteFunctionMetaFile("/tmp/meta");
}

/**
 * Feature: LoadFunctionFailedWhenMetadataLackInformation
 * Description: Load System Function failed
 * Steps:
 * function metadata json lack urn
 * Expectation:
 * cache does not contain function metadata information
 */
TEST_F(LoaderTest, LoadFunctionFailedWhenMetadataLackInformation)
{
    const std::string content =
        R"({"funcMetaData":{"layers":[],"name":"","description":"","version":"$latest","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:faascontroller","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:faascontroller:$latest","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"12345678901234561234567890123456","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/function"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}, "extendedMetaData":{"instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0}}})";
    LoaderTest::GenFunctionMetaFile("/tmp/meta", content);
    std::unordered_map<std::string, FunctionMeta> map;
    LoadLocalFuncMeta(map, "/tmp/meta");

    EXPECT_TRUE(map.find("12345678901234561234567890123456/faascontroller/$latest") == map.end());

    LoaderTest::DeleteFunctionMetaFile("/tmp/meta");
}

TEST_F(LoaderTest, TransToInstanceInfoFromJson)
{
    std::string json =
        R"({"instanceID":"0123456789abcdef0","requestID":"0123456789abcdef","runtimeID":"test-runtime","restartPolicy":"auto-scaling"})";
    InstanceInfo instance;
    instance.set_instanceid("0123456789abcdef0");
    instance.set_requestid("0123456789abcdef");
    instance.set_runtimeid("test-runtime");
    instance.set_restartpolicy("auto-scaling");

    std::string json_str;
    bool reverse_res = TransToJsonFromInstanceInfo(json_str, instance);

    EXPECT_TRUE(reverse_res);
    EXPECT_EQ(json, json_str);
    InstanceInfo instance1;
    bool res = TransToInstanceInfoFromJson(instance1, json_str);
    EXPECT_TRUE(res);
    EXPECT_EQ(instance1.instanceid(), instance.instanceid());
}

TEST_F(LoaderTest, GetFuncMetaFromJson)
{
    std::string func_meta_json = R"({
      "funcMetaData": {
        "layers": [],
        "name": "0-system-faascontroller",
        "description": "",
        "functionUrn": "sn:cn:yrk:12345678901234561234567890123456:function:0-system-faascontroller",
        "reversedConcurrency": 0,
        "tags": null,
        "functionUpdateTime": "",
        "functionVersionUrn": "sn:cn:yrk:12345678901234561234567890123456:function:0-system-faascontroller:$latest",
        "codeSize": 5789050,
        "codeSha256": "9114f5795c215b6f8b8d506bd7502c3582b9425538c30d54ba8d722d2947156e",
        "handler": "",
        "runtime": "go1.13",
        "timeout": 900,
        "version": "$latest",
        "versionDescription": "$latest",
        "deadLetterConfig": "",
        "latestVersionUpdateTime": "",
        "publishTime": "",
        "businessId": "yrk",
        "tenantId": "12345678901234561234567890123456",
        "domain_id": "",
        "project_name": "",
        "revisionId": "20230203063332624",
        "created": "2023-02-03 06:33:32.626 UTC",
        "statefulFlag": false,
        "hookHandler": {
          "call": "faascontroller.CallHandler",
          "init": "faascontroller.InitHandler"
        }
      },
      "codeMetaData": {
        "storage_type": "local",
        "code_path": "/home/sn/function/package/faascontroller"
      },
      "envMetaData": {
        "envKey": "",
        "environment": "",
        "encrypted_user_data": ""
      },
      "resourceMetaData": {
        "cpu": 500,
        "memory": 500,
        "customResources": ""
      },
      "extendedMetaData": {
        "image_name": "",
        "role": {
          "xrole": "",
          "app_xrole": ""
        },
        "mount_config": {
          "mount_user": {
            "user_id": 0,
            "user_group_id": 0
          },
          "func_mounts": null
        },
        "strategy_config": {
          "concurrency": 0
        },
        "extend_config": "",
        "initializer": {
          "initializer_handler": "",
          "initializer_timeout": 0
        },
        "enterprise_project_id": "",
        "log_tank_service": {
          "logGroupId": "",
          "logStreamId": ""
        },
        "tracing_config": {
          "tracing_ak": "",
          "tracing_sk": "",
          "project_name": ""
        },
        "user_type": "",
        "instance_meta_data": {
          "maxInstance": 100,
          "minInstance": 0,
          "concurrentNum": 100,
          "cacheInstance": 0
        },
        "extended_handler": null,
        "extended_timeout": null
      }
    })";
    FunctionMeta functionMeta = GetFuncMetaFromJson(func_meta_json);
    EXPECT_EQ(functionMeta.funcMetaData.runtime, "go1.13");
    EXPECT_EQ(functionMeta.codeMetaData.deployDir, "/home/sn/function/package/faascontroller");
    EXPECT_TRUE(functionMeta.envMetaData.envInfo.empty());
    EXPECT_EQ(functionMeta.extendedMetaData.instanceMetaData.maxInstance, 100);
}

TEST_F(LoaderTest, GetNspFuncMetaFromJson)
{
    const std::string nsp_json = R"({
          "codeMetaData": {
            "storage_type": "nsp",
            "appId": "***",
            "bucketId": "bucket-test-*",
            "objectId": "object-test-***",
            "bucketUrl": "https://bucket-test-*.**.cn:**"
          }
          })";
    FunctionMeta nsp_function = GetFuncMetaFromJson(nsp_json);
    EXPECT_EQ(nsp_function.codeMetaData.storageType, "nsp");
    EXPECT_EQ(nsp_function.codeMetaData.bucketID, "bucket-test-*");
    EXPECT_EQ(nsp_function.codeMetaData.objectID, "object-test-***");
    EXPECT_EQ(nsp_function.codeMetaData.bucketUrl, "https://bucket-test-*.**.cn:**");
}

TEST_F(LoaderTest, GetFuncMounts)
{
    const std::string FUNC_MOUNTS = "func_mounts";
    const std::string FUNC_MOUNT_TYPE = "mount_type";
    const std::string FUNC_MOUNT_RESOURCE = "mount_resource";
    const std::string FUNC_MOUNT_SHARE_PATH = "mount_share_path";
    const std::string FUNC_MOUNT_LOCAL_MOUNT_PATH = "local_mount_path";
    const std::string FUNC_MOUNT_STATUS = "status";
    nlohmann::json str;
    str[FUNC_MOUNTS] = "test";
    str[FUNC_MOUNT_TYPE] = "test";
    str[FUNC_MOUNT_RESOURCE] = "test";
    str[FUNC_MOUNT_LOCAL_MOUNT_PATH] = "test";
    str[FUNC_MOUNT_STATUS] = "test";
    MountConfig mountConfig;
    functionsystem::GetFuncMounts(mountConfig, str);
    EXPECT_TRUE(true);
}

TEST_F(LoaderTest, GetEntryFileAndHandler)
{
    nlohmann::json str;
    std::string func_meta_json = R"({
          "funcMetaData": {
            "layers": [],
            "name": "0-system-faascontroller",
            "description": "",
            "functionUrn": "sn:cn:yrk:12345678901234561234567890123456:function:0-system-faascontroller",
            "reversedConcurrency": 0,
            "tags": null,
            "functionUpdateTime": "",
            "functionVersionUrn": "sn:cn:yrk:12345678901234561234567890123456:function:0-system-faascontroller:$latest",
            "codeSize": 5789050,
            "codeSha256": "9114f5795c215b6f8b8d506bd7502c3582b9425538c30d54ba8d722d2947156e",
            "handler": "",
            "runtime": "go1.13",
            "timeout": 900,
            "version": "$latest",
            "versionDescription": "$latest",
            "deadLetterConfig": "",
            "latestVersionUpdateTime": "",
            "publishTime": "",
            "businessId": "yrk",
            "tenantId": "12345678901234561234567890123456",
            "domain_id": "",
            "project_name": "",
            "revisionId": "20230203063332624",
            "created": "2023-02-03 06:33:32.626 UTC",
            "statefulFlag": false,
            "hookHandler": {
              "call": "faascontroller.CallHandler",
              "init": "faascontroller.InitHandler"
            }
          },
          "codeMetaData": {
            "storage_type": "local",
            "code_path": "/home/sn/function/package/faascontroller"
          },
          "envMetaData": {
            "envKey": "",
            "environment": "",
            "encrypted_user_data": ""
          },
          "resourceMetaData": {
            "cpu": 500,
            "memory": 500,
            "customResources": ""
          },
          "extendedMetaData": {
            "image_name": "",
            "role": {
              "xrole": "",
              "app_xrole": ""
            },
            "mount_config": {
              "mount_user": {
                "user_id": 0,
                "user_group_id": 0
              },
              "func_mounts": null
            },
            "strategy_config": {
              "concurrency": 0
            },
            "extend_config": "",
            "initializer": {
              "initializer_handler": "",
              "initializer_timeout": 0
            },
            "enterprise_project_id": "",
            "log_tank_service": {
              "logGroupId": "",
              "logStreamId": ""
            },
            "tracing_config": {
              "tracing_ak": "",
              "tracing_sk": "",
              "project_name": ""
            },
            "user_type": "",
            "instance_meta_data": {
              "maxInstance": 100,
              "minInstance": 0,
              "concurrentNum": 100,
              "cacheInstance": 0
            },
            "extended_handler": null,
            "extended_timeout": null
          }
        })";
    FunctionMeta functionMeta1 = GetFuncMetaFromJson(func_meta_json);
    GetEntryFileAndHandler(functionMeta1, str);

    // Test branches in GetFuncMetaFromJson
    func_meta_json = R"({
          "funcMetaData": {}
          })";
    FunctionMeta functionMeta = GetFuncMetaFromJson(func_meta_json);

    func_meta_json = R"({
          "funcMetaData": {
            "handler": "a::b",
            "runtime": "java1.8"
          }
          })";
    functionMeta = GetFuncMetaFromJson(func_meta_json);

    func_meta_json = R"({
          "funcMetaData": {
            "handler": "a.b",
            "runtime": "python"
          }
          })";
    functionMeta = GetFuncMetaFromJson(func_meta_json);

    func_meta_json = R"({
          "funcMetaData": {
            "handler": "a",
            "runtime": "python"
          }
          })";
    functionMeta = GetFuncMetaFromJson(func_meta_json);

    func_meta_json = R"({
          "funcMetaData": {
            "handler": "a",
            "runtime": "cpp"
          },
          "codeMetaData": {
            "storage_type": "local",
            "code_path": "/home/sn/function/package/faascontroller"
          },
          "extendedMetaData": {
            "instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0},
            "mount_config": {
              "mount_user": {},
              "func_mounts": null
            }
          }
          })";
    functionMeta = GetFuncMetaFromJson(func_meta_json);

    func_meta_json = R"({
      "funcMetaData": {
        "handler": "a",
        "runtime": "cpp"
      },
      "codeMetaData": {
        "storage_type": "local",
        "code_path": "/home/sn/function/package/faascontroller"
      },
      "extendedMetaData": {
        "instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0},
        "mount_config": {
          "mount_user": {
            "user_id": 0,
            "user_group_id": 0
          },
          "func_mounts": null
        }
      }
      })";
    functionMeta = GetFuncMetaFromJson(func_meta_json);

    // Test branches in GetFuncMounts
    func_meta_json = R"({
      "funcMetaData": {
        "handler": "a",
        "runtime": "cpp"
      },
      "codeMetaData": {
        "storage_type": "local",
        "code_path": "/home/sn/function/package/faascontroller"
      },
      "extendedMetaData": {
        "instance_meta_data":{"maxInstance":100, "minInstance":0, "concurrentNum":10, "cacheInstance":0},
        "mount_config": {
          "mount_user": {
            "user_id": 0,
            "user_group_id": 0
          },
          "func_mounts": {
            "mount_type": {"mount_type": "test"},
            "mount_resource": {"mount_resource": "test"},
            "mount_share_path": {"mount_share_path": "test"},
            "local_mount_path": {"local_mount_path": "test"},
            "status": {"status": "test"}
          }
        }
      }
      })";
    functionMeta = GetFuncMetaFromJson(func_meta_json);
    EXPECT_TRUE(true);
}

TEST_F(LoaderTest, ParseDelegateDownloadInfo)
{
    nlohmann::json parser1 = {
        {"objectId", "objectIdValue"},
        {"appId", "appIdValue"},
        {"bucketId", "bucketIdValue"},
        {"hostName", "hostNameValue"},
        {"securityToken", "securityTokenValue"},
        {"temporaryAccessKey", "temporaryAccessKeyValue"},
        {"temporarySecretKey", "temporarySecretKeyValue"},
        {"storage_type", "local"},
        {"code_path", "/home/sn"}
    };

    Layer layer1 = ParseDelegateDownloadInfo(parser1);
    EXPECT_EQ(layer1.appID, "appIdValue");
    EXPECT_EQ(layer1.bucketID, "bucketIdValue");
    EXPECT_EQ(layer1.hostName, "hostNameValue");
    EXPECT_EQ(layer1.securityToken, "securityTokenValue");
    EXPECT_EQ(layer1.temporaryAccessKey, "temporaryAccessKeyValue");
    EXPECT_EQ(layer1.temporarySecretKey, "temporarySecretKeyValue");
    EXPECT_EQ(layer1.storageType, "local");
    EXPECT_EQ(layer1.codePath, "/home/sn");

    nlohmann::json parser2 = {
        {"storage_type", ""},
    };

    Layer layer2 = ParseDelegateDownloadInfo(parser2);
    EXPECT_TRUE(layer2.appID.empty());
    EXPECT_TRUE(layer2.bucketID.empty());
    EXPECT_TRUE(layer2.hostName.empty());
    EXPECT_TRUE(layer2.securityToken.empty());
    EXPECT_TRUE(layer2.temporaryAccessKey.empty());
    EXPECT_TRUE(layer2.temporarySecretKey.empty());
    EXPECT_EQ(layer2.storageType, "s3");
    EXPECT_TRUE(layer2.codePath.empty());

    nlohmann::json parser3 = {
    };

    Layer layer3 = ParseDelegateDownloadInfo(parser3);
    EXPECT_TRUE(layer3.appID.empty());
    EXPECT_TRUE(layer3.bucketID.empty());
    EXPECT_TRUE(layer3.hostName.empty());
    EXPECT_TRUE(layer3.securityToken.empty());
    EXPECT_TRUE(layer3.temporaryAccessKey.empty());
    EXPECT_TRUE(layer3.temporarySecretKey.empty());
    EXPECT_EQ(layer3.storageType, "s3");
    EXPECT_TRUE(layer3.codePath.empty());

    nlohmann::json parser4 = {
        {"storage_type", "working_dir"},
        {"code_path", "file:///home/xxx/xxy.zip"}
    };

    Layer layer4 = ParseDelegateDownloadInfo(parser4);
    EXPECT_TRUE(layer4.appID.empty());
    EXPECT_TRUE(layer4.bucketID.empty());
    EXPECT_TRUE(layer4.hostName.empty());
    EXPECT_TRUE(layer4.securityToken.empty());
    EXPECT_TRUE(layer4.temporaryAccessKey.empty());
    EXPECT_TRUE(layer4.temporarySecretKey.empty());
    EXPECT_EQ(layer4.storageType, "working_dir");
    EXPECT_EQ(layer4.codePath, "file:///home/xxx/xxy.zip");
}

TEST_F(LoaderTest, ParseDelegateDownloadInfoByStr)
{
    nlohmann::json parser1 = {
        {"objectId", "objectIdValue"},
        {"appId", "appIdValue"},
        {"bucketId", "bucketIdValue"},
        {"hostName", "hostNameValue"},
        {"securityToken", "securityTokenValue"},
        {"temporaryAccessKey", "temporaryAccessKeyValue"},
        {"temporarySecretKey", "temporarySecretKeyValue"},
        {"storage_type", ""},
        {"code_path", ""}
    };
    std::cout << parser1.dump() << std::endl;
    auto layer1 = ParseDelegateDownloadInfoByStr(parser1.dump());
    EXPECT_TRUE(layer1.IsSome());
    EXPECT_EQ(layer1.Get().appID, "appIdValue");
    EXPECT_EQ(layer1.Get().bucketID, "bucketIdValue");
    EXPECT_EQ(layer1.Get().hostName, "hostNameValue");
    EXPECT_EQ(layer1.Get().securityToken, "securityTokenValue");
    EXPECT_EQ(layer1.Get().temporaryAccessKey, "temporaryAccessKeyValue");
    EXPECT_EQ(layer1.Get().temporarySecretKey, "temporarySecretKeyValue");
    EXPECT_EQ(layer1.Get().storageType, "s3");
    EXPECT_TRUE(layer1.Get().codePath.empty());

    nlohmann::json parser2 = {
        {"objectId", "objectIdValue"},
        {"appId", "appIdValue"},
        {"bucketId", "bucketIdValue"},
        {"hostName", "hostNameValue"},
        {"securityToken", "securityTokenValue"},
        {"temporaryAccessKey", "temporaryAccessKeyValue"},
        {"temporarySecretKey", "temporarySecretKeyValue"},
    };
    auto layer2 = ParseDelegateDownloadInfoByStr(parser2.dump());
    EXPECT_TRUE(layer2.IsSome());
    EXPECT_EQ(layer2.Get().appID, "appIdValue");
    EXPECT_EQ(layer2.Get().bucketID, "bucketIdValue");
    EXPECT_EQ(layer2.Get().hostName, "hostNameValue");
    EXPECT_EQ(layer2.Get().securityToken, "securityTokenValue");
    EXPECT_EQ(layer2.Get().temporaryAccessKey, "temporaryAccessKeyValue");
    EXPECT_EQ(layer2.Get().temporarySecretKey, "temporarySecretKeyValue");
    EXPECT_EQ(layer2.Get().storageType, "s3");
    EXPECT_TRUE(layer2.Get().codePath.empty());

    auto layer3 = ParseDelegateDownloadInfoByStr("{parser2");
    EXPECT_TRUE(layer3.IsNone());
}

TEST_F(LoaderTest, TransToInstanceInfoFromRouteInfoTest)
{
    resources::RouteInfo routeInfo;
    routeInfo.set_instanceid("instance_id");
    routeInfo.set_runtimeaddress("runtime_address");
    routeInfo.set_functionagentid("function_agent_id");
    routeInfo.set_function("function");
    routeInfo.set_functionproxyid("function_proxy_id");
    routeInfo.set_jobid("job_id");
    routeInfo.set_parentid("parent_id");
    routeInfo.set_requestid("request_id");
    routeInfo.set_tenantid("tenant_id");
    routeInfo.set_issystemfunc(true);
    routeInfo.set_version(1);
    routeInfo.mutable_instancestatus()->set_code(2);

    InstanceInfo instanceInfo;
    TransToInstanceInfoFromRouteInfo(routeInfo, instanceInfo);


    EXPECT_EQ(instanceInfo.instanceid(), "instance_id");
    EXPECT_EQ(instanceInfo.runtimeaddress(), "runtime_address");
    EXPECT_EQ(instanceInfo.functionagentid(), "function_agent_id");
    EXPECT_EQ(instanceInfo.function(), "function");
    EXPECT_EQ(instanceInfo.functionproxyid(), "function_proxy_id");
    EXPECT_EQ(instanceInfo.jobid(), "job_id");
    EXPECT_EQ(instanceInfo.parentid(), "parent_id");
    EXPECT_EQ(instanceInfo.requestid(), "request_id");
    EXPECT_EQ(instanceInfo.tenantid(), "tenant_id");
    EXPECT_EQ(instanceInfo.issystemfunc(), true);
    EXPECT_EQ(instanceInfo.version(), 1);
    EXPECT_EQ(instanceInfo.instancestatus().code(), 2);
}

TEST_F(LoaderTest, TransToRouteInfoFromInstanceInfoTest)
{
    InstanceInfo instanceInfo;
    instanceInfo.set_instanceid("instance_id");
    instanceInfo.set_runtimeaddress("runtime_address");
    instanceInfo.set_functionagentid("function_agent_id");
    instanceInfo.set_function("function");
    instanceInfo.set_functionproxyid("function_proxy_id");
    instanceInfo.set_jobid("job_id");
    instanceInfo.set_parentid("parent_id");
    instanceInfo.set_requestid("request_id");
    instanceInfo.set_tenantid("tenant_id");
    instanceInfo.set_issystemfunc(true);
    instanceInfo.set_version(1);
    instanceInfo.mutable_instancestatus()->set_code(2);

    resources::RouteInfo routeInfo;
    TransToRouteInfoFromInstanceInfo(instanceInfo, routeInfo);

    EXPECT_EQ(routeInfo.instanceid(), "instance_id");
    EXPECT_EQ(routeInfo.runtimeaddress(), "runtime_address");
    EXPECT_EQ(routeInfo.functionagentid(), "function_agent_id");
    EXPECT_EQ(routeInfo.function(), "function");
    EXPECT_EQ(routeInfo.functionproxyid(), "function_proxy_id");
    EXPECT_EQ(routeInfo.jobid(), "job_id");
    EXPECT_EQ(routeInfo.parentid(), "parent_id");
    EXPECT_EQ(routeInfo.requestid(), "request_id");
    EXPECT_EQ(routeInfo.tenantid(), "tenant_id");
    EXPECT_EQ(routeInfo.issystemfunc(), true);
    EXPECT_EQ(routeInfo.version(), 1);
    EXPECT_EQ(routeInfo.instancestatus().code(), 2);
}

TEST_F(LoaderTest, GetInstanceMetaFromJson)
{
    const std::string ins_json = R"({
        "instanceMetaData": {
            "maxInstance": 20,
            "minInstance": 2,
            "concurrentNum": 1000,
            "diskLimit": 0,
            "scalePolicy": "concurrency"
        }
    })";
    FunctionMeta ins_function = GetFuncMetaFromJson(ins_json);
    EXPECT_EQ(ins_function.instanceMetaData.maxInstance, 20);
    EXPECT_EQ(ins_function.instanceMetaData.minInstance, 2);
    EXPECT_EQ(ins_function.instanceMetaData.concurrentNum, 1000);
    EXPECT_EQ(ins_function.instanceMetaData.scalePolicy, "concurrency");
}

TEST_F(LoaderTest, GetExtendedMetaDataFromJson)
{
    const std::string ins_json = R"({
        "extendedMetaData": {
            "initializer": {
                "initializer_handler": "",
                "initializer_timeout": 180
            },
            "user_agency": {
                "accessKey": "123",
                "secretKey": "",
                "token": "",
                "securityAk": "",
                "securitySk": "",
                "securityToken": ""
            },
            "runtime_graceful_shutdown": {
                "maxShutdownTimeout": 10
            }
        }
    })";
    FunctionMeta ins_function = GetFuncMetaFromJson(ins_json);
    EXPECT_EQ(ins_function.extendedMetaData.initializer.timeout, 180);
    EXPECT_EQ(ins_function.extendedMetaData.userAgency.accessKey, "123");
    EXPECT_EQ(ins_function.extendedMetaData.customGracefulShutdown.maxShutdownTimeout, 10);
}
}
