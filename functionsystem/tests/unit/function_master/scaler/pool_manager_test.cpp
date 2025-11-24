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
#include <gtest/gtest.h>

#include "function_master/scaler/pool/pool_manager.h"

namespace functionsystem::scaler::test {

const std::string POD_POOL_INFO_STR = R"(
{
     "id": "pool1",
     "size": 1,
     "max_size": 1,
     "scalable": false,
     "group": "rg1",
     "reuse": true,
     "image": "runtime-manager:version1",
     "init_image": "function-agent-init:version1",
     "labels": {
       "label1":"val1"
     },
     "environment": {
       "env1": "key1",
       "env2": "key2"
     },
     "volumes": "",
     "volume_mounts": "",
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
     "idle_recycle_time": {
        "reserved": -1,
        "scaled": 1
     },
     "runtime_class_name": "runc",
     "node_selector": {
         "label1":"val1"
      },
     "tolerations": "",
     "affinities": "",
     "topology_spread_constraints": ""
}
)";

class PoolManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        poolManager_ = std::make_shared<PoolManager>(nullptr);
    }

    void TearDown() override
    {
    }

    std::shared_ptr<PoolManager> poolManager_;
};

std::shared_ptr<V1Pod> GenNewPod(const std::string &podName)
{
    auto pod1 = std::make_shared<V1Pod>();
    pod1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod1->GetMetadata()->SetName(podName);
    pod1->GetMetadata()->SetLabels({});
    pod1->GetMetadata()->SetAnnotations({});
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::model::V1PodStatus>());
    pod1->GetStatus()->SetContainerStatuses({ std::make_shared<functionsystem::kube_client::model::V1ContainerStatus>() });
    pod1->GetStatus()->GetContainerStatuses().front()->SetContainerID("runtime-manager");
    pod1->GetStatus()->SetPhase("Running");
    return pod1;
}

std::shared_ptr<V1Deployment> GenNewDeployment(const std::string &deploymentName)
{
    auto deployment1 = std::make_shared<V1Deployment >();
    deployment1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    deployment1->GetMetadata()->SetName(deploymentName);
    deployment1->GetMetadata()->SetUid(deploymentName);
    deployment1->SetSpec(std::make_shared<functionsystem::kube_client::model::V1DeploymentSpec>());
    deployment1->GetSpec()->SetRTemplate(std::make_shared<functionsystem::kube_client::model::V1PodTemplateSpec>());
    deployment1->GetSpec()->GetRTemplate()->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    deployment1->GetSpec()->GetRTemplate()->GetMetadata()->SetName(deploymentName);
    deployment1->GetSpec()->GetRTemplate()->GetMetadata()->SetLabels({{"app", deploymentName}});
    deployment1->GetSpec()->GetRTemplate()->GetMetadata()->SetAnnotations({});
    return deployment1;
}

TEST_F(PoolManagerTest, ParsePodPool)
{
    auto podPool = poolManager_->GetOrNewPool("pool1");
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    EXPECT_EQ(podPool->maxSize, 1);
    EXPECT_EQ(podPool->scalable, false);
    EXPECT_EQ(podPool->idleRecycleTime.reserved, -1);
    EXPECT_EQ(podPool->idleRecycleTime.scaled, 1);
}

