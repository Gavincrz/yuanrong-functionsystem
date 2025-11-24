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

#include "common/metrics/metrics_adapter.h"

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>

#include "common/resource_view/view_utils.h"
#include "meta_store_client/meta_store_struct.h"
#include "metrics/api/provider.h"
#define private public
#include "runtime_manager/metrics/metrics_actor.h"

namespace functionsystem::test {
namespace metrics_ = functionsystem::metrics;
using namespace functionsystem::metrics;
namespace MetricsApi = observability::api::metrics;

const std::string PromExporterJsonStr = R"(
{
    "enabledMetrics": ["yr_instance_running_duration", "yr_app_instance_billing_invoke_latency", "yr_pod_resource", "yr_instance_memory_usage", "yr_node_cpu_usage", "yr_node_npu", "yr_instance_npu"],
    "backends": [
        {
            "immediatelyExport": {
                "name": "Scenario",
                "enable": true,
                "exporters": [
                    {
                        "prometheusPushExporter": {
                        }
                    }
                ]
            }
        }
    ]
}
    )";

const std::string FileExporterJsonStr = R"(
{
    "backends": [
        {
            "immediatelyExport": {
                "name": "file",
                "enable": true,
                "exporters": [
                    {
                        "fileExporter": {
                            "enable": true,
                            "fileDir": "/tmp/",
                            "rolling": {
                                "enable": true,
                                "maxFiles": 3,
                                "maxSize": 10000
                            },
                            "contentType": "STANDARD"
                        }
                    }
                ]
            }
        }
    ]
}
    )";

const std::string AlarmExporterJsonStr = R"(
{
    "enabledMetrics": ["yr_k8s_alarm", "yr_proxy_alarm", "fake_metrics", "yr_election_alarm", "yr_etcd_alarm", "yr_metastore_alarm"],
    "backends": [
        {
            "immediatelyExport": {
                "name": "Alarm",
                "enable": true,
                "custom": {
                    "labels": {
                        "site": "",
                        "tenant_id": "",
                        "application_id": "",
                        "service_id": ""
                    }
                },
                "exporters": [
                    {
                        "fileExporter": {
                            "enable": true,
                            "enabledInstruments": ["yr_etcd_alarm", "yr_election_alarm"],
                            "fileDir": "/tmp/",
                            "rolling": {
                                "enable": true,
                                "maxFiles": 3,
                                "maxSize": 10
                            },
                            "contentType": "LABELS"
                        }
                    }
                ]
            }
        }
    ]
}
    )";

class TestMetricsActor : public runtime_manager::MetricsActor {
public:
    TestMetricsActor() : runtime_manager::MetricsActor("test_metrics_actor")
    {
    }


    resources::ResourceUnit BuildResourceUnit(const std::vector<litebus::Future<runtime_manager::Metrics>> &metricses)
    {
        return runtime_manager::MetricsActor::BuildResourceUnit(metricses);
    }
};

class MetricsAdapterTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
    }

    void SetUp()
    {
        metricsActor_ = std::make_shared<TestMetricsActor>();
        litebus::Spawn(metricsActor_);
    }

    void TearDown()
    {
        MetricsAdapter::GetInstance().GetMetricsContext().SetEnabledInstruments({});
        MetricsAdapter::GetInstance().ClearEnabledInstruments();
        MetricsAdapter::GetInstance().GetMetricsContext().EraseBillingInstance();
        MetricsAdapter::GetInstance().GetMetricsContext().EraseExtraBillingInstance();
        MetricsAdapter::GetInstance().GetMetricsContext().SetAttr("component_name", "");
        MetricsAdapter::GetInstance().GetMetricsContext().ErasePodResource();
        litebus::Terminate(metricsActor_->GetAID());
        litebus::Await(metricsActor_->GetAID());
    }

    std::shared_ptr<TestMetricsActor> metricsActor_;
};

TEST_F(MetricsAdapterTest, NrGaugeInstrument)
{
    struct functionsystem::metrics::MeterTitle title {
        "NrGaugeInstrument", "normal gauge instrument", ""
    };
    struct functionsystem::metrics::MeterData data {
        111111111.1,
    };
    EXPECT_NO_THROW(metrics_::MetricsAdapter::GetInstance().ReportGauge(title, data));
}

std::string GetMetricsFilesName(const std::string &backendName)
{
    return "nodeID-componentName-metrics.data";
}

TEST_F(MetricsAdapterTest, InitImmediatelyMetricsFromJson)
{
    auto nullMeterProvider = MetricsApi::Provider::GetMeterProvider();
    const std::string jsonStr = R"(
{
    "backends": [
        {
            "immediatelyExport": {
                "name": "Alarm",
                "enable": true,
                "custom": {
                    "labels": {
                        "site": "",
                        "tenant_id": "",
                        "application_id": "",
                        "service_id": ""
                    }
                },
                "exporters": [
                    {
                        "fileExporter": {
                            "enable": true,
                            "fileDir": "/home/sn/metrics/",
                            "rolling": {
                                "enable": true,
                                "maxFiles": 3,
                                "maxSize": 10000
                            },
                            "contentType": "LABELS"
                        }
                    }
                ]
            }
        }
    ]
}
    )";
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(jsonStr), GetMetricsFilesName, {});
    EXPECT_NE(MetricsApi::Provider::GetMeterProvider(), nullMeterProvider);
    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
}

