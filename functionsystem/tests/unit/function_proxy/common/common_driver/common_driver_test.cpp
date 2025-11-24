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


#include "common/etcd_service/etcd_service_driver.h"
#include "utils/future_test_helper.h"
#include "utils/grpc_client_helper.h"
#include "function_proxy/common/common_driver/common_driver.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace function_proxy;
class CommonDriverTest : public ::testing::Test {
public:
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
        etcdSrvDriver_->StopServer();
    }
};

TEST_F(CommonDriverTest, MetaConnectedFailed)
{
    auto helper = GrpcClientHelper(10);
    auto flags = Flags();
    auto dsConfig = std::make_shared<DSAuthConfig>();
    auto commonDriver = std::make_shared<CommonDriver>(flags, dsConfig);
    auto status = commonDriver->Init();
    EXPECT_EQ(status, StatusCode::FAILED);
}

TEST_F(CommonDriverTest, SuccessfulDrived)
{
    auto helper = GrpcClientHelper(500);
    auto flags = Flags();
    flags.metaStoreAddress_ = metaStoreServerHost_;
    flags.etcdAddress_ = metaStoreServerHost_;
    flags.iamMetastoreAddress_ = metaStoreServerHost_;
    auto dsConfig = std::make_shared<DSAuthConfig>();
    auto commonDriver = std::make_shared<CommonDriver>(flags, dsConfig);
    auto status = commonDriver->Init();
    EXPECT_EQ(status, StatusCode::SUCCESS);
    status = commonDriver->Start();
    EXPECT_EQ(status, StatusCode::SUCCESS);
    status = commonDriver->Sync();
    EXPECT_EQ(status, StatusCode::SUCCESS);
    status = commonDriver->Stop();
    EXPECT_EQ(status, StatusCode::SUCCESS);
    commonDriver->Await();
}
};  // namespace functionsystem::test
