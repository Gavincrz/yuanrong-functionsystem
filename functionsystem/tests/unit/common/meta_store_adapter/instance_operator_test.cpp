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

#include "common/meta_store_adapter/instance_operator.h"

#include <gtest/gtest.h>

#include <string>

#include "common/etcd_service/etcd_service_driver.h"
#include "meta_store_monitor/meta_store_monitor_factory.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/utils/meta_store_kv_operation.h"
#include "utils/future_test_helper.h"
#include "utils/grpc_client_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
const std::string key =
    "/sn/instance/business/yrk/tenant/12345678901234561234567890123456/function/0-opc-opc/version/$latest/"
    "defaultaz/job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0/"
    "0ee7cafc-93b9-4be3-1111-000000000080";
const std::string instanceKey =
    "/sn/instance/business/yrk/tenant/12345678901234561234567890123456/function/0-opc-opc/version/$latest/"
    "defaultaz/job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0/"
    "0ee7cafc-93b9-4be3-ae01-000000000075";
const std::string routeKey = "/yr/route/business/yrk/0ee7cafc-93b9-4be3-ae01-000000000075";
const std::string routeKey2 = "/yr/route/business/yrk/instanceID2";

class InstanceOperatorTest : public ::testing::Test {
protected:
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::string metaStoreServerHost_;

    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        ASSERT_TRUE(client->Delete(INSTANCE_ROUTE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                        .Get()
                        ->status.IsOk());
        ASSERT_TRUE(
            client->Delete(INSTANCE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true }).Get()->status.IsOk());
        etcdSrvDriver_->StopServer();
    }

    void SetUp() override
    {
        options_.operationRetryIntervalLowerBound = 10;
        options_.operationRetryIntervalUpperBound = 100;
        options_.operationRetryTimes = 3;
        options_.grpcTimeout = 1;
        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ },
                                                   GrpcSslConfig(), options_);
        ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                        .Get()
                        ->status.IsOk());
        ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_ROUTE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                        .Get()
                        ->status.IsOk());
    }

    void TearDown() override
    {
        metaStoreClient_ = nullptr;
    }

    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    MetaStoreTimeoutOption options_;
};

TEST_F(InstanceOperatorTest, CreateInstanceSuccess)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736"})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value);
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsOk());

    instancePutInfo->key = instanceKey;
    routePutInfo = nullptr;
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsOk());
}

TEST_F(InstanceOperatorTest, CreateInstanceExist)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736"})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);

    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsOk());

    const std::string value2 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22737"})";
    instancePutInfo->value = value2;
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsError());

    instancePutInfo->key = instanceKey;
    routePutInfo->value = value2;
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsError());

    instancePutInfo->key = key;
    routePutInfo = nullptr;
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsError());
}

TEST_F(InstanceOperatorTest, CreateRouteExist)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736"})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);

    const std::string value2 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22737"})";
    instancePutInfo->key = "fake_key";
    routePutInfo->value = value2;
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsError());
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);

    InstanceInfo instanceInfoSaved;
    EXPECT_TRUE(TransToInstanceInfoFromJson(instanceInfoSaved, fut.Get().value));
}

TEST_F(InstanceOperatorTest, CreateInstanceETCDUnavailable)
{
    auto helper = GrpcClientHelper(10);
    auto metaStoreClient = MetaStoreClient::Create({ .etcdAddress = "127.0.0.1:111" }, GrpcSslConfig(), options_);

    InstanceOperator instanceOpt(metaStoreClient);

    const std::string value =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736"})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value);
    std::shared_ptr<StoreInfo> routePutInfo;

    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY_FOR(fut, 20000);
    EXPECT_TRUE(fut.Get().status.IsError());

    routePutInfo = std::make_shared<StoreInfo>(routeKey, value);
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY_FOR(fut, 20000);
    EXPECT_TRUE(fut.Get().status.IsError());
}

