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

#include <atomic>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>

#include <nlohmann/json.hpp>

#include "common/constants/actor_name.h"
#include "common/constants/constants.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "common/logs/logging.h"
#include "common/metrics/metrics_adapter.h"
#include "common/resource_view/view_utils.h"
#include "common/types/instance_state.h"
#include "common/utils/files.h"
#include "common/hex/hex.h"
#include "common/kube_client/api/apps_v1_api.h"
#include "common/kube_client/model/deployment/v1_deployment.h"
#include "common/kube_client/model/deployment/v1_deployment_status.h"
#include "function_master/scaler/scaler_actor.h"
#include "mocks/mock_kube_client.h"
#include "mocks/mock_meta_store_client.h"
#include "scaler_test_actor.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/os_utils.hpp"
#include "utils/port_helper.h"

namespace functionsystem::scaler::test {
using namespace functionsystem::test;

using V1Deployment = functionsystem::kube_client::model::V1Deployment;
using Object = functionsystem::kube_client::model::Object;
using V1NodeStatus = functionsystem::kube_client::model::V1NodeStatus;
using V1NodeAddress = functionsystem::kube_client::model::V1NodeAddress;
using V1ObjectMeta = functionsystem::kube_client::model::V1ObjectMeta;
using V1NodeSpec = functionsystem::kube_client::model::V1NodeSpec;
using V1DeploymentStatus = functionsystem::kube_client::model::V1DeploymentStatus;

const std::string DELEGATE_CONTAINER_STR = R"(
{
	"image": "image",
	"env": [{
		"name": "env_x",
		"value": "value_x"
	}, {
		"name": "env_y",
		"valueFrom": {
			"fieldRef": {
				"apiVersion": "v1",
				"fieldPath": "status.podIP"
			}
		}
	}, {
		"name": "env_z",
		"valueFrom": {
			"resourceFieldRef": {
				"containerName": "delegate_container",
				"resource": "resource_x",
				"divisor": "divisor_x"
			}
		}
	}],
	"command": {
		"command1": "100",
		"command2": "1"
	},
	"workingDir": "workingDir",
	"args": {
		"args1": "100",
		"args2": "1"
	},
	"uid": 1003,
	"gid": 1003,
	"volumeMounts": [{
			"name": "uds",
			"mountPath": "/home/uds",
			"subPath": "uds sub path",
			"subPathExpr": "sub path expr",
			"mountPropagation": "mount propagation",
			"readOnly": true
		}, {
			"mountPath": "xx"
		}, {
            "name": "xx"
        }
	],
    "livenessProbe": {
        "exec":{
            "command":["liveness","probe"]
        },
        "initialDelaySeconds": 100,
        "timeoutSeconds": 200,
        "periodSeconds": 300,
        "successThreshold": 400,
        "failureThreshold": 500
    },
    "readinessProbe": {
        "exec":{
            "command":["readiness","probe"]
        },
        "initialDelaySeconds": 1100,
        "timeoutSeconds": 1200,
        "periodSeconds": 1300,
        "successThreshold": 1400,
        "failureThreshold": 1500
    }
}
)";

const std::string POD_POOL_INFO_STR = R"(
{
     "id": "pool1",
     "size": 1,
     "group": "rg1",
     "reuse": true,
     "image": "runtime-manager:version1",
     "init_image": "function-agent-init:version1",
     "labels": {
       "label1":"val1",
       "":""
     },
     "environment": {
       "env1": "key1",
       "env2": "key2"
     },
     "volumes": "[{\"name\": \"volume-1\", \"hostPath\": { \"path\": \"/home/xxx\",\"type\": \"DirectoryOrCreate\"}}, {\"name\": \"pvc-xx\", \"persistentVolumeClaim\": {\"caimName\": \"pvc-xxx\"}}]",
     "volume_mounts": "[{\"name\": \"pvc-xx\",\"mountPath\": \"/home/snuser/models\"}]",
     "resources": {
       "limits": {
         "cpu": "2",
         "memory": "6Gi",
         "huawei.com/ascend-1980": "1"
       },
       "requests": {
         "cpu": "2",
         "memory": "6Gi",
         "huawei.com/ascend-1980": "1"
       }
     },
     "runtime_class_name": "runc",
     "node_selector": {
         "label1":"val1"
      },
     "tolerations": "[{\"key\":\"node.kubernetes.io/disk-pressure\",\"operator\": \"Equal\", \"value\": \"true\", \"effect\": \"NoSchedule\"}]",
     "affinities": "{\"nodeAffinity\": {\"requiredDuringSchedulingIgnoredDuringExecution\": {\"nodeSelectorTerms\": [{\"matchExpressions\": [{ \"key\": \"node-type\",\"operator\": \"In\",\"values\": [\"system\"]}]}]}}}",
     "topology_spread_constraints": "[{\"maxSkew\":1,\"minDomains\":1,\"topologyKey\":\"zone\",\"whenUnsatisfiable\":\"DoNotSchedule\", \"matchLabelKeys\": [\"test1\",\"test2\"],\"labelSelector\":{\"matchLabels\": {\"app\":\"foo\"}} }]"
}
)";

const std::string POD_POOL_WITH_HPA_INFO_STR = R"(
{
     "id": "pool1",
     "size": 1,
     "group": "rg1",
     "reuse": true,
     "image": "runtime-manager:version1",
     "init_image": "function-agent-init:version1",
     "labels": {
       "label1":"val1",
       "":""
     },
     "environment": {
       "env1": "key1",
       "env2": "key2"
     },
     "volumes": "[{\"name\": \"volume-1\", \"hostPath\": { \"path\": \"/home/xxx\",\"type\": \"DirectoryOrCreate\"}}, {\"name\": \"pvc-xx\", \"persistentVolumeClaim\": {\"caimName\": \"pvc-xxx\"}}]",
     "volume_mounts": "[{\"name\": \"pvc-xx\",\"mountPath\": \"/home/snuser/models\"}]",
     "resources": {
       "limits": {
         "cpu": "2",
         "memory": "6Gi",
         "huawei.com/ascend-1980": "1"
       },
       "requests": {
         "cpu": "2",
         "memory": "6Gi",
         "huawei.com/ascend-1980": "1"
       }
     },
    "affinities": "{\"nodeAffinity\": {\"requiredDuringSchedulingIgnoredDuringExecution\": {\"nodeSelectorTerms\": [{\"matchExpressions\": [{ \"key\": \"node-type\",\"operator\": \"In\",\"values\": [\"system\"]}]}]}},\"podAntiAffinity\": {\"preferredDuringSchedulingIgnoredDuringExecution\": [{\"weight\": 1,\"podAffinityTerm\": {\"labelSelector\": {\"matchExpressions\": [{\"key\": \"app\",\"operator\": \"In\",\"values\": [\"function-agent-pool-1\"]}]},\"topologyKey\": \"kubernetes.io/hostname\"}}]}}",
     "horizontal_pod_autoscaler_spec": "{\"minReplicas\": 1, \"maxReplicas\": 2, \"metrics\":[{\"resource\": {\"name\":\"cpu\", \"target\":{\"averageUtilization\":20, \"type\":\"Utilization\"}}, \"type\":\"Resource\"}, {\"resource\": {\"name\":\"memory\", \"target\":{\"averageUtilization\":50, \"type\":\"Utilization\"}}, \"type\":\"Resource\"}]}"
}
)";

const std::string POD_POOL_TEST_STR = R"(
{
  "id": "pool1",
  "group": "rg1",
  "resources": {
    "limits": {
      "cpu": "500m",
      "memory": "500Mi"
    },
    "requests": {
      "cpu": "500m",
      "memory": "500Mi"
    }
  },
  "size": 1,
  "reuse": true
}
)";

const std::string DEFAULT_DEPLOYMENT_JSON_STR = R"(
{
    "apiVersion": "v1",
    "kind": "Deployment",
    "metadata": {
       "name": "deploy-001"
    },
    "spec": {
    }
}
)";
const std::string NODE_AFFINITY_TEMPLATE_STR = R"(
{
	"requiredDuringSchedulingIgnoredDuringExecution": {
		"nodeSelectorTerms": [{
			"matchExpressions": [{
				"key": "key1",
				"operator": "In",
				"values": ["val1"]
			}, {
				"key": "key2",
				"operator": "In",
				"values": ["val2"]
			}],
			"matchFields": [{
				"key": "key1",
				"operator": "In",
				"values": ["val1"]
			}, {
				"key": "key2",
				"operator": "In",
				"values": ["val2"]
			}]
		}, {
			"matchExpressions": [{
				"key": "key3",
				"operator": "In",
				"values": ["val3"]
			}, {
				"key": "key3",
				"operator": "In",
				"values": ["val3"]
			}],
			"matchFields": [{
				"key": "key4",
				"operator": "In",
				"values": ["val4"]
			}, {
				"key": "key4",
				"operator": "In",
				"values": ["val4"]
			}]
		}]
	},
	"preferredDuringSchedulingIgnoredDuringExecution": [{
		"preference": {
			"matchExpressions": [{
				"key": "node.kubernetes.io/instance-type",
				"operator": "In",
				"values": ["physical.kat2ne.48xlarge.8.376t.ei.c002.ondemand", "physical.kat2ne.48xlarge.8.ei.pod101.ondemand"]
			}]
		},
		"weight": 1
	}]
}
)";

const std::string instanceKey1 = R"(/sn/instance/business/yrk/tenant/12345678901234561234567890123456/function/0-system-faasExecutorPython3.9/version/$latest/defaultaz/requestID1/instanceID1)";
const std::string instanceInfoJson1 = R"({"scheduleOption":{"schedPolicyName":"monopoly"},"instanceID":"instanceID1","requestID":"requestID1","runtimeID":"runtime-1","runtimeAddress":"10.42.2.101","functionAgentID":"functionagent-pool1-776c6db574-1","functionProxyID":"siaphisprg00911","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","scheduleTimes":1,"instanceStatus":{"code":1,"msg":"scheduling"},"jobID":"job-12345678","parentID":"4e7cd507-8645-4600-b33c-f045f13e4beb","deployTimes":1,"version":"1"})";

const std::string instanceKey2 = R"(/sn/instance/business/yrk/tenant/12345678901234561234567890123456/function/0-system-faasExecutorPython3.9/version/$latest/defaultaz/requestID2/instanceID2)";
const std::string instanceInfoJson2 = R"({"scheduleOption":{"schedPolicyName":"monopoly"},"instanceID":"instanceID2","requestID":"requestID2","runtimeID":"runtime-2","runtimeAddress":"10.42.2.102","functionAgentID":"functionagent-pool1-776c6db574-2","functionProxyID":"siaphisprg00912","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","scheduleTimes":1,"instanceStatus":{"code":2,"msg":"creating"},"jobID":"job-12345678","parentID":"4e7cd507-8645-4600-b33c-f045f13e4beb","deployTimes":1,"version":"1"})";

const std::string instanceKey3 = R"(/sn/instance/business/yrk/tenant/12345678901234561234567890123456/function/0-system-faasExecutorPython3.9/version/$latest/defaultaz/requestID3/instanceID3)";
const std::string instanceInfoJson3 = R"({"instanceID":"instanceID3","requestID":"requestID3","runtimeID":"runtime-3","runtimeAddress":"10.42.2.103","functionAgentID":"functionagent-pool1-776c6db574-3","functionProxyID":"siaphisprg00913","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","scheduleTimes":1,"instanceStatus":{"code":3,"msg":"running"},"jobID":"job-12345678","parentID":"4e7cd507-8645-4600-b33c-f045f13e4beb","deployTimes":1,"version":"1"})";


class ScalerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
        auto client = std::make_shared<MockKubeClient>();
        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        metaStorageAccessor_ = std::make_shared<MetaStorageAccessor>(metaStoreClient_);
        counter = std::make_shared<std::atomic<int>>(0);
        ScalerParams scalerParams{ .k8sNamespace = DEFAULT_NAMESPACE,
                                   .gracePeriodSeconds = 25,
                                   .systemUpgradeParam = {
                                       .isEnabled = true,
                                       .systemUpgradeKey = DEFAULT_SYSTEM_UPGRADE_KEY,
                                       .azID = 1,
                                       .systemUpgradeWatcher = metaStorageAccessor_,
                                       .handlers = { .systemUpgradeHandler = [](bool isUpgrading) {},
                                                     .localSchedFaultHandler = [](const std::string &nodeName) {},
                                       .evictAgentHandler = [cnt(counter)](const std::string &localID, const std::shared_ptr<messages::EvictAgentRequest> &req){
                                                         (*cnt)++;
                                                         return Status::OK();
                                                     }}

                                   } };
        scalerParams.poolConfigPath = "/home/sn/scaler/config/functionsystem-pools.json";
        scalerParams.agentTemplatePath = "/home/sn/scaler/template/function-agent.json";
        scalerParams.enableFrontendPool = true;
        actor_ = std::make_shared<scaler::ScalerActor>(SCALER_ACTOR, client, metaStorageAccessor_, scalerParams);
        litebus::Spawn(actor_);

        litebus::Async(actor_->GetAID(), &ScalerActor::UpdateLeaderInfo, GetLeaderInfo(actor_->GetAID()));

        testActor_ = std::make_shared<ScalerTestActor>("ScalerTestActor");
        litebus::Spawn(testActor_);

        pools_ = std::make_shared<std::vector<scaler::ResourcePool>>();
        scaler::ResourcePool pool1{ .name = "600-600-1pool",
                                    .poolSize = 1,
                                    .requestResources = { { CPU_RESOURCE, "600m" }, { MEMORY_RESOURCE, "600Mi" } },
                                    .limitResources = { { CPU_RESOURCE, "600m" }, { MEMORY_RESOURCE, "600Mi" } } };
        pools_->push_back(pool1);
        scaler::ResourcePool pool2{ .name = "500-500-2pool",
                                    .poolSize = 1,
                                    .requestResources = { { CPU_RESOURCE, "500m" }, { MEMORY_RESOURCE, "500Mi" } },
                                    .limitResources = { { CPU_RESOURCE, "500m" }, { MEMORY_RESOURCE, "500Mi" } } };
        pools_->push_back(pool2);

        auto localVarResult = std::make_shared<V1Deployment>();
        nlohmann::json localVarJson = nlohmann::json::parse(DEPLOYMENT_JSON);
        auto converted = localVarResult->FromJson(localVarJson);
        if (!converted) {
            YRLOG_ERROR("failed to convert deployment");
        }
        // mock template deployment
        actor_->SetTemplateDeployment(localVarResult);
    }

    void TearDown() override
    {
        metaStorageAccessor_->metaClient_ = metaStoreClient_; // for replace origin client while using mockClient

        litebus::Terminate(actor_->GetAID());
        litebus::Terminate(testActor_->GetAID());

        litebus::Await(actor_);
        litebus::Await(testActor_);

        // delete all data after terminate actor. Otherwise, the test case occasionally fails.
        metaStorageAccessor_->Delete("/", true).Get();

        metaStoreClient_ = nullptr;
        metaStorageAccessor_ = nullptr;
        actor_ = nullptr;
        testActor_ = nullptr;
        etcdSrvDriver_->StopServer();
        etcdSrvDriver_ = nullptr;
    }

protected:
    std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    std::shared_ptr<scaler::ScalerActor> actor_;
    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_;
    std::shared_ptr<ScalerTestActor> testActor_;
    std::shared_ptr<std::vector<scaler::ResourcePool>> pools_;
    std::vector<std::shared_ptr<PodPoolInfo>> podPools_;
    std::shared_ptr<std::atomic<int>> counter;
    std::string metaStoreServerHost_;

    static resource_view::InstanceInfo CreateInstance(const std::string &id, int code)
    {
        resource_view::InstanceInfo output;
        output.set_instanceid(id);
        output.set_requestid(INSTANCE_PATH_PREFIX + "/001");
        output.set_functionagentid("function-agent-001");
        output.mutable_instancestatus()->set_code(code);
        output.mutable_scheduleoption()->set_schedpolicyname("monopoly");
        output.mutable_labels()->Add("label");
        return output;
    }

    messages::CreateAgentResponse GetTestResponse()
    {
        return litebus::Async(testActor_->GetAID(), &ScalerTestActor::GetResponse).Get();
    }

    messages::UpdateNodeTaintResponse GetUpdateNodeTaintResponse()
    {
        return litebus::Async(testActor_->GetAID(), &ScalerTestActor::GetTaintResponse).Get();
    }

    static std::shared_ptr<V1Pod> GetReadyPod()
    {
        auto pod = std::make_shared<V1Pod>();
        auto containerStatus = std::make_shared<functionsystem::kube_client::api::V1ContainerStatus>();
        containerStatus->SetContainerID("docker://1234567");
        containerStatus->SetName("runtime");
        containerStatus->SetReady(true);
        std::vector<std::shared_ptr<functionsystem::kube_client::api::V1ContainerStatus>> statuses;
        statuses.push_back(containerStatus);
        auto status = std::make_shared<functionsystem::kube_client::api::V1PodStatus>();
        status->SetHostIP("127.0.0.1");
        status->SetContainerStatuses(statuses);
        pod->SetStatus(status);
        auto metaData = std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>();
        metaData->SetUid("uid-123");
        metaData->SetName("function-agent-123");
        metaData->SetLabels({});
        metaData->GetLabels()["yr-reuse"] = "false";
        pod->SetMetadata(metaData);
        auto podSpec = std::make_shared<functionsystem::kube_client::api::V1PodSpec>();
        podSpec->SetNodeName("node-001");
        pod->SetSpec(podSpec);
        return pod;
    }

    static std::shared_ptr<V1Node> GetReadyNode()
    {
        auto address = std::make_shared<V1NodeAddress>();
        address->SetType("InternalIP");
        address->SetAddress("127.0.0.1");
        auto status = std::make_shared<V1NodeStatus>();
        status->SetAddresses({ address });
        auto node = std::make_shared<V1Node>();
        auto spec = std::make_shared<V1NodeSpec>();
        node->SetSpec(spec);
        node->GetSpec()->SetTaints({});
        node->SetStatus(status);
        auto metadata = std::make_shared<V1ObjectMeta>();
        metadata->SetName("127.0.0.1");
        node->SetMetadata(metadata);
        return node;
    }

    std::vector<std::shared_ptr<V1Node>> GetTaintNodeList()
    {
        std::string taintKey = "test-taint";
        actor_->member_->proxyTaintKey = taintKey;
        std::vector<std::shared_ptr<V1Node>> nodeList;
        auto node1 = GetReadyNode();
        node1->GetMetadata()->SetName("node1");
        std::vector<std::shared_ptr<V1Taint>> taints;
        taints.emplace_back(std::make_shared<V1Taint>());
        auto taint = std::make_shared<V1Taint>();
        taint->SetKey("test-taint");
        taints.emplace_back(taint);
        node1->GetSpec()->SetTaints(taints);
        actor_->member_->nodes["node1"] = node1;
        nodeList.emplace_back(node1);
        auto node2 = GetReadyNode();
        node2->GetMetadata()->SetName("node2");
        actor_->member_->nodes["node2"] = node2;
        nodeList.emplace_back(node2);
        return nodeList;
    }

    static bool ContainValue(const std::shared_ptr<Object> &body, const std::string &value)
    {
        YRLOG_INFO("body value is {}", body->ToJson().dump());
        return body->ToJson().dump().find(value) != std::string::npos;
    }

    static ::resources::Resources GetResources()
    {
        ::resources::Resources resources;
        ::resources::Resource cpuResource;
        cpuResource.set_name("CPU");
        cpuResource.mutable_scalar()->set_value(500);
        cpuResource.mutable_scalar()->set_limit(500);
        ::resources::Resource memoryResource;
        memoryResource.set_name("Memory");
        memoryResource.mutable_scalar()->set_value(500);
        memoryResource.mutable_scalar()->set_limit(500);
        resources.mutable_resources()->operator[]("CPU").CopyFrom(cpuResource);
        resources.mutable_resources()->operator[]("Memory").CopyFrom(memoryResource);
        return resources;
    }

    // if etcd watch is create slowly, sometimes it will trigger etcd event twice
    void WaitRegisterComplete()
    {
        EXPECT_AWAIT_TRUE([&]() -> bool { return  actor_->member_->metaStorageAccessor->metaClient_->metaStoreClientMgr_->GetKvClient("")->readyRecords_.size() >= 3; });
        auto mockClient = std::make_shared<MockKubeClient>();
        actor_->SetKubeClient(mockClient);
        litebus::Promise<std::shared_ptr<functionsystem::V1Pod>> errPromise;
        errPromise.SetFailed(404);
        bool isRead = false;
        EXPECT_CALL(*mockClient, ReadNamespacedPod)
            .WillRepeatedly(testing::DoAll(testing::Assign(&isRead, true), testing::Return(errPromise.GetFuture())));
        resource_view::InstanceInfo instance =
            CreateInstance(INSTANCE_PATH_PREFIX + "/ins", static_cast<int32_t>(InstanceState::FATAL));
        instance.set_functionagentid("function-agent-not-found");
        instance.mutable_scheduleoption()->set_schedpolicyname("");
        std::string insJson;
        TransToJsonFromInstanceInfo(insJson, instance);
        metaStorageAccessor_->Put(INSTANCE_PATH_PREFIX + "/ins", insJson).Get();
        EXPECT_AWAIT_TRUE([&isRead]() -> bool { return isRead; });
    }

    static std::vector<WatchEvent> GetWatchEvents(const std::string &key, const std::string &jsonStr)
    {
        KeyValue kv;
        kv.set_key(key);
        kv.set_value(jsonStr);
        KeyValue prevKv;
        auto event = WatchEvent{ EventType::EVENT_TYPE_PUT, kv, prevKv };
        std::vector<WatchEvent> events{ event };
        return events;
    }

    static litebus::Future<std::shared_ptr<V1Deployment>> GetErrorDeploymentFuture(int errorCode)
    {
        litebus::Promise<std::shared_ptr<functionsystem::V1Deployment>> errPromise;
        errPromise.SetFailed(errorCode);
        return errPromise.GetFuture();
    }
};

