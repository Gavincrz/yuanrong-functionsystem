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

#include "common/kube_client/api/core_v1_api.h"
#include "common/kube_client/kube_client.h"
#include "kube_api_server.h"
#include "mocks/mock_kube_client.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::kube_client::test {
using namespace functionsystem::test;
using namespace functionsystem::kube_client::model;
using namespace functionsystem::kube_client::api;

class KubeClientApiTest : public ::testing::Test {
public:
    void SetUpApiClient()
    {
        SslConfig sslConfig{ .clientCertFile = "", .clientKeyFile = "", .caCertFile = "",
                             .credential = "k8sClient", .isSkipTlsVerify = false };
        HealthMonitorParam param{
            .maxFailedTimes = 5,
            .checkIntervalMs = 20,
        };
        uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
        kubeClient_ =
            KubeClient::CreateKubeClient("http://127.0.0.1:" + std::to_string(port) + "/k8s", sslConfig, true, param);
        kubeClient_->SetK8sClientRetryTime(2);
        kubeClient_->SetK8sClientCycMs(20);
        kubeClient_->SetRetryCycMsUpper(10);
        apiClient_ = std::make_shared<ApiClient>(std::make_shared<ApiConfiguration>());
    }

    void SetUp() override
    {
        SetUpApiClient();
        apiServer = std::make_shared<KubeApiServer>("k8s");
        litebus::Spawn(apiServer);
    }

    void TearDown() override
    {
        auto healthMonitor = kubeClient_->GetHealthMonitor();
        if (healthMonitor != nullptr) {
            litebus::Terminate(healthMonitor->GetAID());
            litebus::Await(healthMonitor->GetAID());
        }
        kubeClient_ = nullptr;
        litebus::Terminate(apiServer->GetAID());
        litebus::Await(apiServer->GetAID());
    }
protected:
    std::shared_ptr<ApiClient> apiClient_ = nullptr;
    std::shared_ptr<CoreV1Api> CoreV1Api_ = nullptr;
    std::shared_ptr<KubeApiServer> apiServer = nullptr;
    std::shared_ptr<KubeClient> kubeClient_ = nullptr;

    static std::shared_ptr<kube_client::model::Object> CreatePatchNodeBody()
    {
        std::shared_ptr<V1Node> node = std::make_shared<V1Node>();
        std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
        metadata->SetName("node001");
        node->SetMetadata(metadata);
        std::shared_ptr<V1Node> newNode = std::make_shared<V1Node>();
        std::shared_ptr<V1ObjectMeta> newMetadata =  std::make_shared<V1ObjectMeta>();
        newMetadata->SetName("node001");
        newNode->SetMetadata(newMetadata);
        std::shared_ptr<V1NodeSpec> newNodeSpec = std::make_shared<V1NodeSpec>();
        newNodeSpec->SetTaints({});
        newNode->SetSpec(newNodeSpec);
        nlohmann::json nodeDiffPatch = nlohmann::json::diff(node->ToJson(), newNode->ToJson());
        std::shared_ptr<kube_client::model::Object> body =
            std::make_shared<kube_client::model::Object>();
        (void)body->FromJson(nodeDiffPatch);
        return body;
    }

    static std::shared_ptr<kube_client::model::Object> CreatePatchNamespacedDeploymentBody()
    {
        std::shared_ptr<V1Deployment> deployment = std::make_shared<V1Deployment>();
        deployment->SetApiVersion("0");
        deployment->SetKind("0");
        std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
        metadata->SetName("function-agent-300m-300mi");
        deployment->SetMetadata(metadata);
        std::shared_ptr<V1Deployment> newDeployment = std::make_shared<V1Deployment>();
        newDeployment->SetApiVersion("1");
        newDeployment->SetKind("1");
        std::shared_ptr<V1ObjectMeta> newMetadata = std::make_shared<V1ObjectMeta>();
        newMetadata->SetName("function-agent-300m-300mi");
        newDeployment->SetMetadata(newMetadata);
        nlohmann::json podDiffPatch = nlohmann::json::diff(deployment->ToJson(), newDeployment->ToJson());
        std::shared_ptr<kube_client::model::Object> body =
            std::make_shared<kube_client::model::Object>();
        (void)body->FromJson(podDiffPatch);
        return body;
    }

    static std::shared_ptr<kube_client::model::Object> CreatePatchNamespacedPod()
    {
        std::shared_ptr<V1Pod> pod = std::make_shared<V1Pod>();
        pod->SetApiVersion("0");
        pod->SetKind("0");
        std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
        metadata->SetName("function-agent-pod-0");
        pod->SetMetadata(metadata);
        std::shared_ptr<V1PodSpec> Spec = std::make_shared<V1PodSpec>();
        pod->SetSpec(Spec);

        std::shared_ptr<V1Pod> newPod = std::make_shared<V1Pod>();
        pod->SetApiVersion("1");
        pod->SetKind("1");
        std::shared_ptr<V1ObjectMeta> newMetadata =  std::make_shared<V1ObjectMeta>();
        newMetadata->SetName("function-agent-pod-0");
        newPod->SetMetadata(newMetadata);
        nlohmann::json podDiffPatch = nlohmann::json::diff(pod->ToJson(), newPod->ToJson());
        std::shared_ptr<kube_client::model::Object> body =
            std::make_shared<kube_client::model::Object>();
        (void)body->FromJson(podDiffPatch);
        return body;
    }