TEST_F(PoolManagerTest, PodEvent)
{
    auto  persistentCounter = std::make_shared<std::atomic<int>>(0);
    poolManager_->RegisterPersistHandler([cnt(persistentCounter)](const std::string &poolID) {
        (*cnt)++;
    });
    auto  scaleUpCounter = std::make_shared<std::atomic<int>>(0);
    poolManager_->RegisterScaleUpHandler([cnt(scaleUpCounter)](const std::string &poolID, bool isReserved) {
        (*cnt)++;
    });
    auto podPool = poolManager_->GetOrNewPool("pool1");
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    {
        // pod pool is not scalable
        podPool->scalable = false;
        auto pod1 = GenNewPod("function-agent-pool1-1");
        poolManager_->OnPodUpdate(pod1);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 0);
        pod1->GetMetadata()->GetAnnotations()["yr-pod-pool"] = "pool2";
        poolManager_->OnPodUpdate(pod1);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 0);
        pod1->GetMetadata()->GetAnnotations()["yr-pod-pool"] = "pool1";
        poolManager_->OnPodUpdate(pod1);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 0);
    }
    {
        // pod pool is scalable
        podPool->scalable = true;
        podPool->size = 2;
        podPool->maxSize = 3;
        podPool->readyCount = 0;
        podPool->status = static_cast<int32_t>(PoolState::CREATING);
        auto pod1 = GenNewPod("function-agent-pool1-1");
        pod1->GetMetadata()->GetAnnotations()["yr-pod-pool"] = "pool1";
        poolManager_->OnPodUpdate(pod1);

        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 1);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->status, static_cast<int32_t>(PoolState::CREATING));
        EXPECT_EQ(*persistentCounter, 1);
        // put same pod again
        poolManager_->OnPodUpdate(pod1);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 1);
        EXPECT_EQ(*persistentCounter, 1);
        EXPECT_EQ(*scaleUpCounter, 2);
        // put new pod
        auto pod2 = GenNewPod("function-agent-pool1-2");
        pod2->GetMetadata()->GetAnnotations()["yr-pod-pool"] = "pool1";
        poolManager_->OnPodUpdate(pod2);
        EXPECT_EQ(*persistentCounter, 2);
        EXPECT_EQ(*scaleUpCounter, 3);
        auto pod3 = GenNewPod("function-agent-pool1-3");
        pod3->GetMetadata()->GetAnnotations()["yr-pod-pool"] = "pool2";
        poolManager_->OnPodUpdate(pod3);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 2);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->status, static_cast<int32_t>(PoolState::RUNNING));
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyPodSet.size(), 2);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->pendingCreatePodSet.size(), 0);
        EXPECT_EQ(*persistentCounter, 2);
        EXPECT_EQ(*scaleUpCounter, 3);
        // delete pod
        poolManager_->OnPodDelete(pod3);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 2);
        EXPECT_EQ(*persistentCounter, 2);
        poolManager_->OnPodDelete(pod2);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 1);
        EXPECT_EQ(*persistentCounter, 3);
        // test terminating pod
        EXPECT_FALSE(IsPodTerminating(pod1));
        pod1->SetStatus(std::make_shared<functionsystem::kube_client::model::V1PodStatus>());
        EXPECT_FALSE(IsPodTerminating(pod1));
        pod1->GetStatus()->SetContainerStatuses({ std::make_shared<functionsystem::kube_client::model::V1ContainerStatus>() });
        EXPECT_FALSE(IsPodTerminating(pod1));
        pod1->GetStatus()->GetContainerStatuses().front()->SetContainerID("test");
        EXPECT_FALSE(IsPodTerminating(pod1));
        pod1->GetStatus()->GetContainerStatuses().front()->SetState(std::make_shared<functionsystem::kube_client::model::V1ContainerState>());
        EXPECT_FALSE(IsPodTerminating(pod1));
        pod1->GetStatus()->GetContainerStatuses().front()->GetState()->SetTerminated(std::make_shared<functionsystem::kube_client::model::V1ContainerStateTerminated>());
        EXPECT_TRUE(IsPodTerminating(pod1));
        poolManager_->OnPodUpdate(pod1);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 0);
        EXPECT_EQ(*persistentCounter, 4);
        EXPECT_EQ(*scaleUpCounter, 5);
    }

}

TEST_F(PoolManagerTest, PendingPodEvent)
{
    {
        // pod pool is scalable, pod is pending
        auto podPool1 = poolManager_->GetOrNewPool("pending-pool1");
        PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool1);
        podPool1->scalable = true;
        podPool1->size = 2;
        podPool1->maxSize = 3;
        podPool1->readyCount = 0;
        podPool1->status = static_cast<int32_t>(PoolState::CREATING);
        auto pod1 = GenNewPod("function-agent-pending-pool1-1");
        pod1->GetMetadata()->GetAnnotations()["yr-pod-pool"] = "pending-pool1";
        pod1->GetStatus()->SetPhase("Pending");
        poolManager_->OnPodUpdate(pod1);
        EXPECT_EQ(poolManager_->GetPodPool("pending-pool1")->readyCount, 0);
        EXPECT_EQ(poolManager_->GetPodPool("pending-pool1")->pendingCreatePodSet.size(), 1);
        pod1->GetStatus()->SetPhase("Running");
        poolManager_->OnPodUpdate(pod1);
        EXPECT_EQ(poolManager_->GetPodPool("pending-pool1")->readyCount, 1);
        EXPECT_EQ(poolManager_->GetPodPool("pending-pool1")->pendingCreatePodSet.size(), 0);
        auto pod2 = GenNewPod("function-agent-pending-pool1-2");
        pod2->GetMetadata()->GetAnnotations()["yr-pod-pool"] = "pending-pool1";
        poolManager_->OnPodUpdate(pod2);
        EXPECT_EQ(poolManager_->GetPodPool("pending-pool1")->readyCount, 2);
        EXPECT_EQ(poolManager_->GetPodPool("pending-pool1")->status, static_cast<int32_t>(PoolState::RUNNING));
    }
}