/**
 * Test instance deleted before pod ready
 *
 * Steps:
 *   1. mock k8s client, templates
 *   2. create agent
 *   3. mock event, delete instance id
 *   4. mock event, pod is ready
 *
 * Expect:
 *   1. delete pod is called
 */
TEST_F(ScalerTest, CreatePodAutoCleaning)
{
    // mock k8s client
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    actor_->SetPodMap({});
    auto localVarResult = std::make_shared<V1Deployment>();
    nlohmann::json localVarJson = nlohmann::json::parse(DEPLOYMENT_JSON);
    auto converted = localVarResult->FromJson(localVarJson);
    if (!converted) {
        YRLOG_ERROR("failed to convert deployment");
    }
    // mock template deployment
    actor_->SetTemplateDeployment(localVarResult);

    // make a suitable create agent request
    auto req = std::make_shared<messages::CreateAgentRequest>();
    auto instanceinfo = view_utils::Get1DInstance();
    instanceinfo.set_requestid("requestid123");
    instanceinfo.set_instanceid("instanceid123");
    req->mutable_instanceinfo()->CopyFrom(instanceinfo);
    req->mutable_instanceinfo()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);

    litebus::Future<std::shared_ptr<V1Pod>> createPodCalledArg;
    EXPECT_CALL(*mockClient, CreateNamespacedPod)
        .WillOnce(testing::DoAll(test::FutureArg<1>(&createPodCalledArg), testing::Return(GetReadyPod())));

    litebus::Future<std::string> deletePodCalledArg;
    EXPECT_CALL(*mockClient, DeleteNamespacedPod)
        .WillOnce(testing::DoAll(test::FutureArg<0>(&deletePodCalledArg), testing::Return(GetReadyPod())))
        .WillOnce(testing::DoAll(test::FutureArg<0>(&deletePodCalledArg), testing::Return(GetReadyPod())));

    // Checkpoint: check craete pod request is called
    litebus::Future<std::string> listPodCalledArg;

    testActor_->CreateAgent(actor_->GetAID(), req->SerializeAsString());

    // Checkpoint: check create pod request is called
    ASSERT_AWAIT_READY(createPodCalledArg);
    req->mutable_instanceinfo()->set_functionagentid(createPodCalledArg.Get()->GetMetadata()->GetName());
    actor_->HandleInstanceDelete(req->instanceinfo());

    // mock pod is ready event
    auto pod = GetReadyPod();
    pod->GetMetadata()->SetName(createPodCalledArg.Get()->GetMetadata()->GetName());
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_ADD, pod);

    // Expect: delete pod is called
    ASSERT_AWAIT_READY(deletePodCalledArg);
    EXPECT_EQ(deletePodCalledArg.Get(), createPodCalledArg.Get()->GetMetadata()->GetName());
    litebus::Async(actor_->GetAID(), &ScalerActor::OnPodModified, K8sEventType::EVENT_TYPE_DELETE, pod);
    ASSERT_AWAIT_TRUE([&]() -> bool {return actor_->member_->podNameMap.size() == 0;});
}

TEST_F(ScalerTest, CreatePodAutoCleaningWithSameInstanceID)
{
    // mock k8s client
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    auto localVarResult = std::make_shared<V1Deployment>();
    nlohmann::json localVarJson = nlohmann::json::parse(DEPLOYMENT_JSON);
    localVarResult->FromJson(localVarJson);
    // mock template deployment
    actor_->SetTemplateDeployment(localVarResult);
    // make a suitable create agent request
    auto req = std::make_shared<messages::CreateAgentRequest>();
    auto instanceinfo = view_utils::Get1DInstance();
    instanceinfo.set_requestid("requestid123");
    instanceinfo.set_instanceid("same-ins001");
    req->mutable_instanceinfo()->CopyFrom(instanceinfo);
    req->mutable_instanceinfo()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    litebus::Future<std::shared_ptr<V1Pod>> createPodCalledArg;
    litebus::Future<std::shared_ptr<V1Pod>> createPodCalledArg1;
    auto createPod2 = GetReadyPod();
    createPod2->GetMetadata()->SetName("function-agent-ins002");
    EXPECT_CALL(*mockClient, CreateNamespacedPod)
        .WillOnce(testing::DoAll(test::FutureArg<1>(&createPodCalledArg), testing::Return(GetReadyPod())))
        .WillOnce(testing::DoAll(test::FutureArg<1>(&createPodCalledArg1), testing::Return(GetReadyPod())));

    litebus::Future<std::string> deletePodCalledArg;
    litebus::Future<std::string> deletePodCalledArg1;
    litebus::Future<std::string> deletePodCalledArg2;
    EXPECT_CALL(*mockClient, DeleteNamespacedPod)
        .WillOnce(testing::DoAll(test::FutureArg<0>(&deletePodCalledArg), testing::Return(GetReadyPod())))
        .WillOnce(testing::DoAll(test::FutureArg<0>(&deletePodCalledArg1), testing::Return(GetReadyPod())));

    testActor_->CreateAgent(actor_->GetAID(), req->SerializeAsString());
    // Checkpoint: check create pod request is called
    ASSERT_AWAIT_READY(createPodCalledArg);
    req->mutable_instanceinfo()->mutable_instancestatus()->set_code(6);
    req->mutable_instanceinfo()->set_functionproxyid("node-001");
    actor_->HandleInstancePut(req->instanceinfo());
    ASSERT_AWAIT_READY(deletePodCalledArg);
    req->mutable_instanceinfo()->set_functionproxyid("InstanceManagerOwner");
    actor_->HandleInstanceDelete(req->instanceinfo());
    // create agent again
    req->mutable_instanceinfo()->mutable_instancestatus()->set_code(1);
    req->mutable_instanceinfo()->set_functionproxyid("node-001");
    testActor_->CreateAgent(actor_->GetAID(), req->SerializeAsString());
    ASSERT_AWAIT_READY(createPodCalledArg1);
    req->mutable_instanceinfo()->set_functionproxyid("InstanceManagerOwner");
    actor_->HandleInstanceDelete(req->instanceinfo());
    ASSERT_AWAIT_READY(deletePodCalledArg1);
}

TEST_F(ScalerTest, CreateFunctionPoolsMockSuccess)
{
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    litebus::Future<std::shared_ptr<functionsystem::V1Deployment>> mockDeploymentFuture;
    EXPECT_CALL(*mockClient.get(), MockCreateNamespacedDeployment(testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<0>(&mockDeploymentFuture)));
    ASSERT_AWAIT_TRUE([&]() -> bool {
        return litebus::Async(actor_->GetAID(), &ScalerActor::CreateResourcePools, pools_).Get() ==
               static_cast<StatusCode>(0);
    });
    ASSERT_AWAIT_READY(mockDeploymentFuture);
    auto mockDeployment = mockDeploymentFuture.Get();
    std::string expectMetadata = R"({"labels":{"app":"function-agent-600-600-1pool","yr-reuse":"false"}})";
    EXPECT_STREQ(mockDeployment->GetSpec()->GetRTemplate()->GetMetadata()->ToJson().dump().c_str(),
                 expectMetadata.c_str());
    std::string expectAffinity =
        "{\"podAntiAffinity\":{\"preferredDuringSchedulingIgnoredDuringExecution\":[{\"podAffinityTerm\":{\"labelSelector\":{\"matchLabels\":{\"app\":\"function-agent-600-600-1pool\"}},\"topologyKey\":\"kubernetes.io/hostname\"},\"weight\":100}],\"requiredDuringSchedulingIgnoredDuringExecution\":[]}}";
    EXPECT_STREQ(mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetAffinity()->ToJson().dump().c_str(),
                 expectAffinity.c_str());

    ASSERT_AWAIT_TRUE([&]() -> bool {
        return litebus::Async(actor_->GetAID(), &ScalerActor::GetPoolDeploymentsMap).Get().size() >= 2;
    });
    scaler::ResourcePool pool1{ .name = "600-600-1pool",
                                .poolSize = 1,
                                .requestResources = { { CPU_RESOURCE, "700m" }, { MEMORY_RESOURCE, "700Mi" } },
                                .limitResources = { { CPU_RESOURCE, "700m" }, { MEMORY_RESOURCE, "700Mi" } } };

    for (unsigned long int i = 0; i < pools_->size(); i++) {
        if (pools_->at(i).name == "600-600-1pool") {
            pools_->erase(pools_->begin() + i);
            pools_->push_back(pool1);
        }
    }
    litebus::Async(actor_->GetAID(), &ScalerActor::CreateResourcePools, pools_);
    ASSERT_AWAIT_TRUE([&]() -> bool {
        auto m = litebus::Async(actor_->GetAID(), &ScalerActor::GetPoolDeploymentsMap).Get();
        if (auto poolIter = m.find("function-agent-600-600-2pool"); poolIter != m.end()) {
            return true;
        }
        return false;
    });
}

TEST_F(ScalerTest, IsSameDeploymentConfig)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    scaler::ResourcePool pool1{ .name = "600-600-1pool",
                                .poolSize = 1,
                                .requestResources = { { CPU_RESOURCE, "600m" }, { MEMORY_RESOURCE, "600Mi" } },
                                .limitResources = { { CPU_RESOURCE, "600m" }, { MEMORY_RESOURCE, "600Mi" } } };

    std::shared_ptr<V1Deployment> body;
    auto deployment = mockClient->CreateNamespacedDeployment("", body).Get();
    EXPECT_TRUE(actor_->IsSameDeploymentConfig(pool1, deployment));
    pool1.limitResources[MEMORY_RESOURCE] = "700Mi";
    EXPECT_FALSE(actor_->IsSameDeploymentConfig(pool1, deployment));
    pool1.limitResources[CPU_RESOURCE] = "700m";
    EXPECT_FALSE(actor_->IsSameDeploymentConfig(pool1, deployment));
    pool1.requestResources[MEMORY_RESOURCE] = "700Mi";
    EXPECT_FALSE(actor_->IsSameDeploymentConfig(pool1, deployment));
    pool1.requestResources[CPU_RESOURCE] = "700m";
    EXPECT_FALSE(actor_->IsSameDeploymentConfig(pool1, deployment));
    pool1.poolSize = 2;
    EXPECT_FALSE(actor_->IsSameDeploymentConfig(pool1, deployment));
}

    TEST_F(ScalerTest, LoadFunctionPodPoolsConfigSuccess)
{
    (void)litebus::os::Rmdir("/home/sn/scaler/config/");
    (void)litebus::os::Mkdir("/home/sn/scaler/config/");
    auto writeStatus = Write("/home/sn/scaler/config/functionsystem-pools.json", "fake json");
    YRLOG_INFO("write json file result: {}", writeStatus);
    // Failure
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    poolManager->LoadPodPoolsConfig("/home/sn/scaler/config/functionsystem-pools.json");
    auto localPoolMap = poolManager->GetLocalPodPools();
    ASSERT_AWAIT_TRUE([&]() -> bool { return localPoolMap.empty(); });
    // Success
    std::string poolsStr =
        R"({"pools":[{"id":"pool1-500-500-a","resources":{"limits":{"cpu":"500m","memory":"500Mi"},"requests":{"cpu":"500m","memory":"500Mi"}},"size":1,"reuse":true},{"id":"pool2-600-600-a","resources":{"limits":{"cpu":"600m","memory":"600Mi"},"requests":{"cpu":"600m","memory":"600Mi"}},"size":2}]})";
    writeStatus = Write("/home/sn/scaler/config/functionsystem-pools.json", poolsStr);
    YRLOG_INFO("write json file result: {}", writeStatus);
    poolManager->LoadPodPoolsConfig("/home/sn/scaler/config/functionsystem-pools.json");
    localPoolMap = poolManager->GetLocalPodPools();
    ASSERT_AWAIT_TRUE([&]() -> bool { return localPoolMap.size() == 2; });
    EXPECT_TRUE(localPoolMap["pool1-500-500-a"]->reuse);
    EXPECT_FALSE(localPoolMap["pool2-600-600-a"]->reuse);
    (void)litebus::os::Rmdir("/home/sn/scaler/config/");
}

const std::string DELEGATE_CONTAINER_VOLUME_MOUNTS_NO = R"(
{
    "image": "image",
    "env": [],
    "command":{
        "command1": "100",
        "command2": "1"
    },
    "args": {
        "args1": "100",
        "args2": "1"
    },
    "workingDir": "workingDir",
    "uid": 1003,
    "gid": 1003
}
)";

const std::string DELEGATE_CONTAINER_VOLUME_MOUNTS_INVALID = R"(
{
    "image": "image",
    "env": [],
    "command": {
        "command1": "100",
        "command2": "1"
    },
    "workingDir": "workingDir",
    "args": {
        "args1": "100",
        "args2": "1"
    },
    "uid": 1003,
    "gid": 1003,
    "volumeMounts": {}
}
)";

const std::string DELEGATE_CONTAINER_VOLUME_MOUNTS_NO_NAME = R"(
{
	"image": "image",
	"env": [],
	"command": {
		"command1": "100",
		"command2": "1"
	},
	"workingDir": "workingDir",
	"args": {
		"args1": "100",
		"args2": "1"
	},
	"uid": 1003,
	"gid": 1003,
	"volumeMounts": [{
			"name": "uds",
			"mountPath": "/home/uds"
		},
		{
			"mountPath": "/home/uds/invalid"
		}
	]
}
)";

const std::string DELEGATE_CONTAINER_NO_UID_GID_AND_SENSITIVE_VOLUME_MOUNT = R"(
{
	"image": "image",
	"env": [],
	"command": {
		"command1": "100",
		"command2": "1"
	},
    "volumeMounts": [
        {
			"name": "sts-config",
			"mountPath": "/home/uds"
		},
        {
            "name": "uds",
            "mountPath": "/home/uds"
        }
	],
	"workingDir": "workingDir",
	"args": {
		"args1": "100",
		"args2": "1"
	}
}
)";

TEST_F(ScalerTest, ParseContainerInfoTest)
{
    ::resources::InstanceInfo instanceInfo;
    instanceInfo.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = DELEGATE_CONTAINER_STR;

    litebus::Option<std::shared_ptr<V1Container>> delegateContainerOp;
    ParseDelegateContainer(instanceInfo, delegateContainerOp);
    EXPECT_TRUE(delegateContainerOp.IsSome());

    auto delegateContainer = delegateContainerOp.Get();
    EXPECT_EQ(delegateContainer->GetSecurityContext()->GetRunAsGroup(), 1003);
    EXPECT_EQ(delegateContainer->GetSecurityContext()->GetRunAsUser(), 1003);

    // readiness
    EXPECT_TRUE(delegateContainer->ReadinessProbeIsSet());
    EXPECT_TRUE(delegateContainer->GetReadinessProbe()->ExecIsSet());
    EXPECT_EQ(delegateContainer->GetReadinessProbe()->GetExec()->GetCommand().at(0), "readiness");
    EXPECT_EQ(delegateContainer->GetReadinessProbe()->GetExec()->GetCommand().at(1), "probe");
    EXPECT_EQ(delegateContainer->GetReadinessProbe()->GetInitialDelaySeconds(), 1100);
    EXPECT_EQ(delegateContainer->GetReadinessProbe()->GetTimeoutSeconds(), 1200);
    EXPECT_EQ(delegateContainer->GetReadinessProbe()->GetPeriodSeconds(), 1300);
    EXPECT_EQ(delegateContainer->GetReadinessProbe()->GetSuccessThreshold(), 1400);
    EXPECT_EQ(delegateContainer->GetReadinessProbe()->GetFailureThreshold(), 1500);

    // liveness
    EXPECT_TRUE(delegateContainer->LivenessProbeIsSet());
    EXPECT_TRUE(delegateContainer->GetLivenessProbe()->ExecIsSet());
    EXPECT_EQ(delegateContainer->GetLivenessProbe()->GetExec()->GetCommand().at(0), "liveness");
    EXPECT_EQ(delegateContainer->GetLivenessProbe()->GetExec()->GetCommand().at(1), "probe");
    EXPECT_EQ(delegateContainer->GetLivenessProbe()->GetInitialDelaySeconds(), 100);
    EXPECT_EQ(delegateContainer->GetLivenessProbe()->GetTimeoutSeconds(), 200);
    EXPECT_EQ(delegateContainer->GetLivenessProbe()->GetPeriodSeconds(), 300);
    EXPECT_EQ(delegateContainer->GetLivenessProbe()->GetSuccessThreshold(), 400);
    EXPECT_EQ(delegateContainer->GetLivenessProbe()->GetFailureThreshold(), 500);

    auto envs = delegateContainer->GetEnv();
    EXPECT_EQ(envs.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(envs.at(0)->GetName(), "env_x");
    EXPECT_EQ(envs.at(0)->GetValue(), "value_x");

    EXPECT_EQ(envs.at(1)->GetName(), "env_y");
    EXPECT_EQ(envs.at(1)->GetValueFrom()->GetFieldRef()->GetApiVersion(), "v1");
    EXPECT_EQ(envs.at(1)->GetValueFrom()->GetFieldRef()->GetFieldPath(), "status.podIP");

    EXPECT_EQ(envs.at(2)->GetName(), "env_z");
    EXPECT_EQ(envs.at(2)->GetValueFrom()->GetResourceFieldRef()->GetContainerName(), "delegate_container");
    EXPECT_EQ(envs.at(2)->GetValueFrom()->GetResourceFieldRef()->GetResource(), "resource_x");
    EXPECT_EQ(envs.at(2)->GetValueFrom()->GetResourceFieldRef()->GetDivisor(), "divisor_x");

    EXPECT_EQ(delegateContainer->GetVolumeMounts().size(), static_cast<uint32_t>(1));
    for (const auto &mount : delegateContainer->GetVolumeMounts()) {
        if (mount->GetName() == "uds") {
            EXPECT_EQ(mount->GetMountPath(), "/home/uds");
            EXPECT_EQ(mount->GetSubPath(), "uds sub path");
            EXPECT_EQ(mount->GetMountPropagation(), "mount propagation");
            EXPECT_TRUE(mount->IsReadOnly());
        }
    }

    instanceInfo.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = DELEGATE_CONTAINER_VOLUME_MOUNTS_NO;
    ParseDelegateContainer(instanceInfo, delegateContainerOp);
    EXPECT_TRUE(delegateContainerOp.Get()->GetVolumeMounts().empty());

    instanceInfo.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = DELEGATE_CONTAINER_VOLUME_MOUNTS_INVALID;
    ParseDelegateContainer(instanceInfo, delegateContainerOp);
    EXPECT_TRUE(delegateContainerOp.Get()->GetVolumeMounts().empty());

    instanceInfo.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = DELEGATE_CONTAINER_VOLUME_MOUNTS_NO_NAME;
    ParseDelegateContainer(instanceInfo, delegateContainerOp);
    EXPECT_TRUE(delegateContainerOp.IsSome());
    EXPECT_EQ(delegateContainerOp.Get()->GetVolumeMounts().size(), static_cast<uint32_t>(1));
    EXPECT_EQ(delegateContainerOp.Get()->GetVolumeMounts().at(0)->GetName(), "uds");

    instanceInfo.mutable_createoptions()->operator[](DELEGATE_CONTAINER) =
        DELEGATE_CONTAINER_NO_UID_GID_AND_SENSITIVE_VOLUME_MOUNT;
    ParseDelegateContainer(instanceInfo, delegateContainerOp);
    EXPECT_TRUE(delegateContainerOp.IsSome());
    EXPECT_TRUE(delegateContainerOp.Get()->GetSecurityContext()->RunAsNonRootIsSet());
    EXPECT_FALSE(delegateContainerOp.Get()->GetSecurityContext()->IsRunAsNonRoot());
}

const std::string DELEGATE_VOLUMES = R"(
[{
	"name": "uds-volume",
	"hostPath": {
		"type": "type",
		"path": "/home/uds"
	}
}, {
	"name": "empty-volume",
	"emptyDir": {
		"medium": "medium",
		"sizeLimit": "sizeLimit"
	}
}, {
	"name": "secret-volume",
	"secret": {
		"secretName": "secret name",
		"defaultMode": 1,
		"items": [{
			"key": "password",
			"mode": 511,
			"path": "/home/xxx"
		}],
		"optional": true
	}
}, {
	"name": "config-map-volume",
	"configMap": {
		"defaultMode": 440,
		"name": "xx-config",
		"items": [{
			"mode": 1,
			"key": "xx-key",
			"path": "/home/xxx"
		}]
	}
}, {
	"emptyDir": {}
}]
)";

const std::string DELEGATE_HOST_ALIASES = R"(
{
    "ip1": [
        "hostname1-1"
    ],
    "ip2": [
        "hostname2-1",
        "hostname2-2"
    ],
    "ip3": []
}
)";