    static std::shared_ptr<V1Pod> CreateNamespacedPodBody()
    {
        std::shared_ptr<V1Pod> body = std::make_shared<V1Pod>();
        body->SetApiVersion("0");
        body->SetKind("0");
        std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
        metadata->SetName("function-agent-pod-0");
        body->SetMetadata(metadata);
        std::shared_ptr<V1PodSpec> Spec = std::make_shared<V1PodSpec>();
        body->SetSpec(Spec);
        return body;
    }

    static std::shared_ptr<V1Deployment> CreateNamespacedDeploymentBody()
    {
        std::shared_ptr<V1Deployment> body = std::make_shared<V1Deployment>();
        body->SetApiVersion("0");
        body->SetKind("0");
        std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
        metadata->SetName("function-agent-300m-300mi");
        body->SetMetadata(metadata);
        return body;
    }

    static std::shared_ptr<V1Lease> CreateNamespacedLeaseBody()
    {
        std::shared_ptr<V1Lease> body = std::make_shared<V1Lease>();
        body->SetApiVersion("0");
        body->SetKind("Lease");
        std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
        metadata->SetName("function-master-lease");
        body->SetMetadata(metadata);
        return body;
    }

    void SetRetryResponseQueue (int time, ResponseCode code) {
        std::queue<std::pair<ResponseCode, std::string>> responseQueue;
        for (int i = 0; i < time; i++) {
            responseQueue.emplace(code, "");
        }
        apiServer->SetResponseQueue(responseQueue);
    }

    template <typename FutureType>
    static void CheckFutureError(const litebus::Future<FutureType> &future, int32_t errorCode)
    {
        ASSERT_AWAIT_TRUE([future]() { return future.IsError(); });
        EXPECT_EQ(future.GetErrorCode(), errorCode);
    }
};

const std::string DEFAULT_LIST_JSON_STR = R"(
{
   "kind": "List",
   "metadata": {
     "resourceVersion": "",
     "selfLink": ""
   },
   "apiVersion": "v1",
   "items": []
}
)";

const std::string DEFAULT_NODE_JSON_STR = R"(
{
  "apiVersion": "v1",
  "kind": "Node",
  "metadata": {
     "name": "node-001"
  },
  "spec": {
  },
  "status": {
      "addresses": [
        {
          "address": "10.247.23.146",
           "type": "InternalIP"
        }]
  }
}
)";