TEST_F(MetricsAdapterTest, InitBatchMetricsFromJson)
{
    auto nullMeterProvider = MetricsApi::Provider::GetMeterProvider();
    const std::string jsonStr = R"(
{
    "backends": [
        {
            "batchExport": {
                "name": "Alarm",
                "enable": true,
                "custom": {
                    "labels": {
                        "site": "",
                        "tenant_id": "",
                        "application_id": "",
                        "service_id": ""
                    }
                },
                "exporters": [
                    {
                        "fileExporter": {
                            "enable": true,
                            "batchSize": 2,
                            "batchIntervalSec": 10,
                            "failureQueueMaxSize": 2,
                            "failureDataDir": "/home/sn/metrics/failure",
                            "failureDataFileMaxCapacity": 1,
                            "initConfig": {
                                "fileDir": "",
                                "rolling": {
                                    "enable": true,
                                    "maxFiles": 3,
                                    "maxSize": 10000
                                },
                                "contentType": "STANDARD"
                            }
                        }
                    }, {
                        "invalidExporter": {
                        }
                    }
                ]
            }
        }
    ]
}
    )";
    MetricsAdapter::GetInstance().SetContextAttr("component_name", "function_proxy");
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(jsonStr), GetMetricsFilesName, {});
    EXPECT_NE(MetricsApi::Provider::GetMeterProvider(), nullMeterProvider);
    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
    EXPECT_EQ(MetricsAdapter::GetInstance().GetMetricsContext().GetAttr("component_name"), "function_proxy");
}

TEST_F(MetricsAdapterTest, InvalidBackEndKey)
{
    auto nullMeterProvider = MetricsApi::Provider::GetMeterProvider();
    const std::string jsonStr = R"(
{
    "invalid": []
}
    )";
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(jsonStr), GetMetricsFilesName, {});
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullMeterProvider);
}

TEST_F(MetricsAdapterTest, EtcdFiring)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(AlarmExporterJsonStr),
                                                      GetMetricsFilesName, {});
    MetricsAdapter::GetInstance().GetMetricsContext().SetAttr("component_name", "function_master");

    // resolve etcd firing when no alarm exists
    MetricsAdapter::GetInstance().StorageBackendUnhealthyResolved(AlarmLevel::CRITICAL, ETCD_BACKEND);
    auto alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::ETCD_ALARM) == alarmMap.end());
    auto alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongEtcdConnection00001") == alarmInfoMap.end());

    // unhealthy etcd firing
    MetricsAdapter::GetInstance().StorageBackendUnhealthyFiring(AlarmLevel::CRITICAL, "firing", ETCD_BACKEND);
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::ETCD_ALARM) != alarmMap.end());
    alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongEtcdConnection00001") != alarmInfoMap.end());
    auto etcdAlarmInfo = alarmInfoMap.find("YuanrongEtcdConnection00001")->second;
    EXPECT_EQ(etcdAlarmInfo.endsAt, 0);

    // resolve etcd firing
    MetricsAdapter::GetInstance().StorageBackendUnhealthyResolved(AlarmLevel::CRITICAL, ETCD_BACKEND);
    alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongEtcdConnection00001") == alarmInfoMap.end());

    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
}

TEST_F(MetricsAdapterTest, MetastoreFiring)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(AlarmExporterJsonStr),
                                                      GetMetricsFilesName, {});
    MetricsAdapter::GetInstance().GetMetricsContext().SetAttr("component_name", "function_proxy");

    // resolve metastore firing when no alarm exists
    auto alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::METASTORE_ALARM) == alarmMap.end());
    MetricsAdapter::GetInstance().StorageBackendUnhealthyResolved(AlarmLevel::CRITICAL, METASTORE_BACKEND);
    auto alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongMetastoreConnection00001") == alarmInfoMap.end());

    // unhealthy metastore firing
    MetricsAdapter::GetInstance().StorageBackendUnhealthyFiring(AlarmLevel::CRITICAL, "firing", METASTORE_BACKEND);
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::METASTORE_ALARM) != alarmMap.end());
    alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongMetastoreConnection00001") != alarmInfoMap.end());
    auto metastoreAlarmInfo = alarmInfoMap.find("YuanrongMetastoreConnection00001")->second;
    EXPECT_EQ(metastoreAlarmInfo.endsAt, 0);

    // resolve metastore firing
    MetricsAdapter::GetInstance().StorageBackendUnhealthyResolved(AlarmLevel::CRITICAL, METASTORE_BACKEND);
    alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongMetastoreConnection00001") == alarmInfoMap.end());

    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
}

TEST_F(MetricsAdapterTest, ElectionAlarmFiring)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(AlarmExporterJsonStr),
                                                      GetMetricsFilesName, {});
    MetricsAdapter::GetInstance().GetMetricsContext().SetAttr("component_name", "function_master");

    // resolve election firing when no alarm exists
    MetricsAdapter::GetInstance().ElectionFiringResolved(
        "leader 10.91.2.10 is elected successfully for /yr/leader/function-master");
    auto alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::ELECTION_ALARM) == alarmMap.end());
    auto alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongMasterElection00001") == alarmInfoMap.end());

    // election firing
    MetricsAdapter::GetInstance().ElectionFiring("No leader elected for /yr/leader/function-master");
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::ELECTION_ALARM) != alarmMap.end());
    alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongMasterElection00001") != alarmInfoMap.end());
    auto etcdAlarmInfo = alarmInfoMap.find("YuanrongMasterElection00001")->second;
    EXPECT_EQ(etcdAlarmInfo.endsAt, 0);

    // resolve election firing
    MetricsAdapter::GetInstance().ElectionFiringResolved(
        "leader 10.91.2.10 is elected successfully for /yr/leader/function-master");
    alarmInfoMap = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetAlarmInfoMap();
    EXPECT_TRUE(alarmInfoMap.find("YuanrongMasterElection00001") == alarmInfoMap.end());

    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
}