TEST_F(ScalerTest, ParseDelegateVolumesTest)
{
    ::resources::InstanceInfo instanceInfo;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_VOLUMES") = DELEGATE_VOLUMES;

    std::vector<std::shared_ptr<V1Volume>> volumes;
    ParseDelegateVolumes(instanceInfo, volumes);
    EXPECT_EQ(volumes.size(), static_cast<uint32_t>(4));  // emptyDir is invalid, because no name.

    EXPECT_EQ(volumes.at(0)->GetName(), "uds-volume");
    EXPECT_EQ(volumes.at(0)->GetHostPath()->GetType(), "type");

    EXPECT_EQ(volumes.at(1)->GetName(), "empty-volume");
    EXPECT_EQ(volumes.at(1)->GetEmptyDir()->GetMedium(), "medium");
    EXPECT_EQ(volumes.at(1)->GetEmptyDir()->GetSizeLimit(), "sizeLimit");

    EXPECT_EQ(volumes.at(2)->GetName(), "secret-volume");
    EXPECT_EQ(volumes.at(2)->GetSecret()->GetSecretName(), "secret name");
    EXPECT_EQ(volumes.at(2)->GetSecret()->GetDefaultMode(), 1);
    EXPECT_TRUE(volumes.at(2)->GetSecret()->IsOptional());

    EXPECT_EQ(volumes.at(2)->GetSecret()->GetItems().size(), static_cast<uint32_t>(1));
    EXPECT_EQ(volumes.at(2)->GetSecret()->GetItems().at(0)->GetKey(), "password");
    EXPECT_EQ(volumes.at(2)->GetSecret()->GetItems().at(0)->GetMode(), 511);
    EXPECT_EQ(volumes.at(2)->GetSecret()->GetItems().at(0)->GetPath(), "/home/xxx");

    EXPECT_EQ(volumes.at(3)->GetName(), "config-map-volume");
    EXPECT_EQ(volumes.at(3)->GetConfigMap()->GetName(), "xx-config");
    EXPECT_EQ(volumes.at(3)->GetConfigMap()->GetDefaultMode(), 440);
    EXPECT_EQ(volumes.at(3)->GetConfigMap()->GetItems().size(), static_cast<uint32_t>(1));
    EXPECT_EQ(volumes.at(3)->GetConfigMap()->GetItems().at(0)->GetMode(), 1);
    EXPECT_EQ(volumes.at(3)->GetConfigMap()->GetItems().at(0)->GetKey(), "xx-key");
    EXPECT_EQ(volumes.at(3)->GetConfigMap()->GetItems().at(0)->GetPath(), "/home/xxx");

    std::vector<std::shared_ptr<V1Volume>> volumes_illegal;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_VOLUMES") = "{]";
    ParseDelegateVolumes(instanceInfo, volumes_illegal);  // invalid
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_VOLUMES") = "{}";
    ParseDelegateVolumes(instanceInfo, volumes_illegal);  // not array
    EXPECT_TRUE(volumes_illegal.empty());
}

TEST_F(ScalerTest, ParseDelegateVolumeMountsTest)
{
    ::resources::InstanceInfo instanceInfo;
    std::vector<std::shared_ptr<V1VolumeMount>> volumeMounts;
    ParseDelegateVolumeMounts(instanceInfo,"DELEGATE_VOLUME_MOUNTS", volumeMounts);

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_VOLUME_MOUNTS") = "{]";
    ParseDelegateVolumeMounts(instanceInfo,"DELEGATE_VOLUME_MOUNTS", volumeMounts);
    EXPECT_TRUE(volumeMounts.empty());

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_VOLUME_MOUNTS") = "{}";
    ParseDelegateVolumeMounts(instanceInfo,"DELEGATE_VOLUME_MOUNTS", volumeMounts);
    EXPECT_TRUE(volumeMounts.empty());

    const std::string DELEGATE_VOLUME_MOUNTS = R"(
    [{
        "name": "delegate-container-log",
        "mountPath": "/var/lib/docker/",
        "subPath": "delegate-container-log sub path",
        "subPathExpr": "delegate-container-log sub path expr",
        "mountPropagation": "delegate-container-log mount propagation"
    }, {
        "name": "kubepods_burstable",
        "mountPath": "/sys/fs/cgroup/memory/kubepods/burstable/",
        "subPath": "kubepods_burstable sub path",
        "subPathExpr": "kubepods_burstable sub path expr",
        "mountPropagation": "kubepods_burstable mount propagation",
        "readOnly": true
    }, {
        "name": "invalid-mount_path"
    }, {
        "mountPath": "/sys/fs/cgroup/memory/kubepods/burstable/"
    }]
    )";
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_VOLUME_MOUNTS") = DELEGATE_VOLUME_MOUNTS;
    ParseDelegateVolumeMounts(instanceInfo, "DELEGATE_VOLUME_MOUNTS", volumeMounts);
    EXPECT_EQ(volumeMounts.size(), static_cast<uint32_t>(2));

    EXPECT_FALSE(volumeMounts.at(0)->IsReadOnly());
    EXPECT_EQ(volumeMounts.at(0)->GetName(), "delegate-container-log");
    EXPECT_EQ(volumeMounts.at(0)->GetMountPath(), "/var/lib/docker/");

    EXPECT_TRUE(volumeMounts.at(1)->IsReadOnly());
    EXPECT_EQ(volumeMounts.at(1)->GetName(), "kubepods_burstable");
    EXPECT_EQ(volumeMounts.at(1)->GetSubPath(), "kubepods_burstable sub path");
}

const std::string DELEGATE_SIDECAR = R"(
[{
    "name": "name",
	"image": "image",
    "resourceRequirements": {
        "limits":{
            "cpu":"100",
            "mem":"101"
        },
        "requests":{
            "cpu":"102",
            "mem":"103"
        }
    },
    "lifecycle":{
        "preStop":{
            "exec":{
                "command":["pre","stop"]
            }
        },
        "postStart":{
            "exec":{
                "command":["post","start"]
            }
        }
    }
}]
)";

TEST_F(ScalerTest, ParseDelegateSidecarsTest)
{
    ::resources::InstanceInfo instanceInfo;
    std::vector<std::shared_ptr<V1Container>> sidecars;
    ParseDelegateSidecars(instanceInfo, sidecars);
    EXPECT_TRUE(sidecars.empty());

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_SIDECARS") = "{]";
    ParseDelegateSidecars(instanceInfo, sidecars);
    EXPECT_TRUE(sidecars.empty());

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_SIDECARS") = "{}";
    ParseDelegateSidecars(instanceInfo, sidecars);
    EXPECT_TRUE(sidecars.empty());

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_SIDECARS") = DELEGATE_SIDECAR;
    ParseDelegateSidecars(instanceInfo, sidecars);
    EXPECT_EQ(sidecars.size(), static_cast<uint32_t>(1));

    auto container = sidecars.at(0);
    EXPECT_EQ(container->GetName(), "name");
    EXPECT_EQ(container->GetImage(), "image");

    EXPECT_EQ(container->GetResources()->GetLimits().at("cpu"), "100");
    EXPECT_EQ(container->GetResources()->GetLimits().at("mem"), "101");
    EXPECT_EQ(container->GetResources()->GetRequests().at("mem"), "103");
    EXPECT_EQ(container->GetResources()->GetRequests().at("cpu"), "102");

    EXPECT_EQ(container->GetLifecycle()->GetPreStop()->GetExec()->GetCommand().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(container->GetLifecycle()->GetPreStop()->GetExec()->GetCommand().at(0), "pre");
    EXPECT_EQ(container->GetLifecycle()->GetPreStop()->GetExec()->GetCommand().at(1), "stop");

    EXPECT_EQ(container->GetLifecycle()->GetPostStart()->GetExec()->GetCommand().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(container->GetLifecycle()->GetPostStart()->GetExec()->GetCommand().at(1), "start");
    EXPECT_EQ(container->GetLifecycle()->GetPostStart()->GetExec()->GetCommand().at(0), "post");
}

TEST_F(ScalerTest, ParseDelegateInitContainersTest)
{
    ::resources::InstanceInfo instanceInfo;
    std::vector<std::shared_ptr<V1Container>> initContainers;
    ParseDelegateSidecars(instanceInfo, initContainers);
    EXPECT_TRUE(initContainers.empty());

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_INIT_CONTAINERS") = "{]";
    ParseDelegateInitContainers(instanceInfo, initContainers);
    EXPECT_TRUE(initContainers.empty());

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_INIT_CONTAINERS") = "{}";
    ParseDelegateInitContainers(instanceInfo, initContainers);
    EXPECT_TRUE(initContainers.empty());

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_INIT_CONTAINERS") = DELEGATE_SIDECAR;
    ParseDelegateInitContainers(instanceInfo, initContainers);
    EXPECT_EQ(initContainers.size(), static_cast<uint32_t>(1));

    auto container = initContainers.at(0);
    EXPECT_EQ(container->GetName(), "name");
    EXPECT_EQ(container->GetImage(), "image");

    EXPECT_EQ(container->GetResources()->GetLimits().at("cpu"), "100");
    EXPECT_EQ(container->GetResources()->GetLimits().at("mem"), "101");
    EXPECT_EQ(container->GetResources()->GetRequests().at("cpu"), "102");
    EXPECT_EQ(container->GetResources()->GetRequests().at("mem"), "103");

    EXPECT_EQ(container->GetLifecycle()->GetPreStop()->GetExec()->GetCommand().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(container->GetLifecycle()->GetPreStop()->GetExec()->GetCommand().at(0), "pre");
    EXPECT_EQ(container->GetLifecycle()->GetPreStop()->GetExec()->GetCommand().at(1), "stop");

    EXPECT_EQ(container->GetLifecycle()->GetPostStart()->GetExec()->GetCommand().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(container->GetLifecycle()->GetPostStart()->GetExec()->GetCommand().at(0), "post");
    EXPECT_EQ(container->GetLifecycle()->GetPostStart()->GetExec()->GetCommand().at(1), "start");
}

const std::string DELEGATE_TOLERATIONS = R"(
[{
"key": "key1",
"operator": "Equal",
"value": "value",
"effect": "NoSchedule"
}, {
"key": "key2",
"operator": "Equal",
"value": "value",
"effect": "NoSchedule"
}]
)";

const std::string DELEGATE_ANNOTATIONS = R"(
{
  "key1": "value1",
  "key2": "value2"
}
)";

const std::string DELEGATE_POD_SECCOMP_PROFILE = R"(
{ "type": "RuntimeDefault" }
)";

const std::string DELEGATE_NODE_AFFINITY = R"(
{
  "requiredDuringSchedulingIgnoredDuringExecution": {
      "nodeSelectorTerms": [{
	      "matchExpressions": [{
		     "key": "node.kubernetes.io/instance-type",
			 "operator": "In",
			 "values": ["physical.kat2ne.48xlarge.8.376t.ei.c002.ondemand", "physical.kat2ne.48xlarge.8.ei.pod101.ondemand"]
		  }]
	   }]
   }
}
)";

const std::string DELEGATE_AFFINITY = R"(
{
	"podAntiAffinity": {
		"preferredDuringSchedulingIgnoredDuringExecution": [{
			"podAffinityTerm": {
				"labelSelector": {
					"matchLabels": {
						"faasfrontend": ""
					}
				},
				"topologyKey": "topology.kubernetes.io/zone"
			},
			"weight": 100
		}]
	}
}
)";

const std::string DELEGATE_NODE_PREFERRED_AFFINITY = R"(
{
  "preferredDuringSchedulingIgnoredDuringExecution": [{
      "preference": {
	      "matchExpressions": [{
		     "key": "node.kubernetes.io/instance-type",
			 "operator": "In",
			 "values": ["physical.kat2ne.48xlarge.8.376t.ei.c002.ondemand", "physical.kat2ne.48xlarge.8.ei.pod101.ondemand"]
		  }]
	   },
      "weight": 1
   }]
}
)";

const std::string DELEGATE_RUNTIME_MANAGER = R"(
{
	"image": "testImage",
    "env": [{"name":"SOLOMON_APP_ID","value":"redis-bridge"},{"name":"SOLOMON_CONFIG_CENTER_URL"},{"name":"SOLOMON_DOCKER_ENV","value":"kwe_perf"}]
}
)";

const std::string DELEGATE_INIT_VOLUME_MOUNTS = R"(
[{
  "name": "delegate-container-init",
  "mountPath": "/opt/huawei/logs"
}]
)";

const std::string DELEGATE_INTI_ENVS = R"(
[
  {
    "name": "env1",
    "value": "value1"
  },
  {
    "name": "env2",
    "value": "value2"
  }
]
)";

const std::string DELEGATE_POD_INIT_LABELS = R"(
{
    "intK1": "val1",
    "intK2": "val2",
    "securityGroup": "123456"
}
)";

TEST_F(ScalerTest, CreatePodTemplateSpec)
{
    ::resources::InstanceInfo instanceInfo;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_VOLUMES") = DELEGATE_VOLUMES;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_HOST_ALIASES") = DELEGATE_HOST_ALIASES;
    instanceInfo.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = DELEGATE_CONTAINER_STR;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_SIDECARS") = DELEGATE_SIDECAR;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_INIT_CONTAINERS") = DELEGATE_SIDECAR;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_TOLERATIONS") =  DELEGATE_TOLERATIONS;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_POD_ANNOTATIONS") =  DELEGATE_ANNOTATIONS;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_POD_SECCOMP_PROFILE") =  DELEGATE_POD_SECCOMP_PROFILE;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_NODE_AFFINITY") =  DELEGATE_NODE_AFFINITY;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_AFFINITY") =  DELEGATE_AFFINITY;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_INIT_VOLUME_MOUNTS") =  DELEGATE_INIT_VOLUME_MOUNTS;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_INIT_ENV") =  DELEGATE_INTI_ENVS;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_POD_INIT_LABELS") =  DELEGATE_POD_INIT_LABELS;

    std::vector<std::shared_ptr<V1Volume>> volumes;
    ParseDelegateVolumes(instanceInfo, volumes);

    std::vector<std::shared_ptr<V1HostAlias>> delegateHostAliases;
    ParseDelegateHostAliases(instanceInfo, delegateHostAliases);

    litebus::Option<std::shared_ptr<V1Container>> delegateContainer;
    ParseDelegateContainer(instanceInfo, delegateContainer);
    std::vector<std::shared_ptr<V1VolumeMount>> delegateInitVolumeMounts;
    ParseDelegateVolumeMounts(instanceInfo, "DELEGATE_INIT_VOLUME_MOUNTS", delegateInitVolumeMounts);
    std::vector<std::shared_ptr<V1Container>> delegateSidecars;
    ParseDelegateSidecars(instanceInfo, delegateSidecars);
    std::vector<std::shared_ptr<V1Toleration>> tolerations;
    ParseDelegateTolerations(instanceInfo, tolerations);
    std::vector<std::shared_ptr<V1EnvVar>> initEnvs;
    ParseDelegateInitEnv(instanceInfo, initEnvs);
    auto annotations = ParseDelegateAnnotations(instanceInfo);
    EXPECT_EQ(annotations.size(), static_cast<size_t>(2));
    instanceInfo.add_labels("k1:v1");
    auto podInitLabels = GetPodLabelsForCreate(instanceInfo);
    EXPECT_EQ(podInitLabels.size(), static_cast<size_t>(4));
    EXPECT_TRUE(podInitLabels.find("k1") != podInitLabels.end() && podInitLabels.find("k1")->second == "v1");
    nlohmann::json deploymentJSON = nlohmann::json::parse(DEPLOYMENT_JSON);
    auto templateDeployment = std::make_shared<V1Deployment>();
    templateDeployment->FromJson(deploymentJSON);

    scaler::ResourcePool pool{ .name = "",
                               .poolSize = 1,
                               .requestResources = { { CPU_RESOURCE, "500m" }, { MEMORY_RESOURCE, "500Mi" } },
                               .limitResources = { { CPU_RESOURCE, "500m" }, { MEMORY_RESOURCE, "500Mi" } } };

    pool.delegateVolumes = volumes;
    pool.delegateHostAliases = delegateHostAliases;
    pool.delegateContainer = delegateContainer;
    pool.delegateSidecars = delegateSidecars;
    pool.delegateTolerations = tolerations;
    pool.delegateInitEnvs = initEnvs;
    pool.delegateSecCompProfile = ParseSeccompProfile(instanceInfo);
    pool.delegateInitVolumeMounts = delegateInitVolumeMounts;
    std::unordered_map<AffinityType, std::vector<std::string>> affinityTypeLabels;
    pool.affinity = ParseAffinity(affinityTypeLabels);
    ParseAffinityFromCreateOpts(instanceInfo, pool.affinity);
    ParseNodeAffinity(instanceInfo, pool.affinity);

    auto spec = actor_->GeneratePodTemplateSpec(pool, templateDeployment);
    EXPECT_EQ(spec->GetSpec()->GetVolumes().size(), static_cast<uint32_t>(18));
    int32_t count = 0;
    for (const auto &item : spec->GetSpec()->GetVolumes()) {
        if (item->GetName() == "uds-volume" || item->GetName() == "empty-volume" ||
            item->GetName() == "secret-volume") {
            count++;
        }
    }
    // There are 3 valid volumes variables in JSON.
    EXPECT_EQ(count, 3);

    EXPECT_EQ(spec->GetSpec()->GetTolerations().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(spec->GetSpec()->GetSecurityContext()->GetSeccompProfile()->GetType(), "RuntimeDefault");
    EXPECT_EQ(spec->GetSpec()->GetAffinity()->GetNodeAffinity()->RequiredDuringSchedulingIgnoredDuringExecutionIsSet(),
              true);
    EXPECT_EQ(spec->GetSpec()->GetContainers().size(), static_cast<uint32_t>(4));

    for (auto container: spec->GetSpec()->GetContainers()) {
        YRLOG_INFO("container name : {}", container->GetName());
    }
    // There are 3 environment variables in JSON.
    // By default, ip and port of datasystem are added in code.
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetEnv().size(), static_cast<uint32_t>(5));

    auto env = spec->GetSpec()->GetContainers().at(0)->GetEnv().at(2);
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetEnv().at(2)->GetName(), "env_z");
    EXPECT_EQ(env->GetValueFrom()->GetResourceFieldRef()->GetContainerName(), "delegate_container");

    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetEnv().at(3)->GetName(), "DATASYSTEM_HOST");
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetEnv().at(4)->GetName(), "DATASYSTEM_PORT");

    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetVolumeMounts().size(), static_cast<uint32_t>(1));
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetVolumeMounts().at(0)->GetName(), "uds");
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetVolumeMounts().at(0)->GetMountPath(), "/home/uds");

    EXPECT_EQ(spec->GetSpec()->GetContainers().at(1)->GetResources()->GetLimits()["cpu"], "3");
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(1)->GetResources()->GetLimits()["memory"], "6Gi");
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetResources()->GetLimits()["cpu"], "500m");
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetResources()->GetLimits()["memory"], "500Mi");

    EXPECT_EQ(spec->GetSpec()->GetInitContainers().size(), static_cast<uint32_t>(1));
    EXPECT_EQ(spec->GetSpec()->GetInitContainers()[0]->GetVolumeMounts().size(), static_cast<uint32_t>(3));
    EXPECT_EQ(spec->GetSpec()->GetInitContainers()[0]->GetEnv().size(), static_cast<uint32_t>(2));

    EXPECT_TRUE(spec->GetSpec()->HostAliasesIsSet());
    EXPECT_EQ(spec->GetSpec()->GetHostAliases().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(spec->GetSpec()->GetHostAliases().at(0)->GetIp(), "ip1");
    EXPECT_EQ(spec->GetSpec()->GetHostAliases().at(0)->GetHostnames().size(), static_cast<uint32_t>(1));
    EXPECT_EQ(spec->GetSpec()->GetHostAliases().at(0)->GetHostnames().at(0), "hostname1-1");
    EXPECT_EQ(spec->GetSpec()->GetHostAliases().at(1)->GetIp(), "ip2");
    EXPECT_EQ(spec->GetSpec()->GetHostAliases().at(1)->GetHostnames().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(spec->GetSpec()->GetHostAliases().at(1)->GetHostnames().at(0), "hostname2-1");
    EXPECT_EQ(spec->GetSpec()->GetHostAliases().at(1)->GetHostnames().at(1), "hostname2-2");

    EXPECT_TRUE(spec->GetSpec()->AutomountServiceAccountTokenIsSet());
    EXPECT_FALSE(spec->GetSpec()->IsAutomountServiceAccountToken());
    EXPECT_FALSE(spec->GetSpec()->ServiceAccountNameIsSet());

    // create container without uid/gid
    instanceInfo.mutable_createoptions()->operator[](DELEGATE_CONTAINER) =
        DELEGATE_CONTAINER_NO_UID_GID_AND_SENSITIVE_VOLUME_MOUNT;

    // create system function
    instanceInfo.mutable_createoptions()->operator[](RESOURCE_OWNER_KEY) = SYSTEM_OWNER_VALUE;

    ParseDelegateContainer(instanceInfo, delegateContainer);
    pool.delegateContainer = delegateContainer;
    pool.isSystemFunc = IsSystemFunction(instanceInfo);
    actor_->member_->etcdAuthType = "TLS";
    actor_->AddVolumesAndMountsForSystemFunc(pool);

    spec = actor_->GeneratePodTemplateSpec(pool, templateDeployment);
    EXPECT_TRUE(spec->GetSpec()->GetContainers().at(0)->GetSecurityContext()->RunAsNonRootIsSet());
    EXPECT_FALSE(spec->GetSpec()->GetContainers().at(0)->GetSecurityContext()->IsRunAsNonRoot());
    EXPECT_FALSE(spec->GetSpec()->GetContainers().at(0)->GetSecurityContext()->RunAsGroupIsSet());
    EXPECT_FALSE(spec->GetSpec()->GetContainers().at(0)->GetSecurityContext()->RunAsUserIsSet());
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(0)->GetVolumeMounts().size(), static_cast<uint32_t>(1));

    EXPECT_TRUE(spec->GetSpec()->AutomountServiceAccountTokenIsSet());
    EXPECT_TRUE(spec->GetSpec()->IsAutomountServiceAccountToken());
    EXPECT_TRUE(spec->GetSpec()->ServiceAccountNameIsSet());
    EXPECT_EQ(spec->GetSpec()->GetServiceAccountName(), "system-function");

    // create delegate runtime manager
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_RUNTIME_MANAGER") = DELEGATE_RUNTIME_MANAGER;
    litebus::Option<std::shared_ptr<V1Container>> delegateRuntimeManager;
    ParseDelegateRuntimeManager(instanceInfo, delegateRuntimeManager);
    pool.delegateRuntimeManager = delegateRuntimeManager;
    auto prevEnvSize = spec->GetSpec()->GetContainers().at(1)->GetEnv().size();
    spec = actor_->GeneratePodTemplateSpec(pool, templateDeployment);
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(1)->GetImage(), "testImage");
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(1)->GetEnv().size(), prevEnvSize + 3);

    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_NODE_AFFINITY") =  DELEGATE_NODE_PREFERRED_AFFINITY;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_AFFINITY") =  DELEGATE_AFFINITY;
    ParseNodeAffinity(instanceInfo, pool.affinity);
    ParseAffinityFromCreateOpts(instanceInfo, pool.affinity);
    spec = actor_->GeneratePodTemplateSpec(pool, templateDeployment);
    EXPECT_EQ(spec->GetSpec()->GetAffinity()->GetNodeAffinity()->PreferredDuringSchedulingIgnoredDuringExecutionIsSet(),
              true);
    EXPECT_EQ(spec->GetSpec()->GetAffinity()->GetPodAntiAffinity()->PreferredDuringSchedulingIgnoredDuringExecutionIsSet(),
              true);
}