const std::string DEFAULT_POD_JSON_STR = R"(
{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {
       "name": "pod-001",
       "namespace": "default"
    },
    "spec": {
    }
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

TEST_F(KubeClientApiTest, testRegisterK8sStatusChangeHandler)
{
    std::function<void()> callback = [=] {};
    auto result = kubeClient_->RegisterK8sStatusChangeHandler(K8sStatusChangeEvent::ON_MAX, "UT", callback);
    EXPECT_FALSE(result);
    result = kubeClient_->RegisterK8sStatusChangeHandler(K8sStatusChangeEvent::ON_RECOVER, "ON_RECOVER_subscriber1", callback);
    EXPECT_TRUE(result);
    result = kubeClient_->RegisterK8sStatusChangeHandler(K8sStatusChangeEvent::ON_RECOVER, "ON_RECOVER_subscriber2", callback);
    EXPECT_TRUE(result);
    result = kubeClient_->RegisterK8sStatusChangeHandler(K8sStatusChangeEvent::ON_FAIL, "ON_FAIL_subscriber3", callback);
    EXPECT_TRUE(result);
    auto k8sStatusChangeHandlerMap = kubeClient_->GetK8sStatusChangeHandlerMap();
    auto handlerMap = k8sStatusChangeHandlerMap.find(K8sStatusChangeEvent::ON_RECOVER)->second;
    EXPECT_TRUE(k8sStatusChangeHandlerMap.find(K8sStatusChangeEvent::ON_RECOVER)->second.find("ON_RECOVER_subscriber1") != handlerMap.end());
    EXPECT_TRUE(k8sStatusChangeHandlerMap.find(K8sStatusChangeEvent::ON_RECOVER)->second.find("ON_RECOVER_subscriber2") != handlerMap.end());
    EXPECT_TRUE(k8sStatusChangeHandlerMap.find(K8sStatusChangeEvent::ON_FAIL)->second.find("ON_FAIL_subscriber3") != handlerMap.end());
}

TEST_F(KubeClientApiTest, testOnK8sRecoverCallback)
{
    bool isCallbackCalled = false;
    std::function<void()> callback = [&] {
        isCallbackCalled = true;
    };
    auto result = kubeClient_->RegisterK8sStatusChangeHandler(K8sStatusChangeEvent::ON_RECOVER,
                                                              "ON_RECOVER_subscriber1", callback);
    auto healthMonitor = kubeClient_->GetHealthMonitor();
    healthMonitor->RunHealthCallback(true);
    EXPECT_TRUE(result);
    EXPECT_TRUE(isCallbackCalled);
}

TEST_F(KubeClientApiTest, testCreateNamespacedPod)
{
    std::string r_namespace = "default";
    std::shared_ptr<V1Pod> body;
    auto result = kubeClient_->CreateNamespacedPod(r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsError());
    body = std::make_shared<V1Pod>();
    body->SetApiVersion("0");
    body->SetKind("0");
    std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
    metadata->SetName("function-agent-pod-0");
    body->SetMetadata(metadata);
    std::shared_ptr<V1PodSpec> Spec = std::make_shared<V1PodSpec>();
    body->SetSpec(Spec);
    result = kubeClient_->CreateNamespacedPod(r_namespace, body);
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-pod-0");
}

TEST_F(KubeClientApiTest, CreatePodWithOwnerReferrence)
{
    kubeClient_->InitOwnerReference("default", "function-master");
    std::shared_ptr<V1Deployment> functionMasterDeploy = std::make_shared<V1Deployment>();
    std::shared_ptr<V1ObjectMeta> masterMeta = std::make_shared<V1ObjectMeta>();
    masterMeta->SetName("function-master");
    masterMeta->SetUid("master-000001");
    functionMasterDeploy->SetMetadata(masterMeta);
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    responseQueue.push(std::make_pair(ResponseCode::OK, functionMasterDeploy->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    auto body = CreateNamespacedPodBody();
    auto result = kubeClient_->CreateNamespacedPod("default", body);
    result.Get();
    EXPECT_TRUE(result.Get()->GetMetadata()->GetOwnerReferences().size() > 0);
    EXPECT_EQ(result.Get()->GetMetadata()->GetOwnerReferences()[0]->GetUid(), "master-000001");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::BAD_REQUEST, functionMasterDeploy->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    body = CreateNamespacedPodBody();
    result = kubeClient_->CreateNamespacedPod("default", body);
    result.Get();
    EXPECT_TRUE(result.IsError());
}

TEST_F(KubeClientApiTest, testK8sNormal)
{
    auto healthMonitor = kubeClient_->GetHealthMonitor();
    healthMonitor->RunHealthCallback(true);
    std::string r_namespace = "default";
    std::shared_ptr<V1Pod> body;
    body = std::make_shared<V1Pod>();
    body->SetApiVersion("0");
    body->SetKind("0");
    std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
    metadata->SetName("function-agent-pod-0");
    body->SetMetadata(metadata);
    std::shared_ptr<V1PodSpec> Spec = std::make_shared<V1PodSpec>();
    body->SetSpec(Spec);
    auto result = kubeClient_->CreateNamespacedPod(r_namespace, body);
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-pod-0");
}

TEST_F(KubeClientApiTest, testK8sFuse)
{
    auto healthMonitor = kubeClient_->GetHealthMonitor();
    healthMonitor->RunHealthCallback(false);
    std::string r_namespace = "default";
    std::shared_ptr<V1Pod> body;
    body = std::make_shared<V1Pod>();
    body->SetApiVersion("0");
    body->SetKind("0");
    std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
    metadata->SetName("function-agent-pod-0");
    body->SetMetadata(metadata);
    std::shared_ptr<V1PodSpec> Spec = std::make_shared<V1PodSpec>();
    body->SetSpec(Spec);
    auto result = kubeClient_->CreateNamespacedPod(r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsError());
    EXPECT_EQ(result.GetErrorCode(), StatusCode::ERR_K8S_UNAVAILABLE);
}

TEST_F(KubeClientApiTest, testK8sFuseThenRecover)
{
    std::string r_namespace = "default";
    std::shared_ptr<V1Pod> body;
    body = std::make_shared<V1Pod>();
    body->SetApiVersion("0");
    body->SetKind("0");
    std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
    metadata->SetName("function-agent-pod-0");
    body->SetMetadata(metadata);
    std::shared_ptr<V1PodSpec> Spec = std::make_shared<V1PodSpec>();
    body->SetSpec(Spec);

    auto healthMonitor = kubeClient_->GetHealthMonitor();
    healthMonitor->RunHealthCallback(false);
    auto result = kubeClient_->CreateNamespacedPod(r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsError());
    EXPECT_EQ(result.GetErrorCode(), StatusCode::ERR_K8S_UNAVAILABLE);

    healthMonitor->RunHealthCallback(true);
    result = kubeClient_->CreateNamespacedPod(r_namespace, body);
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-pod-0");
}

TEST_F(KubeClientApiTest, testDeleteNamespacedPod)
{
    std::string r_namespace = "default";
    auto result = kubeClient_->DeleteNamespacedPod("function-agent-pod-0", r_namespace);
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-pod-0");
}

TEST_F(KubeClientApiTest, testPatchNamespacedPod)
{
    std::string r_namespace = "default";
    auto body = CreatePatchNamespacedPod();
    auto result = kubeClient_->PatchNamespacedPod("function-agent-pod-0", r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsOK());
}

TEST_F(KubeClientApiTest, testReadNamespacePod)
{
    std::string r_namespace = "default";
    nlohmann::json podJson = nlohmann::json::parse(DEFAULT_POD_JSON_STR);
    std::shared_ptr<V1Pod> pod1 = std::make_shared<V1Pod>();
    pod1->FromJson(podJson);
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    responseQueue.push(std::make_pair(ResponseCode::OK, pod1->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::BAD_REQUEST, pod1->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    auto result = kubeClient_->ReadNamespacedPod(r_namespace, "pod-001");
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "pod-001");
    result = kubeClient_->ReadNamespacedPod(r_namespace, "pod-001");
    result.Get();
    EXPECT_TRUE(result.IsError());
}

TEST_F(KubeClientApiTest, testListNamespacedPod)
{
    std::string r_namespace = "default";
    kubeClient_->SetPageLimit(2);
    nlohmann::json listJson = nlohmann::json::parse(DEFAULT_LIST_JSON_STR);
    nlohmann::json podJson = nlohmann::json::parse(DEFAULT_POD_JSON_STR);
    std::shared_ptr<V1PodList> podListWithEmpty = std::make_shared<V1PodList>();
    podListWithEmpty->FromJson(listJson);

    std::shared_ptr<V1PodList> podListWithOne = std::make_shared<V1PodList>();
    podListWithOne->FromJson(listJson);
    std::shared_ptr<V1Pod> pod1 = std::make_shared<V1Pod>();
    pod1->FromJson(podJson);
    podListWithOne->SetItems({pod1});

    std::shared_ptr<V1PodList> podListWithTwo = std::make_shared<V1PodList>();
    podListWithTwo->FromJson(listJson);
    std::shared_ptr<V1Pod> pod2 = std::make_shared<V1Pod>();
    pod2->FromJson(podJson);
    podListWithTwo->SetItems({pod1, pod2});
    // list empty pod
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithEmpty->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    auto result = kubeClient_->ListNamespacedPod(r_namespace);
    result.Get();
    EXPECT_EQ(result.Get()->GetItems().size(), static_cast<long unsigned int>(0));
    // list one pod
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedPod(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), static_cast<long unsigned int>(1));
    // list two pod
    podListWithTwo->GetMetadata()->SetRContinue("id-123");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithEmpty->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedPod(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), static_cast<long unsigned int>(2));
    // list pod with two page
    podListWithTwo->GetMetadata()->SetRContinue("id-123");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedPod(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), 3);
    // list pods with second page error
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::BAD_REQUEST, podListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedPod(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), 2);
    // list pod with 4 items
    responseQueue = {};
    podListWithTwo->GetMetadata()->SetRContinue("id-123");
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithTwo->ToJson().dump().c_str()));
    podListWithTwo->GetMetadata()->SetRContinue("");
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, podListWithTwo->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedPod(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), 4);
    EXPECT_EQ(apiServer->GetResponseQueue().size(), 1);
}

