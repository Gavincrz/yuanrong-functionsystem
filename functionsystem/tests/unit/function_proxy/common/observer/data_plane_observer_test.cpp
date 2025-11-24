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

#include "function_proxy/common/observer/data_plane_observer/data_plane_observer.h"

#include <gtest/gtest.h>
#include <stdlib.h>

#include <list>

#include "common/constants/actor_name.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "meta_store_client/key_value/watcher.h"
#include "meta_store_client/watch_client.h"
#include "common/metrics/metrics_adapter.h"
#include "common/metrics/metrics_constants.h"
#include "common/status/status.h"
#include "common/types/instance_state.h"
#include "common/utils/meta_store_kv_operation.h"
#include "function_proxy/common/observer/observer_actor.h"
#include "function_proxy/common/state_machine/instance_context.h"
#include "mocks/mock_internal_iam.h"
#include "mocks/mock_shared_client.h"
#include "mocks/mock_shared_client_manager_proxy.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using ::testing::_;
using ::testing::Return;
class ObserverActorTest : public ::testing::Test {
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

    void SetUp() override
    {
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        litebus::Initialize("tcp://127.0.0.1:" + std::to_string(port));

        auto metaClient = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        auto metaStorageAccessor = std::make_shared<MetaStorageAccessor>(metaClient);
        observerActor_ = std::make_shared<function_proxy::ObserverActor>(
            FUNCTION_PROXY_OBSERVER_ACTOR_NAME, "nodeA", metaStorageAccessor, function_proxy::ObserverParam{});
        mockSharedClientManagerProxy_ = std::make_shared<MockSharedClientManagerProxy>();
        observerActor_->BindDataInterfaceClientManager(mockSharedClientManagerProxy_);
        function_proxy::InternalIAM::Param iamParam;
        iamParam.isEnableIAM = true;
        iamParam.credType = IAMCredType::AK_SK;
        internalIAM_ = std::make_shared<MockInternalIAM>(iamParam);
        observerActor_->BindInternalIAM(internalIAM_);
        EXPECT_CALL(*internalIAM_, IsSystemTenant).WillRepeatedly(testing::Return(false));
        litebus::Spawn(observerActor_);
        dataPlaneObserver_ = std::make_shared<function_proxy::DataPlaneObserver>(observerActor_);

        setenv("HOST_IP", "127.0.0.1", 1);
        setenv("HOSTNAME", "nodeA", 1);
    }

    void TearDown() override
    {
        litebus::Terminate(observerActor_->GetAID());
        litebus::Await(observerActor_);

        observerActor_ = nullptr;
        dataPlaneObserver_ = nullptr;
        internalIAM_ = nullptr;

        unsetenv("HOST_IP");
        unsetenv("HOSTNAME");
    };

protected:
    std::shared_ptr<function_proxy::ObserverActor> observerActor_;
    std::shared_ptr<function_proxy::DataPlaneObserver> dataPlaneObserver_;
    std::shared_ptr<MockSharedClientManagerProxy> mockSharedClientManagerProxy_;
    std::shared_ptr<MockInternalIAM> internalIAM_;
};

inline RouteInfo GenRouteInfo(const std::string &instanceID, const std::string &funcAgentID,
                              const std::string &function, InstanceState instanceStatus)
{
    RouteInfo routeInfo;
    routeInfo.set_instanceid(instanceID);
    routeInfo.mutable_instancestatus()->set_code(int32_t(instanceStatus));
    return routeInfo;
}

std::vector<WatchEvent> GetInstanceRouteEventRsp(const std::string &instanceID, const EventType &eventType,
                                                 InstanceState status = InstanceState::RUNNING,
                                                 const std::string &node = "nodeA")
{
    auto key = INSTANCE_PATH_PREFIX + "/0/function/helloWorld/version/latest/defaultaz/requestid/" + instanceID;

    resource_view::RouteInfo routeInfo = GenRouteInfo(instanceID, "functionAgentA", "123/helloworld/$latest", status);
    routeInfo.set_functionproxyid(node);

    std::string jsonStr;
    TransToJsonFromRouteInfo(jsonStr, routeInfo);
    KeyValue kv;
    kv.set_key(key);
    kv.set_value(jsonStr);
    auto event = WatchEvent{ eventType, kv, {} };

    std::vector<WatchEvent> events{ event };
    return events;
}

std::vector<WatchEvent> GetProxyEventRsp(const EventType &eventType)
{
    auto key = BUSPROXY_PATH_PREFIX + "/0/function/busproxy/version/latest/defaultaz/busproxy_a";
    auto jsonStr = R"({"node":"nodeB","aid":"busproxy_a"})";
    KeyValue kv;
    kv.set_key(key);
    kv.set_value(jsonStr);
    auto event = WatchEvent{ eventType, kv, {} };

    std::vector<WatchEvent> events{ event };
    return events;
}

TEST_F(ObserverActorTest, InstanceEvent)
{
    std::string instanceID = "instanceA";
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*mockSharedClientManagerProxy_, NewDataInterfacePosixClient(_, _, _))
        .WillOnce(Return(mockSharedClient));
    EXPECT_CALL(*mockSharedClientManagerProxy_, DeleteClient(instanceID)).WillOnce(Return(Status::OK()));

    auto instancePutRsp = GetInstanceRouteEventRsp(instanceID, EVENT_TYPE_PUT);
    observerActor_->UpdateInstanceRouteEvent(instancePutRsp, false);

    auto instance = observerActor_->GetInstanceInfoByID(instanceID).Get();
    EXPECT_TRUE(instance.IsSome());
    auto instanceDeleteRsp = GetInstanceRouteEventRsp(instanceID, EVENT_TYPE_DELETE);
    instanceDeleteRsp[0].kv.set_mod_revision(instancePutRsp[0].kv.mod_revision() + 1);
    observerActor_->UpdateInstanceRouteEvent(instanceDeleteRsp, false);
    instance = observerActor_->GetInstanceInfoByID(instanceID).Get();
    EXPECT_TRUE(instance.IsNone());
}

