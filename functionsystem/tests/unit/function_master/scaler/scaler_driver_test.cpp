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

#include "function_master/scaler/scaler_driver.h"

#include <gtest/gtest.h>

#include "common/etcd_service/etcd_service_driver.h"
#include "meta_store_client/meta_store_struct.h"
#include "function_master/common/flags/flags.h"
#include "utils/port_helper.h"

namespace functionsystem::scaler::test {

const GrpcSslConfig sslConfig{};

class ScalerDriverTest : public ::testing::Test {
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

    void SetUp() override
    {
    }

    void TearDown() override
    {
    }

    ScalerHandlers handlers{ .systemUpgradeHandler = [](bool isUpgrading) {},
                             .localSchedFaultHandler = [](const std::string &nodeName) {} };
};

TEST_F(ScalerDriverTest, DriverStartNoK8s)
{
    const char *argv2[] = { "./function_master",
                            R"(--log_config={
                                "filepath":"/home/yr/log",
                                "level":"DEBUG",
                                "rolling":{
                                    "maxsize":100,
                                    "maxfiles":1
                                }
                            })",
                            "--node_id=10",
                            "--ip=127.0.0.1",
                            "--d1=1",
                            "--d2=1",
                            "--meta_store_address=127.0.0.1",
                            "--k8s_base_path=" };

    functionsystem::functionmaster::Flags flags;
    flags.ParseFlags(8, argv2);

    auto metaStoreTimeoutOpt = MetaStoreTimeoutOption();
    metaStoreTimeoutOpt.operationRetryTimes = 2;
    metaStoreTimeoutOpt.grpcTimeout = 1000;

    auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }, sslConfig,
                                          metaStoreTimeoutOpt);
    auto kubeClient = KubeClient::CreateKubeClient(flags.GetK8sBasePath(), KubeClient::ClusterSslConfig("", "", false));
    auto driver = std::make_shared<ScalerDriver>(flags, client, client, kubeClient, handlers);
    EXPECT_EQ(driver->Start(), StatusCode::SUCCESS);

    (void)driver->Stop();
    driver->Await();
}

TEST_F(ScalerDriverTest, DriverStartNoPool)
{
    const char *argv[] = { "./function_master",
                           R"(--log_config={
                                "filepath":"/home/yr/log",
                                "level":"DEBUG",
                                "rolling":{
                                    "maxsize":100,
                                    "maxfiles":1
                                }
                            })",
                           "--node_id=10",
                           "--ip=127.0.0.1",
                           "--d1=1",
                           "--d2=1",
                           "--meta_store_address=127.0.0.1",
                           "--k8s_base_path=127.0.0.1:443",
                           "--skip_k8s_tls_verify=true" };

    functionsystem::functionmaster::Flags flags;
    flags.ParseFlags(9, argv);

    auto metaStoreTimeoutOpt = MetaStoreTimeoutOption();
    metaStoreTimeoutOpt.operationRetryTimes = 2;
    metaStoreTimeoutOpt.grpcTimeout = 1000;

    auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }, sslConfig,
                                          metaStoreTimeoutOpt);
    auto kubeClient = KubeClient::CreateKubeClient(flags.GetK8sBasePath(), KubeClient::ClusterSslConfig("", "", false));
    auto driver = std::make_shared<ScalerDriver>(flags, client, client, kubeClient, handlers);
    EXPECT_EQ(driver->Start(), StatusCode::SUCCESS);

    (void)driver->Stop();
    driver->Await();
}

/**
 * Feature: Start sclaer with driver
 * Description: start sclaer
 * Steps:
 * 1. set up params
 * 2. Start driver
 * 3. Stop driver
 *
 * Expectation:
 * 2. StatusCode::SUCCESS
 * 3. StatusCode::SUCCESS
 */
TEST_F(ScalerDriverTest, DriverStart)
{
    (void)litebus::os::Mkdir("/home/sn/scaler/config/");
    std::string poolsStr = R"(
    [
        {
            "name":"pool1-500-500-a",
            "poolSize":1,
            "requestCpu":"500m",
            "requestMemory":"500Mi",
            "limitCpu":"500m",
            "limitMemory":"500Mi"
        },
        {
            "name":"pool2-600-600-a",
            "poolSize":1,
            "requestCpu":"600m",
            "requestMemory":"600Mi",
            "limitCpu":"600m",
            "limitMemory":"600Mi"
        }
    ]
    )";
    Write("/home/sn/scaler/config/functionsystem-pools.json", poolsStr);

    const char *argv[] = { "./function_master",
                           R"(--log_config={
                                "filepath":"/home/yr/log",
                                "level":"DEBUG",
                                "rolling":{
                                    "maxsize":100,
                                    "maxfiles":1
                                }
                            })",
                           "--node_id=10",
                           "--ip=127.0.0.1",
                           "--d1=1",
                           "--d2=1",
                           "--meta_store_address=127.0.0.1",
                           "--k8s_base_path=127.0.0.1:443",
                           "--skip_k8s_tls_verify=true",
                           "--taint_tolerance_list=unavailable",
                           "--worker_taint_exclude_labels=node-role=edge;",
                           "--system_upgrade_watch_enable=true",
                           "--az_id=0",
                           "--system_upgrade_key=/hms-caas/edgems/upgrade-zones",
                           "--system_upgrade_address=127.0.0.1",
                           "--system_upgrade_address=10" };

    functionsystem::functionmaster::Flags flags;
    flags.ParseFlags(11, argv);

    auto metaStoreTimeoutOpt = MetaStoreTimeoutOption();
    metaStoreTimeoutOpt.operationRetryTimes = 2;
    metaStoreTimeoutOpt.grpcTimeout = 1000;

    auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }, sslConfig,
                                          metaStoreTimeoutOpt);
    auto kubeClient = KubeClient::CreateKubeClient(flags.GetK8sBasePath(), KubeClient::ClusterSslConfig("", "", false));
    auto driver = std::make_shared<ScalerDriver>(flags, client, client, kubeClient, handlers);
    EXPECT_EQ(driver->Start(), StatusCode::SUCCESS);

    (void)driver->Stop();
    driver->Await();

    (void)litebus::os::Rmdir("/home/sn/scaler");
}

}  // namespace functionsystem::scaler::test