TEST_F(ScalerTest, CreateAgentTest)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);

    litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(), "invalid_request");
    auto createAgentRequest = std::make_shared<messages::CreateAgentRequest>();
    ::resources::InstanceInfo info;
    info.set_requestid("test-request-01");
    info.set_instanceid("test-instance-01");
    createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
    litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                   createAgentRequest->SerializeAsString());
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-01"; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().code() == StatusCode::GS_START_CREATE_POD_FAILED; });

    actor_->member_->templateDeployment = nullptr;
    actor_->member_->agentTemplatePath = "";
    ::resources::Resources resources;
    info.mutable_resources()->CopyFrom(resources);
    info.set_requestid("test-request-error-deployment");
    createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
    litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                   createAgentRequest->SerializeAsString());
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-error-deployment"; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().code() == StatusCode::GS_START_CREATE_POD_FAILED; });

    auto localVarResult = std::make_shared<V1Deployment>();
    nlohmann::json localVarJson = nlohmann::json::parse(DEPLOYMENT_JSON);
    auto converted = localVarResult->FromJson(localVarJson);
    if (!converted) {
        YRLOG_ERROR("failed to convert deployment");
    }
    actor_->SetTemplateDeployment(localVarResult);

    info.set_requestid("test-request-02");
    createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
    litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                   createAgentRequest->SerializeAsString());
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-02"; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().code() == StatusCode::GS_START_CREATE_POD_FAILED; });
    info.mutable_resources()->CopyFrom(GetResources());
    info.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = "invalid json";
    info.set_requestid("test-request-03");
    createAgentRequest->mutable_instanceinfo()->CopyFrom(info);

    litebus::Promise<std::shared_ptr<V1Pod>> promise;
    promise.SetFailed(100);
    EXPECT_CALL(*mockClient, CreateNamespacedPod).WillOnce(testing::Return(promise.GetFuture()));
    litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                   createAgentRequest->SerializeAsString());
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-03"; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().code() == StatusCode::GS_START_CREATE_POD_FAILED; });

    auto pod = GetReadyPod();
    EXPECT_CALL(*mockClient, CreateNamespacedPod).WillRepeatedly(testing::Return(pod));
    litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                   createAgentRequest->SerializeAsString());
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-03"; });
    ASSERT_AWAIT_TRUE([&]() -> bool {
        litebus::Async(actor_->GetAID(), &ScalerActor::OnPodModified, K8sEventType::EVENT_TYPE_MODIFIED, pod);
        return GetTestResponse().code() == StatusCode::SUCCESS;
    });

    info.set_requestid("test-request-04");
    info.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = DELEGATE_CONTAINER_STR;
    createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
    litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                   createAgentRequest->SerializeAsString());
    ASSERT_AWAIT_TRUE([&]() -> bool {
        litebus::Async(actor_->GetAID(), &ScalerActor::OnPodModified, K8sEventType::EVENT_TYPE_MODIFIED, pod);
        return GetTestResponse().requestid() == "test-request-04";
    });
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().code() == StatusCode::SUCCESS; });
    ASSERT_AWAIT_TRUE([&]() -> bool {
        auto creteOpt = GetTestResponse().updatedcreateoptions();
        auto iter = creteOpt.find(DELEGATE_CONTAINER_ID_KEY);
        return iter != creteOpt.end() && iter->second == "1234567";
    });
}

TEST_F(ScalerTest, CreateAgentTestWithAffinity)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    auto pod = GetReadyPod();

    litebus::Future<std::shared_ptr<V1Pod>> podArgFuture;
    EXPECT_CALL(*mockClient, CreateNamespacedPod)
        .WillRepeatedly(testing::DoAll(test::FutureArg<1>(&podArgFuture), testing::Return(pod)));

    nlohmann::json localVarJson = nlohmann::json::parse(DEPLOYMENT_JSON);

    auto localVarResult = std::make_shared<V1Deployment>();
    auto converted = localVarResult->FromJson(localVarJson);
    if (!converted) {
        YRLOG_ERROR("failed to convert deployment");
    }
    litebus::Async(actor_->GetAID(), &ScalerActor::SetTemplateDeployment, localVarResult);

    ::resources::InstanceInfo info;

    info.set_requestid("test-request-01");
    info.mutable_labels()->Add("label");
    info.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = DELEGATE_CONTAINER_STR;
    info.mutable_createoptions()->operator[]("DELEGATE_POD_ANNOTATIONS") = DELEGATE_ANNOTATIONS;
    info.mutable_createoptions()->operator[]("DELEGATE_POD_INIT_LABELS") = DELEGATE_POD_INIT_LABELS;
    info.mutable_resources()->CopyFrom(GetResources());
    info.mutable_scheduleoption()->mutable_affinity()->mutable_instanceaffinity()->mutable_affinity()->operator[](
        "antiAffinityInstance") = AffinityType::PreferredAntiAffinity;
    info.mutable_scheduleoption()->mutable_affinity()->mutable_instanceaffinity()->mutable_affinity()->operator[](
        "affinityInstance") = AffinityType::RequiredAffinity;

    auto createAgentRequest = std::make_shared<messages::CreateAgentRequest>();
    createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
    litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                   createAgentRequest->SerializeAsString());
    ASSERT_AWAIT_TRUE([&]() -> bool {
        litebus::Async(actor_->GetAID(), &ScalerActor::OnPodModified, K8sEventType::EVENT_TYPE_MODIFIED, pod);
        return GetTestResponse().requestid() == "test-request-01";
    });
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().code() == StatusCode::SUCCESS; });
    ASSERT_AWAIT_TRUE([&]() -> bool {
        auto creteOpt = GetTestResponse().updatedcreateoptions();
        auto iter = creteOpt.find(DELEGATE_CONTAINER_ID_KEY);
        return iter != creteOpt.end() && iter->second == "1234567";
    });

    ASSERT_AWAIT_READY(podArgFuture);
    auto podArg = podArgFuture.Get();

    EXPECT_TRUE(podArg->GetMetadata()->GetLabels().find("label") != podArg->GetMetadata()->GetLabels().end());
    EXPECT_TRUE(podArg->GetMetadata()->GetLabels().find("securityGroup") != podArg->GetMetadata()->GetLabels().end());
    EXPECT_TRUE(podArg->GetMetadata()->GetLabels()["securityGroup"] == "123456");
    std::string expectAffinity =
        "{\"podAffinity\":{\"preferredDuringSchedulingIgnoredDuringExecution\":[],"
        "\"requiredDuringSchedulingIgnoredDuringExecution\":[{\"labelSelector\":{\"matchLabels\":{\"affinityInstance\":"
        "\"\"}},\"topologyKey\":\"kubernetes.io/"
        "hostname\"}]},\"podAntiAffinity\":{\"preferredDuringSchedulingIgnoredDuringExecution\":[{\"podAffinityTerm\":{"
        "\"labelSelector\":{\"matchLabels\":{\"antiAffinityInstance\":\"\"}},\"topologyKey\":\"kubernetes.io/"
        "hostname\"},\"weight\":100}],\"requiredDuringSchedulingIgnoredDuringExecution\":[]}}";
    EXPECT_STREQ(podArg->GetSpec()->GetAffinity()->ToJson().dump().c_str(), expectAffinity.c_str());
    EXPECT_EQ(podArg->GetMetadata()->GetAnnotations().size(), static_cast<uint32_t>(3));
    EXPECT_EQ(podArg->GetMetadata()->GetAnnotations()["yr-default"], "yr-default");
}

TEST_F(ScalerTest, CreatePodTestCPUBind)
{
    ::resources::InstanceInfo instanceInfo;
    instanceInfo.mutable_createoptions()->operator[](DELEGATE_CONTAINER) = DELEGATE_CONTAINER_STR;

    litebus::Option<std::shared_ptr<V1Container>> delegateContainer;
    ParseDelegateContainer(instanceInfo, delegateContainer);
    nlohmann::json deploymentJSON = nlohmann::json::parse(DEPLOYMENT_JSON);
    auto templateDeployment = std::make_shared<V1Deployment>();
    templateDeployment->FromJson(deploymentJSON);

    scaler::ResourcePool pool{ .name = "",
                               .poolSize = 1,
                               .requestResources = { { CPU_RESOURCE, "24000m" }, { MEMORY_RESOURCE, "500Mi" } },
                               .limitResources = { { CPU_RESOURCE, "24000m" }, { MEMORY_RESOURCE, "500Mi" } } };
    pool.delegateContainer = delegateContainer;
    pool.isNeedBindCPU = IsNeedBindCPU(pool.limitResources);

    auto spec = actor_->GeneratePodTemplateSpec(pool, templateDeployment);
    EXPECT_EQ(spec->GetSpec()->GetContainers().size(), static_cast<uint32_t>(3));
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(1)->GetResources()->GetLimits()["cpu"], "1000m");
    EXPECT_EQ(spec->GetSpec()->GetContainers().at(1)->GetResources()->GetLimits()["memory"], "900Mi");
}

TEST_F(ScalerTest, WatchInstanceTest)
{
    actor_->Register();
    WaitRegisterComplete();
    auto mockClient = std::make_shared<MockKubeClient>();
    mockClient->InitOwnerReference("default", "function-master");
    mockClient->GetOwnerReference()->SetUid("abc");
    actor_->SetKubeClient(mockClient);
    resource_view::InstanceInfo instance0 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/000", static_cast<int32_t>(InstanceState::SCHEDULING));
    metaStorageAccessor_->Put(instance0.instanceid(), "invalid json").Get();

    auto pod1 = std::make_shared<V1Pod>();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPodIP("127.0.0.1");
    pod1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod1->GetMetadata()->SetName("function-agent-001");
    pod1->GetMetadata()->GetLabels()["yr-reuse"] = "false";
    pod1->GetMetadata()->GetLabels()["app"] = "function-agent";
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillRepeatedly(testing::Return(pod1));

    auto pod2 = std::make_shared<V1Pod>();
    pod2->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod2->GetStatus()->SetPodIP("127.0.0.2");
    pod2->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod2->GetMetadata()->SetName("function-agent-002");
    pod2->GetMetadata()->GetLabels()["yr-reuse"] = "false";
    pod2->GetMetadata()->GetLabels()["app"] = "function-agent";

    std::string jsonString1;
    resource_view::InstanceInfo instance1 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/001", static_cast<int32_t>(InstanceState::SCHEDULING));
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString1, instance1));
    metaStorageAccessor_->Put(instance1.instanceid(), jsonString1).Get();

    std::string jsonString2;
    resource_view::InstanceInfo instance2 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/002", static_cast<int32_t>(InstanceState::CREATING));
    (*instance2.mutable_createoptions())["DELEGATE_POD_LABELS"] = R"({"123":"123"})";
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString2, instance2));

    std::string jsonString3;
    resource_view::InstanceInfo instance3 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/003", static_cast<int32_t>(InstanceState::FATAL));
    (*instance3.mutable_createoptions())["DELEGATE_POD_LABELS"] = R"({"123":"123"})";
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString3, instance3));
    metaStorageAccessor_->Put(instance3.instanceid(), jsonString3).Get();

    jsonString3 = "";
    instance3 = CreateInstance(INSTANCE_PATH_PREFIX + "/003", static_cast<int32_t>(InstanceState::FAILED));
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString3, instance3));
    metaStorageAccessor_->Put(instance3.instanceid(), jsonString3).Get();

    std::string jsonString4;
    resource_view::InstanceInfo instance4 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/004", static_cast<int32_t>(InstanceState::FATAL));
    instance4.set_functionagentid("function-agent-004");
    instance4.mutable_scheduleoption()->set_schedpolicyname("");
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString4, instance4));
    litebus::Promise<std::shared_ptr<functionsystem::V1Pod>> errPromise;
    errPromise.SetFailed(500);
    bool isRead = false;
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillOnce(testing::DoAll(testing::Assign(&isRead, true), testing::Return(errPromise.GetFuture())));
    metaStorageAccessor_->Put(instance4.instanceid(), jsonString4).Get();
    EXPECT_AWAIT_TRUE([&isRead]() -> bool { return isRead; });
    std::string jsonString5;
    resource_view::InstanceInfo instance5 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/005", static_cast<int32_t>(InstanceState::CREATING));
    (*instance5.mutable_createoptions())["DELEGATE_POD_LABELS"] = R"(123)";
    instance5.add_labels("label");
    instance5.add_labels("tenantId:123");
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString5, instance5));

    std::string jsonString6;
    resource_view::InstanceInfo instance6 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/006", static_cast<int32_t>(InstanceState::RUNNING));
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString6, instance6));

    std::string jsonString7;
    resource_view::InstanceInfo instance7 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/007", static_cast<int32_t>(InstanceState::SUB_HEALTH));
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString7, instance7));

    std::string jsonString8;
    resource_view::InstanceInfo instance8 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/008", static_cast<int32_t>(InstanceState::SCHEDULE_FAILED));
    instance8.set_functionagentid("");
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString8, instance8));

    std::string jsonString9;
    resource_view::InstanceInfo instance9 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/009", static_cast<int32_t>(InstanceState::FATAL));
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString9, instance9));
    instance9.set_functionproxyid(INSTANCE_MANAGER_OWNER);

    litebus::Future<std::shared_ptr<Object>> body1;
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillRepeatedly(testing::Return(pod1));
    EXPECT_CALL(*mockClient, PatchNamespacedPod).WillOnce(testing::DoAll(FutureArg<2>(&body1), testing::Return(pod1)));
    metaStorageAccessor_->Put(instance5.instanceid(), jsonString5).Get();
    ASSERT_AWAIT_READY(body1);
    EXPECT_TRUE(ContainValue(body1.Get(), "\"tenantId\":\"123\""));
    EXPECT_TRUE(ContainValue(body1.Get(), "\"label\":\"\""));
    EXPECT_TRUE(ContainValue(body1.Get(), "function-master"));

    bool isFinished = false;
    EXPECT_CALL(*mockClient, PatchNamespacedPod)
        .WillOnce(testing::DoAll(testing::Assign(&isFinished, true), testing::Return(pod1)));
    metaStorageAccessor_->Put(instance5.instanceid(), jsonString5).Get();
    EXPECT_AWAIT_TRUE([&isFinished]() -> bool { return isFinished; });

    jsonString5 = "";
    instance5.mutable_createoptions()->clear();
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString5, instance5));
    isFinished = false;
    EXPECT_CALL(*mockClient, PatchNamespacedPod)
        .WillOnce(testing::DoAll(testing::Assign(&isFinished, true), testing::Return(pod1)));
    metaStorageAccessor_->Put(instance5.instanceid(), jsonString5).Get();
    EXPECT_AWAIT_TRUE([&isFinished]() -> bool { return isFinished; });

    isFinished = false;
    EXPECT_CALL(*mockClient, PatchNamespacedPod)
        .WillOnce(testing::DoAll(testing::Assign(&isFinished, true), testing::Return(pod1)));
    metaStorageAccessor_->Put(instance2.instanceid(), jsonString2).Get();
    EXPECT_AWAIT_TRUE([&isFinished]() -> bool { return isFinished; });

    litebus::Future<std::shared_ptr<Object>> body;
    EXPECT_CALL(*mockClient, PatchNamespacedPod).WillOnce(testing::DoAll(FutureArg<2>(&body), testing::Return(pod1)));
    metaStorageAccessor_->Put(instance6.instanceid(), jsonString6).Get();
    ASSERT_AWAIT_READY(body);
    EXPECT_TRUE(ContainValue(body.Get(), "\"instanceStatus\":\"running\""));

    body = {};
    EXPECT_CALL(*mockClient, PatchNamespacedPod).WillRepeatedly(testing::DoAll(FutureArg<2>(&body), testing::Return(pod1)));
    metaStorageAccessor_->Put(instance7.instanceid(), jsonString7).Get();
    ASSERT_AWAIT_READY(body);
    EXPECT_TRUE(ContainValue(body.Get(), "\"instanceStatus\":\"subHealth\""));

    litebus::Future<std::string> podName = "";
    EXPECT_CALL(*mockClient, DeleteNamespacedPod)
        .WillOnce(testing::DoAll(FutureArg<0>(&podName), testing::Return(pod1)));
    metaStorageAccessor_->Put(instance9.instanceid(), jsonString9).Get();
    metaStorageAccessor_->Put(instance8.instanceid(), jsonString8).Get();
    ASSERT_AWAIT_READY(podName);
    EXPECT_EQ(podName.Get(), "function-agent-001");

    bool isDeleteFinished = false;
    EXPECT_CALL(*mockClient, DeleteNamespacedPod)
        .WillRepeatedly(testing::DoAll(testing::Assign(&isDeleteFinished, true), testing::Return(pod2)));
    metaStorageAccessor_->Delete(instance8.instanceid()).Get();
    metaStorageAccessor_->Delete(instance9.instanceid()).Get();
    metaStorageAccessor_->Delete(instance7.instanceid()).Get();
    metaStorageAccessor_->Delete(instance6.instanceid()).Get();
    metaStorageAccessor_->Delete(instance5.instanceid()).Get();
    metaStorageAccessor_->Delete(instance4.instanceid()).Get();
    metaStorageAccessor_->Delete(instance3.instanceid()).Get();
    metaStorageAccessor_->Delete(instance0.instanceid()).Get();
    metaStorageAccessor_->Delete(instance2.instanceid()).Get();
    EXPECT_AWAIT_TRUE([&isDeleteFinished]() -> bool { return isDeleteFinished; });
    actor_->SetPodMap({});
    actor_->SetNodeMap({});
}

TEST_F(ScalerTest, UpdateLabelPodNotFound)
{
    actor_->Register();
    WaitRegisterComplete();
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    resource_view::InstanceInfo instanceInvalid =
        CreateInstance(INSTANCE_PATH_PREFIX + "/000", static_cast<int32_t>(InstanceState::RUNNING));
    instanceInvalid.set_functionagentid(FUNCTION_AGENT_ID_PREFIX + "127.0.0.2-8080");
    std::string jsonStringInvalid;
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonStringInvalid, instanceInvalid));

    auto isFinished = false;
    litebus::Future<std::shared_ptr<Object>> body;
    litebus::Future<std::shared_ptr<Object>> body2;
    litebus::Promise<std::shared_ptr<functionsystem::V1Pod>> errPromise;
    errPromise.SetFailed(500);
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillRepeatedly(testing::DoAll(testing::Assign(&isFinished, true), testing::Return(errPromise.GetFuture())));
    metaStorageAccessor_->Put(instanceInvalid.instanceid(), jsonStringInvalid).Get();
    EXPECT_AWAIT_TRUE([&isFinished]() -> bool { return isFinished; });
    actor_->SetPodMap({});
    actor_->SetNodeMap({});
}

TEST_F(ScalerTest, UpdateLabelSyncTest)
{
    actor_->Register();
    WaitRegisterComplete();
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    auto pod1 = std::make_shared<V1Pod>();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPodIP("127.0.0.1");
    pod1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod1->GetMetadata()->SetName("function-agent-001");
    pod1->GetMetadata()->GetLabels()["yr-reuse"] = "false";
    pod1->GetMetadata()->GetLabels()["123"] = "1234";

    resource_view::InstanceInfo instance =
        CreateInstance(INSTANCE_PATH_PREFIX + "/001", static_cast<int32_t>(InstanceState::RUNNING));
    (*instance.mutable_createoptions())["DELEGATE_POD_LABELS"] = R"({"123":"123"})";
    std::string jsonString1;
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString1, instance));

    (*instance.mutable_createoptions())["DELEGATE_POD_LABELS"] = R"({"123":"1234"})";
    std::string jsonString2;
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString2, instance));

    auto isFinished = false;
    litebus::Future<std::shared_ptr<Object>> body;
    litebus::Future<std::shared_ptr<Object>> body2;
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillRepeatedly(testing::Return(pod1));
    EXPECT_CALL(*mockClient, PatchNamespacedPod)
        .WillOnce(testing::DoAll(FutureArg<2>(&body), testing::Return(pod1)))
        .WillOnce(testing::DoAll(testing::Assign(&isFinished, true), FutureArg<2>(&body2), testing::Return(pod1))).WillRepeatedly(testing::Return(pod1));
    metaStorageAccessor_->Put(instance.instanceid(), jsonString1).Get();
    metaStorageAccessor_->Put(instance.instanceid(), jsonString2).Get();
    ASSERT_AWAIT_READY(body);
    EXPECT_TRUE(ContainValue(body.Get(), "\"123\""));

    ASSERT_AWAIT_READY(body2);
    EXPECT_TRUE(ContainValue(body2.Get(), "\"123\""));
    EXPECT_AWAIT_TRUE([&isFinished]() -> bool { return isFinished; });

    auto isDeleteFinished = false;
    EXPECT_CALL(*mockClient, DeleteNamespacedPod)
        .WillOnce(testing::DoAll(testing::Assign(&isDeleteFinished, true), testing::Return(pod1)));
    metaStorageAccessor_->Delete(instance.instanceid()).Get();
    EXPECT_AWAIT_TRUE([&isDeleteFinished]() -> bool { return isDeleteFinished; });
    actor_->SetPodMap({});
    actor_->SetNodeMap({});
}

/*case
 * @title: 监听到有效的ds-worker信息
 * @type: Function test
 * @ar: Scaler感知ds-worker存活
 * @dts:
 * @precondition: 1.初始化ScalerActor类及其依赖
 * @precondition: 2.初始化MetaStore Service
 * @step:  1.ScalerActor注册Watch ds-worker事件
 * @step:  2.向MetaStore中Put有效的ds-worker信息
 * @step:  3.向MetaStore中Delete前一步Put的ds-worker信息
 * @expect:  1.监听到Put事件后，workerAliveNodes-worker所在节点IP，且调用mockClient的PatchNode方法
 * @expect:  2.监听到Delete事件后，workerAliveNodes中不存在ds-worker所在节点IP，且调用mockClient的PatchNode方法
 * @author: w00521000
 */