TEST_F(MetricsAdapterTest, DoubleGauge)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(FileExporterJsonStr),
                                                      GetMetricsFilesName, {});
    MeterData data;
    data.value = 1.0f;
    data.labels["label_key"] = "label_value";
    MetricsAdapter::GetInstance().ReportGauge({ "name", "description", "unit" }, data);

    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
}

TEST_F(MetricsAdapterTest, ReportClusterSourceState)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(FileExporterJsonStr),
                                                      GetMetricsFilesName, {});
    std::shared_ptr<resource_view::ResourceUnit> unit = std::make_shared<resource_view::ResourceUnit>();
    resources::Resource res;
    res.mutable_scalar()->set_value(1.0);
    (*unit->mutable_capacity()->mutable_resources())[functionsystem::resource_view::CPU_RESOURCE_NAME] = res;
    (*unit->mutable_allocatable()->mutable_resources())[functionsystem::resource_view::CPU_RESOURCE_NAME] = res;
    (*unit->mutable_capacity()->mutable_resources())[functionsystem::resource_view::MEMORY_RESOURCE_NAME] = res;
    (*unit->mutable_allocatable()->mutable_resources())[functionsystem::resource_view::MEMORY_RESOURCE_NAME] = res;
    MetricsAdapter::GetInstance().ReportClusterSourceState(unit);

    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
}

TEST_F(MetricsAdapterTest, ReportBillingInvokeLatency)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr),
                                                      GetMetricsFilesName, {});
    std::string requestID = "test_request_id";
    std::string functionName = "function_name_001";
    std::string instanceID = "instance_id";
    std::map<std::string, std::string> invokeOptMap = { { "endpoint", "endpoint_val" } };
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingInvokeOptions(requestID, invokeOptMap, functionName,
                                                                              instanceID);
    NodeLabelsType nodeLabelsMap = { { "label1", { { "label_key1_1", 1 }, { "label_key1_2", 2 } } },
                                     { "label2", { { "label_key2_1", 11 }, { "label_key2_2", 22 } } } };
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingNodeLabels(instanceID, nodeLabelsMap);
    std::string cpuType = "Intel(R) Xeon(R) Gold 6161 CPU @ 2.20GHz";
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingCpuType(instanceID, cpuType);
    std::map<std::string, std::string> schedulingExtensions = { { "app_name", "yr_test" }, { "tenet_id", "tenet_01" } };

    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingSchedulingExtensions(schedulingExtensions, instanceID);
    auto billingInvokeOption = MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInvokeOption(requestID);
    EXPECT_EQ(billingInvokeOption.invokeOptions, invokeOptMap);

    auto billingFunctionOption =
        MetricsAdapter::GetInstance().GetMetricsContext().GetBillingFunctionOption(instanceID);
    EXPECT_EQ(billingFunctionOption.nodeLabels, nodeLabelsMap);
    EXPECT_EQ(billingFunctionOption.cpuType, cpuType);
    EXPECT_EQ(billingFunctionOption.schedulingExtensions, schedulingExtensions);

    MetricsAdapter::GetInstance().ReportBillingInvokeLatency(requestID, 0, 100000, 100005);

    MetricsAdapter::GetInstance().GetMetricsContext().EraseBillingFunctionOptionItem(instanceID);
    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
}

TEST_F(MetricsAdapterTest, ReportBillingInvokeLatencyNonEnabled)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(AlarmExporterJsonStr),
                                                      GetMetricsFilesName, {});
    auto enabledInstruments = MetricsAdapter::GetInstance().GetMetricsContext().GetEnabledInstruments();
    EXPECT_TRUE(enabledInstruments.find(YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY) == enabledInstruments.end());

    std::string requestID = "test_request_id";
    std::string functionName = "function_name_001";
    std::string instanceID = "instance_id";
    std::map<std::string, std::string> invokeOptMap = { { "endpoint", "endpoint_val" } };
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingInvokeOptions(requestID, invokeOptMap, functionName, instanceID);
    EXPECT_EQ(MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInvokeOptionsMap().size(),
        static_cast<long unsigned int>(0));

    MetricsAdapter::GetInstance().CleanMetrics();
    EXPECT_EQ(MetricsApi::Provider::GetMeterProvider(), nullptr);
}

TEST_F(MetricsAdapterTest, RegisterPhysicalCPUUtilization)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr),
                                                      GetMetricsFilesName, {});
    std::string agentID = "agent001";
    std::string nodeID = "node1ID";
    // register observable instrument
    MetricsAdapter::GetInstance().RegisterPhysicalNodeCPUUtilization();
    auto observableInstrumentMap = MetricsAdapter::GetInstance().GetObservableInstrumentMap();
    EXPECT_TRUE(observableInstrumentMap.find("yr_node_cpu_usage") != observableInstrumentMap.end());

    // init cpu metrics info
    auto given = std::vector<litebus::Future<runtime_manager::Metrics>>{
            {runtime_manager::Metrics{26.3512, 0.0, {}, {}, runtime_manager::metrics_type::CPU}}
    };
    auto resourceUnit = metricsActor_->BuildUpdateMetricsRequest(given);
    messages::UpdateResourcesRequest req;
    EXPECT_TRUE(req.ParseFromString(resourceUnit) == 1);
    MetricsAdapter::GetInstance().GetMetricsContext().SetPhysicalMetrics(agentID, nodeID, req.resourceunit());

    auto obRes = std::make_shared<MetricsApi::ObserveResultT<double>>();
    MetricsAdapter::GetInstance().CollectGeneralMetricsNodeData(obRes, "CPU", true, {});
    auto observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), static_cast<long unsigned int>(1));
    EXPECT_NEAR(observedVal[0].second, 26.3512, 0.0009);
    auto labels = observedVal[0].first;
    std::string testAgentID = "";
    std::string testNodeID = "";
    for (auto it : labels) {
        if (it.first == "agent_id") {
            testAgentID = it.second;
        } else {
            testNodeID = it.second;
        }
    }
    EXPECT_EQ(testAgentID, "agent001");
    EXPECT_EQ(testNodeID, "node1ID");
}

