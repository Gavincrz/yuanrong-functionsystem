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

#include <string>

#include "busproxy/startup/busproxy_startup.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "common/status/status.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace functionsystem::meta_store::test;

class StartupBusproxyTest : public ::testing::Test {
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
        etcdSrvDriver_->StopServer();
    }

protected:
    void SetUp() override
    {
        param_.nodeID = "nodeA";
        param_.modelName = "function_proxy";
    }

    void TearDown() override
    {
    }

protected:
    BusProxyStartParam param_;
};

TEST_F(StartupBusproxyTest, StartupBusproxy)
{
    auto busproxyStartup = std::make_shared<BusproxyStartup>(
        BusProxyStartParam(param_),
        std::make_shared<MetaStorageAccessor>(MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ })));

    auto status = busproxyStartup->Run();

    EXPECT_EQ(status.IsOk(), true);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

}  // namespace functionsystem::test