TEST_F(KubeClientApiTest, ListNamespacedDeployment)
{
    std::string r_namespace = "default";
    kubeClient_->SetPageLimit(2);
    nlohmann::json listJson = nlohmann::json::parse(DEFAULT_LIST_JSON_STR);
    nlohmann::json deployJson = nlohmann::json::parse(DEFAULT_DEPLOYMENT_JSON_STR);
    std::shared_ptr<V1DeploymentList> deployListWithEmpty = std::make_shared<V1DeploymentList>();
    deployListWithEmpty->FromJson(listJson);

    std::shared_ptr<V1DeploymentList> deployListWithOne = std::make_shared<V1DeploymentList>();
    deployListWithOne->FromJson(listJson);
    std::shared_ptr<V1Deployment> deploy1 = std::make_shared<V1Deployment>();
    deploy1->FromJson(deployJson);
    deployListWithOne->SetItems({deploy1});

    std::shared_ptr<V1DeploymentList> deployListWithTwo = std::make_shared<V1DeploymentList>();
    deployListWithTwo->FromJson(listJson);
    std::shared_ptr<V1Deployment> deploy2 = std::make_shared<V1Deployment>();
    deploy2->FromJson(deployJson);
    deployListWithTwo->SetItems({deploy1, deploy2});
    // list empty pod
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithEmpty->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    auto result = kubeClient_->ListNamespacedDeployment(r_namespace);
    result.Get();
    EXPECT_EQ(result.Get()->GetItems().size(), 0);
    // list one pod
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedDeployment(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), 1);
    // list two pod
    deployListWithTwo->GetMetadata()->SetRContinue("id-123");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithEmpty->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedDeployment(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), 2);
    // list pod with two page
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedDeployment(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), 3);
    // list pods with second page error
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::BAD_REQUEST, deployListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedDeployment(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), 2);
    // list pod with 4 items
    responseQueue = {};
    deployListWithTwo->GetMetadata()->SetRContinue("id-123");
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithTwo->ToJson().dump().c_str()));
    deployListWithTwo->GetMetadata()->SetRContinue("");
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, deployListWithTwo->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNamespacedDeployment(r_namespace);
    EXPECT_EQ(result.Get()->GetItems().size(), 4);
    EXPECT_EQ(apiServer->GetResponseQueue().size(), 1);
}

TEST_F(KubeClientApiTest, ListNamespacedHorizontalPodAutoscaler)
{
    auto result = kubeClient_->ListNamespacedHorizontalPodAutoscaler("default", false);
    EXPECT_EQ(result.Get()->GetItems().size(), 0);
}

