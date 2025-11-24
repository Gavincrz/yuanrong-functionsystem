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

#include "function_master/system_function_loader/bootstrap_driver.h"

#include <gtest/gtest.h>

#include "common/etcd_service/etcd_service_driver.h"
#include "utils/port_helper.h"

namespace functionsystem::system_function_loader {
const std::string META_STORE_ADDRESS = "127.0.0.1:123";
class BootstrapDriverTest : public ::testing::Test {
protected:
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;

    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        etcdSrvDriver_->StartServer(META_STORE_ADDRESS);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        etcdSrvDriver_->StopServer();
    }
};

/**
 * Feature: Start Bootstrap with driver
 * Description: start Bootstrap
 * Steps:
 * 1. set up params
 * 2. Start driver
 * 3. Stop driver
 *
 * Expectation:
 * 2. StatusCode::SUCCESS
 * 3. StatusCode::SUCCESS
 */
TEST_F(BootstrapDriverTest, StartSuccess)
{
    system_function_loader::SystemFunctionLoaderStartParam param{ .globalSched =
                                                                      std::make_shared<global_scheduler::GlobalSched>(),
                                                                  .sysFuncRetryPeriod = 5000,
                                                                  .sysFuncCustomArgs = "" };

    int metaStoreServerPort = functionsystem::test::FindAvailablePort();
    std::string metaStoreServerHost = "127.0.0.1:" + std::to_string(metaStoreServerPort);
    auto bootstrapDriver = std::make_shared<system_function_loader::BootstrapDriver>(
        param, MetaStoreClient::Create(MetaStoreConfig{ .etcdAddress = metaStoreServerHost },
                                       GrpcSslConfig(), MetaStoreTimeoutOption{ .operationRetryIntervalLowerBound = 100,
                                                                                .operationRetryIntervalUpperBound = 100,
                                                                                .operationRetryTimes = 1 }));

    auto status = bootstrapDriver->Start();
    EXPECT_EQ(status, StatusCode::SUCCESS);

    status = bootstrapDriver->Stop();
    EXPECT_EQ(status, StatusCode::SUCCESS);
    bootstrapDriver->Await();
}

}  // namespace functionsystem::system_function_loader