TEST_F(MetricsAdapterTest, RegisterInstNPUUsage)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr),
                                                          GetMetricsFilesName, {});
    std::string agentID = "agent001";
    std::string nodeID = "node1ID";
    metricsActor_->nodeID = nodeID;
    // register observable instrument
    MetricsAdapter::GetInstance().RegisterInstNPUUsage();
    auto observableInstrumentMap = MetricsAdapter::GetInstance().GetObservableInstrumentMap();
    EXPECT_TRUE(observableInstrumentMap.find("yr_instance_npu") != observableInstrumentMap.end());

    runtime_manager::DevClusterMetrics npuMetrics{ "a1b2c3d4",
                                                   1,
                                                   { { "product_model", "Ascend310" } },
                                                   {
                                                       { "ids", { 0 } },
                                                       { "used_ids", { 0 } },
                                                       { "usedMemory", {} },
                                                         { "memory", {} },
                                                        { "HBM", { 100} },
                                                       { "usedHBM", { 50 } } } };
    auto given = std::vector<litebus::Future<runtime_manager::Metrics>>{ { runtime_manager::Metrics{
        1.0,
        0.0,
        {"d15b0853-d008-4000-8000-0000a80934ef"},
        {},
        runtime_manager::metrics_type::NPU,
        runtime_manager::collector_type::INSTANCE,
        { npuMetrics } } } };
    auto resourceUnitStr = metricsActor_->BuildUpdateMetricsRequest(given);
    messages::UpdateResourcesRequest req;
    EXPECT_TRUE(req.ParseFromString(resourceUnitStr) == 1);

    auto resourceUnit = req.mutable_resourceunit();
    resources::Value::Counter cnter;
    cnter.mutable_items()->insert({ nodeID, 1 });
    resourceUnit->mutable_nodelabels()->insert({ "NODE_ID", cnter });

    MetricsAdapter::GetInstance().GetMetricsContext().InitInstanceMetrics(req);
    auto obRes = std::make_shared<MetricsApi::ObserveResultT<double>>();
    MetricsAdapter::GetInstance().CollectInstanceMetrics(obRes, "NPU");
    auto observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), static_cast<long unsigned int>(1));
    auto labels = observedVal[0].first;
    std::string expectedDetail = R"({"0":{"capacity":100.0,"type":"Ascend310","used":50.0}})";
    for (auto it : labels) {
        if (it.first == "details") {
            EXPECT_EQ(it.second, expectedDetail);
            break;
        }
    }
}

TEST_F(MetricsAdapterTest, RegisterPhysicalNodeNpu)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr), GetMetricsFilesName,
                                                      {});
    std::string agentID = "agent001";
    std::string nodeID = "node1ID";
    metricsActor_->nodeID = nodeID;
    YRLOG_INFO("nodeID in metricsActor is {}", metricsActor_->nodeID);
    MetricsAdapter::GetInstance().RegisterPhysicalNodeNpu();
    auto observableInstrumentMap = MetricsAdapter::GetInstance().GetObservableInstrumentMap();
    EXPECT_TRUE(observableInstrumentMap.find("yr_node_npu") != observableInstrumentMap.end());

    runtime_manager::DevClusterMetrics npuMetrics{ "a1b2c3d4",
                                                   1,
                                                   { { "product_model", "Ascend310" } },
                                                   { { "HBM", { 100 } },
                                                       { "ids", { 0 } },
                                                       { "usedHBM", { 50 } } } };
    auto given = std::vector<litebus::Future<runtime_manager::Metrics>>{ { runtime_manager::Metrics{
        1.0,
        1.0,
        {},
        {},
        runtime_manager::metrics_type::NPU,
        runtime_manager::collector_type::NODE,
        { npuMetrics } } } };

    auto resourceUnit = metricsActor_->BuildUpdateMetricsRequest(given);
    messages::UpdateResourcesRequest req;
    EXPECT_TRUE(req.ParseFromString(resourceUnit) == 1);
    MetricsAdapter::GetInstance().GetMetricsContext().SetPhysicalMetrics(agentID, nodeID, req.resourceunit());

    auto obRes = std::make_shared<MetricsApi::ObserveResultT<double>>();
    MetricsAdapter::GetInstance().CollectPhysicalNodeNPUData(obRes);
    auto observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), static_cast<long unsigned int>(1));
    auto labels = observedVal[0].first;
    std::string expectedDetail = R"({"0":{"capacity":100.0,"type":"Ascend310","used":50.0}})";
    for (auto it : labels) {
        if (it.first == "details") {
            EXPECT_EQ(it.second, expectedDetail);
            break;
        }
    }
}