TEST_F(KubeClientApiTest, CreateNamespacedHorizontalPodAutoscaler)
{
    std::string r_namespace = "default";
    std::shared_ptr<V2HorizontalPodAutoscaler> body;
    auto result = kubeClient_->CreateNamespacedHorizontalPodAutoscaler(r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsError());

    body = std::make_shared<V2HorizontalPodAutoscaler>();
    body->SetApiVersion("autoscaling/v2beta2");
    body->SetKind("HorizontalPodAutoscaler");
    auto metaData = std::make_shared<V1ObjectMeta>();
    metaData->SetName("function-agent-pool1-hpa");
    metaData->SetRNamespace(r_namespace);
    body->SetMetadata(metaData);
    result = kubeClient_->CreateNamespacedHorizontalPodAutoscaler(r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsOK());
    std::cout << result.Get()->ToJson().dump() << std::endl;
}

TEST_F(KubeClientApiTest, DeleteNamespacedHorizontalPodAutoscaler)
{
    std::string r_namespace = "default";
    auto result = kubeClient_->DeleteNamespacedHorizontalPodAutoscaler("function-agent-pool1-hpa", r_namespace, litebus::None(), litebus::None());
    result.Get();
    EXPECT_TRUE(result.IsOK());
}

TEST_F(KubeClientApiTest, PatchNamespacedHorizontalPodAutoscaler)
{
    std::string r_namespace = "default";
    std::shared_ptr<Object> body;
    auto result = kubeClient_->PatchNamespacedHorizontalPodAutoscaler("function-agent-pool1-hpa", r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsOK());
}

TEST_F(KubeClientApiTest, DeleteNamespacedDeployment)
{
    std::string r_namespace = "default";
    auto result = kubeClient_->DeleteNamespacedDeployment("function-agent-pool1", r_namespace);
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-pool1");
}

TEST_F(KubeClientApiTest, CreateNamespacedDeployment)
{
    std::string r_namespace = "default";
    std::shared_ptr<V1Deployment> body;
    auto result = kubeClient_->CreateNamespacedDeployment(r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsError());
    body = CreateNamespacedDeploymentBody();
    result = kubeClient_->CreateNamespacedDeployment(r_namespace, body);
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-300m-300mi");
}