TEST_F(InstanceOperatorTest, ModifyInstanceSuccess)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000076","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";

    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo;
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);

    const std::string value2 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":2,"msg":"creating"}})";

    instancePutInfo->value = value2;
    fut = instanceOpt.Modify(instancePutInfo, routePutInfo, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsOk());
    EXPECT_EQ(fut.Get().value, "");

    const std::string value3 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":3,"msg":"running"}})";
    instancePutInfo->value = value3;

    routePutInfo = std::make_shared<StoreInfo>(routeKey, value3);
    fut = instanceOpt.Modify(instancePutInfo, routePutInfo, 2, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsOk());
    EXPECT_EQ(fut.Get().value, "");
}

TEST_F(InstanceOperatorTest, ModifyInstanceNotExist)
{
    ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_ROUTE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                    .Get()
                    ->status.IsOk());
    ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                    .Get()
                    ->status.IsOk());
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000077","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";

    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo;
    auto fut = instanceOpt.Modify(instancePutInfo, routePutInfo, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsError());
    EXPECT_EQ(fut.Get().status.GetMessage(), "[get response KV is empty]");

    // need to delete, because Modify will create it
    fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 1, false);
    EXPECT_AWAIT_READY(fut);

    routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    fut = instanceOpt.Modify(instancePutInfo, routePutInfo, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsError());
    EXPECT_EQ(fut.Get().status.GetMessage(), "[get response KV is empty]");
}

TEST_F(InstanceOperatorTest, ModifyInstanceETCDUnavailable)
{
    auto helper = GrpcClientHelper(10);
    auto metaStoreClient = MetaStoreClient::Create({ .etcdAddress = "127.0.0.1:111" }, GrpcSslConfig(), options_);
    InstanceOperator instanceOpt(metaStoreClient);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";

    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo;
    auto fut = instanceOpt.Modify( instancePutInfo, routePutInfo, 1, false);
    EXPECT_AWAIT_READY_FOR(fut, 20000);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::GRPC_UNAVAILABLE);

    routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    fut = instanceOpt.Modify(instancePutInfo, routePutInfo,  1, false);
    EXPECT_AWAIT_READY_FOR(fut, 20000);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::GRPC_UNAVAILABLE);
}

TEST_F(InstanceOperatorTest, ModifyInstanceRevisionUnmatched)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);

    const std::string value2 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":2,"msg":"creating"}})";

    instancePutInfo->value = value2;
    routePutInfo->value = value2;
    fut = instanceOpt.Modify(instancePutInfo, routePutInfo, 2, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);

    routePutInfo = nullptr ;
    fut = instanceOpt.Modify(instancePutInfo, routePutInfo, 2, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);
}

TEST_F(InstanceOperatorTest, ModifyInstanceRouteInfoRevisionUnmatched)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(metaStoreClient_->Put(routeKey, value1, {}).Get()->status.IsOk());

    const std::string value2 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":2,"msg":"creating"}})";

    instancePutInfo->value = value2;
    routePutInfo->value = value2;
    fut = instanceOpt.Modify(instancePutInfo, routePutInfo, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::SUCCESS);
}

TEST_F(InstanceOperatorTest, ModifyInstanceWhileRouteInfoNoExist)
{
    InstanceOperator instanceOpt(metaStoreClient_);
    ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                    .Get()
                    ->status.IsOk());
    ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_ROUTE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                    .Get()
                    ->status.IsOk());
    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo;
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsOk());

    const std::string value2 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":2,"msg":"fatal"}})";

    instancePutInfo->value = value2;
    routePutInfo = std::make_shared<StoreInfo>(routeKey, value2);
    fut = instanceOpt.Modify(instancePutInfo, routePutInfo, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::SUCCESS);

}

TEST_F(InstanceOperatorTest, DeleteInstanceSuccess)
{
    InstanceOperator instanceOpt(metaStoreClient_);
    ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                    .Get()
                    ->status.IsOk());
    ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_ROUTE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                    .Get()
                    ->status.IsOk());
    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo;

    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);

    fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::SUCCESS);

    routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);

    fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::SUCCESS);
}