TEST_F(PoolManagerTest, TryScaleUpPod)
{
    EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", true).IsNone());
    EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", false).IsNone());
    auto podPool = poolManager_->GetOrNewPool("pool1");
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    {
        podPool->scalable = false;
        EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", true).IsNone());
    }
    {
        podPool->scalable = true;
        podPool->status = static_cast<int32_t>(PoolState::NEW);
        EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", true).IsNone());
        podPool->status = static_cast<int32_t>(PoolState::FAILED);
        EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", true).IsNone());
        podPool->status = static_cast<int32_t>(PoolState::DELETED);
        EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", true).IsNone());
    }
    {
        podPool->scalable = true;
        podPool->status = static_cast<int32_t>(PoolState::RUNNING);
        // count reach maxSize
        podPool->size = 1;
        podPool->maxSize = 2;
        podPool->pendingCreatePodSet.emplace("function-agent-pool1-1");
        podPool->readyPodSet.emplace("function-agent-pool1-2");
        EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", true).IsNone());
        // isReserved and count reach size
        podPool->maxSize = 3;
        podPool->size = 2;
        EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", true).IsNone());
        // failed to get deployment
        podPool->pendingCreatePodSet.clear();
        EXPECT_TRUE(poolManager_->TryScaleUpPod("pool1", true).IsNone());
    }
    {
        podPool->scalable = true;
        podPool->status = static_cast<int32_t>(PoolState::RUNNING);
        // count reach maxSize
        podPool->size = 1;
        podPool->maxSize = 2;
        podPool->pendingCreatePodSet.clear();
        podPool->readyPodSet.clear();
        auto deployment1 = GenNewDeployment("function-agent-pool1");
        deployment1->GetSpec()->GetRTemplate()->GetMetadata()->GetLabels()["key1"] = "val1";
        deployment1->GetSpec()->GetRTemplate()->GetMetadata()->GetAnnotations()["key1"] = "val1";
        poolManager_->PutDeployment(deployment1);
        // scale reserved
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->pendingCreatePodSet.size(), 0);
        auto pod = poolManager_->TryScaleUpPod("pool1", true).Get();
        EXPECT_EQ(pod->GetMetadata()->GetLabels()["yr-idle-to-recycle"], "unlimited");
        EXPECT_EQ(pod->GetMetadata()->GetAnnotations()["yr-pod-pool"], "pool1");
        EXPECT_EQ(pod->GetMetadata()->GetAnnotations()["key1"], "val1");
        EXPECT_EQ(pod->GetMetadata()->GetLabels()["key1"], "val1");
        EXPECT_EQ(pod->GetMetadata()->GetOwnerReferences()[0]->GetName(), "function-agent-pool1");
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->pendingCreatePodSet.size(), 1);
        // scale scaled
        auto pod1 = poolManager_->TryScaleUpPod("pool1", false).Get();
        EXPECT_EQ(pod1->GetMetadata()->GetLabels()["yr-idle-to-recycle"], "1");
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->pendingCreatePodSet.size(), 2);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 0);
        pod1->SetStatus(std::make_shared<functionsystem::kube_client::model::V1PodStatus>());
        pod1->GetStatus()->SetContainerStatuses({ std::make_shared<functionsystem::kube_client::model::V1ContainerStatus>() });
        pod1->GetStatus()->GetContainerStatuses().front()->SetContainerID("runtime-manager");
        pod1->GetStatus()->SetPhase("Running");
        poolManager_->OnPodUpdate(pod1);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->pendingCreatePodSet.size(), 1);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 1);
        pod->SetStatus(std::make_shared<functionsystem::kube_client::model::V1PodStatus>());
        pod->GetStatus()->SetContainerStatuses({ std::make_shared<functionsystem::kube_client::model::V1ContainerStatus>() });
        pod->GetStatus()->GetContainerStatuses().front()->SetContainerID("runtime-manager");
        pod->GetStatus()->SetPhase("Running");
        poolManager_->OnPodUpdate(pod);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->pendingCreatePodSet.size(), 0);
        EXPECT_EQ(poolManager_->GetPodPool("pool1")->readyCount, 2);
    }
}

TEST_F(PoolManagerTest, ValidatePodPoolCreateParams)
{
    // PodId
    auto podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->id = "ERROR_ID";
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // PodPoolGroup
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->group = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // Size
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->size = -1;
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // MaxSize
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->maxSize = 1;
    podPool->size = 2;
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // HorizontalPodAutoscalerSpec
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->maxSize = 1;
    podPool->size = 0;
    podPool->horizontalPodAutoscalerSpec =
        R"({"minReplicas": 1, "maxReplicas": 2, "metrics":[{"resource": {"name":"cpu", "target":{"averageUtilization":20, "type":"Utilization"}}, "type":"Resource"}, {"resource": {"name":"memory", "target":{"averageUtilization":50, "type":"Utilization"}}, "type":"Resource"}]})";
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // IdleRecycleTime.Scaled
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->idleRecycleTime.scaled = -2;
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // IdleRecycleTime.Reserved
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->idleRecycleTime.reserved = -2;
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // Image (lenth = 201)
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->image = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaa";
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // InitImage (lenth = 201)
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->initImage = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                         "aaaaaaaaaaaaaaaaaaaaa";
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // RuntimeClassName (lenth = 65)
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->runtimeClassName = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // PodPendingDurationThreshold
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    podPool->podPendingDurationThreshold = -1;
    EXPECT_FALSE(PoolManager::ValidatePodPoolCreateParams(podPool));
    // Success
    podPool = std::make_shared<PodPoolInfo>();
    PoolManager::TransToPoolInfoFromJson(POD_POOL_INFO_STR, podPool);
    EXPECT_TRUE(PoolManager::ValidatePodPoolCreateParams(podPool));
}
}