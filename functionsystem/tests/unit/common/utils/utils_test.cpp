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

#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdlib.h>

#include <exec/exec.hpp>

#include "common/utils/exception.h"
#include "common/utils/exec_utils.h"
#include "common/utils/files.h"
#include "common/utils/meta_store_kv_operation.h"
#include "common/utils/param_check.h"
#include "common/utils/path.h"
#include "common/utils/ssl_config.h"
#include "common/utils/struct_transfer.h"
#include "common/utils/random_number.h"
#include "common/utils/sensitive_value.h"
#include "exec/exec.hpp"
#include "utils/future_test_helper.h"

using namespace functionsystem;

const int BUFFER_SIZE = 250;
namespace functionsystem::test {
class UtilsTest : public ::testing::Test {};

TEST_F(UtilsTest, GetInstanceIDValid)
{
    auto key = "/yr/instance/business/yrk/tenant/0/function/helloWorld/version/latest/defaultaz/requestid/instanceA";

    auto instance = GetInstanceID(key);

    EXPECT_TRUE(instance == "instanceA");
}

TEST_F(UtilsTest, GetInstanceIDInvalid)
{
    auto key = "/yr/instance/business/yrk/tenant/0/function/helloWorld/version/latest/defaultaz";

    auto instance = GetInstanceID(key);

    EXPECT_TRUE(instance == "");
}

TEST_F(UtilsTest, IsNodeIDValid)
{
    std::string nodeID = "";
    auto isNodeIDValid = IsNodeIDValid(nodeID);
    EXPECT_TRUE(!isNodeIDValid);

    nodeID = "node-123-456";
    isNodeIDValid = IsNodeIDValid(nodeID);
    EXPECT_TRUE(isNodeIDValid);

    nodeID = "node-123/456";
    isNodeIDValid = IsNodeIDValid(nodeID);
    EXPECT_TRUE(!isNodeIDValid);

    nodeID = "/";
    isNodeIDValid = IsNodeIDValid(nodeID);
    EXPECT_TRUE(!isNodeIDValid);

    nodeID = "1 2";
    isNodeIDValid = IsNodeIDValid(nodeID);
    EXPECT_TRUE(!isNodeIDValid);

    nodeID = std::string(129, 'a');
    isNodeIDValid = IsNodeIDValid(nodeID);
    EXPECT_TRUE(!isNodeIDValid);
}

TEST_F(UtilsTest, IsAliasValid)
{
    std::string alias = "";
    auto isAliasIDValid = IsAliasValid(alias);
    EXPECT_TRUE(isAliasIDValid);

    alias = "alias-123-456";
    isAliasIDValid = IsAliasValid(alias);
    EXPECT_TRUE(isAliasIDValid);

    alias = "alias-123/456";
    isAliasIDValid = IsAliasValid(alias);
    EXPECT_TRUE(!isAliasIDValid);

    alias = "/";
    isAliasIDValid = IsAliasValid(alias);
    EXPECT_TRUE(!isAliasIDValid);

    alias = " ";
    isAliasIDValid = IsAliasValid(alias);
    EXPECT_TRUE(!isAliasIDValid);

    alias = std::string(129, 'a');
    isAliasIDValid = IsAliasValid(alias);
    EXPECT_TRUE(!isAliasIDValid);
}

TEST_F(UtilsTest, IsIPValid)
{
    std::string ip = "";
    auto isIpValid = IsIPValid(ip);
    EXPECT_TRUE(!isIpValid);

    ip = "127.0.0.1";
    isIpValid = IsIPValid(ip);
    EXPECT_TRUE(isIpValid);

    ip = "127.0.0.1.6";
    isIpValid = IsIPValid(ip);
    EXPECT_TRUE(!isIpValid);

    ip = "127.0.0";
    isIpValid = IsIPValid(ip);
    EXPECT_TRUE(!isIpValid);

    ip = "127.0.0.266";
    isIpValid = IsIPValid(ip);
    EXPECT_TRUE(!isIpValid);
}

TEST_F(UtilsTest, IsPortValid)
{
    std::string port = "";
    auto isPortValid = IsPortValid(port);
    EXPECT_TRUE(!isPortValid);

    port = "0";
    isPortValid = IsPortValid(port);
    EXPECT_TRUE(isPortValid);

    port = "65535";
    isPortValid = IsPortValid(port);
    EXPECT_TRUE(isPortValid);

    port = "80";
    isPortValid = IsPortValid(port);
    EXPECT_TRUE(isPortValid);

    port = "-1";
    isPortValid = IsPortValid(port);
    EXPECT_TRUE(!isPortValid);

    port = "65536";
    isPortValid = IsPortValid(port);
    EXPECT_TRUE(!isPortValid);

    port = "fake_port";
    isPortValid = IsPortValid(port);
    EXPECT_TRUE(!isPortValid);
}

TEST_F(UtilsTest, IsAddressesValid)
{
    std::string addresses = "";
    auto isAddressesValid = IsAddressesValid(addresses);
    EXPECT_TRUE(!isAddressesValid);

    addresses = "10.10.10.1";
    isAddressesValid = IsAddressesValid(addresses);
    EXPECT_TRUE(!isAddressesValid);

    addresses = "10.10.10.266:8080";
    isAddressesValid = IsAddressesValid(addresses);
    EXPECT_TRUE(!isAddressesValid);

    addresses = "10.10.10.255:dsfahjkll";
    isAddressesValid = IsAddressesValid(addresses);
    EXPECT_TRUE(!isAddressesValid);

    addresses = "10.10.10.1:8080";
    isAddressesValid = IsAddressesValid(addresses);
    EXPECT_TRUE(isAddressesValid);

    addresses = "10.10.10.1:8080,10.10.10.1";
    isAddressesValid = IsAddressesValid(addresses);
    EXPECT_TRUE(!isAddressesValid);

    addresses = "10.10.10.1:8080,10.10.10.1:8080";
    isAddressesValid = IsAddressesValid(addresses);
    EXPECT_TRUE(isAddressesValid);

    addresses = "10.10.10.1:8080, 10.10.10.1:8080";
    isAddressesValid = IsAddressesValid(addresses);
    EXPECT_TRUE(!isAddressesValid);
}

TEST_F(UtilsTest, IsInnerService)
{
    EXPECT_FALSE(IsInnerServiceAddress("www.test.com"));
    EXPECT_FALSE(IsInnerServiceAddress("www.xxxxxxxxxxxxxxxxxxxxxxx.com"));
    EXPECT_TRUE(IsInnerServiceAddress("minio.default.svc.cluster.local"));
}

TEST_F(UtilsTest, LookPathWithNotExistFile)
{
    // given
    std::string given[] = {
        "/tmp/spike_execute_file_not_exist",  // not exist
        "spike_execute_file_not_exist",
    };

    // got
    for (const auto &i : given) {
        EXPECT_EQ(LookPath(i).IsNone(), true);
    }
}

TEST_F(UtilsTest, LookPathWithExistFile)
{
    // given
    std::string given[] = {
        "/tmp/spike_execute_file",
        "spike_execute_file",
    };

    // want
    litebus::Option<std::string> want[] = {
        { "/tmp/spike_execute_file" },
        { "/tmp/spike_execute_file" },
    };

    // got
    auto optionEnv = litebus::os::GetEnv("PATH");
    std::string env;
    if (optionEnv.IsSome()) {
        env = optionEnv.Get();
    }

    litebus::os::SetEnv("PATH", litebus::os::Join("/tmp", env, ':'));
    (void)litebus::os::Rm("/tmp/spike_execute_file");
    auto fd = open("/tmp/spike_execute_file", O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    close(fd);
    for (size_t i = 0; i < sizeof(given) / sizeof(std::string); i++) {
        EXPECT_EQ(LookPath(given[i]).Get(), want[i].Get());
    }
    (void)litebus::os::Rm("/tmp/spike_execute_file");
    litebus::os::SetEnv("PATH", env);
}

TEST_F(UtilsTest, FILEIOTest)
{
    (void)litebus::os::Rmdir("/tmp/runtime-log");
    std::string dirPath = "/tmp/runtime-log";
    if (access(dirPath.c_str(), 0) != 0) {
        if (!litebus::os::Mkdir(dirPath).IsNone()) {
            YRLOG_ERROR("mkdir log dir failed.");
        } else {
            YRLOG_INFO("mkdir log dir success");
        }
    }
    std::string logPath = dirPath + "/test_runtimeID-log.txt";
    (void)TouchFile(logPath); // test
    ASSERT_AWAIT_TRUE([=]() { return FileExists(logPath); });
    int ret = system("echo FileIO test > /tmp/runtime-log/test_runtimeID-log.txt");
    EXPECT_EQ(ret, 0);
    ASSERT_AWAIT_TRUE([=]() {
        auto output = Read(logPath); // test
        return output.find("FileIO test") != output.npos;
    });
}

TEST_F(UtilsTest, IsFileTest)
{
    TouchFile("/tmp/testfile");
    EXPECT_TRUE(IsFile("/tmp/testfile"));

    (void)litebus::os::Rm("/tmp/testfile");
    EXPECT_FALSE(IsFile("/tmp/testfile"));

    (void)litebus::os::Mkdir("/tmp/testdir");
    EXPECT_TRUE(litebus::os::ExistPath("/tmp/testdir"));
    EXPECT_FALSE(IsFile("/tmp/testdir"));
    (void)litebus::os::Rmdir("/tmp/testdir");
}

TEST_F(UtilsTest, IsDirTest)
{
    TouchFile("/tmp/testfile");
    EXPECT_FALSE(IsDir("/tmp/testfile"));

    (void)litebus::os::Rm("/tmp/testfile");
    EXPECT_FALSE(IsDir("/tmp/testfile"));

    (void)litebus::os::Mkdir("/tmp/testdir");
    EXPECT_TRUE(litebus::os::ExistPath("/tmp/testdir"));
    EXPECT_TRUE(IsDir("/tmp/testdir"));
    (void)litebus::os::Rmdir("/tmp/testdir");
}

TEST_F(UtilsTest, RealPath)
{
    char currPath[BUFFER_SIZE];
    (void)getcwd(currPath, BUFFER_SIZE);
    auto relativePath = "../";
    auto realPath = GetRealPath(relativePath);
    int index = std::string(currPath).find_last_of('/');
    std::string subStr = std::string(currPath).substr(0, index);
    EXPECT_EQ(realPath, subStr);
}

TEST_F(UtilsTest, LoadHbmToCreateRequestTest)
{
    FunctionMeta functionMeta{};
    functionMeta.extendedMetaData.deviceMetaData = DeviceMetaData{
        .hbm = 1000,
        .latency = 120,
        .stream = 100,
        .count = 8,
        .model = "cuda",
        .type = "GPU",
    };
    auto expectedHbm = functionMeta.extendedMetaData.deviceMetaData.hbm;

    // LoadHbmToCreateRequest, when the cpu & memory resources do not exist.
    CreateRequest createRequest{};
    LoadHbmToCreateRequest(createRequest, functionMeta);
    auto requestRes = createRequest.schedulingops().resources();
    auto hbmResKey = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_MEM_KEY;
    EXPECT_TRUE(requestRes.find(hbmResKey) != requestRes.end());
    EXPECT_TRUE(static_cast<int32_t>(requestRes.at(hbmResKey)) == expectedHbm);

    // LoadHbmToCreateRequest, when the cpu & memory resources exist.
    CreateRequest createRequest2{};
    (*createRequest2.mutable_schedulingops()->mutable_resources())[CPU_RESOURCE_NAME] = 500;
    (*createRequest2.mutable_schedulingops()->mutable_resources())[MEMORY_RESOURCE_NAME] = 500;
    LoadHbmToCreateRequest(createRequest2, functionMeta);
    auto requestRes2 = createRequest2.schedulingops().resources();
    auto hbmResKey2 = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_MEM_KEY;
    EXPECT_TRUE(requestRes2.find(hbmResKey2) != requestRes2.end());
    EXPECT_TRUE(static_cast<int32_t>(requestRes2.at(hbmResKey2)) == expectedHbm);
}

TEST_F(UtilsTest, LoadDeviceFunctionMetaToCreateRequest)
{
    FunctionMeta functionMeta{};
    functionMeta.extendedMetaData.deviceMetaData = DeviceMetaData{
        .hbm = 1000,
        .latency = 120,
        .stream = 100,
        .count = 8,
        .model = "cuda",
        .type = "GPU",
    };
    auto namedFunctionMeta = functionMeta.extendedMetaData.deviceMetaData;

    auto createRequest = std::make_shared<CreateRequest>();

    LoadDeviceFunctionMetaToCreateRequest(*createRequest, functionMeta);

    auto requestRes = createRequest->schedulingops().resources();
    auto hbmResKey = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_MEM_KEY;
    EXPECT_TRUE(requestRes.find(hbmResKey) != requestRes.end());
    EXPECT_TRUE(static_cast<int32_t>(requestRes.at(hbmResKey)) == namedFunctionMeta.hbm);
    auto latencyResKey = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_LATENCY_KEY;
    EXPECT_TRUE(requestRes.find(latencyResKey) != requestRes.end());
    EXPECT_TRUE(static_cast<int32_t>(requestRes.at(latencyResKey)) == namedFunctionMeta.latency);
    auto streamResKey = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_STREAM_KEY;
    EXPECT_TRUE(requestRes.find(streamResKey) != requestRes.end());
    EXPECT_TRUE(static_cast<uint32_t>(requestRes.at(streamResKey)) == namedFunctionMeta.stream);
}

TEST_F(UtilsTest, LoadDeviceFunctionMetaToCreateRequestNPU)
{
    FunctionMeta functionMeta{};
    functionMeta.extendedMetaData.deviceMetaData = DeviceMetaData{
        .hbm = 1000,
        .latency = 120,
        .stream = 1,
        .count = 8,
        .model = "310",
        .type = "NPU",
    };
    auto namedFunctionMeta = functionMeta.extendedMetaData.deviceMetaData;

    auto createRequest = std::make_shared<CreateRequest>();

    LoadDeviceFunctionMetaToCreateRequest(*createRequest, functionMeta);

    auto requestRes = createRequest->schedulingops().resources();
    auto hbmResKey = NPU_RESOURCE_NAME + "/" + DEFAULT_NPU_PRODUCT + "/" + HETEROGENEOUS_MEM_KEY;
    EXPECT_TRUE(requestRes.find(hbmResKey) != requestRes.end());
    EXPECT_TRUE(static_cast<int32_t>(requestRes.at(hbmResKey)) == namedFunctionMeta.hbm);
    auto latencyResKey = NPU_RESOURCE_NAME + "/" + DEFAULT_NPU_PRODUCT + "/" + HETEROGENEOUS_LATENCY_KEY;
    EXPECT_TRUE(requestRes.find(latencyResKey) != requestRes.end());
    EXPECT_TRUE(static_cast<int32_t>(requestRes.at(latencyResKey)) == namedFunctionMeta.latency);
    auto streamResKey = NPU_RESOURCE_NAME + "/" + DEFAULT_NPU_PRODUCT + "/" + HETEROGENEOUS_STREAM_KEY;
    EXPECT_TRUE(requestRes.find(streamResKey) != requestRes.end());
    EXPECT_TRUE(static_cast<uint32_t>(requestRes.at(streamResKey)) == namedFunctionMeta.stream);
}

TEST_F(UtilsTest, LoadHbmToScheduleRequestTest)
{
    FunctionMeta functionMeta{};
    functionMeta.extendedMetaData.deviceMetaData = DeviceMetaData{
        .hbm = 1000,
        .latency = 120,
        .stream = 100,
        .count = 8,
        .model = "cuda",
        .type = "GPU",
    };
    auto expectedHbm = functionMeta.extendedMetaData.deviceMetaData.hbm;

    // LoadHbmToCreateRequest, when the cpu & memory resources do not exist.
    auto scheduleReq1 = std::make_shared<messages::ScheduleRequest>();
    LoadHbmToScheduleRequest(scheduleReq1, functionMeta);
    auto requestRes = scheduleReq1->instance().resources().resources();
    auto hbmResKey = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_MEM_KEY;
    EXPECT_TRUE(requestRes.find(hbmResKey) != requestRes.end());
    EXPECT_EQ(static_cast<int32_t>(requestRes.at(hbmResKey).scalar().value()), expectedHbm);
    EXPECT_EQ(requestRes.at(hbmResKey).name(), hbmResKey);

    // LoadHbmToCreateRequest, when the cpu & memory resources exist.
    auto scheduleReq2 = std::make_shared<messages::ScheduleRequest>();
    resource_view::Resource cpuResource;
    cpuResource.set_name(CPU_RESOURCE_NAME);
    cpuResource.set_type(resource_view::ValueType::Value_Type_SCALAR);
    cpuResource.mutable_scalar()->set_value(500);
    (*scheduleReq2->mutable_instance()->mutable_resources()->mutable_resources())[CPU_RESOURCE_NAME] = cpuResource;

    resource_view::Resource memResource;
    memResource.set_name(MEMORY_RESOURCE_NAME);
    memResource.set_type(resource_view::ValueType::Value_Type_SCALAR);
    memResource.mutable_scalar()->set_value(500);
    (*scheduleReq2->mutable_instance()->mutable_resources()->mutable_resources())[MEMORY_RESOURCE_NAME] = memResource;

    LoadHbmToScheduleRequest(scheduleReq2, functionMeta);
    auto requestRes2 = scheduleReq2->instance().resources().resources();
    auto hbmResKey2 = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_MEM_KEY;
    EXPECT_TRUE(requestRes2.find(hbmResKey2) != requestRes2.end());
    EXPECT_EQ(static_cast<int32_t>(requestRes2.at(hbmResKey2).scalar().value()), expectedHbm);
    EXPECT_EQ(requestRes2.at(hbmResKey2).name(), hbmResKey2);
}

TEST_F(UtilsTest, LoadDeviceFunctionMetaToScheduleRequest)
{
    FunctionMeta functionMeta{};
    functionMeta.extendedMetaData.deviceMetaData = DeviceMetaData{
        .hbm = 1000,
        .latency = 120,
        .stream = 100,
        .count = 8,
        .model = "cuda",
        .type = "GPU",
    };
    auto namedFunctionMeta = functionMeta.extendedMetaData.deviceMetaData;

    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    LoadDeviceFunctionMetaToScheduleRequest(scheduleReq, functionMeta);

    auto requestRes = scheduleReq->instance().resources().resources();
    auto hbmResKey = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_MEM_KEY;
    EXPECT_TRUE(requestRes.find(hbmResKey) != requestRes.end());
    EXPECT_EQ(static_cast<int32_t>(requestRes.at(hbmResKey).scalar().value()), namedFunctionMeta.hbm);
    EXPECT_EQ(requestRes.at(hbmResKey).name(), hbmResKey);
    auto latencyResKey = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_LATENCY_KEY;
    EXPECT_TRUE(requestRes.find(latencyResKey) != requestRes.end());
    EXPECT_EQ(static_cast<int32_t>(requestRes.at(latencyResKey).scalar().value()), namedFunctionMeta.latency);
    EXPECT_EQ(requestRes.at(latencyResKey).name(), latencyResKey);
    auto streamResKey = GPU_RESOURCE_NAME + "/" + DEFAULT_GPU_PRODUCT + "/" + HETEROGENEOUS_STREAM_KEY;
    EXPECT_TRUE(requestRes.find(streamResKey) != requestRes.end());
    EXPECT_EQ(static_cast<uint32_t>(requestRes.at(streamResKey).scalar().value()), namedFunctionMeta.stream);
    EXPECT_EQ(requestRes.at(streamResKey).name(), streamResKey);
}

TEST_F(UtilsTest, LoadDeviceFunctionMetaToScheduleRequestNPU)
{
    FunctionMeta functionMeta{};
    functionMeta.extendedMetaData.deviceMetaData = DeviceMetaData{
        .hbm = 1000,
        .latency = 120,
        .stream = 1,
        .count = 8,
        .model = "310",
        .type = "NPU",
    };
    auto namedFunctionMeta = functionMeta.extendedMetaData.deviceMetaData;

    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();

    LoadDeviceFunctionMetaToScheduleRequest(scheduleReq, functionMeta);

    auto requestRes = scheduleReq->instance().resources().resources();
    auto hbmResKey = NPU_RESOURCE_NAME + "/" + DEFAULT_NPU_PRODUCT + "/" + HETEROGENEOUS_MEM_KEY;
    EXPECT_TRUE(requestRes.find(hbmResKey) != requestRes.end());
    EXPECT_EQ(static_cast<int32_t>(requestRes.at(hbmResKey).scalar().value()), namedFunctionMeta.hbm);
    EXPECT_EQ(requestRes.at(hbmResKey).name(), hbmResKey);

    auto latencyResKey = NPU_RESOURCE_NAME + "/" + DEFAULT_NPU_PRODUCT + "/" + HETEROGENEOUS_LATENCY_KEY;
    EXPECT_TRUE(requestRes.find(latencyResKey) != requestRes.end());
    EXPECT_EQ(static_cast<int32_t>(requestRes.at(latencyResKey).scalar().value()), namedFunctionMeta.latency);
    EXPECT_EQ(requestRes.at(latencyResKey).name(), latencyResKey);

    auto streamResKey = NPU_RESOURCE_NAME + "/" + DEFAULT_NPU_PRODUCT + "/" + HETEROGENEOUS_STREAM_KEY;
    EXPECT_TRUE(requestRes.find(streamResKey) != requestRes.end());
    EXPECT_EQ(static_cast<uint32_t>(requestRes.at(streamResKey).scalar().value()), namedFunctionMeta.stream);
    EXPECT_EQ(requestRes.at(streamResKey).name(), streamResKey);
}

affinity::LabelExpression ExistLabel(const std::string &key)
{
    auto expression = affinity::LabelExpression();
    expression.set_key(key);
    expression.mutable_op()->mutable_exists();
    return expression;
}

affinity::Selector PreferredSelect(bool isPriority,
                                   std::vector<std::vector<affinity::LabelExpression>> labels)
{
    auto selector = affinity::Selector();
    selector.mutable_condition()->set_orderpriority(isPriority);
    for (auto i = 0; i < (int)labels.size(); i++) {
        auto label = labels[i];
        auto expressGroup = selector.mutable_condition()->add_subconditions();
        for (auto express : label) {
            *expressGroup->add_expressions() = express;
        }
    }
    return selector;
}

TEST_F(UtilsTest, SetAffinityOpt)
{
    CreateRequest createReq;
    auto scheduleAffinity = createReq.mutable_schedulingops()->mutable_scheduleaffinity();
    auto instanceAffinity = scheduleAffinity->mutable_instance();
    auto resourceAffinity = scheduleAffinity->mutable_resource();

    (*instanceAffinity->mutable_preferredaffinity()) =
        PreferredSelect(true, { { ExistLabel("key5") }, { ExistLabel("key2") } });
    (*instanceAffinity->mutable_preferredantiaffinity()) =
        PreferredSelect(true, { { ExistLabel("key5") } });

    (*resourceAffinity->mutable_preferredaffinity()) =
        PreferredSelect(false, { { ExistLabel("key3") }, { ExistLabel("key4") } });
    (*resourceAffinity->mutable_preferredantiaffinity()) =
        PreferredSelect(true, { { ExistLabel("key5") } });

    auto scheduleReq = TransFromCreateReqToScheduleReq(std::move(createReq), "");
    auto instanceInfo = scheduleReq->mutable_instance();
    SetAffinityOpt(*instanceInfo, createReq, scheduleReq);
    instanceAffinity = instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    resourceAffinity = instanceInfo->mutable_scheduleoption()->mutable_affinity()->mutable_resource();
    EXPECT_EQ(instanceAffinity->scope(), affinity::POD);
    EXPECT_EQ(instanceAffinity->preferredaffinity().condition().subconditions(0).weight(), 100);
    EXPECT_EQ(instanceAffinity->preferredaffinity().condition().subconditions(1).weight(), 90);
    EXPECT_EQ(resourceAffinity->preferredaffinity().condition().subconditions(0).weight(), 100);
    EXPECT_EQ(resourceAffinity->preferredaffinity().condition().subconditions(1).weight(), 100);
    EXPECT_EQ((*scheduleReq->mutable_contexts())["LabelAffinitPlugin"].affinityctx().maxscore(), 400);

    // test preemptedallowed is true
    instanceInfo->mutable_scheduleoption()->set_preemptedallowed(true);
    instanceInfo->mutable_labels()->Clear();
    instanceAffinity->clear_scope();

    SetPreemptionAffinity(scheduleReq);
    auto preemptAffinity = instanceInfo->scheduleoption().affinity().inner().preempt();
    EXPECT_EQ(preemptAffinity.preferredaffinity().condition().subconditions_size(), 1);
    EXPECT_EQ(preemptAffinity.preferredaffinity().condition().subconditions(0).expressions_size(), 1);
    EXPECT_EQ(preemptAffinity.preferredaffinity().condition().subconditions(0).weight(), 3);
    EXPECT_EQ(preemptAffinity.preferredaffinity().condition().subconditions(0).expressions(0).key(), PREEMPTIBLE);
    EXPECT_EQ(preemptAffinity.preferredantiaffinity().condition().subconditions_size(), 1);
    EXPECT_EQ(preemptAffinity.preferredantiaffinity().condition().subconditions(0).expressions_size(), 1);
    EXPECT_EQ(preemptAffinity.preferredantiaffinity().condition().subconditions(0).weight(), 3);
    EXPECT_EQ(preemptAffinity.preferredantiaffinity().condition().subconditions(0).expressions(0).key(),
              NOT_PREEMPTIBLE);
    EXPECT_EQ(instanceInfo->labels(0), PREEMPTIBLE);

    // test preemptedallowed is false
    instanceInfo->mutable_scheduleoption()->set_preemptedallowed(false);
    instanceInfo->mutable_labels()->Clear();
    instanceAffinity->clear_scope();
    SetPreemptionAffinity(scheduleReq);
    preemptAffinity = instanceInfo->scheduleoption().affinity().inner().preempt();
    EXPECT_EQ(preemptAffinity.preferredaffinity().condition().subconditions_size(), 1);
    EXPECT_EQ(preemptAffinity.preferredaffinity().condition().subconditions(0).expressions_size(), 1);
    EXPECT_EQ(preemptAffinity.preferredaffinity().condition().subconditions(0).weight(), 3);
    EXPECT_EQ(preemptAffinity.preferredaffinity().condition().subconditions(0).expressions(0).key(), NOT_PREEMPTIBLE);
    EXPECT_EQ(preemptAffinity.preferredantiaffinity().condition().subconditions_size(), 1);
    EXPECT_EQ(preemptAffinity.preferredantiaffinity().condition().subconditions(0).expressions_size(), 1);
    EXPECT_EQ(preemptAffinity.preferredantiaffinity().condition().subconditions(0).weight(), 3);
    EXPECT_EQ(preemptAffinity.preferredantiaffinity().condition().subconditions(0).expressions(0).key(), PREEMPTIBLE);
    EXPECT_EQ(instanceInfo->labels(0), NOT_PREEMPTIBLE);
}

TEST_F(UtilsTest, Sha256CalculateFileTest)
{
    (void)litebus::os::Rmdir("/home/layer/func/bucket");
    std::string objDir = "/home/layer/func/bucket/files";
    std::string destFile = "/home/layer/func/bucket/test.zip";
    litebus::os::Mkdir(objDir);
    auto file = objDir + "/a.txt";
    (void)ExecuteCommandByPopen("echo a > " + file, INT32_MAX);
    ASSERT_AWAIT_TRUE([=]() { return FileExists(file); });
    (void)ExecuteCommandByPopen("/usr/bin/zip -r " + destFile + " " + objDir + "/a.txt", INT32_MAX);
    ASSERT_AWAIT_TRUE([=]() { return FileExists(destFile); });

    unsigned char sha256Value[32];
    auto result = Sha256CalculateFile(destFile.c_str(), sha256Value, 32); // test
    if (result != 0) {
        YRLOG_ERROR("openssl sha256 failed");
    }
    std::stringstream resultSs;
    for (int i = 0; i < 32; i++) {
        resultSs << std::hex << std::setw(2) << std::setfill('0') << (int)sha256Value[i];
    }
    std::string cmd = "/usr/bin/sha256sum " + destFile;
    EXPECT_TRUE(litebus::strings::StartsWithPrefix(ExecuteCommandByPopen(cmd, INT32_MAX), resultSs.str()));
    EXPECT_TRUE(litebus::os::Rmdir("/home/layer/func/bucket").IsNone());
}

TEST_F(UtilsTest, ExceptionTest)
{
    EXPECT_TRUE(GetTid() > 0);
}

TEST_F(UtilsTest, RealPathTest)
{
    EXPECT_EQ(GetRealPath(""), "");
}

TEST_F(UtilsTest, MetaStoreKvOperationTest)
{
    auto key = GetLastFunctionNameFromKey("12345678901234561234567890123456/0-test-helloWorld/$latest");
    EXPECT_TRUE(key.IsSome());
    EXPECT_EQ(key.Get(), "helloWorld");

    key = GetLastFunctionNameFromKey("12345678901234561234567890123456/0-test-helloWorld/$latest/123/123/123");
    EXPECT_TRUE(key.IsNone());

    key = GetLastFunctionNameFromKey("12345678901234561234567890123456/0-test-helloWorld-123-123/$latest");
    EXPECT_TRUE(key.IsNone());
}

TEST_F(UtilsTest, SensitiveValueTest)
{
    std::string value = "123";
    SensitiveValue sensitiveValue;
    SensitiveValue sensitiveValue2("1234");
    sensitiveValue = value.c_str();
    EXPECT_FALSE(sensitiveValue == sensitiveValue2);

    sensitiveValue = sensitiveValue2;
    sensitiveValue = "";
    sensitiveValue2 = "";
    EXPECT_TRUE(sensitiveValue == sensitiveValue2);
}

TEST_F(UtilsTest, TransCreateReqReliabilityTypeTest)
{
    std::shared_ptr<CreateRequest> lowCreateReq = std::make_shared<CreateRequest>();
    auto createOptions = lowCreateReq->mutable_createoptions();
    (*createOptions)[functionsystem::RELIABILITY_TYPE] = "low";
    auto scheduleReq = TransFromCreateReqToScheduleReq(std::move(*lowCreateReq), "parentid");
    EXPECT_TRUE(scheduleReq->instance().lowreliability());

    std::shared_ptr<CreateRequest> createReq = std::make_shared<CreateRequest>();
    scheduleReq = TransFromCreateReqToScheduleReq(std::move(*lowCreateReq), "parentid");
    EXPECT_FALSE(scheduleReq->instance().lowreliability());
}

TEST_F(UtilsTest, ChmodErrorTest)
{
    litebus::os::Rmdir("/invalid");
    int result = chmod("/invalid", 0770);
    EXPECT_NE(result, 0);
    EXPECT_THAT(litebus::os::Strerror(errno), testing::HasSubstr("No such file or directory"));

    litebus::os::Mkdir("/invalid");
    result = chmod("/invalid", 0770);
    EXPECT_EQ(result, 0);

    litebus::os::Rmdir("/invalid");
}

TEST_F(UtilsTest, ExtractProxyIDFromProxyAIDTest)
{
    std::string proxyAID = "dggphis151700-LocalSchedInstanceCtrlActor@127.0.0.1:22772";
    EXPECT_EQ(ExtractProxyIDFromProxyAID(proxyAID), "dggphis151700");

    proxyAID = "dggphis151700-LocalSchedInstanceCtrlActor";
    EXPECT_EQ(ExtractProxyIDFromProxyAID(proxyAID), "dggphis151700");

    proxyAID = "-LocalSchedInstanceCtrlActor";
    EXPECT_EQ(ExtractProxyIDFromProxyAID(proxyAID), "");

    proxyAID = "-LocalSchedInstanceCtrlActorABC";
    EXPECT_EQ(ExtractProxyIDFromProxyAID(proxyAID), "");

    proxyAID = "dggphis151700-LocalSchedtrlActor";
    EXPECT_EQ(ExtractProxyIDFromProxyAID(proxyAID), "");

    proxyAID = "";
    EXPECT_EQ(ExtractProxyIDFromProxyAID(proxyAID), "");
}

TEST_F(UtilsTest, GenerateRuntimeID)
{
    const uint32_t defaultUUIDLength = 36;
    std::string UUID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    EXPECT_EQ(UUID.size(), defaultUUIDLength);

    std::string runtimeID = GenerateRuntimeID("");
    EXPECT_TRUE(runtimeID.find("runtime-") != std::string::npos);
    const uint32_t defaultRuntimeIDLength = 44;
    EXPECT_EQ(runtimeID.size(), defaultRuntimeIDLength);
    std::string runtimeID1 = GenerateRuntimeID("a-b-instance");
    EXPECT_TRUE(runtimeID1.find("runtime-") != std::string::npos);
    EXPECT_TRUE(runtimeID1.find("a-b-instance") != std::string::npos);
}

TEST_F(UtilsTest, ParseValueFromKey)
{
    auto funcName = TrimKeyPrefix("/yr/functions/business/yrk/tenant/0/function/0-system-faascontroller/version/$latest", "");
    EXPECT_EQ("/yr/functions/business/yrk/tenant/0/function/0-system-faascontroller/version/$latest", funcName);
    funcName = TrimKeyPrefix("/yr/functions/business/yrk/tenant/0/function/0-system-faascontroller/version/$latest", "/test");
    EXPECT_EQ("/yr/functions/business/yrk/tenant/0/function/0-system-faascontroller/version/$latest", funcName);
    funcName = TrimKeyPrefix("/test/yr/functions/business/yrk/tenant/0/function/0-system-faascontroller/version/$latest", "/test");
    EXPECT_EQ("/yr/functions/business/yrk/tenant/0/function/0-system-faascontroller/version/$latest", funcName);
}

/**
* @tc.name  : ShuffleStringByDelimiter_ShouldReturnEmptyString_WhenInputIsEmpty
* @tc.number: ShuffleStringByDelimiter_Test_001
* @tc.desc  : Test ShuffleStringByDelimiter function with empty input string.
 */
TEST_F(UtilsTest, ShuffleStringByDelimiter_ShouldReturnEmptyString_WhenInputIsEmpty) {
    std::string input = "";
    std::string pattern = ",";
    std::string result = ShuffleStringByDelimiter(input, pattern);
    EXPECT_EQ(result, "");
}

/**
* @tc.name  : ShuffleStringByDelimiter_ShouldReturnSameString_WhenInputHasNoDelimiter
* @tc.number: ShuffleStringByDelimiter_Test_002
* @tc.desc  : Test ShuffleStringByDelimiter function with input string that has no delimiter.
 */
TEST_F(UtilsTest, ShuffleStringByDelimiter_ShouldReturnSameString_WhenInputHasNoDelimiter) {
    std::string input = "127.0.0.1:2379";
    std::string pattern = ",";
    std::string result = ShuffleStringByDelimiter(input, pattern);
    EXPECT_EQ(result, "127.0.0.1:2379");
    result = ShuffleStringByDelimiter("ds-core-etcd.default.svc.cluster.local:2379", pattern);
    EXPECT_EQ(result, "ds-core-etcd.default.svc.cluster.local:2379");
}

/**
* @tc.name  : ShuffleStringByDelimiter_ShouldReturnShuffledString_WhenInputHasMultipleDelimiters
* @tc.number: ShuffleStringByDelimiter_Test_003
* @tc.desc  : Test ShuffleStringByDelimiter function with input string that has multiple delimiters.
 */
TEST_F(UtilsTest, ShuffleStringByDelimiter_ShouldReturnShuffledString_WhenInputHasMultipleDelimiters) {
    std::string input = "127.0.0.1:2379,127.0.0.2:2379,127.0.0.3:2379";
    std::string pattern = ",";
    std::string result = ShuffleStringByDelimiter(input, pattern);
    // Since the result is shuffled, we cannot predict the exact output.
    // However, we can check if the result contains the same elements.
    std::vector<std::string> expected = {"127.0.0.1:2379", "127.0.0.2:2379", "127.0.0.3:2379"};
    std::vector<std::string> resultVec = litebus::strings::Split(result, ",");
    std::sort(expected.begin(), expected.end());
    std::sort(resultVec.begin(), resultVec.end());
    EXPECT_EQ(resultVec, expected);
}
}  // namespace functionsystem::test