TEST_F(InstanceOperatorTest, DeleteInstanceETCDUnavailable)
{
    auto helper = GrpcClientHelper(10);
    auto metaStoreClient = MetaStoreClient::Create({ .etcdAddress = "127.0.0.1:111" }, GrpcSslConfig(), options_);
    InstanceOperator instanceOpt(metaStoreClient);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000075","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";

    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo;

    auto fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 1, false);
    EXPECT_AWAIT_READY_FOR(fut, 20000);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::GRPC_UNAVAILABLE);

    routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 1, false);
    EXPECT_AWAIT_READY_FOR(fut, 20000);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::GRPC_UNAVAILABLE);
}

TEST_F(InstanceOperatorTest, DeleteInstanceNotExist)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000079","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";

    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo;

    auto fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED);

    routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED);
    EXPECT_EQ(fut.Get().status.GetMessage(), "[get response KV is empty]");
}

TEST_F(InstanceOperatorTest, DeleteInstanceRevisionUnmatched)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000080","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);

    fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 2, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);

    routePutInfo = nullptr;
    fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 2, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION);
}

TEST_F(InstanceOperatorTest, DeleteInstanceRouteVersionUnmatched)
{
    InstanceOperator instanceOpt(metaStoreClient_);
    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000080","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    EXPECT_TRUE(metaStoreClient_->Put(key, value1, {}).Get()->status.IsOk());
    EXPECT_TRUE(metaStoreClient_->Put(routeKey, value1, {}).Get()->status.IsOk());
    EXPECT_TRUE(metaStoreClient_->Put(routeKey, value1, {}).Get()->status.IsOk());
    auto fut = instanceOpt.Delete(instancePutInfo, routePutInfo, nullptr, 1, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::SUCCESS);
}

TEST_F(InstanceOperatorTest, MaintenanceClientActorTest)
{
    auto helper = GrpcClientHelper(10);
    auto metaStoreClient = MetaStoreClient::Create({ .etcdAddress = "127.0.0.1:111" }, GrpcSslConfig(), options_);
    auto actor = metaStoreClient->GetMaintenanceClientActor();
    EXPECT_EQ(actor->HealthCheck().IsOK(), true);
}

TEST_F(InstanceOperatorTest, GetInstance)
{
    ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_ROUTE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                    .Get()
                    ->status.IsOk());
    ASSERT_TRUE(metaStoreClient_->Delete(INSTANCE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true })
                    .Get()
                    ->status.IsOk());
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000080","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";

    auto fut = instanceOpt.GetInstance(routeKey);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::FAILED);
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);

    fut = instanceOpt.GetInstance(routeKey);
    EXPECT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().status.IsOk());

}