TEST_F(ObserverActorTest, CommonGetInstanceID)
{
    std::string input = "abc";
    auto key = INSTANCE_PATH_PREFIX + "/0/function/helloWorld/version/latest/defaultaz/requestid/" + input;
    auto output = GetInstanceID(key);
    EXPECT_EQ(output, input);

    input = "abc";
    key = INSTANCE_PATH_PREFIX + "/0/function/helloWorld/" + input;
    output = GetInstanceID(key);
    EXPECT_EQ(output, "");
}

TEST_F(ObserverActorTest, CommonGetProxyNode)
{
    std::string input = "nodeA";
    auto key = BUSPROXY_PATH_PREFIX + "/0/node/" + input;
    auto output = GetProxyNode(key);
    EXPECT_EQ(output, input);

    key = BUSPROXY_PATH_PREFIX + "/0/" + input;
    output = GetProxyNode(key);
    EXPECT_EQ(output, "");
}

TEST_F(ObserverActorTest, SetInstanceBillingContext)
{
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetEnabledInstruments({functionsystem::metrics::YRInstrument::YR_INSTANCE_RUNNING_DURATION});
    std::string key1 = "/sn/instance/business/yrk/tenant/1/function/0-system-faasExecutorPython3.9/version/$latest/defaultaz/req1/ins001";
    auto jsonStr1 = R"({"instanceID":"ins001","functionProxyID":"nodeA", "scheduleOption":{"extension":{"YR_Metrics":"{\"endpoint\":\"127.0.0.1\"}"}}, "instanceStatus":{"code":3,"msg":"running"}})";
    KeyValue kv1;
    kv1.set_key(key1);
    kv1.set_value(jsonStr1);
    KeyValue prevKv1;
    auto event1 = WatchEvent{ EventType::EVENT_TYPE_PUT, kv1, prevKv1 };

    std::string key2 = "/sn/instance/business/yrk/tenant/2/function/0-system-faasExecutorPython3.9/version/$latest/defaultaz/req2/ins002";
    auto jsonStr = R"({"instanceID":"ins002","functionProxyID":"nodeB", "instanceStatus":{"code":0,"msg":"new"}})";
    KeyValue kv2;
    kv2.set_key(key2);
    kv2.set_value(jsonStr);
    KeyValue prevKv2;
    auto event2 = WatchEvent{ EventType::EVENT_TYPE_PUT, kv2, prevKv2 };

    std::string key3 = "/sn/instance/business/yrk/tenant/1/function/0-system-faasExecutorPython3.9/version/$latest/defaultaz/req3/ins003";
    auto jsonStr3 =
        R"({"instanceID":"ins003","functionProxyID":"nodeA", "scheduleOption":{"extension":{"YR_Metrics":"{\"endpoint\":\"127.0.0.1\"}"}}, "instanceStatus":{"code":4,"msg":"failed"}})";
    KeyValue kv3;
    kv3.set_mod_revision(1);
    kv3.set_key(key3);
    kv3.set_value(jsonStr3);
    KeyValue prevKv3;
    auto event3 = WatchEvent{ EventType::EVENT_TYPE_PUT, kv3, prevKv3 };

    kv3.set_mod_revision(0);
    auto event4 = WatchEvent{ EventType::EVENT_TYPE_PUT, kv3, prevKv3 };
    std::vector<WatchEvent> events = { event1, event2, event3, event4 };

    observerActor_->UpdateInstanceEvent(events, true);
    auto billingInstanceMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInstanceMap();
    auto extraBillingInstanceMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetExtraBillingInstanceMap();
    auto it1 = billingInstanceMap.find("ins001");
    auto it2 = billingInstanceMap.find("ins002");
    auto it3 = billingInstanceMap.find("ins003");
    auto extraIt1 = extraBillingInstanceMap.find("ins001");
    auto extraIt2 = extraBillingInstanceMap.find("ins002");
    auto extraIt3 = extraBillingInstanceMap.find("ins003");
    EXPECT_TRUE(it1 != billingInstanceMap.end());
    EXPECT_TRUE(it1->second.customCreateOption.find("endpoint")->second == "127.0.0.1");
    EXPECT_TRUE(it2 == billingInstanceMap.end());
    EXPECT_TRUE(it3 == billingInstanceMap.end());

    EXPECT_TRUE(extraIt1 != extraBillingInstanceMap.end());
    EXPECT_TRUE(extraIt1->second.lastReportTimeMillis == 0);
    EXPECT_TRUE(extraIt1->second.endTimeMillis != 0);
    EXPECT_TRUE(extraIt2 == extraBillingInstanceMap.end());
    EXPECT_TRUE(extraIt3 != extraBillingInstanceMap.end());

    EXPECT_NE(observerActor_->instanceModRevisionMap_.find("ins001"), observerActor_->instanceModRevisionMap_.end());
    EXPECT_EQ(observerActor_->instanceModRevisionMap_.find("ins001")->second, 0);
    EXPECT_NE(observerActor_->instanceModRevisionMap_.find("ins002"), observerActor_->instanceModRevisionMap_.end());
    EXPECT_EQ(observerActor_->instanceModRevisionMap_.find("ins002")->second, 0);
    EXPECT_NE(observerActor_->instanceModRevisionMap_.find("ins003"), observerActor_->instanceModRevisionMap_.end());
    EXPECT_EQ(observerActor_->instanceModRevisionMap_.find("ins003")->second, 1);
}
}  // namespace functionsystem::test