TEST_F(MetricsAdapterTest, RegisterBillingInstanceRunningDuration)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr),
                                                      GetMetricsFilesName, {});
    std::string requestID = "test_request_id";
    std::string functionName = "function_name_001";
    std::string instanceID = "instance_id";
    std::string agentId = "agent001";
    std::map<std::string, std::string> createOptions = { {"app_name", "testApp"}, {"endpoint", "127.0.0.1"} };

    // register observable instrument
    MetricsAdapter::GetInstance().RegisterBillingInstanceRunningDuration();
    auto observableInstrumentMap = MetricsAdapter::GetInstance().GetObservableInstrumentMap();
    EXPECT_TRUE(observableInstrumentMap.find("yr_instance_running_duration") != observableInstrumentMap.end());

    // init instance info
    MetricsAdapter::GetInstance().GetMetricsContext().InitBillingInstance(instanceID, agentId, createOptions);
    auto startTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto billingInstanceMap = MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInstanceMap();
    auto billingInstance = billingInstanceMap.find(instanceID);
    EXPECT_TRUE(billingInstance != billingInstanceMap.end());
    EXPECT_TRUE(billingInstance->second.customCreateOption.find("app_name")->second == "testApp");
    EXPECT_TRUE(billingInstance->second.customCreateOption.find("endpoint")->second == "127.0.0.1");

    // init extra instance info
    auto endTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    MetricsAdapter::GetInstance().GetMetricsContext().InitExtraBillingInstance(instanceID, agentId, createOptions);
    auto extraBillingInstanceMap = MetricsAdapter::GetInstance().GetMetricsContext().GetExtraBillingInstanceMap();
    auto extraBillingInstance = extraBillingInstanceMap.find(instanceID);
    EXPECT_TRUE(extraBillingInstance != extraBillingInstanceMap.end());
    EXPECT_TRUE(extraBillingInstance->second.endTimeMillis >= endTimeMillis);
    EXPECT_TRUE(extraBillingInstance->second.lastReportTimeMillis == 0);
    EXPECT_TRUE(extraBillingInstance->second.startTimeMillis == 0);

    std::string cpuType = "Intel(R) Xeon(R) Gold 6161 CPU @ 2.20GHz";
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingCpuType(instanceID, cpuType);

    // collect first time
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto reportTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto obRes = std::make_shared<MetricsApi::ObserveResultT<uint64_t>>();
    MetricsAdapter::GetInstance().CollectBillingInstanceRunningDuration(obRes);
    auto observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), static_cast<long unsigned int>(2));
    EXPECT_TRUE(observedVal[0].second >= static_cast<long unsigned int>(reportTimeMillis - startTimeMillis));
    EXPECT_TRUE(observedVal[1].second >= static_cast<long unsigned int>(endTimeMillis));
    auto labels = observedVal[0].first;
    std::string cpuTypeSet = "";
    std::string poolLabel = "";
    for (auto it : labels) {
        if (it.first == "cpu_type") {
            cpuTypeSet = it.second;
        } else if (it.first == "pool_label") {
            poolLabel = it.second;
        }
    }
    EXPECT_EQ(cpuTypeSet, cpuType);
    EXPECT_EQ(poolLabel, "[]");
    extraBillingInstanceMap = MetricsAdapter::GetInstance().GetMetricsContext().GetExtraBillingInstanceMap();
    extraBillingInstance = extraBillingInstanceMap.find(instanceID);
    EXPECT_TRUE(extraBillingInstance == extraBillingInstanceMap.end());

    auto unit = view_utils::Get1DResourceUnit(agentId);
    ::resources::Value::Counter cnter;
    cnter.mutable_items()->insert({ "lable1", 1 });
    cnter.mutable_items()->insert({ "lable2", 2 });
    unit.mutable_nodelabels()->insert({ "nodelabel1", cnter });
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetPodResource(agentId, unit);

    // collect second time
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingInstanceReportTime(instanceID, reportTimeMillis - 10);
    MetricsAdapter::GetInstance().CollectBillingInstanceRunningDuration(obRes);
    observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), static_cast<long unsigned int>(1));
    EXPECT_TRUE(observedVal[0].second >= 10);
    labels = observedVal[0].first;
    poolLabel = "";
    for (auto it : labels) {
        if (it.first == "pool_label") {
            poolLabel = it.second;
        }
    }
    EXPECT_EQ(poolLabel, "[\"nodelabel1:lable1\",\"nodelabel1:lable2\"]");

    //set end time
    auto billingInstanceInfo = MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInstance(instanceID);
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingInstanceEndTime(instanceID, billingInstanceInfo.lastReportTimeMillis + 10);
    MetricsAdapter::GetInstance().CollectBillingInstanceRunningDuration(obRes);
    observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), static_cast<long unsigned int>(1));
    EXPECT_TRUE(observedVal[0].second >= 10);

    // instance is cleared, no observable res
    MetricsAdapter::GetInstance().CollectBillingInstanceRunningDuration(obRes);
    observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), 0);
    auto billingFunctionOptionMap = MetricsAdapter::GetInstance().GetMetricsContext().GetBillingFunctionOptionsMap();
    EXPECT_TRUE(billingFunctionOptionMap.find(instanceID) == billingFunctionOptionMap.end());
}