TEST_F(InstanceOperatorTest, PrintResponseTest)
{
    InstanceOperator instanceOpt(metaStoreClient_);
    OperateInfo operateInfo;
    operateInfo.response = std::make_shared<TxnResponse>();
    operateInfo.key = "/sn/instance/test";

    // deleteResponse
    KeyValue delKeyValue;
    delKeyValue.set_key(operateInfo.key) ;
    delKeyValue.set_value("del-value");

    DeleteResponse del;
    del.status = Status::OK();
    del.header.revision = 1;
    del.deleted = 1;
    del.prevKvs.emplace_back(delKeyValue);
    del.prevKvs.emplace_back(delKeyValue);
    TxnOperationResponse delOperationResponse;
    delOperationResponse.response = del;
    delOperationResponse.operationType = TxnOperationType::OPERATION_DELETE;
    (void)operateInfo.response->responses.emplace_back(delOperationResponse);

    // putResponse
    KeyValue putKeyValue;
    putKeyValue.set_key(operateInfo.key) ;
    putKeyValue.set_value("put-preValue");

    PutResponse put;
    put.status = Status::OK();
    put.header.revision = 2;
    put.prevKv = putKeyValue;
    TxnOperationResponse putOperationResponse;
    putOperationResponse.response = put;
    putOperationResponse.operationType = TxnOperationType::OPERATION_PUT;
    (void)operateInfo.response->responses.emplace_back(putOperationResponse);

    // getResponse
    KeyValue getKeyValue;
    getKeyValue.set_key(operateInfo.key) ;
    getKeyValue.set_value(R"({"instanceID":"551d163a-a7c9-4e99-9cf2-84b627ee7167","runtimeAddress":"127.0.0.1:22771","functionAgentID":"function-agent-84b627ee7167-1000m-4000mi-faasscheduler-6fe0041f","function":"0/0-system-faasscheduler/$latest","functionProxyID":"dggpalpha00009","instanceStatus":{"code":3,"msg":"running"},"jobID":"job-12345678","parentID":"0-system-faascontroller-0","requestID":"a4e11567b387ba8c00","tenantID":"0","isSystemFunc":true,"version":"3"})");
    getKeyValue.set_version(1);
    getKeyValue.set_mod_revision(1);
    getKeyValue.set_create_revision(0);

    GetResponse get;
    get.status = Status::OK();
    get.header.revision = 3;
    get.kvs.emplace_back(getKeyValue);
    get.kvs.emplace_back(getKeyValue);
    TxnOperationResponse getOperationResponse;
    getOperationResponse.response = get;
    getOperationResponse.operationType = TxnOperationType::OPERATION_GET;
    (void)operateInfo.response->responses.emplace_back(getOperationResponse);
    instanceOpt.OnPrintResponse(getKeyValue);
    EXPECT_TRUE(instanceOpt.PrintResponse(operateInfo));
}

TEST_F(InstanceOperatorTest, ForceDeleteTest)
{
    InstanceOperator instanceOpt(metaStoreClient_);

    const std::string value1 =
        R"({"instanceID":"0ee7cafc-93b9-4be3-ae01-000000000080","requestID":"job-3d8f88d4-task-daf90ea7-f29e-4c9e-ada4-b11cea549201-694d1ff7031c-0","functionProxyID":"siaphis12332-22736","instanceStatus":{"code":1,"msg":"scheduling"}})";
    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(key, value1);
    std::shared_ptr<StoreInfo> routePutInfo = std::make_shared<StoreInfo>(routeKey, value1);
    auto fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    auto getFuture = metaStoreClient_->Get(routeKey, GetOption{ .prefix = false });
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(getFuture.Get()->kvs.size(), static_cast<long unsigned int>(1));  // check put success

    fut = instanceOpt.ForceDelete(instancePutInfo, routePutInfo, nullptr, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::SUCCESS);
    getFuture = metaStoreClient_->Get(key, GetOption{ .prefix = false });
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(getFuture.Get()->kvs.size(), static_cast<long unsigned int>(0));
    getFuture = metaStoreClient_->Get(routeKey, GetOption{ .prefix = false });
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(getFuture.Get()->kvs.size(), static_cast<long unsigned int>(0)); // check delete success

    const std::string value2 = "";
    instancePutInfo = std::make_shared<StoreInfo>(key, value2);
    routePutInfo = std::make_shared<StoreInfo>(routeKey, value2);
    fut = instanceOpt.Create(instancePutInfo, routePutInfo, false);
    EXPECT_AWAIT_READY(fut);
    getFuture = metaStoreClient_->Get(key, GetOption{ .prefix = false });
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(getFuture.Get()->kvs.size(), 1); // check put success

    fut = instanceOpt.ForceDelete(instancePutInfo, routePutInfo, nullptr, false);
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(fut.Get().status.StatusCode(), StatusCode::SUCCESS);
    getFuture = metaStoreClient_->Get(key, GetOption{ .prefix = false });
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(getFuture.Get()->kvs.size(), 0);
    getFuture = metaStoreClient_->Get(routeKey, GetOption{ .prefix = false });
    EXPECT_AWAIT_READY(fut);
    EXPECT_EQ(getFuture.Get()->kvs.size(), 0); // check delete success
}

}  // namespace functionsystem::test