TEST_F(ScalerTest, UpdateNodeTaintTest)
{
    actor_->Register();
    WaitRegisterComplete();
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    litebus::Async(actor_->GetAID(), &ScalerActor::SetKubeClient, mockClient);
    const char *argv[] = { "./function_master", "--node_id=node1", "--ip=127.0.0.1:22771",
                           "--meta_store_address=127.0.0.1:32279",
                           "--self_taint_prefix=az1/" };
    functionmaster::Flags flags;
    ASSERT_TRUE(flags.ParseFlags(5, argv).IsNone());
    actor_->ParseParams(flags);

    auto address = std::make_shared<V1NodeAddress>();
    address->SetType("InternalIP");
    address->SetAddress("7.189.21.43");
    auto status = std::make_shared<V1NodeStatus>();
    status->SetAddresses({ address });
    auto node = std::make_shared<V1Node>();
    node->SetStatus(status);

    auto metadata = std::make_shared<V1ObjectMeta>();
    metadata->SetName("7.189.21.43 node");
    node->SetMetadata(metadata);

    auto spec = std::make_shared<V1NodeSpec>();
    node->SetSpec(spec);
    node->GetSpec()->SetTaints({});
    actor_->OnNodeModified(K8sEventType::EVENT_TYPE_ADD, node);

    // add taint
    std::shared_ptr<V1NodeList> nodeList = std::make_shared<V1NodeList>();
    std::vector<std::shared_ptr<V1Node>> items{node};
    nodeList->SetItems(items);

    bool isListFinished = false;
    EXPECT_CALL(*mockClient, ListNode)
        .WillOnce(testing::DoAll(testing::Assign(&isListFinished, true), testing::Return(nodeList)));

    bool isPatchFinished = false;
    litebus::Future<std::shared_ptr<Object>> body;
    EXPECT_CALL(*mockClient, PatchNode)
        .WillOnce(testing::DoAll(FutureArg<1>(&body), testing::Assign(&isPatchFinished, true), testing::Return(node)));

    auto req = std::make_shared<messages::UpdateNodeTaintRequest>();
    req->set_requestid("req-001");
    req->set_key("is-function-proxy-unready");
    req->set_healthy(false);
    req->set_ip("7.189.21.43");
    testActor_->UpdateNodeTaints(actor_->GetAID(), req->SerializeAsString());
    EXPECT_AWAIT_TRUE([&isListFinished]() -> bool { return isListFinished; });
    EXPECT_AWAIT_TRUE([&isPatchFinished]() -> bool { return isPatchFinished; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetUpdateNodeTaintResponse().requestid() == "req-001"; });
    EXPECT_TRUE(ContainValue(body.Get(), "\"az1/is-function-proxy-unready\""));

    // remove taint
    isPatchFinished = false;
    isListFinished = false;
    std::shared_ptr<V1NodeList> nodeEmptyList = std::make_shared<V1NodeList>();
    EXPECT_CALL(*mockClient, ListNode)
        .WillOnce(testing::DoAll(testing::Return(nodeEmptyList)))
        .WillOnce(testing::DoAll(testing::Assign(&isListFinished, true), testing::Return(nodeList)));

    std::vector<std::shared_ptr<V1Taint>> Taints;
    auto taint = std::make_shared<V1Taint>();
    taint->SetKey("az1/is-function-proxy-unready");
    Taints.push_back(taint);
    node->GetSpec()->SetTaints(Taints);

    actor_->SetIsUpgrading(true);
    actor_->OnNodeModified(K8sEventType::EVENT_TYPE_ADD, node);

    actor_->SetIsUpgrading(false);
    actor_->OnNodeModified(K8sEventType::EVENT_TYPE_ADD, node);

    litebus::Future<std::shared_ptr<Object>> body1;
    EXPECT_CALL(*mockClient, PatchNode)
        .WillOnce(testing::DoAll(FutureArg<1>(&body1), testing::Assign(&isPatchFinished, true), testing::Return(node)));
    req->set_requestid("req-002");
    req->set_key("is-function-proxy-unready");
    req->set_healthy(true);
    req->set_ip("7.189.21.43");
    testActor_->UpdateNodeTaints(actor_->GetAID(), req->SerializeAsString());
    testActor_->UpdateNodeTaints(actor_->GetAID(), req->SerializeAsString());
    EXPECT_AWAIT_TRUE([&isListFinished]() -> bool { return isListFinished; });
    EXPECT_AWAIT_TRUE([&isPatchFinished]() -> bool { return isPatchFinished; });
    ASSERT_AWAIT_TRUE([&]() -> bool { return GetUpdateNodeTaintResponse().requestid() == "req-002"; });
}

/**
 * Feature: PodPoolTest
 * Description: pod pool CRUD
 * Steps:
 * 1. Sync deployment and pod pool info
 * 2. receive pod pool add event
 * 3. receive pod pool update event
 * 4. receive pod pool delete event
 * Expectation:
 * 1. Add Pod Pool
 * 2. Update Pod Pool
 * 3. Delete Pod pool
 */
TEST_F(ScalerTest, PodPoolTest)
{
    std::string mem1 = "1Gi";
    std::string cpu1 = "1";
    std::string errMemVal = "mb122";
    std::string errCpuVal = "xxxx";
    EXPECT_EQ("1024", ParseMemoryEnv(mem1));
    EXPECT_EQ("1000", ParseCPUEnv(cpu1));
    EXPECT_EQ("", ParseMemoryEnv(errMemVal));
    EXPECT_EQ("", ParseCPUEnv(errCpuVal));
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    EXPECT_AWAIT_TRUE([&]() -> bool { return actor_->curStatus_ == "master"; });
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    auto oldPoolInfo = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, oldPoolInfo);
    oldPoolInfo->status = 1;
    oldPoolInfo->readyCount = 0;
    oldPoolInfo->id = "pool2";
    oldPoolInfo->group ="group2";
    litebus::Future<std::shared_ptr<functionsystem::V1Deployment>> mockDeploymentFuture;
    metaStorageAccessor_->Put("/yr/podpools/info/" + oldPoolInfo->id, poolManager->TransToJsonFromPodPoolInfo(oldPoolInfo)).Get();
    actor_->SyncDeploymentAndPodPool().Get();
    EXPECT_TRUE(poolManager->GetPodPool("pool2") != nullptr);
    EXPECT_EQ(static_cast<uint32_t>(3), poolManager->GetPoolDeploymentsMap().size());
    EXPECT_CALL(*mockClient.get(), MockCreateNamespacedDeployment(testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<0>(&mockDeploymentFuture)));
    actor_->SetKubeClient(mockClient);
    std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, poolInfo);
    actor_->UpdatePodPoolEvent(GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
    ASSERT_AWAIT_READY(mockDeploymentFuture);
    auto mockDeployment = mockDeploymentFuture.Get();
    EXPECT_EQ("function-agent-pool1", mockDeployment->GetMetadata()->GetName());
    EXPECT_EQ("rg1", mockDeployment->GetSpec()->GetRTemplate()->GetMetadata()->GetLabels()["rg1"]);
    EXPECT_EQ("runc", mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetRuntimeClassName());
    EXPECT_EQ("val1", mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetNodeSelector()["label1"]);
    EXPECT_EQ("node.kubernetes.io/disk-pressure", mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetTolerations()[0]->GetKey());
    EXPECT_TRUE(mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetAffinity()->GetPodAntiAffinity()->PreferredDuringSchedulingIgnoredDuringExecutionIsSet());
    EXPECT_EQ(true, mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->TopologySpreadConstraintsIsSet());
    EXPECT_EQ(1, mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetTopologySpreadConstraints()[0]->GetMaxSkew());
    EXPECT_EQ(1, mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetTopologySpreadConstraints()[0]->GetLabelSelector()->GetMatchLabels().size());
    EXPECT_EQ(static_cast<long unsigned int>(3),
        mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetInitContainers()[0]->GetVolumeMounts().size());
    std::shared_ptr<V1DeploymentStatus> deploymentStatus = std::make_shared<V1DeploymentStatus>();
    deploymentStatus->SetAvailableReplicas(1);
    deploymentStatus->SetReplicas(1);
    mockDeployment->SetStatus(deploymentStatus);
    mockDeployment->GetMetadata()->GetLabels()[poolInfo->group] = poolInfo->group;
    actor_->OnDeploymentModified(K8sEventType::EVENT_TYPE_MODIFIED, mockDeployment);
    EXPECT_AWAIT_TRUE([&]() -> bool { return poolManager->GetPodPool("pool1")->readyCount == 1; });
    poolInfo->status = 2;
    poolInfo->size = 2;
    actor_->UpdatePodPoolEvent(GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
    std::shared_ptr<V1DeploymentStatus> deploymentStatus1 = std::make_shared<V1DeploymentStatus>();
    EXPECT_AWAIT_TRUE([&]() -> bool { return poolManager->GetPodPool("pool1")->status == 3; });
    deploymentStatus1->SetAvailableReplicas(2);
    deploymentStatus1->SetReplicas(0);
    mockDeployment->SetStatus(deploymentStatus1);
    actor_->OnDeploymentModified(K8sEventType::EVENT_TYPE_MODIFIED, mockDeployment);
    EXPECT_AWAIT_TRUE([&]() -> bool { return poolManager->GetPodPool("pool1")->status == 4; });
    poolInfo->status = 6;
    litebus::Future<std::shared_ptr<functionsystem::V1Deployment>> mockDeleteDeploymentFuture;
    bool isDeleteFinished = false;
    EXPECT_CALL(*mockClient.get(), DeleteNamespacedDeployment)
        .WillOnce(testing::DoAll(testing::Assign(&isDeleteFinished, true), testing::Return(mockDeployment))).WillRepeatedly(testing::Return(mockDeployment));
    actor_->UpdatePodPoolEvent(GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
    EXPECT_AWAIT_TRUE([&isDeleteFinished]() -> bool { return isDeleteFinished; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return poolManager->GetPodPool("pool1") == nullptr; });

    nlohmann::json deployJson = nlohmann::json::parse(DEFAULT_DEPLOYMENT_JSON_STR);
    std::shared_ptr<V1Deployment> deploy3 = std::make_shared<V1Deployment>();
    deploy3->FromJson(deployJson);
    deploy3->GetMetadata()->SetName("function-agent-pool3");
    EXPECT_TRUE(poolManager->GetPodPool("pool3") == nullptr);
    isDeleteFinished = false;
    auto mockClient1 = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient1);
    EXPECT_CALL(*mockClient1.get(), DeleteNamespacedDeployment)
        .WillOnce(testing::DoAll(testing::Assign(&isDeleteFinished, true), testing::Return(mockDeployment))).WillRepeatedly(testing::Return(mockDeployment));
    actor_->OnDeploymentModified(K8sEventType::EVENT_TYPE_MODIFIED, deploy3);
    EXPECT_AWAIT_TRUE([&isDeleteFinished]() -> bool { return isDeleteFinished; });
}

TEST_F(ScalerTest, PodPoolTestWithDeleteError)
{
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    EXPECT_AWAIT_TRUE([&]() -> bool { return actor_->curStatus_ == "master"; });
    {
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, poolInfo);
        poolInfo->status = 6;
        poolInfo->id = "pool3";
        auto mockClient = std::make_shared<MockKubeClient>();
        actor_->SetKubeClient(mockClient);
        EXPECT_CALL(*mockClient.get(), DeleteNamespacedDeployment)
            .WillOnce(testing::Return(GetErrorDeploymentFuture(500)));
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool3") != nullptr && poolManager->GetPodPool("pool3")->status == 5;
        });
    }
    {
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, poolInfo);
        poolInfo->status = 6;
        poolInfo->id = "pool4";
        auto mockClient = std::make_shared<MockKubeClient>();
        actor_->SetKubeClient(mockClient);
        EXPECT_CALL(*mockClient.get(), DeleteNamespacedDeployment)
            .WillOnce(testing::Return(GetErrorDeploymentFuture(404)));
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool { return poolManager->GetPodPool("pool4") == nullptr; });
    }
}

TEST_F(ScalerTest, PodPoolTestWithCreateError)
{
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    EXPECT_AWAIT_TRUE([&]() -> bool { return actor_->curStatus_ == "master"; });
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    {
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, poolInfo);
        poolInfo->id = "pool-error-0";
        actor_->member_->templateDeployment = nullptr;
        actor_->member_->agentTemplatePath = "";
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool-error-0") != nullptr && poolManager->GetPodPool("pool-error-0")->status == 5;
        });
    }
    {
        auto localVarResult = std::make_shared<V1Deployment>();
        nlohmann::json localVarJson = nlohmann::json::parse(DEPLOYMENT_JSON);
        localVarResult->FromJson(localVarJson);
        actor_->SetTemplateDeployment(localVarResult);
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, poolInfo);
        poolInfo->id = "pool-error-1";
        auto deployment = std::make_shared<V1Deployment>();
        deployment->SetMetadata(std::make_shared<V1ObjectMeta>());
        deployment->GetMetadata()->SetName("function-agent-pool-error-1");
        actor_->member_->poolManager->PutDeployment(deployment);
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool-error-1") != nullptr && poolManager->GetPodPool("pool-error-1")->status == 5;
        });
    }
    {
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, poolInfo);
        poolInfo->id = "pool-error-2";
        litebus::Future<std::shared_ptr<V1Deployment>> future;
        future.SetFailed(StatusCode::FAILED);
        actor_->member_->k8sNamespace = "mock";
        EXPECT_CALL(*mockClient.get(), MockCreateNamespacedDeployment0).WillOnce(testing::Return(future));
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool-error-2") != nullptr && poolManager->GetPodPool("pool-error-2")->status == 5;
        });
        actor_->member_->k8sNamespace = DEFAULT_NAMESPACE;
    }
    {
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_WITH_HPA_INFO_STR, poolInfo);
        poolInfo->id = "pool-hpa-error-0";
        poolInfo->horizontalPodAutoscalerSpec = "invalid json";
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool-hpa-error-0") != nullptr && poolManager->GetPodPool("pool-hpa-error-0")->status == 5;
        });
    }
    {
        litebus::Future<std::shared_ptr<V2HorizontalPodAutoscalerList>> future;
        future.SetFailed(StatusCode::FAILED);
        EXPECT_CALL(*mockClient.get(), MOCKListNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
            .WillRepeatedly(testing::Return(future));
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_WITH_HPA_INFO_STR, poolInfo);
        poolInfo->id = "pool-hpa-error-1";
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool-hpa-error-1") != nullptr && poolManager->GetPodPool("pool-hpa-error-1")->status == 5;
        });
    }
}