TEST_F(MetricsAdapterTest, RegisterInstanceMetrics)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr),
                                                  GetMetricsFilesName, {});
    messages::UpdateResourcesRequest req;

    auto resourceUnit = req.mutable_resourceunit();
    std::string agentId = "randomFuncAgentName_function-agent-pool08012-869f9697f8-7h8bj";
    resourceUnit->set_id(agentId);

    auto instances = resourceUnit->mutable_instances();

    resources::InstanceInfo instanceInfo1;
    auto instanceId = "d15b0853-d008-4000-8000-0000a80934ef";
    instanceInfo1.set_instanceid(instanceId);

    auto actualUse = instanceInfo1.mutable_actualuse()->mutable_resources();
    resources::Resource setMemory;
    setMemory.set_name("Memory");
    auto memoryScalar = setMemory.mutable_scalar();
    double setMemoryValue = 111.99609375;
    memoryScalar->set_value(setMemoryValue);
    actualUse->insert({ "Memory", setMemory });

    instances->insert({ instanceId, instanceInfo1 });

    resources::Value::Counter cnter;
    auto nodeId = "dggphispra26945";
    cnter.mutable_items()->insert({ nodeId, 1 });
    resourceUnit->mutable_nodelabels()->insert({ "NODE_ID", cnter });

    MetricsAdapter::GetInstance().GetMetricsContext().InitInstanceMetrics(req);
    auto instanceMetricsMap = MetricsAdapter::GetInstance().GetMetricsContext().GetInstanceMetricsMap();
    auto instanceInfoPair = instanceMetricsMap.find(instanceId);
    EXPECT_TRUE(instanceInfoPair != instanceMetricsMap.end());
    EXPECT_EQ(instanceInfoPair->second.instanceId, instanceId);
    EXPECT_EQ(instanceInfoPair->second.agentId, agentId);
    EXPECT_TRUE(instanceInfoPair->second.nodeId == nodeId);
    auto actualMemory = instanceInfoPair->second.resources.resources().at("Memory").scalar().value();
    EXPECT_TRUE(actualMemory == setMemoryValue);

    MetricsAdapter::GetInstance().RegisterInstMemoryUsage();
    auto observableInstrumentMap = MetricsAdapter::GetInstance().GetObservableInstrumentMap();
    EXPECT_TRUE(observableInstrumentMap.find("yr_instance_memory_usage") != observableInstrumentMap.end());

    auto obRes = std::make_shared<MetricsApi::ObserveResultT<double>>();
    MetricsAdapter::GetInstance().CollectInstanceMetrics(obRes, "Memory");
    for (auto value : obRes->Value()) {
        auto iter = value.first.front();
        if (iter.first == "instance_id") {
            EXPECT_EQ(iter.second, instanceId);
            break;
        }
    }
}

TEST_F(MetricsAdapterTest, InvalidBillingInstanceRunningDuration)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr),
                                                      GetMetricsFilesName, {});
    std::string instanceID = "instance_id";
    std::map<std::string, std::string> createOptions = { {"app_name", "testApp"}, {"endpoint", "127.0.0.1"} };

    // register observable instrument
    MetricsAdapter::GetInstance().RegisterBillingInstanceRunningDuration();
    auto observableInstrumentMap = MetricsAdapter::GetInstance().GetObservableInstrumentMap();
    EXPECT_TRUE(observableInstrumentMap.find("yr_instance_running_duration") != observableInstrumentMap.end());

    // init instance info
    MetricsAdapter::GetInstance().GetMetricsContext().InitBillingInstance(instanceID, "", createOptions);
    auto startTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto billingInstanceMap = MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInstanceMap();
    auto billingInstance = billingInstanceMap.find(instanceID);
    EXPECT_TRUE(billingInstance != billingInstanceMap.end());
    EXPECT_TRUE(billingInstance->second.customCreateOption.find("app_name")->second == "testApp");
    EXPECT_TRUE(billingInstance->second.customCreateOption.find("endpoint")->second == "127.0.0.1");

    // collect first time
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto reportTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto obRes = std::make_shared<MetricsApi::ObserveResultT<uint64_t>>();
    MetricsAdapter::GetInstance().CollectBillingInstanceRunningDuration(obRes);
    auto observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), static_cast<long unsigned int>(1));
    EXPECT_TRUE(observedVal[0].second >= static_cast<long unsigned int>(reportTimeMillis - startTimeMillis));

    // collect second time when endTime <= lastReportTime
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingInstanceReportTime(instanceID, reportTimeMillis + 10);
    MetricsAdapter::GetInstance().CollectBillingInstanceRunningDuration(obRes);
    observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), 0);
    EXPECT_EQ(MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInstanceMap().size(), 0);
}

TEST_F(MetricsAdapterTest, SystemInstanceRunningDuartion)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr),
                                                      GetMetricsFilesName, {});

    std::string instanceID = "instance_id";
    std::map<std::string, std::string> createOptions = { {"app_name", "testApp"}, {"endpoint", "127.0.0.1"}};

    // register observable instrument
    MetricsAdapter::GetInstance().RegisterBillingInstanceRunningDuration();
    auto observableInstrumentMap = MetricsAdapter::GetInstance().GetObservableInstrumentMap();
    EXPECT_TRUE(observableInstrumentMap.find("yr_instance_running_duration") != observableInstrumentMap.end());

    // init instance info
    MetricsAdapter::GetInstance().GetMetricsContext().InitBillingInstance(instanceID, "", createOptions, true);
    auto billingInstanceMap = MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInstanceMap();
    EXPECT_TRUE(billingInstanceMap.find(instanceID) == billingInstanceMap.end());

    // init instance info
    MetricsAdapter::GetInstance().GetMetricsContext().InitExtraBillingInstance(instanceID, "", createOptions, true);
    auto extraBillingInstanceMap = MetricsAdapter::GetInstance().GetMetricsContext().GetExtraBillingInstanceMap();
    EXPECT_TRUE(extraBillingInstanceMap.find(instanceID) == extraBillingInstanceMap.end());

    // collect
    auto obRes = std::make_shared<MetricsApi::ObserveResultT<uint64_t>>();
    MetricsAdapter::GetInstance().CollectBillingInstanceRunningDuration(obRes);
    auto observedVal = obRes->Value();
    EXPECT_EQ(observedVal.size(), 0);
}

TEST_F(MetricsAdapterTest, ReportBillingInvokeLantencySystemFunction)
{
    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr), 
                                                      GetMetricsFilesName, {});
    std::string requestID = "test_request_id";
    std::string functionName = "0-system-faasmanager";
    std::string instanceID = "instance_id";
    std::map<std::string, std::string> invokeOptMap = { { "endpoint", "endpoint_val" } };
    MetricsAdapter::GetInstance().GetMetricsContext().SetBillingInvokeOptions(requestID, invokeOptMap, functionName,
                                                                              instanceID);

    MetricsAdapter::GetInstance().ReportBillingInvokeLatency(requestID, 0, 100000, 100005);
    auto billingInvokeOption = MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInvokeOption(requestID);
    EXPECT_EQ(billingInvokeOption.functionName, functionName);
}