TEST_F(KubeClientApiTest, CreatePodPoolWithOwnerReferrence)
{
    kubeClient_->InitOwnerReference("default", "function-master");
    std::shared_ptr<V1Deployment> functionMasterDeploy = std::make_shared<V1Deployment>();
    std::shared_ptr<V1ObjectMeta> masterMeta = std::make_shared<V1ObjectMeta>();
    masterMeta->SetName("function-master");
    masterMeta->SetUid("master-000001");
    functionMasterDeploy->SetMetadata(masterMeta);
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    responseQueue.push(std::make_pair(ResponseCode::OK, functionMasterDeploy->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    auto body = CreateNamespacedDeploymentBody();
    auto result = kubeClient_->CreateNamespacedDeployment("default", body);
    result.Get();
    EXPECT_TRUE(result.Get()->GetMetadata()->GetOwnerReferences().size() > 0);
    EXPECT_EQ(result.Get()->GetMetadata()->GetOwnerReferences()[0]->GetUid(), "master-000001");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::BAD_REQUEST, functionMasterDeploy->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    body = CreateNamespacedDeploymentBody();
    result = kubeClient_->CreateNamespacedDeployment("default", body);
    result.Get();
    EXPECT_TRUE(result.IsError());
}

TEST_F(KubeClientApiTest, PatchNamespacedDeployment)
{
    std::string r_namespace = "default";
    auto body = CreatePatchNamespacedDeploymentBody();
    auto result = kubeClient_->PatchNamespacedDeployment("function-agent-300m-300mi", r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsOK());
}

TEST_F(KubeClientApiTest, ListNode)
{
    kubeClient_->SetPageLimit(2);
    nlohmann::json listJson = nlohmann::json::parse(DEFAULT_LIST_JSON_STR);
    nlohmann::json nodeJson = nlohmann::json::parse(DEFAULT_NODE_JSON_STR);
    std::shared_ptr<V1NodeList> nodeListWithEmpty = std::make_shared<V1NodeList>();
    nodeListWithEmpty->FromJson(listJson);

    std::shared_ptr<V1NodeList> nodeListWithOne = std::make_shared<V1NodeList>();
    nodeListWithOne->FromJson(listJson);
    std::shared_ptr<V1Node> node1 = std::make_shared<V1Node>();
    node1->FromJson(nodeJson);
    nodeListWithOne->SetItems({node1});

    std::shared_ptr<V1NodeList> nodeListWithTwo = std::make_shared<V1NodeList>();
    nodeListWithTwo->FromJson(listJson);
    std::shared_ptr<V1Node> node2 = std::make_shared<V1Node>();
    node2->FromJson(nodeJson);
    nodeListWithTwo->SetItems({node1, node2});
    // list empty node
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithEmpty->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    auto result = kubeClient_->ListNode();
    result.Get();
    EXPECT_EQ(result.Get()->GetItems().size(), 0);
    // list one node
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNode();
    EXPECT_EQ(result.Get()->GetItems().size(), 1);
    // list two node
    nodeListWithTwo->GetMetadata()->SetRContinue("id-123");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithEmpty->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNode();
    EXPECT_EQ(result.Get()->GetItems().size(), 2);
    // list nodes with two page
    nodeListWithTwo->GetMetadata()->SetRContinue("id-123");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNode();
    EXPECT_EQ(result.Get()->GetItems().size(), 3);
    // list nodes with second page error
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::BAD_REQUEST, nodeListWithOne->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNode();
    EXPECT_EQ(result.Get()->GetItems().size(), 2);
    // list node with 4 items
    responseQueue = {};
    nodeListWithTwo->GetMetadata()->SetRContinue("id-123");
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithTwo->ToJson().dump().c_str()));
    nodeListWithTwo->GetMetadata()->SetRContinue("");
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithTwo->ToJson().dump().c_str()));
    responseQueue.push(std::make_pair(ResponseCode::OK, nodeListWithTwo->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ListNode();
    EXPECT_EQ(result.Get()->GetItems().size(), 4);
    EXPECT_EQ(apiServer->GetResponseQueue().size(), 1);
}

TEST_F(KubeClientApiTest, PatchNode)
{
    auto body = CreatePatchNodeBody();
    auto result = kubeClient_->PatchNode("node001", body);
    result.Get();
    EXPECT_TRUE(result.IsOK());
}

TEST_F(KubeClientApiTest, WatchHandlerManagerTest)
{
    WatchHandlerManager watchHandlerManager;
    watchHandlerManager.RegisterWatchHandler("", EventType::EVENT_TYPE_ADD, nullptr);
    watchHandlerManager.GetWatchHandlerFunc("", EventType::EVENT_TYPE_ADD);
    watchHandlerManager.GetEventTypeByStr("");

    litebus::http::Response response;
    apiClient_->HandleResponse("path", &response);
}

TEST_F(KubeClientApiTest, HandleResponseTest)
{
    std::string etcdPod =
        R"({"type":"ADDED","object":{"apiVersion":"v1","kind":"Pod","metadata":{"name":"etcd-master1"}}})";
    bool flag = false;
    bool res = apiClient_->RegisterWatchHandler("V1Pod", EventType::EVENT_TYPE_ADD,
                                                [&flag](const EventType &, const std::shared_ptr<ModelBase> &) {
                                                    flag = true;
                                                    YRLOG_DEBUG("Call Watch Handler");
                                                });
    EXPECT_TRUE(res);
    std::string podBody1 = etcdPod.substr(0, etcdPod.size() - 1);
    litebus::http::Response response1(ResponseCode::OK, podBody1);
    apiClient_->HandleResponse("path", &response1);
    EXPECT_FALSE(flag);
    std::string  podBody2 = etcdPod.substr(etcdPod.size() - 1) +  "\n";
    litebus::http::Response response2(ResponseCode::OK, podBody2);
    apiClient_->HandleResponse("path", &response2);
    EXPECT_TRUE(flag);
}

TEST_F(KubeClientApiTest, LeaseTest)
{
    nlohmann::json podJson = nlohmann::json::parse(functionsystem::test::DEFAULT_LEASE_JSON_STR);
    std::shared_ptr<V1Lease> lease = std::make_shared<V1Lease>();
    lease->FromJson(podJson);

    std::string r_namespace = "default";
    std::shared_ptr<V1Lease> body;
    auto result = kubeClient_->CreateNamespacedLease(r_namespace, body);
    result.Get();
    EXPECT_TRUE(result.IsError());
    body = std::make_shared<V1Lease>();
    body->SetApiVersion("0");
    body->SetKind("Lease");
    std::shared_ptr<V1ObjectMeta> metadata = std::make_shared<V1ObjectMeta>();
    metadata->SetName("function-master-lease");
    body->SetMetadata(metadata);
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    lease->GetSpec()->SetHolderIdentity("test");
    responseQueue.push(std::make_pair(ResponseCode::OK, lease->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->CreateNamespacedLease(r_namespace, body);
    result.Get();
    EXPECT_EQ(result.Get()->GetSpec()->GetHolderIdentity(), "test");
    // read lease
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, lease->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ReadNamespacedLease("function-master-lease", r_namespace);
    responseQueue.push(std::make_pair(ResponseCode::OK, lease->ToJson().dump().c_str()));
    EXPECT_EQ(result.Get()->GetSpec()->GetHolderIdentity(), "test");
    // replace lease
    lease->GetSpec()->SetHolderIdentity("test1");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::OK, lease->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    result = kubeClient_->ReplaceNamespacedLease("function-master-lease", r_namespace, lease);
    EXPECT_EQ(result.Get()->GetSpec()->GetHolderIdentity(), "test1");

}

TEST_F(KubeClientApiTest, CreateLeaseWithOwnerReferrence)
{
    kubeClient_->InitOwnerReference("default", "function-master");
    std::shared_ptr<V1Deployment> functionMasterDeploy = std::make_shared<V1Deployment>();
    std::shared_ptr<V1ObjectMeta> masterMeta = std::make_shared<V1ObjectMeta>();
    masterMeta->SetName("function-master");
    masterMeta->SetUid("master-000001");
    functionMasterDeploy->SetMetadata(masterMeta);
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    responseQueue.push(std::make_pair(ResponseCode::OK, functionMasterDeploy->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    auto body = CreateNamespacedLeaseBody();
    auto result = kubeClient_->CreateNamespacedLease("default", body);
    result.Get();
    EXPECT_TRUE(result.Get()->GetMetadata()->GetOwnerReferences().size() > 0);
    EXPECT_EQ(result.Get()->GetMetadata()->GetOwnerReferences()[0]->GetUid(), "master-000001");
    responseQueue = {};
    responseQueue.push(std::make_pair(ResponseCode::BAD_REQUEST, functionMasterDeploy->ToJson().dump().c_str()));
    apiServer->SetResponseQueue(responseQueue);
    body = CreateNamespacedLeaseBody();
    result = kubeClient_->CreateNamespacedLease("default", body);
    result.Get();
    EXPECT_TRUE(result.IsError());
}

TEST_F(KubeClientApiTest, CreateNamespacedPodRetry)
{
    std::string r_namespace = "default";
    auto body = CreateNamespacedPodBody();

    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::INTERNAL_SERVER_ERROR);
    auto result = kubeClient_->CreateNamespacedPod(r_namespace, body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::INTERNAL_SERVER_ERROR);
    result = kubeClient_->CreateNamespacedPod(r_namespace, body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-pod-0");
}

TEST_F(KubeClientApiTest, ListNamespacedPodRetry)
{
    std::string r_namespace = "default";
    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::BAD_GATEWAY);
    auto result = kubeClient_->DoListNamespacePod(r_namespace, {}, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry 2 success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::BAD_GATEWAY);
    result = kubeClient_->DoListNamespacePod(r_namespace, {}, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_EQ(result.Get()->GetKind(), "list");
}

TEST_F(KubeClientApiTest, PatchNamespacedPodRetry)
{
    std::string r_namespace = "default";
    auto body = CreatePatchNamespacedPod();

    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::SERVICE_UNAVAILABLE);
    auto result = kubeClient_->PatchNamespacedPod("function-agent-pod-0", r_namespace, body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::SERVICE_UNAVAILABLE);
    result = kubeClient_->PatchNamespacedPod("function-agent-pod-0", r_namespace, body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsOK());
}

TEST_F(KubeClientApiTest, DeleteNamespacedPodRetry)
{
    std::string r_namespace = "default";

    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::GATEWAY_TIMEOUT);
    auto result = kubeClient_->DeleteNamespacedPod("function-agent-pod-0", r_namespace);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::GATEWAY_TIMEOUT);
    result = kubeClient_->DeleteNamespacedPod("function-agent-pod-0", r_namespace);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-pod-0");
}

TEST_F(KubeClientApiTest, CreateNamespacedDeploymentRetry)
{
    std::string r_namespace = "default";
    auto body = CreateNamespacedDeploymentBody();

    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::INTERNAL_SERVER_ERROR);
    auto result = kubeClient_->CreateNamespacedDeployment(r_namespace, body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::INTERNAL_SERVER_ERROR);
    result = kubeClient_->CreateNamespacedDeployment(r_namespace, body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-300m-300mi");
}

TEST_F(KubeClientApiTest, ListNamespacedDeploymentRetry)
{
    std::string r_namespace = "default";
    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::NOT_IMPLEMENTED);
    auto result = kubeClient_->DoListNamespaceDeployment(r_namespace, {}, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry 2 success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::NOT_IMPLEMENTED);
    result = kubeClient_->DoListNamespaceDeployment(r_namespace, {}, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_EQ(result.Get()->GetKind(), "list");
}

TEST_F(KubeClientApiTest, PatchNamespacedDeploymentRetry)
{
    std::string r_namespace = "default";
    auto body = CreatePatchNamespacedDeploymentBody();

    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::GATEWAY_TIMEOUT);
    auto result = kubeClient_->PatchNamespacedDeployment("function-agent-300m-300mi", r_namespace, body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::GATEWAY_TIMEOUT);
    result = kubeClient_->PatchNamespacedDeployment("function-agent-300m-300mi", r_namespace, body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsOK());
}

TEST_F(KubeClientApiTest, DeleteNamespacedDeploymentRetry)
{
    std::string r_namespace = "default";

    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::INTERNAL_SERVER_ERROR);
    auto result = kubeClient_->DeleteNamespacedDeployment("function-agent-pool1", r_namespace);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::INTERNAL_SERVER_ERROR);
    result = kubeClient_->DeleteNamespacedDeployment("function-agent-pool1", r_namespace);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_EQ(result.Get()->GetMetadata()->GetName(), "function-agent-pool1");
}