TEST_F(ScalerTest, PodPoolTestWithUpdateError)
{
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    EXPECT_AWAIT_TRUE([&]() -> bool { return actor_->curStatus_ == "master"; });
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    {
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_WITH_HPA_INFO_STR, poolInfo);
        poolInfo->id = "pool-hpa-error-0";
        poolInfo->status = 2;
        poolInfo->horizontalPodAutoscalerSpec = "invalid json";
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool-hpa-error-0") != nullptr && poolManager->GetPodPool("pool-hpa-error-0")->status == 5;
        });
    }
    {
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_WITH_HPA_INFO_STR, poolInfo);
        poolInfo->id = "pool-hpa-error-1";
        poolInfo->status = 2;
        litebus::Future<std::shared_ptr<V2HorizontalPodAutoscalerList>> future;
        future.SetFailed(StatusCode::FAILED);
        EXPECT_CALL(*mockClient.get(), MOCKListNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
            .WillRepeatedly(testing::Return(future));
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool-hpa-error-1") != nullptr && poolManager->GetPodPool("pool-hpa-error-1")->status == 5;
        });
    }
    {
        std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(POD_POOL_WITH_HPA_INFO_STR, poolInfo);
        poolInfo->id = "pool-hpa-error-2";
        poolInfo->status = 2;
        auto hpaList = std::make_shared<V2HorizontalPodAutoscalerList>();
        EXPECT_CALL(*mockClient.get(), MOCKListNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
            .WillRepeatedly(testing::Return(hpaList));
        litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> future;
        future.SetFailed(StatusCode::FAILED);
        EXPECT_CALL(*mockClient.get(), MOCKCreateNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
            .WillRepeatedly(testing::Return(future));
        actor_->UpdatePodPoolEvent(
            GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
        EXPECT_AWAIT_TRUE([&]() -> bool {
            return poolManager->GetPodPool("pool-hpa-error-2") != nullptr && poolManager->GetPodPool("pool-hpa-error-2")->status == 5;
        });
    }
}

/**
 * Feature: PodPoolWithHPATest
 * Description: pod pool CRUD
 * Steps:
 * 1. Sync deployment and pod pool info with HPA
 * 2. receive pod pool add event
 * 3. receive pod pool update event
 * 4. receive pod pool delete event
 * Expectation:
 * 1. Add Pod Pool
 * 2. Update Pod Pool
 * 3. Delete Pod pool
 */
#if 0
TEST_F(ScalerTest, PodPoolWithHPATest)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    actor_->SyncDeploymentAndPodPool().Get();
    actor_->Register();
    WaitRegisterComplete();
    actor_->SetKubeClient(mockClient);
    auto poolInfo = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_WITH_HPA_INFO_STR, poolInfo);
    poolInfo->readyCount = 0;
    poolInfo->size = 0;
    EXPECT_TRUE(poolInfo->group == "rg1");
    EXPECT_TRUE(poolInfo->horizontalPodAutoscalerSpec == R"({"minReplicas": 1, "maxReplicas": 2, "metrics":[{"resource": {"name":"cpu", "target":{"averageUtilization":20, "type":"Utilization"}}, "type":"Resource"}, {"resource": {"name":"memory", "target":{"averageUtilization":50, "type":"Utilization"}}, "type":"Resource"}]})");
    EXPECT_TRUE(poolInfo->id == "pool1");

    auto hpa1 = std::make_shared<V2HorizontalPodAutoscaler>();
    hpa1->SetApiVersion("autoscaling/v2beta2");
    hpa1->SetKind("HorizontalPodAutoscaler");
    auto metaData = std::make_shared<V1ObjectMeta>();
    metaData->SetName("function-agent-pool1-hpa");
    hpa1->SetMetadata(metaData);
    EXPECT_CALL(*mockClient.get(), MOCKCreateNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
        .WillOnce(testing::Return(hpa1));

    auto hpaList0 = std::make_shared<V2HorizontalPodAutoscalerList>();
    auto hpaList1 = std::make_shared<V2HorizontalPodAutoscalerList>();
    hpaList1->SetItems({hpa1});
    EXPECT_CALL(*mockClient.get(), MOCKListNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
        .WillOnce(testing::Return(hpaList0))
        .WillRepeatedly(testing::Return(hpaList1));

    litebus::Future<std::shared_ptr<functionsystem::V1Deployment>> mockDeploymentFuture;
    EXPECT_CALL(*mockClient.get(), MockCreateNamespacedDeployment(testing::_))
         .WillRepeatedly(testing::DoAll(test::FutureArg<0>(&mockDeploymentFuture)));

    // Create
    actor_->UpdatePodPoolEvent(GetWatchEvents("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)));
    EXPECT_AWAIT_TRUE([&]() -> bool { return poolManager->GetPodPool("pool1") != nullptr && poolManager->GetPodPool("pool1")->status == 1; });
    auto pool1Info = poolManager->GetPodPool("pool1");
    EXPECT_TRUE(pool1Info != nullptr);
    EXPECT_TRUE(pool1Info->status == 1);
    EXPECT_TRUE(pool1Info->id == "pool1");
    EXPECT_AWAIT_TRUE([&]() -> bool { return metaStorageAccessor_->Get("/yr/podpools/info/" + pool1Info->id).IsSome(); });
    EXPECT_AWAIT_TRUE([&]() -> bool { return metaStorageAccessor_->Get("/yr/podpools/info/" + pool1Info->id).Get().find(R"("status":1)") != std::string::npos; });

    ASSERT_AWAIT_READY(mockDeploymentFuture);
    EXPECT_EQ("function-agent-pool1", mockDeploymentFuture.Get()->GetMetadata()->GetName());
    EXPECT_EQ("rg1", mockDeploymentFuture.Get()->GetSpec()->GetRTemplate()->GetMetadata()->GetLabels()["rg1"]);
    auto deploymentStatus = std::make_shared<V1DeploymentStatus>();
    deploymentStatus->SetAvailableReplicas(1);
    deploymentStatus->SetReplicas(1);
    mockDeploymentFuture.Get()->SetStatus(deploymentStatus);
    actor_->OnDeploymentModified(K8sEventType::EVENT_TYPE_MODIFIED, mockDeploymentFuture.Get());
    EXPECT_AWAIT_TRUE([&]() -> bool { return poolManager->GetPodPool("pool1")->readyCount == 1; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return metaStorageAccessor_->Get("/yr/podpools/info/" + pool1Info->id).Get().find(R"("status":4)") != std::string::npos; });

    // Update
    EXPECT_CALL(*mockClient.get(), MOCKPatchNamespacedHorizontalPodAutoscaler(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(hpa1));
    pool1Info->status = 2;
    actor_->UpdatePodPoolEvent(GetWatchEvents("/yr/podpools/info/" + pool1Info->id, poolManager->TransToJsonFromPodPoolInfo(pool1Info)));
    auto deploymentStatus1 = std::make_shared<V1DeploymentStatus>();
    deploymentStatus1->SetAvailableReplicas(2);
    deploymentStatus1->SetReplicas(0);
    mockDeploymentFuture.Get()->SetStatus(deploymentStatus1);
    actor_->OnDeploymentModified(K8sEventType::EVENT_TYPE_MODIFIED, mockDeploymentFuture.Get());
    EXPECT_AWAIT_TRUE([&]() -> bool { return metaStorageAccessor_->Get("/yr/podpools/info/" + pool1Info->id).Get().find(R"("status":4)") != std::string::npos; });

    // Delete
    auto status = std::make_shared<V1Status>();
    auto mockClient3 = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient3);
    EXPECT_CALL(*mockClient3.get(), MOCKDeleteNamespacedHorizontalPodAutoscaler(testing::_, testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::Return(status));
    bool isDeleteFinished = false;
    EXPECT_CALL(*mockClient3.get(), DeleteNamespacedDeployment)
        .WillRepeatedly(testing::DoAll(testing::Assign(&isDeleteFinished, true), testing::Return(mockDeploymentFuture.Get())));
    pool1Info->status = 6;
    actor_->UpdatePodPoolEvent(GetWatchEvents("/yr/podpools/info/" + pool1Info->id, poolManager->TransToJsonFromPodPoolInfo(pool1Info)));
    EXPECT_AWAIT_TRUE([&isDeleteFinished]() -> bool { return isDeleteFinished; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return poolManager->GetPodPool("pool1") == nullptr; });
    metaStorageAccessor_->Delete("/yr/podpools/info/", true).Get();
}
#endif

/**
 * Feature: PodPoolWithHPATest
 * Description: pod pool CRUD
 * Steps:
 * 1. Sync deployment and pod pool info with HPA
 * 2. receive pod pool add event
 * 3. receive pod pool update event
 * 4. receive pod pool delete event
 * Expectation:
 * 1. Add Pod Pool
 * 2. Update Pod Pool
 * 3. Delete Pod pool
 */
TEST_F(ScalerTest, CreatePodPoolWhenHPAExistTest)
{
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_WITH_HPA_INFO_STR, poolInfo);
    actor_->Register();
    WaitRegisterComplete();
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    poolInfo->readyCount = 0;
    poolInfo->status = 0;
    poolInfo->size = 0;
    EXPECT_TRUE(poolInfo->group == "rg1");
    EXPECT_TRUE(poolInfo->id == "pool1");
    EXPECT_TRUE(poolInfo->horizontalPodAutoscalerSpec == R"({"minReplicas": 1, "maxReplicas": 2, "metrics":[{"resource": {"name":"cpu", "target":{"averageUtilization":20, "type":"Utilization"}}, "type":"Resource"}, {"resource": {"name":"memory", "target":{"averageUtilization":50, "type":"Utilization"}}, "type":"Resource"}]})");

    auto hpa1 = std::make_shared<V2HorizontalPodAutoscaler>();
    hpa1->SetKind("HorizontalPodAutoscaler");
    hpa1->SetApiVersion("autoscaling/v2beta2");
    auto metaData = std::make_shared<V1ObjectMeta>();
    metaData->SetName("function-agent-pool1-hpa");
    hpa1->SetMetadata(metaData);

    auto hpaList1 = std::make_shared<V2HorizontalPodAutoscalerList>();
    hpaList1->SetItems({hpa1});
    EXPECT_CALL(*mockClient.get(), MOCKListNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
        .WillRepeatedly(testing::Return(hpaList1));

    // Create
    metaStorageAccessor_->Put("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)).Get();
    EXPECT_AWAIT_TRUE([&]() -> bool { return metaStorageAccessor_->Get("/yr/podpools/info/" + poolInfo->id).Get().find(R"("status":5)") != std::string::npos; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return metaStorageAccessor_->Get("/yr/podpools/info/" + poolInfo->id).Get().find(R"("msg":"create HPA failed")") != std::string::npos; });
    metaStorageAccessor_->Delete("/yr/podpools/info/", true).Get();
}
TEST_F(ScalerTest, CreatePodPoolWithInvalidData)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    actor_->SyncDeploymentAndPodPool().Get();
    litebus::Future<std::shared_ptr<functionsystem::V1Deployment>> mockDeploymentFuture;
    EXPECT_CALL(*mockClient.get(), MockCreateNamespacedDeployment(testing::_))
        .WillRepeatedly(testing::DoAll(test::FutureArg<0>(&mockDeploymentFuture)));
    actor_->Register();
    WaitRegisterComplete();
    actor_->SetKubeClient(mockClient);
    std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, poolInfo);
    poolInfo->id = "errPool1";
    poolInfo->affinities = "{\"requiredDuringSchedulingIgnoredDuringExecution\": {\"nodeSelectorTerms\": [{\"matchExpressions\": [{ \"key\": \"node-type\",\"operator\": \"In\",\"values\": [\"system\"]}]}]}}, }}";
    poolInfo->tolerations = "[{\"key\":\"node.kubernetes.io/disk-pressure\",\"operator\": \"Equal\", \"value\": \"true\", \"effect\": \"NoSchedule\"]";
    metaStorageAccessor_->Put("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)).Get();
    ASSERT_AWAIT_READY(mockDeploymentFuture);
    auto mockDeployment = mockDeploymentFuture.Get();
    EXPECT_EQ("function-agent-errPool1", mockDeployment->GetMetadata()->GetName());
    EXPECT_EQ(static_cast<long unsigned int>(0),
        mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetTolerations().size());
    EXPECT_FALSE(mockDeployment->GetSpec()->GetRTemplate()->GetSpec()->GetAffinity()->NodeAffinityIsSet());
    metaStorageAccessor_->Delete("/yr/podpools/info", true).Get();
}

TEST_F(ScalerTest, UpdatePodPoolWhenHPANotExistTest)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_WITH_HPA_INFO_STR, poolInfo);
    poolInfo->status = 2;
    poolInfo->readyCount = 0;
    poolInfo->size = 0;
    EXPECT_TRUE(poolInfo->id == "pool1");
    EXPECT_TRUE(poolInfo->group == "rg1");
    EXPECT_TRUE(poolInfo->horizontalPodAutoscalerSpec == R"({"minReplicas": 1, "maxReplicas": 2, "metrics":[{"resource": {"name":"cpu", "target":{"averageUtilization":20, "type":"Utilization"}}, "type":"Resource"}, {"resource": {"name":"memory", "target":{"averageUtilization":50, "type":"Utilization"}}, "type":"Resource"}]})");

    auto hpaList0 = std::make_shared<V2HorizontalPodAutoscalerList>();
    EXPECT_CALL(*mockClient.get(), MOCKListNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
        .WillRepeatedly(testing::Return(hpaList0));

    auto hpa1 = std::make_shared<V2HorizontalPodAutoscaler>();
    hpa1->SetApiVersion("autoscaling/v2beta2");
    hpa1->SetKind("HorizontalPodAutoscaler");
    auto metaData = std::make_shared<V1ObjectMeta>();
    metaData->SetName("function-agent-pool1-hpa");
    hpa1->SetMetadata(metaData);
    EXPECT_CALL(*mockClient.get(), MOCKCreateNamespacedHorizontalPodAutoscaler(testing::_, testing::_))
        .WillRepeatedly(testing::Return(hpa1));

    actor_->Register();
    WaitRegisterComplete();
    actor_->SetKubeClient(mockClient);
    // Update
    metaStorageAccessor_->Put("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)).Get();
    EXPECT_AWAIT_TRUE([&]() -> bool { return metaStorageAccessor_->Get("/yr/podpools/info/" + poolInfo->id).Get().find(R"("status":5)") != std::string::npos; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return metaStorageAccessor_->Get("/yr/podpools/info/" + poolInfo->id).Get().find(R"("msg":"pool not found")") != std::string::npos; });
    metaStorageAccessor_->Delete("/yr/podpools/info", true).Get();
}

TEST_F(ScalerTest, SystemUpgradeTest)
{
    actor_->Register();
    WaitRegisterComplete();
    GetTaintNodeList();
    EXPECT_FALSE(actor_->GetIsUpgrading());

    metaStorageAccessor_->Put(DEFAULT_SYSTEM_UPGRADE_KEY + "/zoneid", R"(fake json)");
    ASSERT_AWAIT_TRUE([&]() { return !actor_->GetIsUpgrading(); });

    metaStorageAccessor_->Put(DEFAULT_SYSTEM_UPGRADE_KEY + "/zoneid", R"({"azId": 0, "status": 1})");
    ASSERT_AWAIT_TRUE([&]() { return !actor_->GetIsUpgrading(); });

    metaStorageAccessor_->Put(DEFAULT_SYSTEM_UPGRADE_KEY + "/zoneid", R"({"azId": 0, "status": 100})");
    ASSERT_AWAIT_TRUE([&]() { return !actor_->GetIsUpgrading(); });

    metaStorageAccessor_->Put(DEFAULT_SYSTEM_UPGRADE_KEY + "/zoneid", R"({"status": 1})");
    ASSERT_AWAIT_TRUE([&]() { return actor_->GetIsUpgrading(); });

    metaStorageAccessor_->Put(DEFAULT_SYSTEM_UPGRADE_KEY + "/zoneid", R"({"azId": 1, "status": 2})");
    ASSERT_AWAIT_TRUE([&]() { return !actor_->GetIsUpgrading(); });

    metaStorageAccessor_->Put(DEFAULT_SYSTEM_UPGRADE_KEY + "/zoneid", R"({"azId": 1, "status": 1})");
    ASSERT_AWAIT_TRUE([&]() { return actor_->GetIsUpgrading(); });

    metaStorageAccessor_->Put(DEFAULT_SYSTEM_UPGRADE_KEY + "/zoneid", R"({"azId": 1, "status": 2})");
    ASSERT_AWAIT_TRUE([&]() { return !actor_->GetIsUpgrading(); });

    metaStorageAccessor_->Put(DEFAULT_SYSTEM_UPGRADE_KEY + "/zoneid", R"({"azId": 1, "status": 1})");
    ASSERT_AWAIT_TRUE([&]() { return actor_->GetIsUpgrading(); });

    metaStorageAccessor_->Delete(DEFAULT_SYSTEM_UPGRADE_KEY, true);

    ASSERT_AWAIT_TRUE([&]() { return !actor_->GetIsUpgrading(); });
}

TEST_F(ScalerTest, GetPodDeleteOptionsTest)
{
    auto opt = actor_->GetPodDeleteOptions("podName");
    EXPECT_TRUE(opt.IsSome());
    EXPECT_EQ(opt.Get()->GetGracePeriodSeconds(), 25);

    opt = actor_->GetPodDeleteOptions("podName", true);
    EXPECT_TRUE(opt.IsSome());
    EXPECT_EQ(opt.Get()->GetGracePeriodSeconds(), 25);

    opt = actor_->GetPodDeleteOptions("podName", false);
    EXPECT_TRUE(opt.IsSome());
    EXPECT_FALSE(opt.Get()->GracePeriodSecondsIsSet());
}

TEST_F(ScalerTest, SlaveTest)
{
    auto member = std::make_shared<ScalerActor::Member>();
    auto slaveBusiness = std::make_shared<ScalerActor::SlaveBusiness>(actor_, member);
    slaveBusiness->OnChange();
    slaveBusiness->UpdatePodLabels({}, false);
    slaveBusiness->DeletePod("");
    slaveBusiness->CreateResourcePools(nullptr);
    slaveBusiness->CreatePodPools({});
    slaveBusiness->CreateAgent({}, "", "");
    slaveBusiness->CreatePodPool(nullptr);
    slaveBusiness->UpdatePodPool(nullptr);
    slaveBusiness->DeletePodPool(nullptr);
    slaveBusiness->UpdateNodeTaint({}, "", "");
    slaveBusiness->DeletePodNotBindInstance(nullptr);
    slaveBusiness->MigratePodInstanceWithTaints("", "");
    slaveBusiness->MigrateNodeInstanceWithTaints(nullptr);
    slaveBusiness->UpdatePodLabelsWithoutInstance(nullptr);
    slaveBusiness->PersistencePoolInfo("");
    slaveBusiness->ScaleUpPodByPoolID("", false, "", litebus::AID());
}

TEST_F(ScalerTest, ParseLabelWithAnnotationTest)
{
    resource_view::InstanceInfo instance1 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/ins-0001", static_cast<int32_t>(InstanceState::RUNNING));
    (*instance1.mutable_createoptions())["DELEGATE_POD_LABELS"] = "{}";
    auto readyPod = GetReadyPod();
    HandlePodLabelsWithAnnotation(instance1, readyPod, false);
    EXPECT_EQ(1, readyPod->GetMetadata()->GetLabels().size());
    EXPECT_EQ(0, readyPod->GetMetadata()->GetAnnotations().size());
    (*instance1.mutable_createoptions())["DELEGATE_POD_LABELS"] = "{\"label1\":\"va1\", \"label2\":\"val2\",\"\":\"\"}";
    HandlePodLabelsWithAnnotation(instance1, readyPod, false);
    EXPECT_EQ(3, readyPod->GetMetadata()->GetLabels().size());
    EXPECT_EQ(1, readyPod->GetMetadata()->GetAnnotations().size());
    HandlePodLabelsWithAnnotation(instance1, readyPod, true);
    EXPECT_EQ(1, readyPod->GetMetadata()->GetLabels().size());
    EXPECT_EQ(0, readyPod->GetMetadata()->GetAnnotations().size());
    auto newPod = GetReadyPod();
    newPod->GetMetadata()->GetLabels()["label2"] = "val2";
    newPod->GetMetadata()->GetLabels()["label3"] = "val3";
    newPod->GetMetadata()->GetLabels()["label4"] = "val4";
    resource_view::InstanceInfo instance2 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/ins-0002", static_cast<int32_t>(InstanceState::FATAL));
    (*instance2.mutable_createoptions())["DELEGATE_POD_LABELS"] = "{\"label2\":\"val2\"}";
    resource_view::InstanceInfo instance3 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/ins-0003", static_cast<int32_t>(InstanceState::SCHEDULE_FAILED));
    (*instance3.mutable_createoptions())["DELEGATE_POD_LABELS"] = "{\"label3\":\"va3\"}";
    newPod->GetMetadata()->GetAnnotations()["yr-labels-" + instance2.instanceid()] = "{\"label2\":\"va2\"}";
    newPod->GetMetadata()->GetAnnotations()["yr-labels-" + instance3.instanceid()] = "{\"label3\":\"va3\"}";
    HandlePodLabelsWithAnnotation(instance2, newPod, false);
    HandlePodLabelsWithAnnotation(instance3, newPod, false);
    EXPECT_EQ(2, newPod->GetMetadata()->GetLabels().size());
    EXPECT_EQ(0, newPod->GetMetadata()->GetAnnotations().size());
}

TEST_F(ScalerTest, WatchSharedInstanceTest)
{
    // 1. Delete all instances
    metaStorageAccessor_->Delete(INSTANCE_PATH_PREFIX, true).Get();
    // 2. Watch instance's events
    litebus::Async(actor_->GetAID(), &ScalerActor::Register);
    WaitRegisterComplete();
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);  // synchronize
    auto pod1 = std::make_shared<V1Pod>();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPodIP("127.0.0.1");
    pod1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod1->GetMetadata()->SetName("function-agent-001");
    pod1->GetMetadata()->SetLabels({});
    pod1->GetMetadata()->GetLabels()["yr-reuse"] = "false";
    pod1->GetMetadata()->SetAnnotations({});
    std::string jsonString0;
    resource_view::InstanceInfo instance0 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/00x", static_cast<int32_t>(InstanceState::RUNNING));
    instance0.set_instanceid("00x");
    (*instance0.mutable_createoptions())["DELEGATE_POD_LABELS"] = "{\"label1\":\"val1\"}";
    instance0.mutable_scheduleoption()->set_schedpolicyname("shared");
    instance0.set_functionagentid("");
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString0, instance0));
    std::string jsonString1;
    resource_view::InstanceInfo instance1 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/001", static_cast<int32_t>(InstanceState::RUNNING));
    instance1.set_instanceid("001");
    (*instance1.mutable_createoptions())["DELEGATE_POD_LABELS"] = "{\"label1\":\"val1\"}";
    instance1.mutable_scheduleoption()->set_schedpolicyname("shared");
    ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString1, instance1));
    litebus::Future<std::shared_ptr<Object>> body;
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillOnce(testing::Return(pod1));
    EXPECT_CALL(*mockClient, PatchNamespacedPod).WillOnce(testing::DoAll(FutureArg<2>(&body), testing::Return(pod1)));
    // 3. Put two instances to MetaStore.
    metaStorageAccessor_->Put(INSTANCE_PATH_PREFIX + "/00x", jsonString0).Get();
    metaStorageAccessor_->Put(INSTANCE_PATH_PREFIX + "/001", jsonString1).Get();
    ASSERT_AWAIT_READY(body);
    EXPECT_TRUE(ContainValue(body.Get(), "label1"));
    EXPECT_TRUE(ContainValue(body.Get(), "yr-labels-" + instance1.instanceid()));
    body = {};
    pod1->GetMetadata()->GetLabels()["label1"] = "val1";
    pod1->GetMetadata()->GetAnnotations()["yr-labels-" + instance1.instanceid()] = "{\"label1\":\"val1\"}";
    litebus::Future<std::shared_ptr<Object>> body1;
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillRepeatedly(testing::Return(pod1));
    EXPECT_CALL(*mockClient, PatchNamespacedPod)
        .WillRepeatedly(testing::DoAll(FutureArg<2>(&body1), testing::Return(pod1)));
    // 4. Delete the second instance.
    metaStorageAccessor_->Delete(INSTANCE_PATH_PREFIX + "/001").Get();
    ASSERT_AWAIT_READY(body1);
    EXPECT_TRUE(ContainValue(body1.Get(), "label1"));
    EXPECT_TRUE(ContainValue(body1.Get(), "yr-labels-" + instance1.instanceid()));
    EXPECT_FALSE(ContainValue(body1.Get(), "yr-default"));
    EXPECT_TRUE(pod1->GetMetadata()->GetAnnotations().find("yr-default") != pod1->GetMetadata()->GetAnnotations().end());
    actor_->SetPodMap({});   // synchronize
    actor_->SetNodeMap({});  // synchronize
}

TEST_F(ScalerTest, UpdatePodLabelsWithoutInstanceTest)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    auto newPod = GetReadyPod();
    newPod->GetMetadata()->GetLabels()["label2"] = "val2";
    newPod->GetMetadata()->GetLabels()["label3"] = "val3";
    newPod->GetMetadata()->GetLabels()["label4"] = "val4";
    newPod->GetMetadata()->SetAnnotations({});
    newPod->GetMetadata()->GetAnnotations()["yr-labels-ins-00033333"] = "{\"label2\":\"va2\"}";
    newPod->GetMetadata()->GetAnnotations()["yr-labels-ins-00044444"] = "{\"label3\":\"va3\"}";
    litebus::Future<std::shared_ptr<Object>> body;
    EXPECT_CALL(*mockClient, PatchNamespacedPod).WillOnce(testing::DoAll(FutureArg<2>(&body), testing::Return(newPod)));
    actor_->UpdatePodLabelsWithoutInstance(newPod);
    ASSERT_AWAIT_READY(body);
    EXPECT_TRUE(ContainValue(body.Get(), "/metadata/labels/label2"));
    EXPECT_TRUE(ContainValue(body.Get(), "/metadata/labels/label3"));
}

TEST_F(ScalerTest, MigrateInstanceTest)
{
    actor_->Register();
    WaitRegisterComplete();
    const char *argv[] = { "./function_master","--node_id=node1", "--ip=127.0.0.1:22771",
                           "--meta_store_address=127.0.0.1:32279",
                           "--self_taint_prefix=az1/" };
    functionmaster::Flags flags;
    ASSERT_TRUE(flags.ParseFlags(5, argv).IsNone());
    actor_->ParseParams(flags);
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    auto node = GetReadyNode();
    node->GetStatus()->GetAddresses()[0]->SetAddress("10.10.10.01");
    node->GetMetadata()->SetName("10.10.10.01");
    actor_->MigratePodInstanceWithTaints("10.10.10.01", "function-agent-001");
    std::unordered_map<std::string, std::shared_ptr<V1Node>> nodeMap;
    nodeMap["10.10.10.01"] = node;
    actor_->SetNodeMap(nodeMap);
    actor_->MigratePodInstanceWithTaints("10.10.10.01", "function-agent-001");
    auto pod1 = std::make_shared<V1Pod>();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPodIP("10.0.0.1");
    pod1->GetStatus()->SetHostIP("10.10.10.01");
    pod1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod1->GetMetadata()->SetName("function-agent-001");
    pod1->GetMetadata()->GetLabels()["yr-reuse"] = "false";
    pod1->GetMetadata()->GetLabels()["app"] = "function-agent-001";
    auto podSpec = std::make_shared<functionsystem::kube_client::api::V1PodSpec>();
    podSpec->SetTerminationGracePeriodSeconds(60);
    pod1->SetSpec(podSpec);
    bool isFinished = false;
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillOnce(testing::Return(pod1));
    EXPECT_CALL(*mockClient, PatchNamespacedPod).WillRepeatedly(testing::DoAll(testing::Assign(&isFinished, true),testing::Return(pod1)));
    std::string jsonString1;
    resource_view::InstanceInfo instance1 =
        CreateInstance(INSTANCE_PATH_PREFIX + "/0011", static_cast<int32_t>(InstanceState::RUNNING));
    instance1.set_functionagentid("function-agent-001");
    instance1.set_functionproxyid("10.10.10.01");
    TransToJsonFromInstanceInfo(jsonString1, instance1);
    metaStorageAccessor_->Put(instance1.instanceid(), jsonString1).Get();
    EXPECT_AWAIT_TRUE([&isFinished]() -> bool { return isFinished; });
    auto nodeTaint = std::make_shared<V1Taint>();
    nodeTaint->SetKey("az1/is-ds-worker-unready");
    nodeTaint->SetValue("true");
    nodeTaint->SetEffect("PreferNoSchedule");
    node->GetSpec()->GetTaints().push_back(nodeTaint);
    nodeMap["10.10.10.01"] = node;
    actor_->SetNodeMap(nodeMap);
    actor_->MigratePodInstanceWithTaints("10.10.10.01", "function-agent-001");
    EXPECT_EQ(*counter, 1);
    actor_->SetPodMap({});
    actor_->SetNodeMap({});
}

