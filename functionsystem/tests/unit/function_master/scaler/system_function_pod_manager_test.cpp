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

#include <map>
#include <string>
#include "common/kube_client/kube_client.h"
#include <utils/string_utils.hpp>

#define private public
#include "function_master/scaler/system_function_pod_manager/system_function_pod_manager.h"

namespace functionsystem::scaler::test {

class SystemFunctionPodManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::unordered_map<std::string, std::string> labelSelector = {{ "reuse", "true"}};
        frontendManager_ = std::make_shared<SystemFunctionPodManager>("faasfrontend", labelSelector);
    }

    void TearDown() override
    {
    }

    std::shared_ptr<SystemFunctionPodManager> frontendManager_;
};

std::shared_ptr<V1Pod> GenPodForManager(const std::string &podName)
{
    auto pod1 = std::make_shared<V1Pod>();
    pod1->SetMetadata(std::make_shared<functionsystem::kube_client::api::V1ObjectMeta>());
    pod1->GetMetadata()->SetName(podName);
    pod1->GetMetadata()->SetLabels({ {"reuse", "true"}});
    pod1->GetMetadata()->SetAnnotations({});
    pod1->SetStatus(std::make_shared<functionsystem::kube_client::model::V1PodStatus>());
    pod1->GetStatus()->SetContainerStatuses({ std::make_shared<functionsystem::kube_client::model::V1ContainerStatus>() });
    pod1->GetStatus()->GetContainerStatuses().front()->SetContainerID("runtime-manager");
    pod1->GetStatus()->SetPhase("Running");
    pod1->SetSpec(std::make_shared<functionsystem::kube_client::model::V1PodSpec>());
    pod1->GetSpec()->SetNodeName("node001");
    return pod1;
}

TEST_F(SystemFunctionPodManagerTest, PodEvent)
{
    {
        // normal pod
        auto pod = GenPodForManager("function-agent-pod1");
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 1);
        frontendManager_->OnPodDelete(pod);
        auto pod1 = GenPodForManager("function-agent-pod2");
        pod1->GetSpec()->SetNodeName("node002");
        frontendManager_->OnPodDelete(pod1);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 0);
    }
    {
        // frontend pod
        auto pod = GenPodForManager("function-agent-a-500m-2048mi-faasfrontend-1");
        pod->GetMetadata()->SetLabels({});
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2SystemFuncPodNames_.size(), 1);
        frontendManager_->OnPodDelete(pod);
        auto pod1 = GenPodForManager("function-agent-a-500m-2048mi-faasfrontend-2");
        pod1->GetSpec()->SetNodeName("node002");
        frontendManager_->OnPodDelete(pod1);
        EXPECT_EQ(frontendManager_->nodeID2SystemFuncPodNames_.size(), 0);
    }
    {
        auto pod = GenPodForManager("function-agent-pod1");
        // pod not ready
        pod->UnsetStatus();
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 0);
        // pod is not match
        pod = GenPodForManager("function-agent-pod1");
        pod->GetMetadata()->SetLabels({ {"reuse", "false"}});
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 0);
        pod->GetMetadata()->SetLabels({});
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 0);
    }
    {
        // pod terminating event
        auto pod = GenPodForManager("function-agent-pod1");
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 1);
        pod->GetStatus()->GetContainerStatuses().front()->SetState(std::make_shared<functionsystem::kube_client::model::V1ContainerState>());
        pod->GetStatus()->GetContainerStatuses().front()->GetState()->SetTerminated(std::make_shared<functionsystem::kube_client::model::V1ContainerStateTerminated>());
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 0);
    }
    {
        auto pod = GenPodForManager("function-agent-pod1");
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 1);
        pod->GetStatus()->SetPhase("Failed");
        pod->GetStatus()->SetContainerStatuses({});
        pod->GetStatus()->UnsetContainerStatuses();
        frontendManager_->OnPodUpdate(pod);
        EXPECT_EQ(frontendManager_->nodeID2PodNames_.size(), 0);
    }
}

TEST_F(SystemFunctionPodManagerTest, CheckSystemFunctionNeedScale)
{
    frontendManager_->CheckSystemFunctionNeedScale();
    auto  scaleDownCounter = std::make_shared<std::atomic<int>>(0);
    auto  scaleUpCounter = std::make_shared<std::atomic<int>>(0);
    frontendManager_->RegisterScaleUpHandler([cnt(scaleUpCounter)](const std::string &podName, uint32_t instanceNumber){
        cnt->fetch_add(instanceNumber);
    });
    frontendManager_->RegisterScaleDownHandler([cnt(scaleDownCounter)](const std::string &podName){
        (*cnt)++;
    });
    frontendManager_->CheckSystemFunctionNeedScale();
    EXPECT_EQ(*scaleUpCounter, 0);
    frontendManager_->nodeID2PodNames_["node001"] = {"function-agent-pool1"};
    frontendManager_->nodeID2SystemFuncPodNames_["node001"] = {"function-agent-a-500m-2048mi-faasfrontend-1"};
    frontendManager_->nodeID2SystemFuncPodNames_["node002"] = {"function-agent-a-500m-2048mi-faasfrontend-2"};
    frontendManager_->CheckSystemFunctionNeedScale();
    EXPECT_EQ(*scaleDownCounter, 1);
    frontendManager_->nodeID2PodNames_.clear();
    frontendManager_->nodeID2SystemFuncPodNames_.clear();
    auto pod1 = GenPodForManager("function-agent-pod1");
    auto pod2 = GenPodForManager("function-agent-pod2");
    frontendManager_->OnPodUpdate(pod1);
    pod2->GetSpec()->SetNodeName("node002");
    frontendManager_->OnPodUpdate(pod2);
    frontendManager_->CheckSystemFunctionNeedScale();
    EXPECT_EQ(*scaleUpCounter, 3);
}
}