TEST_F(MetricsAdapterTest, ReportInstanceStatus)
{
    std::string PrometheusExporterJson = R"(
{
  "backends": [
    {
      "batchExport": {
        "name": "Prometheus",
        "enable": true,
        "exporters": [
          {
            "prometheusPushExporter": {
              "enable": true,
              "batchSize": 20,
              "batchIntervalSec": 10,
              "failureQueueMaxSize": 100,
              "failureDataDir": "/home/sn/metrics/failure",
              "failureDataFileMaxCapacity": 10,
              "enabledInstruments": [
                "yr_app_instance_status"
              ],
              "initConfig": {
                "ip": "prometheus-pushgateway.default.svc.cluster.local",
                "port": 9091,
                "heartbeatUrl": "/healthy"
              }
            }
          }
        ]
      }
    }
  ]
}
)";

    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PrometheusExporterJson),
                                                  GetMetricsFilesName, {});

    std::string instanceID = "instance_id";
    resource_view::InstanceInfo instanceInfo;
    instanceInfo.set_function("function_1");
    instanceInfo.set_functionagentid("agentid");
    instanceInfo.set_parentid("parentid");
    EXPECT_NO_THROW(MetricsAdapter::GetInstance().ReportInstanceStatus(instanceID, instanceInfo));
}

TEST_F(MetricsAdapterTest, ReportInstanceExitLatency)
{
    std::string PrometheusExporterJson = R"(
{
  "backends": [
    {
      "batchExport": {
        "name": "Prometheus",
        "enable": true,
        "exporters": [
          {
            "prometheusPushExporter": {
              "enable": true,
              "batchSize": 20,
              "batchIntervalSec": 10,
              "failureQueueMaxSize": 100,
              "failureDataDir": "/home/sn/metrics/failure",
              "failureDataFileMaxCapacity": 10,
              "enabledInstruments": [
                "yr_instance_exit_latency"
              ],
              "initConfig": {
                "ip": "prometheus-pushgateway.default.svc.cluster.local",
                "port": 9091,
                "heartbeatUrl": "/healthy"
              }
            }
          }
        ]
      }
    }
  ]
}
)";

    MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PrometheusExporterJson),
                                                  GetMetricsFilesName, {});

    std::string instanceID = "instance_id";
    resource_view::InstanceInfo instanceInfo;
    instanceInfo.set_function("function_1");
    instanceInfo.set_functionagentid("agentid");
    instanceInfo.set_parentid("parentid");

    Status status;
    EXPECT_NO_THROW(MetricsAdapter::GetInstance().ReportInstanceExitLatency(status, 1755846561000, instanceInfo));
}

TEST_F(MetricsAdapterTest, convertNormalNodeLabels)
{
    NodeLabelsType nodeLabelsMap = { { "label1", { { "label_value1_1", 1 }, { "label_value1_2", 2 } } },
                                 { "label2", { { "label_value2_1", 1 } } } };
    std::vector<std::string> expectedRes{"label1:label_value1_1", "label1:label_value1_2", "label2:label_value2_1"};
    EXPECT_EQ(MetricsAdapter::GetInstance().ConvertNodeLabels(nodeLabelsMap), expectedRes);
}

TEST_F(MetricsAdapterTest, convertEmptyNodeLabels)
{
    NodeLabelsType nodeLabelsMap = {};
    EXPECT_TRUE(MetricsAdapter::GetInstance().ConvertNodeLabels(nodeLabelsMap).size() == 0);
}

TEST_F(MetricsAdapterTest, invalidYRMetrics)
{
    std::string optionStr = "{\"app_name\":\"app name 001\",\"endpoint\":\"127.0.0.1\",\"project_id\":\"project 001\",\"app_instance_id\":\"app instance 001\"";
    resource_view::InstanceInfo ins1;
    (*ins1.mutable_scheduleoption()->mutable_extension())["YR_Metrics"] = optionStr;
    auto customMetricsOption = MetricsAdapter::GetInstance().GetMetricsContext().GetCustomMetricsOption(ins1);
    EXPECT_TRUE(customMetricsOption.find("YR_Metrics") != customMetricsOption.end());
    EXPECT_EQ(customMetricsOption.find("YR_Metrics")->second, optionStr);
}

TEST_F(MetricsAdapterTest, SendK8sAlarm)
{
    metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(AlarmExporterJsonStr),
                                                               GetMetricsFilesName, {});
    metrics::MetricsAdapter::GetInstance().SendK8sAlarm("cluster1");
    auto alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::K8S_ALARM) == alarmMap.end());

    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetAttr("component_name", "function_proxy");
    metrics::MetricsAdapter::GetInstance().SendK8sAlarm("cluster1");
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::K8S_ALARM) == alarmMap.end());

    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetAttr("component_name", "function_master");
    metrics::MetricsAdapter::GetInstance().SendK8sAlarm("cluster1");
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::K8S_ALARM) != alarmMap.end());
}