TEST_F(ScalerTest, WatchEvictedNodeTaint)
{
    actor_->Register();
    WaitRegisterComplete();
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    actor_->SetPodMap({});
    actor_->SetNodeMap({});

    auto localActor = std::make_shared<ScalerTestActor>("evicted-node" + LOCAL_SCHED_FUNC_AGENT_MGR_ACTOR_NAME_POSTFIX);
    litebus::Spawn(localActor);

    auto node = GetReadyNode();
    node->GetMetadata()->SetName("evicted-node");
    node->GetStatus()->GetAddresses()[0]->SetAddress("10.10.10.01");
    auto nodeTaint = std::make_shared<V1Taint>();
    // add taint
    nodeTaint->SetKey("evicted-taint-key");
    nodeTaint->SetValue("true");
    nodeTaint->SetEffect("PreferNoSchedule");
    node->GetSpec()->SetTaints({});
    node->GetSpec()->GetTaints().push_back(nodeTaint);
    actor_->SetEvictedTaintKey("evicted-taint-key");

    actor_->OnNodeModified(K8sEventType::EVENT_TYPE_ADD, node);
    EXPECT_AWAIT_TRUE([&]() -> bool { return localActor->GetLocalStatusRequest().status() == 4; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return actor_->member_->hasEvictedTaint; });

    // remove taint
    node->GetSpec()->SetTaints({});
    actor_->OnNodeModified(K8sEventType::EVENT_TYPE_ADD, node);
    EXPECT_AWAIT_TRUE([&]() -> bool { return localActor->GetLocalStatusRequest().status() == 1; });
    EXPECT_AWAIT_TRUE([&]() -> bool { return !actor_->member_->hasEvictedTaint; });

    // create pod - after pod instances migration is completed, the annotation is removed
    node->GetSpec()->GetTaints().push_back(nodeTaint);
    auto pod = std::make_shared<V1Pod>();
    pod->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod->GetStatus()->SetHostIP("10.10.10.01");
    pod->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod->GetMetadata()->SetName("function-agent-001");
    pod->GetMetadata()->GetAnnotations()["cluster-autoscaler.kubernetes.io/safe-to-evict"] = "false";
    std::unordered_map<std::string, std::shared_ptr<V1Pod>> podNameMap;
    podNameMap["function-agent-001"] = pod;
    actor_->SetPodMap(podNameMap);
    std::unordered_map<std::string, std::shared_ptr<V1Node>> nodeMap;
    nodeMap["10.10.10.01"] = node;
    actor_->SetNodeMap(nodeMap);

    bool isFinished = false, isFinished1 = false;
    EXPECT_CALL(*mockClient, PatchNamespacedPod)
        .WillOnce(testing::DoAll(testing::Assign(&isFinished, true), testing::Return(pod)))
        .WillOnce(testing::DoAll(testing::Assign(&isFinished1, true), testing::Return(pod)));
    actor_->MigratePodInstanceWithTaints("10.10.10.01", "function-agent-001");
    EXPECT_EQ(*counter, 1);
    EXPECT_AWAIT_TRUE([&isFinished]() -> bool { return isFinished; });
    // add annotation
    resource_view::InstanceInfo instance = CreateInstance(INSTANCE_PATH_PREFIX + "/0011", static_cast<int32_t>(InstanceState::RUNNING));
    instance.set_functionagentid("function-agent-001");
    instance.set_functionproxyid("node1");
    instance.mutable_scheduleoption()->set_schedpolicyname("");
    EXPECT_CALL(*mockClient, ReadNamespacedPod).WillOnce(testing::Return(pod));
    actor_->HandleInstancePut(instance);
    EXPECT_AWAIT_TRUE([&isFinished1]() -> bool { return isFinished1; });
    actor_->OnNodeModified(K8sEventType::EVENT_TYPE_DELETE, node);
    ASSERT_AWAIT_TRUE([&]() -> bool {return actor_->member_->nodes.size() == 0;});
    litebus::Terminate(localActor->GetAID());
    litebus::Await(localActor);
    localActor = nullptr;
}

TEST_F(ScalerTest, KeySyncerGetFailedOrEmptyTest)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    auto originClient = metaStorageAccessor_->GetMetaClient();
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;
    {   // for get failed
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status(StatusCode::FAILED, "");

        auto future = actor_->SystemUpgradeSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());
        future = actor_->InstanceInfoSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());
        future = actor_->PodPoolSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());
    }
    {   // for get response is empty
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();

        auto future = actor_->SystemUpgradeSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
        future = actor_->InstanceInfoSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
        future = actor_->PodPoolSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
    }

    // for reset originClient, if not reset, will delete failed in teardown()
    metaStorageAccessor_->metaClient_ = originClient;
}

TEST_F(ScalerTest, SystemUpgradeSyncerSuccessTest)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    auto originClient = metaStorageAccessor_->GetMetaClient();
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;
    actor_->member_->systemUpgradeParam.systemUpgradeWatcher = metaStorageAccessor_;

    {   // for get response is not empty
        KeyValue getKeyValue;
        getKeyValue.set_key(DEFAULT_SYSTEM_UPGRADE_KEY+"AZ") ;
        getKeyValue.set_value("Node1");

        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        rep->kvs.emplace_back(getKeyValue);

        auto future = actor_->SystemUpgradeSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
    }
    // for reset originClient, if not reset, will delete failed in teardown()
    metaStorageAccessor_->metaClient_ = originClient;
}

std::vector<WatchEvent> GenerateResponseEvent()
{
    std::vector<WatchEvent> events;
    std::list<std::pair<std::string, std::string>> kvMap = {
        {instanceKey1, instanceInfoJson1},
        {instanceKey2, instanceInfoJson2},
        {instanceKey3, instanceInfoJson3},
    };

    for (auto elem : kvMap) {
        KeyValue inst1;
        inst1.set_value(elem.second);
        inst1.set_key(elem.first) ;

        WatchEvent event{ .eventType = EVENT_TYPE_PUT, .kv = inst1, .prevKv = {} };
        events.emplace_back(event);
    }
    return events;
}

std::vector<std::shared_ptr<V1Pod>> GeneratePodlist()
{
    auto pod1 = std::make_shared<V1Pod>();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPodIP("10.42.2.101");
    pod1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod1->GetMetadata()->SetName("functionagent-pool1-776c6db574-1");
    pod1->GetMetadata()->GetLabels()["yr-reuse"] = "false";

    auto pod2 = std::make_shared<V1Pod>();
    pod2->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod2->GetStatus()->SetPodIP("10.42.2.102");
    pod2->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod2->GetMetadata()->SetName("functionagent-pool1-776c6db574-2");
    pod2->GetMetadata()->GetLabels()["yr-reuse"] = "false";

    auto pod3 = std::make_shared<V1Pod>();
    pod3->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod3->GetStatus()->SetPodIP("10.42.2.103");
    pod3->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod3->GetMetadata()->SetName("functionagent-pool1-776c6db574-3");
    pod3->GetMetadata()->GetLabels()["yr-reuse"] = "false";

    std::vector<std::shared_ptr<V1Pod>> podlist({pod1, pod2, pod3});
    return podlist;
}

TEST_F(ScalerTest, InstanceInfoSyncerTest)
{
    auto mockMetaStoreClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    auto originClient = metaStorageAccessor_->GetMetaClient();
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);

    // for get response is not empty
    auto events = GenerateResponseEvent();
    auto podlist = GeneratePodlist();
    std::vector<int> targetPod({0}); // only put instance1 into cache
    for (auto i : targetPod) {
        EXPECT_CALL(*mockClient, ReadNamespacedPod).WillRepeatedly(testing::Return(podlist[i]));
        EXPECT_CALL(*mockClient, PatchNamespacedPod).WillRepeatedly(testing::Return(podlist[i]));
        std::vector<WatchEvent> tmpEvents;
        tmpEvents.emplace_back(events[i]);
        actor_->UpdateInstanceEvent(tmpEvents);
    }

    std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
    rep->status = Status::OK();
    rep->kvs.emplace_back(events[0].kv);
    rep->kvs.emplace_back(events[1].kv);

    std::string jsonString1;
    resource_view::InstanceInfo instance1 = CreateInstance("instanceID4", static_cast<int32_t>(InstanceState::RUNNING));
    instance1.set_functionagentid("function-agent-001");
    instance1.set_functionproxyid("10.10.10.01");
    instance1.set_lowreliability(true);
    TransToJsonFromInstanceInfo(jsonString1, instance1);
    KeyValue inst1;
    inst1.set_value(jsonString1);
    inst1.set_key("/sn/instance/business/yrk/tenant/12345678901234561234567890123456/function/0-system-faasExecutorPython3.9/version/$latest/defaultaz/requestID4/instanceID4");
    rep->kvs.emplace_back(inst1);

    auto future = actor_->InstanceInfoSyncer(rep);
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());

    // instance3 expect to delete
    EXPECT_TRUE(actor_->member_->instanceId2PodName.count("instanceID3") == 0);
    EXPECT_TRUE(actor_->member_->podName2InstanceId.count("functionagent-pool1-776c6db574-3") == 0);

    // instance2 expect to add
    EXPECT_TRUE(actor_->member_->instanceId2PodName.count("instanceID2") == 1);
    EXPECT_TRUE(actor_->member_->podName2InstanceId.count("functionagent-pool1-776c6db574-2") == 1);

    EXPECT_TRUE(actor_->member_->instanceId2PodName.count("instanceID4") == 0);
    // for reset originClient, if not reset, will delete failed in teardown()
    metaStorageAccessor_->metaClient_ = originClient;
}

std::vector<WatchEvent> GenerateResponsePodPoolEvent()
{

    auto key1 = R"(/yr/podpools/info/pool-not-incache)";
    auto value1 = R"({"affinities":"","environment":null,"group":"rg1","horizontal_pod_autoscaler_spec":"","id":"pool-not-incache","image":"","init_image":"","labels":null,"msg":"Running","node_selector":null,"ready_count":1,"resources":{"liits":{"cpu":"600m","memory":"512Mi"},"requests":{"cpu":"600m","memory":"512Mi"}},"reuse":false,"runtime_class_name":"","size":1,"status":4,"tolerations":"","volume_mounts":"","volumes":""})";

    auto key2 = R"(/yr/podpools/info/pool-not-inetcd)";
    auto value2 = R"({"affinities":"","environment":null,"group":"rg1","horizontal_pod_autoscaler_spec":"","id":"pool-not-inetcd","image":"","init_image":"","labels":null,"msg":"Running","node_selector":null,"ready_count":1,"resources":{"liits":{"cpu":"600m","memory":"512Mi"},"requests":{"cpu":"600m","memory":"512Mi"}},"reuse":false,"runtime_class_name":"","size":1,"status":4,"tolerations":"","volume_mounts":"","volumes":""})";

    auto key3 = R"(/yr/podpools/info/pool-inboth)";
    auto value3status1 = R"({"affinities":"","environment":null,"group":"rg1","horizontal_pod_autoscaler_spec":"","id":"pool-inboth","image":"","init_image":"","labels":null,"msg":"Running","node_selector":null,"ready_count":1,"resources":{"liits":{"cpu":"600m","memory":"512Mi"},"requests":{"cpu":"600m","memory":"512Mi"}},"reuse":false,"runtime_class_name":"","size":1,"status":1,"tolerations":"","volume_mounts":"","volumes":""})";
    auto value3status4 = R"({"affinities":"","environment":null,"group":"rg1","horizontal_pod_autoscaler_spec":"","id":"pool-inboth","image":"","init_image":"","labels":null,"msg":"Running","node_selector":null,"ready_count":1,"resources":{"liits":{"cpu":"600m","memory":"512Mi"},"requests":{"cpu":"600m","memory":"512Mi"}},"reuse":false,"runtime_class_name":"","size":1,"status":4,"tolerations":"","volume_mounts":"","volumes":""})";

    auto key4 = R"(/yr/podpools/info/pool-inboth-delete)";
    auto value4 = R"({"affinities":"","environment":null,"group":"rg1","horizontal_pod_autoscaler_spec":"","id":"pool-inboth-delete","image":"","init_image":"","labels":null,"msg":"Running","node_selector":null,"ready_count":1,"resources":{"liits":{"cpu":"600m","memory":"512Mi"},"requests":{"cpu":"600m","memory":"512Mi"}},"reuse":false,"runtime_class_name":"","size":1,"status":6,"tolerations":"","volume_mounts":"","volumes":""})";

    std::vector<WatchEvent> events;
    std::list<std::pair<std::string, std::string>> kvMap = {
        {key1, value1},
        {key2, value2},
        {key3, value3status1},
        {key3, value3status4},
        {key4, value4},
    };

    for (auto elem : kvMap) {
        KeyValue inst1;
        inst1.set_key(elem.first) ;
        inst1.set_value(elem.second);
        WatchEvent event{ .eventType = EVENT_TYPE_PUT, .kv = inst1, .prevKv = {} };
        events.emplace_back(event);
    }
    return events;
}

TEST_F(ScalerTest, PodPoolSyncerTest)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    auto originClient = metaStorageAccessor_->GetMetaClient();
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;

    auto events = GenerateResponsePodPoolEvent();
    std::vector<int> targetPod({1, 2, 4}); // put key2, key3(status 1),, key4 in cache
    for (auto i : targetPod) {
        auto podPool = std::make_shared<PodPoolInfo>();
        PoolManager::TransToPoolInfoFromJson(events[i].kv.value(), podPool);
        PoolManager::TransToPoolInfoFromJson(events[i].kv.value(),
                                             actor_->member_->poolManager->GetOrNewPool(podPool->id));
    }

    // put key1, key3(status 4), key4 in etcd
    std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
    rep->status = Status::OK();
    rep->kvs.emplace_back(events[0].kv);
    rep->kvs.emplace_back(events[3].kv);
    rep->kvs.emplace_back(events[4].kv);

    auto future = actor_->PodPoolSyncer(rep);
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());

    // test exist in etcd but not in cache, need update
    EXPECT_TRUE(actor_->member_->poolManager->GetPodPool("pool-not-incache") != nullptr);

    // test exist both in etcd but cache and value isn't consistent, need update
    EXPECT_TRUE(actor_->member_->poolManager->GetPodPool("pool-inboth")->status == static_cast<int32_t>(PoolState::RUNNING));

    // test poolId not in etcd, need to remove from poolMap
    EXPECT_TRUE(actor_->member_->poolManager->GetPodPool("pool-inboth-inetcd") == nullptr);

    // test exist both in etcd but cache and value is consistent, but state is DELETED, reUpdate
    EXPECT_TRUE(actor_->member_->poolManager->GetDeployment(POOL_NAME_PREFIX +"pool-inboth-delete") == nullptr);
    // for reset originClient, if not reset, will delete failed in teardown()
    metaStorageAccessor_->metaClient_ = originClient;
}

TEST_F(ScalerTest, DeletePodRequest)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    {
        // delete error
        auto req = std::make_shared<messages::DeletePodRequest>();
        req->set_requestid("req-123");
        req->set_functionagentid("function-agent-0001");
        litebus::Promise<std::shared_ptr<V1Pod>> errPromise;
        errPromise.SetFailed(504);
        litebus::Future<std::string> deletePodCalledArg;
        EXPECT_CALL(*mockClient, DeleteNamespacedPod).WillOnce(testing::Return(errPromise.GetFuture()));
        litebus::Future<std::shared_ptr<messages::DeletePodResponse>> deletePodResponseArg;
        EXPECT_CALL(*testActor_.get(), MockDeletePodResponse).WillOnce(test::FutureArg<0>(&deletePodResponseArg));
        litebus::Async(testActor_->GetAID(), &ScalerTestActor::DeletePodRequest, actor_->GetAID(), req->SerializeAsString());
        ASSERT_AWAIT_READY(deletePodResponseArg);
        EXPECT_EQ(deletePodResponseArg.Get()->code(), 504);
    }

    {
        // delete success with 404
        auto req = std::make_shared<messages::DeletePodRequest>();
        req->set_requestid("req-123");
        req->set_functionagentid("function-agent-0001");
        litebus::Promise<std::shared_ptr<V1Pod>> errPromise;
        errPromise.SetFailed(404);
        EXPECT_CALL(*mockClient, DeleteNamespacedPod).WillOnce(testing::Return(errPromise.GetFuture()));
        litebus::Future<std::shared_ptr<messages::DeletePodResponse>> deletePodResponseArg;
        EXPECT_CALL(*testActor_.get(), MockDeletePodResponse).WillOnce(test::FutureArg<0>(&deletePodResponseArg));
        litebus::Async(testActor_->GetAID(), &ScalerTestActor::DeletePodRequest, actor_->GetAID(), req->SerializeAsString());
        ASSERT_AWAIT_READY(deletePodResponseArg);
        EXPECT_EQ(deletePodResponseArg.Get()->code(), 0);
    }

    {
        // delete success
        auto req = std::make_shared<messages::DeletePodRequest>();
        req->set_requestid("req-123");
        req->set_functionagentid("function-agent-0001");
        EXPECT_CALL(*mockClient, DeleteNamespacedPod).WillOnce(testing::Return(GetReadyPod()));
        litebus::Future<std::shared_ptr<messages::DeletePodResponse>> deletePodResponseArg;
        EXPECT_CALL(*testActor_.get(), MockDeletePodResponse).WillOnce(test::FutureArg<0>(&deletePodResponseArg));
        litebus::Async(testActor_->GetAID(), &ScalerTestActor::DeletePodRequest, actor_->GetAID(), req->SerializeAsString());
        ASSERT_AWAIT_READY(deletePodResponseArg);
        EXPECT_EQ(deletePodResponseArg.Get()->code(), 0);
    }
}

TEST_F(ScalerTest, ResourceIsSyncedTest) {
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);

    std::vector<std::shared_ptr<V1Pod>> podItems;
    std::shared_ptr<V1PodList> podList = std::make_shared<V1PodList>();
    podList->SetItems(podItems);
    EXPECT_CALL(*mockClient, ListNamespacedPod).WillRepeatedly(testing::DoAll(testing::Return(podList)));
    litebus::Future<std::shared_ptr<functionsystem::V1Deployment>> mockDeploymentFuture;
    bool isDeleteFinished = false;
    EXPECT_CALL(*mockClient.get(), DeleteNamespacedDeployment)
        .Times(2)
        .WillRepeatedly(testing::DoAll(testing::Assign(&isDeleteFinished, true), testing::Return(mockDeploymentFuture)));
    auto member = std::make_shared<ScalerActor::Member>();
    auto masterBusiness = std::make_shared<ScalerActor::MasterBusiness>(actor_, actor_->member_);
    actor_->member_->isSynced = true;
    masterBusiness->OnChange();
    EXPECT_AWAIT_TRUE([&isDeleteFinished]() -> bool { return isDeleteFinished; });

    std::shared_ptr<PodPoolInfo> poolInfo = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, poolInfo);
    metaStorageAccessor_->Put("/yr/podpools/info/" + poolInfo->id, poolManager->TransToJsonFromPodPoolInfo(poolInfo)).Get();
    EXPECT_CALL(*mockClient.get(), DeleteNamespacedDeployment).Times(0);
    actor_->member_->isSynced = false;
    masterBusiness->OnChange();
    EXPECT_EQ(2, actor_->GetPoolDeploymentsMap().size());
    metaStorageAccessor_->Delete("/yr/podpools/info", true).Get();
}

TEST_F(ScalerTest, DeletePodNotBindInstanceTest)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    actor_->SetPodMap({});
    actor_->SetNodeMap({});

    EXPECT_AWAIT_TRUE([&]() -> bool { return actor_->curStatus_ == "master"; });
    auto node = GetReadyNode();
    node->GetMetadata()->SetName("10.10.10.01");
    node->GetStatus()->GetAddresses()[0]->SetAddress("10.10.10.01");

    std::unordered_map<std::string, std::shared_ptr<V1Node>> nodeMap;
    nodeMap["10.10.10.01"] = node;
    //actor_->SetNodeMap(nodeMap);

    auto pod1 = GetReadyPod();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPodIP("10.0.0.1");
    pod1->GetStatus()->SetHostIP("10.10.10.01");
    pod1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod1->GetMetadata()->SetName("function-agent-001");
    pod1->GetMetadata()->GetLabels()["yr-reuse"] = "true";
    pod1->GetMetadata()->GetLabels()["app"] = "function-agent";
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    ASSERT_AWAIT_TRUE([&]() -> bool {return actor_->member_->podNameMap.size() == 1;});
    pod1->GetMetadata()->GetLabels()["schedule-policy"] = "monopoly";
    auto isDeleteFinished = false;
    EXPECT_CALL(*mockClient, DeleteNamespacedPod)
        .WillOnce(testing::DoAll(testing::Assign(&isDeleteFinished, true), testing::Return(pod1)));
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    EXPECT_AWAIT_TRUE([&isDeleteFinished]() -> bool { return isDeleteFinished; });
}

TEST_F(ScalerTest, MergeNodeAffinityCoverage)
{
    ::resources::InstanceInfo instanceInfo;
    EXPECT_FALSE(IsAggregationMergePolicy(instanceInfo));
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_NODE_AFFINITY_POLICY") = "";
    EXPECT_FALSE(IsAggregationMergePolicy(instanceInfo));
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_NODE_AFFINITY_POLICY") = "coverage";
    bool mergePolicy = IsAggregationMergePolicy(instanceInfo);
    auto nodeAffinity1 = std::make_shared<V1NodeAffinity>();
    nlohmann::json json = nlohmann::json::parse(NODE_AFFINITY_TEMPLATE_STR);
    nodeAffinity1->FromJson(json);
    // src and dest is null
    EXPECT_TRUE(MergeNodeAffinity(nullptr, nullptr, mergePolicy) == nullptr);
    // src is null
    auto mergeAff = MergeNodeAffinity(nullptr, nodeAffinity1, mergePolicy);
    EXPECT_TRUE(mergeAff->RequiredDuringSchedulingIgnoredDuringExecutionIsSet());
    EXPECT_TRUE(mergeAff->PreferredDuringSchedulingIgnoredDuringExecutionIsSet());
    // dest is null
    mergeAff = MergeNodeAffinity(nodeAffinity1, nullptr, mergePolicy);
    EXPECT_TRUE(mergeAff->RequiredDuringSchedulingIgnoredDuringExecutionIsSet());
    EXPECT_TRUE(mergeAff->PreferredDuringSchedulingIgnoredDuringExecutionIsSet());
    auto nodeAffinity2 = std::make_shared<V1NodeAffinity>();
    nodeAffinity2->FromJson(json);
    nodeAffinity2->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms()[0]->GetMatchExpressions()[0]->SetKey("test");
    nodeAffinity2->GetPreferredDuringSchedulingIgnoredDuringExecution()[0]->SetWeight(999);
    // both src and dest set affinity
    mergeAff = MergeNodeAffinity(nodeAffinity1, nodeAffinity2, mergePolicy);
    EXPECT_EQ(mergeAff->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms()[0]->GetMatchExpressions()[0]->GetKey(), "test");
    EXPECT_EQ(mergeAff->GetPreferredDuringSchedulingIgnoredDuringExecution()[0]->GetWeight(), 999);
}