TEST_F(KubeClientApiTest, ListNodeRetry)
{
    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::TOO_MANY_REQUESTS);
    auto result = kubeClient_->DoListNode({}, {}, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry 2 success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::TOO_MANY_REQUESTS);
    result = kubeClient_->DoListNode({}, {}, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_EQ(result.Get()->GetItems().size(), 0);
}

TEST_F(KubeClientApiTest, PatchNodeRetry)
{
    auto body = CreatePatchNodeBody();

    // first request fail, retry 2 times fail
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime() + 1, ResponseCode::INTERNAL_SERVER_ERROR);
    auto result = kubeClient_->PatchNode("node001", body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsError());

    // first request fail, retry 1 time fail, retry success
    SetRetryResponseQueue(kubeClient_->GetK8sClientRetryTime(), ResponseCode::INTERNAL_SERVER_ERROR);
    result = kubeClient_->PatchNode("node001", body);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.Get();
    EXPECT_TRUE(result.IsOK());
}


TEST_F(KubeClientApiTest, HealthMonitorTest)
{
    auto healthMonitor = kubeClient_->GetHealthMonitor();

    auto cb = [=](bool healthy) {
        YRLOG_INFO("transfer health status: {} to {}", !healthy, healthy);
        ASSERT_EQ(healthMonitor->IsHealthy(), healthy);
    };
    healthMonitor->SubscribeK8sHealth(cb);

    // 模拟健康监测2次后，返回成功，提前预置好响应失败结果
    std::queue<std::pair<ResponseCode, std::string>> responseQueue;
    responseQueue.emplace(ResponseCode::INTERNAL_SERVER_ERROR, "");
    responseQueue.emplace(ResponseCode::INTERNAL_SERVER_ERROR, "");
    apiServer->SetResponseQueue(responseQueue);
    // 模拟x次请求失败，在第6次失败时开始健康监测
    for (int i = 0; i < 7; i++) {
        litebus::Async(healthMonitor->GetAID(), &HealthMonitor::Start);
    }
    // 等待监测完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_AWAIT_TRUE([&]() -> bool { return healthMonitor->IsHealthy(); });
    ASSERT_AWAIT_TRUE([&]() -> bool {
        return healthMonitor->GetFailedTime() == static_cast<uint32_t>(0);
    });

    // 模拟x次请求失败，在第6次失败时开始健康监测
    for (int i = 0; i < 7; i++) {
        litebus::Async(healthMonitor->GetAID(), &HealthMonitor::Start);
    }
    // 构造超时请求，等待监测完成
    ASSERT_AWAIT_TRUE([&]() -> bool { return healthMonitor->IsHealthy(); });
    ASSERT_EQ(healthMonitor->GetFailedTime(), static_cast<long unsigned int>(0));

    litebus::Terminate(healthMonitor->GetAID());
    litebus::Await(healthMonitor->GetAID());
}