TEST_F(MetricsAdapterTest, SendSchedulerAlarm)
{
    const std::string jsonStr = R"(
{
	"enabledMetrics": ["yr_proxy_alarm"],
	"backends": [{
		"immediatelyExport": {
			"name": "LakeHouse",
			"enable": true,
			"exporters": [{
				"aomAlarmExporter": {
					"enable": true,
					"ip": "127.0.0.1:8080/",
					"port": 9091
				}
			}]
		}
	}]
}
    )";

    metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(jsonStr), GetMetricsFilesName, {});
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetAttr("component_name", "function_master");
    metrics::MetricsAdapter::GetInstance().SendSchedulerAlarm("proxy,127.0.0.1");
    auto alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::SCHEDULER_ALARM) != alarmMap.end());
}

TEST_F(MetricsAdapterTest, PodResourceContextTest)
{
    const std::string fakeMetricsJson = R"(
{
	"enabledMetrics": ["fake_metrics"],
	"backends": [{
		"immediatelyExport": {
			"name": "LakeHouse",
			"enable": true,
			"exporters": [{
				"aomAlarmExporter": {
					"enable": true,
					"ip": "127.0.0.1:8080/",
					"port": 9091
				}
			}]
		}
	}]
})";

    metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(fakeMetricsJson),
                                                               GetMetricsFilesName, {});
    resource_view::ResourceUnit unit;
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetPodResource("pod1", unit);
    auto map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map.find("pod1"), map.end());
    auto enabledInstruments = MetricsAdapter::GetInstance().GetMetricsContext().GetEnabledInstruments();
    EXPECT_EQ(enabledInstruments.find(YRInstrument::YR_POD_RESOURCE), enabledInstruments.end());

    const std::string podMetricsJson = R"(
{
	"enabledMetrics": ["yr_pod_resource"],
	"backends": [{
		"immediatelyExport": {
			"name": "LakeHouse",
			"enable": true,
			"exporters": [{
				"aomAlarmExporter": {
					"enable": true,
					"ip": "127.0.0.1:8080/",
					"port": 9091
				}
			}]
		}
	}]
})";
    metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(podMetricsJson),
                                                               GetMetricsFilesName, {});
    enabledInstruments = MetricsAdapter::GetInstance().GetMetricsContext().GetEnabledInstruments();
    EXPECT_NE(enabledInstruments.find(YRInstrument::YR_POD_RESOURCE), enabledInstruments.end());

    unit = view_utils::Get1DResourceUnit("pod1");
    ::resources::Value::Counter cnter;
    cnter.mutable_items()->insert({ "value", 1 });
    cnter.mutable_items()->insert({ "value2", 1 });
    unit.mutable_nodelabels()->insert({ "key", cnter });

    // add
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetPodResource("pod1", unit);
    map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map["pod1"].capacity.resources().at(view_utils::RESOURCE_MEM_NAME).scalar().value(),
              view_utils::SCALA_VALUE1);
    EXPECT_EQ(map["pod1"].allocatable.resources().at(view_utils::RESOURCE_CPU_NAME).scalar().value(),
              view_utils::SCALA_VALUE1);
    EXPECT_EQ(map["pod1"].nodeLabels.size(), 1);
    EXPECT_EQ(map["pod1"].nodeLabels["key"].size(), 2);

    // update
    (*unit.mutable_actualuse()) = view_utils::GetCpuMemResources();
    unit.mutable_nodelabels()->clear();
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetPodResource("pod1", unit);
    map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map["pod1"].nodeLabels.size(), 0);

    // add pod2
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetPodResource("pod2",
                                                                              view_utils::Get1DResourceUnit("pod2"));
    map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map.size(), 2);
    EXPECT_NE(map.find("pod2"), map.end());

    // add system pod
    auto systemUnit = view_utils::Get1DResourceUnit("system");
    ::resources::Value::Counter cnter2;
    cnter2.mutable_items()->insert({ "1243", 1 });
    systemUnit.mutable_nodelabels()->insert({ "resource.owner", cnter2 });

    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetPodResource("system", systemUnit);
    map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ(map.find("system"), map.end());

    // delete pod1
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().DeletePodResource("pod1");
    map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.find("pod1"), map.end());

    // clear
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().ErasePodResource();
    map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map.size(), 0);
}

TEST_F(MetricsAdapterTest, CollectPodResourceMetricsTest)
{
    MetricsAdapter::GetInstance().SetContextAttr("component_name", "function_master");
    metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(PromExporterJsonStr),
                                                               GetMetricsFilesName, {});

    auto unit = view_utils::Get1DResourceUnit("pod1");
    ::resources::Value::Counter cnter;
    cnter.mutable_items()->insert({ "value", 1 });
    cnter.mutable_items()->insert({ "value2", 1 });
    unit.mutable_nodelabels()->insert({ "key", cnter });

    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetPodResource("pod1", unit);
    metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetPodResource("pod2",
                                                                              view_utils::Get1DResourceUnit("pod2"));
    auto map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map.size(), 2);

    // register observable instrument
    MetricsAdapter::GetInstance().RegisterPodResource();
    auto observableInstrumentMap = MetricsAdapter::GetInstance().GetObservableInstrumentMap();
    EXPECT_TRUE(observableInstrumentMap.find("yr_pod_resource") != observableInstrumentMap.end());

    auto obRes = std::make_shared<MetricsApi::ObserveResultT<double>>();
    MetricsAdapter::GetInstance().CollectPodResource(obRes);
    auto iter = obRes->Value().at(0).first.front();
    EXPECT_EQ(iter.first, "agent_id");
    EXPECT_EQ(iter.second, "pod1");

    for (auto value : obRes->Value()) {
        for (const auto &key : value.first) {
            YRLOG_DEBUG("{}:{}", key.first, key.second);
            if (key.first == "used_cpu") {
                EXPECT_EQ(key.second, "0.000000");
            }
        }
        YRLOG_DEBUG("value: {}", value.second);
    }
    EXPECT_EQ(obRes->Value().size(), 2);
}

}  // namespace functionsystem::test