TEST_F(ScalerTest, MergeNodeAffinityAggregation)
{
    ::resources::InstanceInfo instanceInfo;
    instanceInfo.mutable_createoptions()->operator[]("DELEGATE_NODE_AFFINITY_POLICY") = "aggregation";
    bool mergePolicy = IsAggregationMergePolicy(instanceInfo);
    auto nodeAffinity1 = std::make_shared<V1NodeAffinity>();
    nlohmann::json json = nlohmann::json::parse(NODE_AFFINITY_TEMPLATE_STR);
    nodeAffinity1->FromJson(json);
    auto nodeAffinity2 = std::make_shared<V1NodeAffinity>();
    nodeAffinity2->FromJson(json);
    nodeAffinity2->UnsetRequiredDuringSchedulingIgnoredDuringExecution();
    // src set require, dest not set require
    auto mergeAff = MergeNodeAffinity(nodeAffinity1, nodeAffinity2, mergePolicy);
    EXPECT_EQ(mergeAff->GetPreferredDuringSchedulingIgnoredDuringExecution().size(), 2);
    EXPECT_EQ(mergeAff->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms().size(), 2);
    // src not set require, dest set require
    mergeAff = MergeNodeAffinity(nodeAffinity2, nodeAffinity1, mergePolicy);
    EXPECT_EQ(mergeAff->GetPreferredDuringSchedulingIgnoredDuringExecution().size(), 2);
    EXPECT_EQ(mergeAff->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms().size(), 2);
    nodeAffinity2->FromJson(json);
    mergeAff = MergeNodeAffinity(nodeAffinity1, nodeAffinity2, mergePolicy);
    EXPECT_EQ(mergeAff->GetPreferredDuringSchedulingIgnoredDuringExecution().size(), 2);
    EXPECT_EQ(mergeAff->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms().size(), 4);
    EXPECT_EQ(mergeAff->GetRequiredDuringSchedulingIgnoredDuringExecution()
                  ->GetNodeSelectorTerms()[0]
                  ->GetMatchExpressions()
                  .size(),
              4);
    EXPECT_EQ(mergeAff->GetRequiredDuringSchedulingIgnoredDuringExecution()
                  ->GetNodeSelectorTerms()[0]
                  ->GetMatchFields()
                  .size(),
              4);
    // exceed limit
    nodeAffinity1->FromJson(json);
    nodeAffinity2->FromJson(json);
    auto nodeSelectorJson =
        nodeAffinity1->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms()[0]->ToJson();
    for (int i = 0; i < 11; i++) {
        auto item = std::make_shared<kube_client::model::V1NodeSelectorTerm>();
        item->FromJson(nodeSelectorJson);
        nodeAffinity1->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms().emplace_back(item);
        nodeAffinity2->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms().emplace_back(item);
    }
    mergeAff = MergeNodeAffinity(nodeAffinity1, nodeAffinity2, mergePolicy);
    EXPECT_EQ(mergeAff->GetRequiredDuringSchedulingIgnoredDuringExecution()->GetNodeSelectorTerms().size(), 100);
}

TEST_F(ScalerTest, AbnormalPodAlarm)
{
    auto masterBusiness = std::make_shared<ScalerActor::MasterBusiness>(actor_, actor_->member_);
    actor_->business_ = masterBusiness;
    metrics::MetricsAdapter::GetInstance().enabledInstruments_.insert(metrics::YRInstrument::YR_POD_ALARM);
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    actor_->SetPodMap({});
    actor_->SetNodeMap({});

    auto node = GetReadyNode();
    node->GetMetadata()->SetName("10.10.10.01");
    node->GetStatus()->GetAddresses()[0]->SetAddress("10.10.10.01");
    std::unordered_map<std::string, std::shared_ptr<V1Node>> nodeMap;
    nodeMap["10.10.10.01"] = node;
    actor_->SetNodeMap(nodeMap);
    auto pod1 = GetReadyPod();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPhase("Failed");
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    auto alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) != alarmMap.end());
    metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_.clear();
    EXPECT_TRUE(actor_->member_->pendingPods.empty());

    auto pod2 = GetReadyPod();
    pod2->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod2->GetStatus()->SetPhase("Unknown");
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod2);
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) != alarmMap.end());
    metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_.clear();
    EXPECT_TRUE(actor_->member_->pendingPods.empty());

    pod2->GetStatus()->SetPhase("Succeeded");
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod2);
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_;
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());
    EXPECT_TRUE(actor_->member_->pendingPods.empty());

    pod2->GetStatus()->SetPhase("Failed");
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_DELETE, pod2);
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_;
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());
    EXPECT_TRUE(actor_->member_->pendingPods.empty());
}

TEST_F(ScalerTest, PendingPodAlarm)
{
    auto masterBusiness = std::make_shared<ScalerActor::MasterBusiness>(actor_, actor_->member_);
    actor_->business_ = masterBusiness;
    metrics::MetricsAdapter::GetInstance().enabledInstruments_.insert(metrics::YRInstrument::YR_POD_ALARM);
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    actor_->SetPodMap({});
    actor_->SetNodeMap({});
    auto node = GetReadyNode();
    node->GetMetadata()->SetName("10.10.10.01");
    node->GetStatus()->GetAddresses()[0]->SetAddress("10.10.10.01");
    std::unordered_map<std::string, std::shared_ptr<V1Node>> nodeMap;
    nodeMap["10.10.10.01"] = node;

    auto pod1 = GetReadyPod();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPhase("Pending");
    std::map<std::string, std::string> annotations = {{PENDING_THRESHOLD, "15"}};
    pod1->GetMetadata()->SetAnnotations(annotations);

    // event comes first time, pending duration does not exceed threshold, no alarm
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    auto receivedTime = std::chrono::system_clock::now() - std::chrono::seconds(10);
    actor_->member_->pendingPods["function-agent-123"] = receivedTime;
    actor_->business_->CheckPodStatus();
    auto alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());

    // event comes second time, pending duration does not exceed threshold, no alarm
    pod1->GetStatus()->SetStartTime(kube_client::utility::Datetime("2023-03-15T12:34:56Z"));
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    EXPECT_EQ(actor_->member_->pendingPods["function-agent-123"].time_since_epoch().count(), receivedTime.time_since_epoch().count());
    actor_->business_->CheckPodStatus();
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());
    metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_.clear();

    // pending duration does not exceed threshold, send alarm
    actor_->member_->pendingPods["function-agent-123"] = receivedTime - std::chrono::seconds(20);
    actor_->business_->CheckPodStatus();
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) != alarmMap.end());
    metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_.clear();

    // pod phase turns to Running, no alarm
    pod1->GetStatus()->SetPhase("Running");
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    EXPECT_TRUE(actor_->member_->pendingPods.find("function-agent-123") == actor_->member_->pendingPods.end());
    actor_->business_->CheckPodStatus();
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());
    metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_.clear();

    // pod deletion action will send Modified event(with Pending phase) first, then Delete event
    pod1->GetStatus()->SetPhase("Pending");
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    EXPECT_TRUE(actor_->member_->pendingPods.find("function-agent-123") != actor_->member_->pendingPods.end());
    actor_->business_->CheckPodStatus();
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());
    metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_.clear();

    actor_->OnPodModified(K8sEventType::EVENT_TYPE_DELETE, pod1);
    EXPECT_TRUE(actor_->member_->pendingPods.find("function-agent-123") == actor_->member_->pendingPods.end());
    actor_->business_->CheckPodStatus();
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());
    metrics::MetricsAdapter::GetInstance().GetAlarmHandler().alarmMap_.clear();
}

TEST_F(ScalerTest, PendingPodClear)
{
    auto masterBusiness = std::make_shared<ScalerActor::MasterBusiness>(actor_, actor_->member_);
    actor_->business_ = masterBusiness;
    metrics::MetricsAdapter::GetInstance().enabledInstruments_.insert(metrics::YRInstrument::YR_POD_ALARM);
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    actor_->SetPodMap({});
    actor_->SetNodeMap({});
    auto node = GetReadyNode();
    node->GetMetadata()->SetName("10.10.10.01");
    node->GetStatus()->GetAddresses()[0]->SetAddress("10.10.10.01");

    std::unordered_map<std::string, std::shared_ptr<V1Node>> nodeMap;
    nodeMap["10.10.10.01"] = node;
    actor_->SetNodeMap(nodeMap);

    // pod exists in pendingPods, but its info does not exist in podNameMap
    auto pod1 = GetReadyPod();
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::api::V1PodStatus>());
    pod1->GetStatus()->SetPhase("Pending");
    pod1->GetStatus()->SetStartTime(kube_client::utility::Datetime("2023-03-15T12:34:56Z"));
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    actor_->member_->podNameMap.clear();
    actor_->business_->CheckPodStatus();
    auto alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());
    EXPECT_TRUE(actor_->member_->pendingPods.empty());

    // pod exists in pendingPods, but its status is not pending anymore
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    actor_->member_->podNameMap.find(pod1->GetMetadata()->GetName())->second->GetStatus()->SetPhase("Succeeded");
    actor_->business_->CheckPodStatus();
    alarmMap = metrics::MetricsAdapter::GetInstance().GetAlarmHandler().GetAlarmMap();
    EXPECT_TRUE(alarmMap.find(metrics::POD_ALARM) == alarmMap.end());
    EXPECT_TRUE(actor_->member_->pendingPods.empty());
}

TEST_F(ScalerTest, CreateAgentByPoolIDTest)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    auto localVarResult = std::make_shared<V1Deployment>();
    nlohmann::json localVarJson = nlohmann::json::parse(DEPLOYMENT_JSON);
    auto converted = localVarResult->FromJson(localVarJson);
    if (!converted) {
        YRLOG_ERROR("failed to convert deployment");
    }
    litebus::Async(actor_->GetAID(), &ScalerActor::SetTemplateDeployment, localVarResult);
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);
    auto  counter = std::make_shared<std::atomic<int>>(0);
    poolManager->RegisterScaleUpHandler([cnt(counter)](const std::string &poolID, bool isReserved) {
        (*cnt)++;
    });

    auto createAgentRequest = std::make_shared<messages::CreateAgentRequest>();
    ::resources::InstanceInfo info;
    info.set_requestid("test-request-01");
    info.set_instanceid("test-instance-01");
    info.mutable_createoptions()->operator[]("AFFINITY_POOL_ID") = "pool1";
    info.mutable_resources()->CopyFrom(GetResources());
    createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
    {
        // poolID is not found
        info.set_requestid("test-request-01");
        info.set_instanceid("test-instance-01");
        createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
        litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                       createAgentRequest->SerializeAsString());
        ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-01"; });
        ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().code() == StatusCode::GS_START_CREATE_POD_FAILED; });
        EXPECT_EQ(GetTestResponse().message(), "failed to scale up pod");
    }
    auto podPool = poolManager->GetOrNewPool("pool1");
    podPool->size = 1;
    podPool->maxSize = 2;
    podPool->readyCount = 0;
    podPool->scalable = true;
    podPool->status = static_cast<int32_t>(PoolState::RUNNING);
    podPool->idleRecycleTime.scaled = 10;
    podPool->idleRecycleTime.reserved = 20;
    auto deployment1 = std::make_shared<V1Deployment>();
    deployment1->FromJson(localVarJson);
    deployment1->GetMetadata()->SetName("function-agent-pool1");
    poolManager->PutDeployment(deployment1);
    {
        litebus::Promise<std::shared_ptr<V1Pod>> promise1;
        litebus::Promise<std::shared_ptr<V1Pod>> promise2;
        // success scale pod
        EXPECT_CALL(*mockClient, CreateNamespacedPod)
            .WillOnce(testing::Invoke([promise1](const std::string &rNamespace, const std::shared_ptr<V1Pod> &body) {
                    promise1.SetValue(body);
                    return body;
                }))
            .WillOnce(testing::Invoke([promise2](const std::string &rNamespace, const std::shared_ptr<V1Pod> &body) {
                promise2.SetValue(body);
                return body;
            }));
        info.set_requestid("test-request-02");
        info.set_instanceid("test-instance-02");
        createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
        litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                       createAgentRequest->SerializeAsString());
        ASSERT_AWAIT_READY(promise1.GetFuture());
        auto pod1 = promise1.GetFuture().Get();
        pod1->SetStatus(std::make_shared<functionsystem::kube_client::model::V1PodStatus>());
        pod1->GetStatus()->SetContainerStatuses({});
        pod1->GetStatus()->SetPhase("Running");
        EXPECT_EQ(pod1->GetMetadata()->GetLabels()["yr-idle-to-recycle"], "20");
        actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
        ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-02"; });
        EXPECT_EQ(GetTestResponse().code(), 0);
        EXPECT_EQ(poolManager->GetPodPool("pool1")->readyCount, 1);
        EXPECT_EQ(*counter, 2);
        info.set_requestid("test-request-03");
        info.set_instanceid("test-instance-03");
        createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
        litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                       createAgentRequest->SerializeAsString());
        ASSERT_AWAIT_READY(promise2.GetFuture());
        auto pod2 = promise2.GetFuture().Get();
        EXPECT_EQ(pod2->GetMetadata()->GetLabels()["yr-idle-to-recycle"], "10");
        pod2->SetStatus(std::make_shared<functionsystem::kube_client::model::V1PodStatus>());
        pod2->GetStatus()->SetContainerStatuses({});
        pod2->GetStatus()->SetPhase("Running");
        actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod2);
        ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-03"; });
        EXPECT_EQ(GetTestResponse().code(), 0);
        EXPECT_EQ(poolManager->GetPodPool("pool1")->readyCount, 2);
        EXPECT_EQ(*counter, 4);
        info.set_requestid("test-request-04");
        info.set_instanceid("test-instance-04");
        createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
        litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                       createAgentRequest->SerializeAsString());
        ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-04"; });
        EXPECT_EQ(GetTestResponse().code(), StatusCode::GS_START_CREATE_POD_FAILED);
        EXPECT_EQ(poolManager->GetPodPool("pool1")->readyCount, 2);
        EXPECT_EQ(*counter, 4);

        // delete pod
        EXPECT_CALL(*mockClient, DeleteNamespacedPod).WillOnce(testing::Return(pod2));
        actor_->DoDeletePod(pod2->GetMetadata()->GetName());
        ASSERT_AWAIT_TRUE([&]() -> bool { return *counter == 5; });
        EXPECT_TRUE(poolManager->GetPodPool("pool1")->deletingPodSet.find(pod2->GetMetadata()->GetName()) != poolManager->GetPodPool("pool1")->deletingPodSet.end());
        actor_->OnPodModified(K8sEventType::EVENT_TYPE_DELETE, pod2);
        ASSERT_AWAIT_TRUE([&]() -> bool { return *counter == 6; });
        EXPECT_TRUE(poolManager->GetPodPool("pool1")->deletingPodSet.find(pod2->GetMetadata()->GetName()) == poolManager->GetPodPool("pool1")->deletingPodSet.end());
    }
    {
        litebus::Promise<std::shared_ptr<V1Pod>> promise;
        promise.SetFailed(100);
        EXPECT_CALL(*mockClient, CreateNamespacedPod).WillRepeatedly(testing::Return(promise.GetFuture()));
        // scale up fail to create pod
        podPool->maxSize = 3;
        info.set_requestid("test-request-05");
        info.set_instanceid("test-instance-05");
        createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
        createAgentRequest->mutable_instanceinfo()->CopyFrom(info);
        litebus::Async(testActor_->GetAID(), &ScalerTestActor::CreateAgent, actor_->GetAID(),
                       createAgentRequest->SerializeAsString());
        ASSERT_AWAIT_TRUE([&]() -> bool { return GetTestResponse().requestid() == "test-request-05"; });
        EXPECT_EQ(GetTestResponse().code(), StatusCode::GS_START_CREATE_POD_FAILED);
        EXPECT_EQ(poolManager->GetPodPool("pool1")->readyCount, 1);
        EXPECT_EQ(*counter, 7);
        EXPECT_EQ(poolManager->GetPodPool("pool1")->pendingCreatePodSet.size(), 0);
    }
}

TEST_F(ScalerTest, LoadFunctionAgentTemplate)
{
    (void)litebus::os::Rmdir("/home/sn/scaler/template/");
    actor_->SetTemplateDeployment(nullptr);
    auto status = litebus::Async(actor_->GetAID(), &ScalerActor::SyncTemplateDeployment);
    EXPECT_EQ(status.Get(), StatusCode::FILE_NOT_FOUND);

    (void)litebus::os::Mkdir("/home/sn/scaler/template/");
    auto writeStatus = Write("/home/sn/scaler/template/function-agent.json", "fake json");
    status = litebus::Async(actor_->GetAID(), &ScalerActor::SyncTemplateDeployment);
    ASSERT_AWAIT_READY(status);
    EXPECT_EQ(status.Get(), StatusCode::JSON_PARSE_ERROR);

    writeStatus = Write("/home/sn/scaler/template/function-agent.json", DEPLOYMENT_JSON);
    YRLOG_INFO("write json file result: {}", writeStatus);
    status = litebus::Async(actor_->GetAID(), &ScalerActor::SyncTemplateDeployment);
    ASSERT_AWAIT_READY(status);
    EXPECT_EQ(status.Get(), StatusCode::SUCCESS);
    (void)litebus::os::Rmdir("/home/sn/scaler/");
}

TEST_F(ScalerTest, SyncPodsTest) {
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    std::shared_ptr<PoolManager> poolManager = std::make_shared<PoolManager>(metaStorageAccessor_);
    actor_->SetPoolManager(poolManager);

    litebus::Future<std::shared_ptr<V1PodList>> failedFuture;
    failedFuture.SetFailed(StatusCode::FAILED);
    EXPECT_CALL(*mockClient, ListNamespacedPod).WillOnce(testing::Return(failedFuture));
    auto future = actor_->SyncPodAndInstance();
    future.Get();
    EXPECT_TRUE(future.IsError());

    std::shared_ptr<V1PodList> podList = std::make_shared<V1PodList>();
    std::vector<std::shared_ptr<V1Pod>> podItems;
    podItems.emplace_back(nullptr);
    auto pod1 = GetReadyPod();
    pod1->GetMetadata()->SetName("error-pod");
    podItems.emplace_back(pod1);
    auto pod2 = GetReadyPod();
    pod2->UnsetStatus();
    pod2->GetMetadata()->SetUid("unready-failed");
    pod2->GetMetadata()->SetName("function-agent-unready-failed");
    podItems.emplace_back(pod2);
    podItems.emplace_back(GetReadyPod());
    podList->SetItems(podItems);
    EXPECT_CALL(*mockClient, ListNamespacedPod).WillRepeatedly(testing::DoAll(testing::Return(podList)));
    future = actor_->SyncPodAndInstance();
    EXPECT_TRUE(future.Get().IsOk());
    EXPECT_TRUE(actor_->member_->podNameMap.size() == 2);
    EXPECT_TRUE(actor_->waitForReadyPods_.find("unready-failed") != actor_->waitForReadyPods_.end());
    bool isDeleteFinished = false;
    EXPECT_CALL(*mockClient, DeleteNamespacedPod)
        .WillRepeatedly(testing::DoAll(testing::Assign(&isDeleteFinished, true), testing::Return(GetReadyPod())));
    actor_->waitForReadyPods_["unready-failed"].SetFailed(StatusCode::FAILED);
    ASSERT_AWAIT_TRUE([&]() -> bool { return isDeleteFinished; });
}

TEST_F(ScalerTest, SystemFunctionPodManager)
{
    // mock k8s client
    auto mockClient = std::make_shared<MockKubeClient>();
    actor_->SetKubeClient(mockClient);
    actor_->SetPodMap({});
    auto scaleUpCounter = std::make_shared<std::atomic<int>>(0);
    actor_->member_->frontendManager->RegisterScaleUpHandler(
        [cnt(scaleUpCounter)](const std::string &podName, uint32_t instanceNumber) { cnt->fetch_add(instanceNumber); });

    auto pod1 = GetReadyPod();
    pod1->GetMetadata()->GetLabels()["yr-reuse"] = "true";
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod1);
    actor_->member_->frontendManager->CheckSystemFunctionNeedScale();

    auto pod2 = GetReadyPod();
    pod2->GetMetadata()->SetName("function-agent-a-500m-2048mi-faasfrontend-1");
    pod2->GetSpec()->SetNodeName("node-0002");
    actor_->OnPodModified(K8sEventType::EVENT_TYPE_MODIFIED, pod2);
    // Expect: delete pod is called
    litebus::Future<std::string> deletePodCalledArg;
    EXPECT_CALL(*mockClient, DeleteNamespacedPod)
        .WillOnce(testing::DoAll(test::FutureArg<0>(&deletePodCalledArg), testing::Return(GetReadyPod())));
    actor_->member_->frontendManager->CheckSystemFunctionNeedScale();
    ASSERT_AWAIT_READY(deletePodCalledArg);
}
}  // namespace functionsystem::scaler::test