TEST_F(KubeClientApiTest, InvalidKubeConfig)
{
    SslConfig sslConfig{
        .clientCertFile = "", .clientKeyFile = "", .caCertFile = "", .credential = "k8sClient", .isSkipTlsVerify = false
    };
    HealthMonitorParam param{
        .maxFailedTimes = 5,
        .checkIntervalMs = 20,
    };
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    auto kubeClient =
        KubeClient::CreateKubeClient("http://127.0.0.1:" + std::to_string(port) + "/k8s", sslConfig, false, param);
    KubeClient::ClusterSslConfig("fake", "fake", true);
    kubeClient->SetIsK8sHealthy(false);

    CheckFutureError(kubeClient->ListNamespacedHorizontalPodAutoscaler("default", false),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    std::string r_namespace = "default";
    std::shared_ptr<V2HorizontalPodAutoscaler> body;
    CheckFutureError(kubeClient->CreateNamespacedHorizontalPodAutoscaler(r_namespace, body),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    std::shared_ptr<Object> objBody;
    CheckFutureError(
        kubeClient->PatchNamespacedHorizontalPodAutoscaler("function-agent-pool1-hpa", r_namespace, objBody),
        static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->DeleteNamespacedHorizontalPodAutoscaler("function-agent-pool1-hpa", r_namespace,
                                                                         litebus::None(), litebus::None()),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->DoListNamespaceDeployment(r_namespace, {}, {}),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    std::shared_ptr<V1Deployment> deploymentBody;
    CheckFutureError(kubeClient->CreateNamespacedDeployment(r_namespace, deploymentBody),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->DeleteNamespacedDeployment("function-agent-pool1", r_namespace),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->PatchNamespacedDeployment("function-agent-300m-300mi", r_namespace,
                                                           CreatePatchNamespacedDeploymentBody()),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->ReadNamespacedDeployment("function-agent-300m-300mi", r_namespace),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->DoListNamespacePod(r_namespace, {}, {}),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->PatchNamespacedPod("function-agent-pod-0", r_namespace, CreatePatchNamespacedPod()),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->DeleteNamespacedPod("function-agent-pod-0", r_namespace),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->ReadNamespacedPod(r_namespace, "pod-001"),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->DoListNode({}, {}, {}), static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    std::shared_ptr<V1Lease> leaseBody;
    CheckFutureError(kubeClient->CreateNamespacedLease(r_namespace, leaseBody),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->ReadNamespacedLease("function-master-lease", r_namespace),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    std::shared_ptr<V1Lease> lease = std::make_shared<V1Lease>();
    CheckFutureError(kubeClient->ReplaceNamespacedLease("function-master-lease", r_namespace, lease),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));

    CheckFutureError(kubeClient->PatchNode("node001", CreatePatchNodeBody()),
                     static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
}
}  // namespace functionsystem::kube_client::test
