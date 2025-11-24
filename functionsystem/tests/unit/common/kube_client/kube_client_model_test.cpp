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

#include "common/kube_client/model/common/v1_capabilities.h"
#include "common/kube_client/model/common/v1_delete_options.h"
#include "common/kube_client/model/common/v1_env_var.h"
#include "common/kube_client/model/common/v1_env_var_source.h"
#include "common/kube_client/model/common/v1_exec_action.h"
#include "common/kube_client/model/common/v1_host_alias.h"
#include "common/kube_client/model/common/v1_key_to_path.h"
#include "common/kube_client/model/common/v1_label_selector.h"
#include "common/kube_client/model/common/v1_label_selector_requirement.h"
#include "common/kube_client/model/common/v1_lifecycle.h"
#include "common/kube_client/model/common/v1_lifecycle_handler.h"
#include "common/kube_client/model/common/v1_list_meta.h"
#include "common/kube_client/model/common/v1_local_object_reference.h"
#include "common/kube_client/model/common/v1_object_field_selector.h"
#include "common/kube_client/model/common/v1_object_meta.h"
#include "common/kube_client/model/common/v1_owner_reference.h"
#include "common/kube_client/model/common/v1_resource_field_selector.h"
#include "common/kube_client/model/common/v1_resource_requirements.h"
#include "common/kube_client/model/common/v1_security_context.h"
#include "common/kube_client/model/container/v1_container.h"
#include "common/kube_client/model/container/v1_container_port.h"
#include "common/kube_client/model/container/v1_container_state.h"
#include "common/kube_client/model/container/v1_container_status.h"
#include "common/kube_client/model/container/v1_probe.h"
#include "common/kube_client/model/container/v1_tcp_socket_action.h"
#include "common/kube_client/model/deployment/v1_deployment.h"
#include "common/kube_client/model/deployment/v1_deployment_list.h"
#include "common/kube_client/model/deployment/v1_deployment_spec.h"
#include "common/kube_client/model/lease/v1_lease.h"
#include "common/kube_client/model/lease/v1_lease_spec.h"
#include "common/kube_client/model/node/v1_node.h"
#include "common/kube_client/model/node/v1_node_address.h"
#include "common/kube_client/model/node/v1_node_list.h"
#include "common/kube_client/model/node/v1_node_spec.h"
#include "common/kube_client/model/node/v1_node_status.h"
#include "common/kube_client/model/node/v1_taint.h"
#include "common/kube_client/model/pod/v1_affinity.h"
#include "common/kube_client/model/pod/v1_pod.h"
#include "common/kube_client/model/pod/v1_pod_affinity.h"
#include "common/kube_client/model/pod/v1_pod_affinity_term.h"
#include "common/kube_client/model/pod/v1_pod_anti_affinity.h"
#include "common/kube_client/model/pod/v1_pod_list.h"
#include "common/kube_client/model/pod/v1_pod_security_context.h"
#include "common/kube_client/model/pod/v1_pod_spec.h"
#include "common/kube_client/model/pod/v1_pod_status.h"
#include "common/kube_client/model/pod/v1_pod_template_spec.h"
#include "common/kube_client/model/pod/v1_weighted_pod_affinity_term.h"
#include "common/kube_client/model/volume/v1_host_path_volume_source.h"
#include "common/kube_client/model/volume/v1_empty_dir_volume_source.h"
#include "common/kube_client/model/volume/v1_secret_volume_source.h"
#include "common/kube_client/model/volume/v1_volume.h"
#include "common/kube_client/model/volume/v1_volume_mount.h"

namespace functionsystem::kube_client::test {

using namespace functionsystem::kube_client::model;

class KubeClientModelTest : public ::testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(KubeClientModelTest, V1CapabilitiesTest)
{
    std::shared_ptr<V1Capabilities> V1Capabilities_ = std::make_shared<V1Capabilities>();
    std::vector<std::string> Add;
    V1Capabilities_->SetAdd(Add);
    std::vector<std::string> Drop;
    V1Capabilities_->SetDrop(Drop);
    auto json = V1Capabilities_->ToJson();
    auto result = V1Capabilities_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Capabilities_->AddIsSet());
    EXPECT_TRUE(V1Capabilities_->DropIsSet());
}

TEST_F(KubeClientModelTest, V1AffinityTest)
{
    std::shared_ptr<V1Affinity> V1Affinity_ = std::make_shared<V1Affinity>();
    V1Affinity_->UnSetPodAffinity();
    V1Affinity_->UnSetPodAntiAffinity();
    std::shared_ptr<V1PodAffinity> PodAffinity = std::make_shared<V1PodAffinity>();
    V1Affinity_->SetPodAffinity(PodAffinity);
    std::shared_ptr<V1PodAntiAffinity> PodAntiAffinity = std::make_shared<V1PodAntiAffinity>();
    V1Affinity_->SetPodAntiAffinity(PodAntiAffinity);
    std::shared_ptr<V1NodeAffinity> nodeAffinity = std::make_shared<V1NodeAffinity>();
    std::shared_ptr<V1NodeSelector> nodeSelector = std::make_shared<V1NodeSelector>();
    std::shared_ptr<V1NodeSelectorTerm> nodeSelectorTerm = std::make_shared<V1NodeSelectorTerm>();
    std::shared_ptr<V1NodeSelectorRequirement> nodeSelectorRequirement = std::make_shared<V1NodeSelectorRequirement>();
    nodeSelectorRequirement->SetKey("key1");
    nodeSelectorRequirement->SetROperator("In");
    nodeSelectorRequirement->SetValues({"val1"});
    std::vector<std::shared_ptr<V1NodeSelectorRequirement>> nodeSelectorRequirements{nodeSelectorRequirement};
    nodeSelectorTerm->SetMatchExpressions(nodeSelectorRequirements);
    std::vector<std::shared_ptr<V1NodeSelectorTerm>> nodeSelectorTerms{nodeSelectorTerm};
    nodeSelector->SetNodeSelectorTerms(nodeSelectorTerms);
    nodeAffinity->SetRequiredDuringSchedulingIgnoredDuringExecution(nodeSelector);
    V1Affinity_->SetNodeAffinity(nodeAffinity);
    auto json = V1Affinity_->ToJson();
    auto result = V1Affinity_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Affinity_->PodAffinityIsSet());
    EXPECT_TRUE(V1Affinity_->PodAntiAffinityIsSet());

    std::shared_ptr<V1PreferredSchedulingTerm> preferredSchedulingTerm = std::make_shared<V1PreferredSchedulingTerm>();
    preferredSchedulingTerm->SetPreference(nodeSelectorTerm);
    preferredSchedulingTerm->SetWeight(1);
    nodeAffinity->SetPreferredDuringSchedulingIgnoredDuringExecution({preferredSchedulingTerm});
    V1Affinity_->SetNodeAffinity(nodeAffinity);
    json = V1Affinity_->ToJson();
    result = V1Affinity_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Affinity_->NodeAffinityIsSet());
    EXPECT_TRUE(V1Affinity_->GetNodeAffinity()->PreferredDuringSchedulingIgnoredDuringExecutionIsSet());
    EXPECT_EQ(static_cast<long unsigned int>(1),
        V1Affinity_->GetNodeAffinity()->GetPreferredDuringSchedulingIgnoredDuringExecution().size());
    EXPECT_EQ(static_cast<int32_t>(1),
        V1Affinity_->GetNodeAffinity()->GetPreferredDuringSchedulingIgnoredDuringExecution().front()->GetWeight());
    EXPECT_TRUE(V1Affinity_->GetNodeAffinity()->GetPreferredDuringSchedulingIgnoredDuringExecution().front()->GetPreference()->MatchExpressionsIsSet());
    EXPECT_EQ(static_cast<long unsigned int>(1), V1Affinity_->GetNodeAffinity()->GetPreferredDuringSchedulingIgnoredDuringExecution().front()->GetPreference()->GetMatchExpressions().size());
    EXPECT_EQ("key1", V1Affinity_->GetNodeAffinity()->GetPreferredDuringSchedulingIgnoredDuringExecution().front()->GetPreference()->GetMatchExpressions().front()->GetKey());
}

TEST_F(KubeClientModelTest, V1ConfigMapVolumeSourceTest)
{
    std::shared_ptr<V1ConfigMapVolumeSource> V1ConfigMapVolumeSource_ = std::make_shared<V1ConfigMapVolumeSource>();
    V1ConfigMapVolumeSource_->UnsetOptional();
    V1ConfigMapVolumeSource_->UnsetDefaultMode();
    V1ConfigMapVolumeSource_->UnsetName();
    V1ConfigMapVolumeSource_->UnsetItems();
    V1ConfigMapVolumeSource_->SetDefaultMode(0);
    std::vector<std::shared_ptr<V1KeyToPath>> Items;
    V1ConfigMapVolumeSource_->SetItems(Items);
    V1ConfigMapVolumeSource_->SetName("0");
    V1ConfigMapVolumeSource_->SetOptional(0);
    auto json = V1ConfigMapVolumeSource_->ToJson();
    auto result = V1ConfigMapVolumeSource_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ConfigMapVolumeSource_->DefaultModeIsSet());
    EXPECT_EQ(0, V1ConfigMapVolumeSource_->GetDefaultMode());
    EXPECT_TRUE(V1ConfigMapVolumeSource_->ItemsIsSet());
    EXPECT_TRUE(V1ConfigMapVolumeSource_->NameIsSet());
    EXPECT_EQ("0", V1ConfigMapVolumeSource_->GetName());
    EXPECT_TRUE(V1ConfigMapVolumeSource_->OptionalIsSet());
    EXPECT_EQ(0, V1ConfigMapVolumeSource_->IsOptional());
}

TEST_F(KubeClientModelTest, V1ContainerPortTest)
{
    std::shared_ptr<V1ContainerPort> V1ContainerPort_ = std::make_shared<V1ContainerPort>();
    V1ContainerPort_->UnsetName();
    V1ContainerPort_->UnsetProtocol();
    V1ContainerPort_->UnsetHostPort();
    V1ContainerPort_->UnsetContainerPort();
    V1ContainerPort_->UnsetHostIP();
    V1ContainerPort_->SetContainerPort(0);
    V1ContainerPort_->SetHostIP("0");
    V1ContainerPort_->SetHostPort(0);
    V1ContainerPort_->SetName("0");
    V1ContainerPort_->SetProtocol("0");
    auto json = V1ContainerPort_->ToJson();
    auto result = V1ContainerPort_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ContainerPort_->ContainerPortIsSet());
    EXPECT_EQ(0, V1ContainerPort_->GetContainerPort());
    EXPECT_TRUE(V1ContainerPort_->HostIPIsSet());
    EXPECT_EQ("0", V1ContainerPort_->GetHostIP());
    EXPECT_TRUE(V1ContainerPort_->HostPortIsSet());
    EXPECT_EQ(0, V1ContainerPort_->GetHostPort());
    EXPECT_TRUE(V1ContainerPort_->NameIsSet());
    EXPECT_EQ("0", V1ContainerPort_->GetName());
    EXPECT_TRUE(V1ContainerPort_->ProtocolIsSet());
    EXPECT_EQ("0", V1ContainerPort_->GetProtocol());
}

TEST_F(KubeClientModelTest, V1ContainerStatusTest)
{
    std::shared_ptr<V1ContainerStatus> V1ContainerStatus_ = std::make_shared<V1ContainerStatus>();
    V1ContainerStatus_->UnsetState();
    V1ContainerStatus_->UnsetStarted();
    V1ContainerStatus_->UnsetRestartCount();
    V1ContainerStatus_->UnsetReady();
    V1ContainerStatus_->UnsetName();
    V1ContainerStatus_->UnsetLastState();
    V1ContainerStatus_->UnsetImageID();
    V1ContainerStatus_->UnsetImage();
    V1ContainerStatus_->UnsetContainerID();
    V1ContainerStatus_->SetContainerID("0");
    V1ContainerStatus_->SetImage("0");
    V1ContainerStatus_->SetImageID("0");
    std::shared_ptr<V1ContainerState> LastState = std::make_shared<V1ContainerState>();
    V1ContainerStatus_->SetLastState(LastState);
    V1ContainerStatus_->SetName("0");
    V1ContainerStatus_->SetReady(0);
    V1ContainerStatus_->SetRestartCount(0);
    V1ContainerStatus_->SetStarted(0);
    std::shared_ptr<V1ContainerState> State = std::make_shared<V1ContainerState>();;
    V1ContainerStatus_->SetState(State);
    auto json = V1ContainerStatus_->ToJson();
    auto result = V1ContainerStatus_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ContainerStatus_->ContainerIDIsSet());
    EXPECT_EQ("0", V1ContainerStatus_->GetContainerID());
    EXPECT_TRUE(V1ContainerStatus_->ImageIsSet());
    EXPECT_EQ("0", V1ContainerStatus_->GetImage());
    EXPECT_TRUE(V1ContainerStatus_->ImageIDIsSet());
    EXPECT_EQ("0", V1ContainerStatus_->GetImageID());
    EXPECT_TRUE(V1ContainerStatus_->LastStateIsSet());
    EXPECT_TRUE(V1ContainerStatus_->NameIsSet());
    EXPECT_EQ("0", V1ContainerStatus_->GetName());
    EXPECT_TRUE(V1ContainerStatus_->ReadyIsSet());
    EXPECT_EQ(0, V1ContainerStatus_->IsReady());
    EXPECT_TRUE(V1ContainerStatus_->RestartCountIsSet());
    EXPECT_EQ(0, V1ContainerStatus_->GetRestartCount());
    EXPECT_TRUE(V1ContainerStatus_->StartedIsSet());
    EXPECT_EQ(0, V1ContainerStatus_->IsStarted());
    EXPECT_TRUE(V1ContainerStatus_->StateIsSet());
}

TEST_F(KubeClientModelTest, V1ContainerTest)
{
    std::shared_ptr<V1Container> V1Container_ = std::make_shared<V1Container>();
    V1Container_->UnsetWorkingDir();
    V1Container_->UnsetVolumeMounts();
    V1Container_->UnSetSecurityContext();
    V1Container_->UnsetResources();
    V1Container_->UnsetReadinessProbe();
    V1Container_->UnsetPorts();
    V1Container_->UnsetName();
    V1Container_->UnsetLivenessProbe();
    V1Container_->UnsetLifecycle();
    V1Container_->UnsetImage();
    V1Container_->UnsetEnv();
    V1Container_->UnsetEnvFrom();
    V1Container_->UnsetCommand();
    V1Container_->UnsetArgs();
    V1Container_->UnsetTerminationMessagePath();
    V1Container_->UnsetTerminationMessagePolicy();
    std::vector<std::string> Args;
    V1Container_->SetArgs(Args);
    std::vector<std::string> Command;
    V1Container_->SetCommand(Command);
    std::vector<std::shared_ptr<V1EnvVar>> Env;
    V1Container_->SetEnv(Env);
    std::vector<std::shared_ptr<V1EnvFromSource>> EnvFrom;
    V1Container_->SetEnvFrom(EnvFrom);
    V1Container_->SetImage("0");
    std::shared_ptr<V1Lifecycle> Lifecycle = std::make_shared<V1Lifecycle>();
    V1Container_->SetLifecycle(Lifecycle);
    std::shared_ptr<V1Probe> LivenessProbe;
    V1Container_->SetLivenessProbe(LivenessProbe);
    V1Container_->SetName("0");
    std::vector<std::shared_ptr<V1ContainerPort>> Ports;
    V1Container_->SetPorts(Ports);
    std::shared_ptr<V1Probe> ReadinessProbe;
    V1Container_->SetReadinessProbe(ReadinessProbe);
    std::shared_ptr<V1ResourceRequirements> Resources;
    V1Container_->SetResources(Resources);
    std::shared_ptr<V1SecurityContext> SecurityContext;
    V1Container_->SetSecurityContext(SecurityContext);
    std::shared_ptr<V1Probe> StartupProbe;
    std::vector<std::shared_ptr<V1VolumeMount>> VolumeMounts;
    V1Container_->SetVolumeMounts(VolumeMounts);
    V1Container_->SetWorkingDir("0");
    V1Container_->SetTerminationMessagePath("/var/tmp/log");
    V1Container_->SetTerminationMessagePolicy("FILE");
    auto json = V1Container_->ToJson();
    auto result = V1Container_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Container_->ArgsIsSet());
    EXPECT_TRUE(V1Container_->CommandIsSet());
    EXPECT_TRUE(V1Container_->EnvIsSet());
    EXPECT_TRUE(V1Container_->EnvFromIsSet());
    EXPECT_TRUE(V1Container_->ImageIsSet());
    EXPECT_EQ("0", V1Container_->GetImage());
    EXPECT_TRUE(V1Container_->LifecycleIsSet());
    EXPECT_TRUE(V1Container_->LivenessProbeIsSet());
    EXPECT_TRUE(V1Container_->NameIsSet());
    EXPECT_EQ("0", V1Container_->GetName());
    EXPECT_TRUE(V1Container_->PortsIsSet());
    EXPECT_TRUE(V1Container_->ReadinessProbeIsSet());
    EXPECT_TRUE(V1Container_->ResourcesIsSet());
    EXPECT_TRUE(V1Container_->SecurityContextIsSet());
    EXPECT_TRUE(V1Container_->VolumeMountsIsSet());
    EXPECT_TRUE(V1Container_->WorkingDirIsSet());
    EXPECT_EQ("0", V1Container_->GetWorkingDir());
}

TEST_F(KubeClientModelTest, V1DeleteOptionsTest)
{
    std::shared_ptr<V1DeleteOptions> V1DeleteOptions_ = std::make_shared<V1DeleteOptions>();
    V1DeleteOptions_->UnsetPropagationPolicy();
    V1DeleteOptions_->UnsetOrphanDependents();
    V1DeleteOptions_->UnsetKind();
    V1DeleteOptions_->UnsetGracePeriodSeconds();
    V1DeleteOptions_->UnsetDryRun();
    V1DeleteOptions_->UnsetApiVersion();
    V1DeleteOptions_->SetApiVersion("0");
    std::vector<std::string> DryRun;
    V1DeleteOptions_->SetDryRun(DryRun);
    V1DeleteOptions_->SetGracePeriodSeconds(0);
    V1DeleteOptions_->SetKind("0");
    V1DeleteOptions_->SetOrphanDependents(0);
    V1DeleteOptions_->SetPropagationPolicy("0");
    auto json = V1DeleteOptions_->ToJson();
    auto result = V1DeleteOptions_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1DeleteOptions_->ApiVersionIsSet());
    EXPECT_EQ("0", V1DeleteOptions_->GetApiVersion());
    EXPECT_TRUE(V1DeleteOptions_->DryRunIsSet());
    EXPECT_TRUE(V1DeleteOptions_->GracePeriodSecondsIsSet());
    EXPECT_EQ(0, V1DeleteOptions_->GetGracePeriodSeconds());
    EXPECT_TRUE(V1DeleteOptions_->KindIsSet());
    EXPECT_EQ("0", V1DeleteOptions_->GetKind());
    EXPECT_TRUE(V1DeleteOptions_->OrphanDependentsIsSet());
    EXPECT_EQ(0, V1DeleteOptions_->IsOrphanDependents());
    EXPECT_TRUE(V1DeleteOptions_->PropagationPolicyIsSet());
    EXPECT_EQ("0", V1DeleteOptions_->GetPropagationPolicy());
}

TEST_F(KubeClientModelTest, V1DeploymentListTest)
{
    std::shared_ptr<V1DeploymentList> V1DeploymentList_ = std::make_shared<V1DeploymentList>();
    V1DeploymentList_->UnsetApiVersion();
    V1DeploymentList_->UnsetItems();
    V1DeploymentList_->UnsetKind();
    V1DeploymentList_->UnsetMetadata();
    V1DeploymentList_->SetApiVersion("0");
    std::vector<std::shared_ptr<V1Deployment>> Items;
    V1DeploymentList_->SetItems(Items);
    V1DeploymentList_->SetKind("0");
    std::shared_ptr<V1ListMeta> Metadata = std::make_shared<V1ListMeta>();
    V1DeploymentList_->SetMetadata(Metadata);
    auto json = V1DeploymentList_->ToJson();
    auto result = V1DeploymentList_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1DeploymentList_->ApiVersionIsSet());
    EXPECT_EQ("0", V1DeploymentList_->GetApiVersion());
    EXPECT_TRUE(V1DeploymentList_->ItemsIsSet());
    EXPECT_TRUE(V1DeploymentList_->KindIsSet());
    EXPECT_EQ("0", V1DeploymentList_->GetKind());
    EXPECT_TRUE(V1DeploymentList_->MetadataIsSet());
}

TEST_F(KubeClientModelTest, V1DeploymentSpecTest)
{
    std::shared_ptr<V1DeploymentSpec> V1DeploymentSpec_ = std::make_shared<V1DeploymentSpec>();
    V1DeploymentSpec_->UnsetMinReadySeconds();
    V1DeploymentSpec_->UnsetPaused();
    V1DeploymentSpec_->UnsetReplicas();
    V1DeploymentSpec_->UnsetProgressDeadlineSeconds();
    V1DeploymentSpec_->UnSetSelector();
    V1DeploymentSpec_->UnsetRevisionHistoryLimit();
    V1DeploymentSpec_->UnsetMinReadySeconds();
    V1DeploymentSpec_->UnsetRTemplate();
    V1DeploymentSpec_->SetMinReadySeconds(0);
    V1DeploymentSpec_->SetPaused(0);
    V1DeploymentSpec_->SetProgressDeadlineSeconds(0);
    V1DeploymentSpec_->SetReplicas(0);
    V1DeploymentSpec_->SetRevisionHistoryLimit(0);
    std::shared_ptr<V1LabelSelector> Selector;
    V1DeploymentSpec_->SetSelector(Selector);
    std::shared_ptr<V1PodTemplateSpec> r_template;
    V1DeploymentSpec_->SetRTemplate(r_template);
    auto json = V1DeploymentSpec_->ToJson();
    auto result = V1DeploymentSpec_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1DeploymentSpec_->MinReadySecondsIsSet());
    EXPECT_EQ(0, V1DeploymentSpec_->GetMinReadySeconds());
    EXPECT_TRUE(V1DeploymentSpec_->PausedIsSet());
    EXPECT_EQ(0, V1DeploymentSpec_->IsPaused());
    EXPECT_TRUE(V1DeploymentSpec_->ProgressDeadlineSecondsIsSet());
    EXPECT_EQ(0, V1DeploymentSpec_->GetProgressDeadlineSeconds());
    EXPECT_TRUE(V1DeploymentSpec_->ReplicasIsSet());
    EXPECT_EQ(0, V1DeploymentSpec_->GetReplicas());
    EXPECT_TRUE(V1DeploymentSpec_->RevisionHistoryLimitIsSet());
    EXPECT_EQ(0, V1DeploymentSpec_->GetRevisionHistoryLimit());
    EXPECT_TRUE(V1DeploymentSpec_->SelectorIsSet());
    EXPECT_TRUE(V1DeploymentSpec_->RTemplateIsSet());
}

TEST_F(KubeClientModelTest, V1DeploymentTest)
{
    std::shared_ptr<V1Deployment> V1Deployment_ = std::make_shared<V1Deployment>();
    V1Deployment_->UnsetMetadata();
    V1Deployment_->UnsetKind();
    V1Deployment_->UnsetApiVersion();
    V1Deployment_->UnsetSpec();
    V1Deployment_->SetApiVersion("0");
    V1Deployment_->SetKind("0");
    std::shared_ptr<V1ObjectMeta> Metadata;
    V1Deployment_->SetMetadata(Metadata);
    std::shared_ptr<V1DeploymentSpec> Spec;
    V1Deployment_->SetSpec(Spec);
    std::shared_ptr<V1DeploymentStatus> deploymentStatus = std::make_shared<V1DeploymentStatus>();
    deploymentStatus->SetAvailableReplicas(1);
    deploymentStatus->SetReplicas(2);
    V1Deployment_->SetStatus(deploymentStatus);
    auto json = V1Deployment_->ToJson();
    auto result = V1Deployment_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Deployment_->ApiVersionIsSet());
    EXPECT_EQ("0", V1Deployment_->GetApiVersion());
    EXPECT_TRUE(V1Deployment_->KindIsSet());
    EXPECT_EQ("0", V1Deployment_->GetKind());
    EXPECT_TRUE(V1Deployment_->MetadataIsSet());
    EXPECT_TRUE(V1Deployment_->SpecIsSet());
    EXPECT_TRUE(V1Deployment_->SpecIsSet());
    EXPECT_EQ(1, V1Deployment_->GetStatus()->GetAvailableReplicas());
    EXPECT_EQ(2, V1Deployment_->GetStatus()->GetReplicas());
}

TEST_F(KubeClientModelTest, V1EmptyDirVolumeSourceTest)
{
    std::shared_ptr<V1EmptyDirVolumeSource> V1EmptyDirVolumeSource_ = std::make_shared<V1EmptyDirVolumeSource>();
    V1EmptyDirVolumeSource_->SetMedium("0");
    V1EmptyDirVolumeSource_->SetSizeLimit("0");
    auto json = V1EmptyDirVolumeSource_->ToJson();
    auto result = V1EmptyDirVolumeSource_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1EmptyDirVolumeSource_->MediumIsSet());
    EXPECT_EQ("0", V1EmptyDirVolumeSource_->GetMedium());
    EXPECT_TRUE(V1EmptyDirVolumeSource_->SizeLimitIsSet());
    EXPECT_EQ("0", V1EmptyDirVolumeSource_->GetSizeLimit());
}

TEST_F(KubeClientModelTest, V1EnvVarTest)
{
    std::shared_ptr<V1EnvVar> V1EnvVar_ = std::make_shared<V1EnvVar>();
    V1EnvVar_->SetName("0");
    V1EnvVar_->SetValue("0");
    std::shared_ptr<V1EnvVarSource> ValueFrom;
    V1EnvVar_->SetValueFrom(ValueFrom);
    auto json = V1EnvVar_->ToJson();
    auto result = V1EnvVar_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1EnvVar_->NameIsSet());
    EXPECT_EQ("0", V1EnvVar_->GetName());
    EXPECT_TRUE(V1EnvVar_->ValueIsSet());
    EXPECT_EQ("0", V1EnvVar_->GetValue());
    EXPECT_TRUE(V1EnvVar_->ValueFromIsSet());
}

TEST_F(KubeClientModelTest, V1EnvVarSourceTest)
{
    std::shared_ptr<V1EnvVarSource> V1EnvVarSource_ = std::make_shared<V1EnvVarSource>();
    std::shared_ptr<V1ObjectFieldSelector> FieldRef;
    V1EnvVarSource_->SetFieldRef(FieldRef);
    std::shared_ptr<V1ResourceFieldSelector> ResourceFieldRef;
    V1EnvVarSource_->SetResourceFieldRef(ResourceFieldRef);
    std::shared_ptr<V1SecretKeySelector> SecretKeyRef;
    V1EnvVarSource_->SetSecretKeyRef(SecretKeyRef);
    auto json = V1EnvVarSource_->ToJson();
    auto result = V1EnvVarSource_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1EnvVarSource_->FieldRefIsSet());
    EXPECT_TRUE(V1EnvVarSource_->ResourceFieldRefIsSet());
    EXPECT_TRUE(V1EnvVarSource_->SecretKeyRefIsSet());
}

TEST_F(KubeClientModelTest, V1EnvFromSourceTest)
{
    std::shared_ptr<V1EnvFromSource> V1EnvFromSource_ = std::make_shared<V1EnvFromSource>();
    V1EnvFromSource_->UnsetConfigMapRef();
    V1EnvFromSource_->UnsetPrefix();
    V1EnvFromSource_->UnsetSecretRef();
    std::shared_ptr<V1ConfigMapEnvSource> configMapEnvRef;
    V1EnvFromSource_->SetConfigMapRef(configMapEnvRef);
    std::shared_ptr<V1SecretEnvSource> secretEnvRef;
    V1EnvFromSource_->SetSecretRef(secretEnvRef);
    V1EnvFromSource_->SetPrefix("prefix");
    auto json = V1EnvFromSource_->ToJson();
    auto result = V1EnvFromSource_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1EnvFromSource_->SecretRefIsSet());
    EXPECT_TRUE(V1EnvFromSource_->ConfigMapRefIsSet());
    EXPECT_TRUE(V1EnvFromSource_->PrefixIsSet());
}

TEST_F(KubeClientModelTest, V1ConfigMapEnvSourceTest)
{
    std::shared_ptr<V1ConfigMapEnvSource> V1ConfigMapEnvSource_ = std::make_shared<V1ConfigMapEnvSource>();
    V1ConfigMapEnvSource_->UnsetName();
    V1ConfigMapEnvSource_->UnsetOptional();
    V1ConfigMapEnvSource_->SetName("name");
    V1ConfigMapEnvSource_->SetOptional(true);
    auto json = V1ConfigMapEnvSource_->ToJson();
    auto result = V1ConfigMapEnvSource_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ConfigMapEnvSource_->NameIsSet());
    EXPECT_TRUE(V1ConfigMapEnvSource_->OptionalIsSet());
}

TEST_F(KubeClientModelTest, V1SecretEnvSourceTest)
{
    std::shared_ptr<V1SecretEnvSource> V1SecretEnvSource_ = std::make_shared<V1SecretEnvSource>();
    V1SecretEnvSource_->UnsetName();
    V1SecretEnvSource_->UnsetOptional();
    V1SecretEnvSource_->SetName("name");
    V1SecretEnvSource_->SetOptional(true);
    auto json = V1SecretEnvSource_->ToJson();
    auto result = V1SecretEnvSource_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1SecretEnvSource_->NameIsSet());
    EXPECT_TRUE(V1SecretEnvSource_->OptionalIsSet());
}

TEST_F(KubeClientModelTest, V1ExecActionTest)
{
    std::shared_ptr<V1ExecAction> V1ExecAction_ = std::make_shared<V1ExecAction>();
    std::vector<std::string> Command;
    V1ExecAction_->SetCommand(Command);
    auto json = V1ExecAction_->ToJson();
    auto result = V1ExecAction_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ExecAction_->CommandIsSet());
}

TEST_F(KubeClientModelTest, V1HostAliasTest)
{
    std::shared_ptr<V1HostAlias> V1HostAlias_ = std::make_shared<V1HostAlias>();
    std::vector<std::string> Hostnames;
    V1HostAlias_->SetHostnames(Hostnames);
    V1HostAlias_->SetIp("0");
    auto json = V1HostAlias_->ToJson();
    auto result = V1HostAlias_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1HostAlias_->HostnamesIsSet());
    EXPECT_TRUE(V1HostAlias_->IpIsSet());
    EXPECT_EQ("0", V1HostAlias_->GetIp());
}

TEST_F(KubeClientModelTest, V1HostPathVolumeSourceTest)
{
    std::shared_ptr<V1HostPathVolumeSource> V1HostPathVolumeSource_ = std::make_shared<V1HostPathVolumeSource>();
    V1HostPathVolumeSource_->SetPath("0");
    V1HostPathVolumeSource_->SetType("0");
    auto json = V1HostPathVolumeSource_->ToJson();
    auto result = V1HostPathVolumeSource_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1HostPathVolumeSource_->PathIsSet());
    EXPECT_EQ("0", V1HostPathVolumeSource_->GetPath());
    EXPECT_TRUE(V1HostPathVolumeSource_->TypeIsSet());
    EXPECT_EQ("0", V1HostPathVolumeSource_->GetType());
}

TEST_F(KubeClientModelTest, V1KeyToPathTest)
{
    std::shared_ptr<V1KeyToPath> V1KeyToPath_ = std::make_shared<V1KeyToPath>();
    V1KeyToPath_->SetKey("0");
    V1KeyToPath_->SetMode(0);
    V1KeyToPath_->SetPath("0");
    auto json = V1KeyToPath_->ToJson();
    auto result = V1KeyToPath_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1KeyToPath_->KeyIsSet());
    EXPECT_EQ("0", V1KeyToPath_->GetKey());
    EXPECT_TRUE(V1KeyToPath_->ModeIsSet());
    EXPECT_EQ(0, V1KeyToPath_->GetMode());
    EXPECT_TRUE(V1KeyToPath_->PathIsSet());
    EXPECT_EQ("0", V1KeyToPath_->GetPath());
}

TEST_F(KubeClientModelTest, V1LabelSelectorRequirementTest)
{
    std::shared_ptr<V1LabelSelectorRequirement> V1LabelSelectorRequirement_ =
        std::make_shared<V1LabelSelectorRequirement>();
    V1LabelSelectorRequirement_->SetKey("0");
    V1LabelSelectorRequirement_->SetROperator("0");
    std::vector<std::string> Values;
    V1LabelSelectorRequirement_->SetValues(Values);
    auto json = V1LabelSelectorRequirement_->ToJson();
    auto result = V1LabelSelectorRequirement_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1LabelSelectorRequirement_->KeyIsSet());
    EXPECT_EQ("0", V1LabelSelectorRequirement_->GetKey());
    EXPECT_TRUE(V1LabelSelectorRequirement_->ROperatorIsSet());
    EXPECT_EQ("0", V1LabelSelectorRequirement_->GetROperator());
    EXPECT_TRUE(V1LabelSelectorRequirement_->ValuesIsSet());
}

TEST_F(KubeClientModelTest, V1LabelSelectorTest)
{
    std::shared_ptr<V1LabelSelector> V1LabelSelector_ = std::make_shared<V1LabelSelector>();
    std::vector<std::shared_ptr<V1LabelSelectorRequirement>> MatchExpressions;
    V1LabelSelector_->SetMatchExpressions(MatchExpressions);
    std::map<std::string, std::string> MatchLabels;
    V1LabelSelector_->SetMatchLabels(MatchLabels);
    auto json = V1LabelSelector_->ToJson();
    auto result = V1LabelSelector_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1LabelSelector_->MatchExpressionsIsSet());
    EXPECT_TRUE(V1LabelSelector_->MatchLabelsIsSet());
}

TEST_F(KubeClientModelTest, V1LifecycleHandlerTest)
{
    std::shared_ptr<V1LifecycleHandler> V1LifecycleHandler_ = std::make_shared<V1LifecycleHandler>();
    std::shared_ptr<V1ExecAction> Exec;
    V1LifecycleHandler_->SetExec(Exec);
    std::shared_ptr<V1TCPSocketAction> TcpSocket;
    auto json = V1LifecycleHandler_->ToJson();
    auto result = V1LifecycleHandler_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1LifecycleHandler_->ExecIsSet());
}

TEST_F(KubeClientModelTest, V1LifecycleTest)
{
    std::shared_ptr<V1Lifecycle> V1Lifecycle_ = std::make_shared<V1Lifecycle>();
    std::shared_ptr<V1LifecycleHandler> PostStart;
    V1Lifecycle_->SetPostStart(PostStart);
    std::shared_ptr<V1LifecycleHandler> PreStop;
    V1Lifecycle_->SetPreStop(PreStop);
    auto json = V1Lifecycle_->ToJson();
    auto result = V1Lifecycle_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Lifecycle_->PostStartIsSet());
    EXPECT_TRUE(V1Lifecycle_->PreStopIsSet());
}

TEST_F(KubeClientModelTest, V1ListMetaTest)
{
    std::shared_ptr<V1ListMeta> V1ListMeta_ = std::make_shared<V1ListMeta>();
    V1ListMeta_->SetRContinue("0");
    V1ListMeta_->SetRemainingItemCount(0);
    V1ListMeta_->SetResourceVersion("0");
    V1ListMeta_->SetSelfLink("0");
    auto json = V1ListMeta_->ToJson();
    auto result = V1ListMeta_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ListMeta_->RContinueIsSet());
    EXPECT_EQ("0", V1ListMeta_->GetRContinue());
    EXPECT_TRUE(V1ListMeta_->RemainingItemCountIsSet());
    EXPECT_EQ(0, V1ListMeta_->GetRemainingItemCount());
    EXPECT_TRUE(V1ListMeta_->ResourceVersionIsSet());
    EXPECT_EQ("0", V1ListMeta_->GetResourceVersion());
    EXPECT_TRUE(V1ListMeta_->SelfLinkIsSet());
    EXPECT_EQ("0", V1ListMeta_->GetSelfLink());
}

TEST_F(KubeClientModelTest, V1LocalObjectReferenceTest)
{
    std::shared_ptr<V1LocalObjectReference> V1LocalObjectReference_ = std::make_shared<V1LocalObjectReference>();
    V1LocalObjectReference_->SetName("0");
    auto json = V1LocalObjectReference_->ToJson();
    auto result = V1LocalObjectReference_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1LocalObjectReference_->NameIsSet());
    EXPECT_EQ("0", V1LocalObjectReference_->GetName());
}

TEST_F(KubeClientModelTest, V1NodeAddressTest)
{
    std::shared_ptr<V1NodeAddress> V1NodeAddress_ = std::make_shared<V1NodeAddress>();
    V1NodeAddress_->SetAddress("0");
    V1NodeAddress_->SetType("0");
    auto json = V1NodeAddress_->ToJson();
    auto result = V1NodeAddress_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1NodeAddress_->AddressIsSet());
    EXPECT_EQ("0", V1NodeAddress_->GetAddress());
    EXPECT_TRUE(V1NodeAddress_->TypeIsSet());
    EXPECT_EQ("0", V1NodeAddress_->GetType());
}

TEST_F(KubeClientModelTest, V1NodeListTest)
{
    std::shared_ptr<V1NodeList> V1NodeList_ = std::make_shared<V1NodeList>();
    V1NodeList_->UnsetApiVersion();
    V1NodeList_->UnsetKind();
    V1NodeList_->UnsetMetadata();
    V1NodeList_->UnsetItems();
    V1NodeList_->SetApiVersion("0");
    std::vector<std::shared_ptr<V1Node>> Items;
    V1NodeList_->SetItems(Items);
    V1NodeList_->SetKind("0");
    std::shared_ptr<V1ListMeta> Metadata = std::make_shared<V1ListMeta>();
    V1NodeList_->SetMetadata(Metadata);
    auto json = V1NodeList_->ToJson();
    auto result = V1NodeList_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1NodeList_->ApiVersionIsSet());
    EXPECT_EQ("0", V1NodeList_->GetApiVersion());
    EXPECT_TRUE(V1NodeList_->ItemsIsSet());
    EXPECT_TRUE(V1NodeList_->KindIsSet());
    EXPECT_EQ("0", V1NodeList_->GetKind());
    EXPECT_TRUE(V1NodeList_->MetadataIsSet());
}

TEST_F(KubeClientModelTest, V1NodeSpecTest)
{
    std::shared_ptr<V1NodeSpec> V1NodeSpec_ = std::make_shared<V1NodeSpec>();
    V1NodeSpec_->UnsetExternalID();
    V1NodeSpec_->UnSetPodCIDR();
    V1NodeSpec_->UnSetPodCIDRs();
    V1NodeSpec_->UnsetTaints();
    V1NodeSpec_->UnsetProviderID();
    V1NodeSpec_->UnsetUnschedulable();
    V1NodeSpec_->SetExternalID("0");
    V1NodeSpec_->SetPodCIDR("0");
    std::vector<std::string> PodCIDRs;
    V1NodeSpec_->SetPodCIDRs(PodCIDRs);
    V1NodeSpec_->SetProviderID("0");
    std::vector<std::shared_ptr<V1Taint>> Taints;
    V1NodeSpec_->SetTaints(Taints);
    V1NodeSpec_->SetUnschedulable(0);
    auto json = V1NodeSpec_->ToJson();
    auto result = V1NodeSpec_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1NodeSpec_->ExternalIDIsSet());
    EXPECT_EQ("0", V1NodeSpec_->GetExternalID());
    EXPECT_TRUE(V1NodeSpec_->PodCIDRIsSet());
    EXPECT_EQ("0", V1NodeSpec_->GetPodCIDR());
    EXPECT_TRUE(V1NodeSpec_->PodCIDRsIsSet());
    EXPECT_TRUE(V1NodeSpec_->ProviderIDIsSet());
    EXPECT_EQ("0", V1NodeSpec_->GetProviderID());
    EXPECT_TRUE(V1NodeSpec_->TaintsIsSet());
    EXPECT_TRUE(V1NodeSpec_->UnschedulableIsSet());
    EXPECT_EQ(0, V1NodeSpec_->IsUnschedulable());
}

TEST_F(KubeClientModelTest, V1NodeStatusTest)
{
    std::shared_ptr<V1NodeStatus> V1NodeStatus_ = std::make_shared<V1NodeStatus>();
    std::vector<std::shared_ptr<V1NodeAddress>> Addresses;
    V1NodeStatus_->SetAddresses(Addresses);
    auto json = V1NodeStatus_->ToJson();
    auto result = V1NodeStatus_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1NodeStatus_->AddressesIsSet());
}

TEST_F(KubeClientModelTest, V1NodeTest)
{
    std::shared_ptr<V1Node> V1Node_ = std::make_shared<V1Node>();
    V1Node_->UnsetKind();
    V1Node_->UnsetApiVersion();
    V1Node_->UnsetKind();
    V1Node_->UnsetMetadata();
    V1Node_->UnsetSpec();
    V1Node_->UnsetStatus();
    V1Node_->SetApiVersion("0");
    V1Node_->SetKind("0");
    std::shared_ptr<V1ObjectMeta> Metadata;
    V1Node_->SetMetadata(Metadata);
    std::shared_ptr<V1NodeSpec> Spec;
    V1Node_->SetSpec(Spec);
    std::shared_ptr<V1NodeStatus> Status;
    V1Node_->SetStatus(Status);
    auto json = V1Node_->ToJson();
    auto result = V1Node_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Node_->ApiVersionIsSet());
    EXPECT_EQ("0", V1Node_->GetApiVersion());
    EXPECT_TRUE(V1Node_->KindIsSet());
    EXPECT_EQ("0", V1Node_->GetKind());
    EXPECT_TRUE(V1Node_->MetadataIsSet());
    EXPECT_TRUE(V1Node_->SpecIsSet());
    EXPECT_TRUE(V1Node_->StatusIsSet());
}

TEST_F(KubeClientModelTest, V1ObjectFieldSelectorTest)
{
    std::shared_ptr<V1ObjectFieldSelector> V1ObjectFieldSelector_ = std::make_shared<V1ObjectFieldSelector>();
    V1ObjectFieldSelector_->SetApiVersion("0");
    V1ObjectFieldSelector_->SetFieldPath("0");
    auto json = V1ObjectFieldSelector_->ToJson();
    auto result = V1ObjectFieldSelector_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ObjectFieldSelector_->ApiVersionIsSet());
    EXPECT_EQ("0", V1ObjectFieldSelector_->GetApiVersion());
    EXPECT_TRUE(V1ObjectFieldSelector_->FieldPathIsSet());
    EXPECT_EQ("0", V1ObjectFieldSelector_->GetFieldPath());
}

TEST_F(KubeClientModelTest, V1ObjectMetaTest)
{
    std::shared_ptr<V1ObjectMeta> V1ObjectMeta_ = std::make_shared<V1ObjectMeta>();
    V1ObjectMeta_->UnsetOwnerReferences();
    V1ObjectMeta_->UnsetRNamespace();
    V1ObjectMeta_->UnsetName();
    V1ObjectMeta_->UnsetLabels();
    V1ObjectMeta_->UnsetGenerateName();
    V1ObjectMeta_->UnsetFinalizers();
    V1ObjectMeta_->UnsetAnnotations();
    V1ObjectMeta_->UnsetDeletionTimestamp();
    std::map<std::string, std::string> Annotations;
    Annotations["test"] = "abc";
    V1ObjectMeta_->SetAnnotations(Annotations);
    V1ObjectMeta_->SetCreationTimestamp(utility::Datetime());
    V1ObjectMeta_->SetDeletionTimestamp(utility::Datetime());
    std::vector<std::string> Finalizers;
    V1ObjectMeta_->SetFinalizers(Finalizers);
    V1ObjectMeta_->SetGenerateName("0");
    V1ObjectMeta_->SetGeneration(0);
    std::map<std::string, std::string> Labels;
    V1ObjectMeta_->SetLabels(Labels);
    V1ObjectMeta_->SetName("0");
    V1ObjectMeta_->SetRNamespace("0");
    std::vector<std::shared_ptr<V1OwnerReference>> OwnerReferences;
    V1ObjectMeta_->SetOwnerReferences(OwnerReferences);
    V1ObjectMeta_->SetResourceVersion("0");
    V1ObjectMeta_->SetSelfLink("0");
    V1ObjectMeta_->SetUid("0");
    auto json = V1ObjectMeta_->ToJson();
    auto result = V1ObjectMeta_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ObjectMeta_->AnnotationsIsSet());
    EXPECT_EQ("abc", V1ObjectMeta_->GetAnnotations()["test"]);
    EXPECT_TRUE(V1ObjectMeta_->CreationTimestampIsSet());
    EXPECT_TRUE(V1ObjectMeta_->DeletionTimestampIsSet());
    EXPECT_TRUE(V1ObjectMeta_->FinalizersIsSet());
    EXPECT_EQ(static_cast<uint32_t>(0), V1ObjectMeta_->GetFinalizers().size());
    EXPECT_TRUE(V1ObjectMeta_->GenerateNameIsSet());
    EXPECT_EQ("0", V1ObjectMeta_->GetGenerateName());
    EXPECT_TRUE(V1ObjectMeta_->GenerationIsSet());
    EXPECT_EQ(0, V1ObjectMeta_->GetGeneration());
    EXPECT_TRUE(V1ObjectMeta_->LabelsIsSet());
    EXPECT_TRUE(V1ObjectMeta_->NameIsSet());
    EXPECT_EQ("0", V1ObjectMeta_->GetName());
    EXPECT_TRUE(V1ObjectMeta_->RNamespaceIsSet());
    EXPECT_EQ("0", V1ObjectMeta_->GetRNamespace());
    EXPECT_TRUE(V1ObjectMeta_->OwnerReferencesIsSet());
    EXPECT_TRUE(V1ObjectMeta_->ResourceVersionIsSet());
    EXPECT_EQ("0", V1ObjectMeta_->GetResourceVersion());
    EXPECT_TRUE(V1ObjectMeta_->SelfLinkIsSet());
    EXPECT_EQ("0", V1ObjectMeta_->GetSelfLink());
    EXPECT_TRUE(V1ObjectMeta_->UidIsSet());
    EXPECT_EQ("0", V1ObjectMeta_->GetUid());
}

TEST_F(KubeClientModelTest, V1OwnerReferenceTest)
{
    std::shared_ptr<V1OwnerReference> V1OwnerReference_ = std::make_shared<V1OwnerReference>();
    V1OwnerReference_->UnsetApiVersion();
    V1OwnerReference_->UnsetKind();
    V1OwnerReference_->UnsetName();
    V1OwnerReference_->UnsetUid();
    V1OwnerReference_->UnsetController();
    V1OwnerReference_->UnsetBlockOwnerDeletion();
    V1OwnerReference_->SetApiVersion("0");
    V1OwnerReference_->SetBlockOwnerDeletion(0);
    V1OwnerReference_->SetController(0);
    V1OwnerReference_->SetKind("0");
    V1OwnerReference_->SetName("0");
    V1OwnerReference_->SetUid("0");
    auto json = V1OwnerReference_->ToJson();
    auto result = V1OwnerReference_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1OwnerReference_->ApiVersionIsSet());
    EXPECT_EQ("0", V1OwnerReference_->GetApiVersion());
    EXPECT_TRUE(V1OwnerReference_->BlockOwnerDeletionIsSet());
    EXPECT_EQ(0, V1OwnerReference_->IsBlockOwnerDeletion());
    EXPECT_TRUE(V1OwnerReference_->ControllerIsSet());
    EXPECT_EQ(0, V1OwnerReference_->IsController());
    EXPECT_TRUE(V1OwnerReference_->KindIsSet());
    EXPECT_EQ("0", V1OwnerReference_->GetKind());
    EXPECT_TRUE(V1OwnerReference_->NameIsSet());
    EXPECT_EQ("0", V1OwnerReference_->GetName());
    EXPECT_TRUE(V1OwnerReference_->UidIsSet());
    EXPECT_EQ("0", V1OwnerReference_->GetUid());
}

TEST_F(KubeClientModelTest, V1PodAffinityTermTest)
{
    std::shared_ptr<V1PodAffinityTerm> V1PodAffinityTerm_ = std::make_shared<V1PodAffinityTerm>();
    V1PodAffinityTerm_->UnsetLabelSelector();
    V1PodAffinityTerm_->UnsetNamespaceSelector();
    V1PodAffinityTerm_->UnsetNamespaces();
    V1PodAffinityTerm_->UnsetTopologyKey();
    std::shared_ptr<V1LabelSelector> LabelSelector = std::make_shared<V1LabelSelector>();
    V1PodAffinityTerm_->SetLabelSelector(LabelSelector);
    std::shared_ptr<V1LabelSelector> NamespaceSelector = std::make_shared<V1LabelSelector>();
    V1PodAffinityTerm_->SetNamespaceSelector(NamespaceSelector);
    std::vector<std::string> Namespaces;
    V1PodAffinityTerm_->SetNamespaces(Namespaces);
    V1PodAffinityTerm_->SetTopologyKey("0");
    auto json = V1PodAffinityTerm_->ToJson();
    auto result = V1PodAffinityTerm_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1PodAffinityTerm_->LabelSelectorIsSet());
    EXPECT_TRUE(V1PodAffinityTerm_->NamespaceSelectorIsSet());
    EXPECT_TRUE(V1PodAffinityTerm_->NamespacesIsSet());
    EXPECT_TRUE(V1PodAffinityTerm_->TopologyKeyIsSet());
    EXPECT_EQ("0", V1PodAffinityTerm_->GetTopologyKey());
}

TEST_F(KubeClientModelTest, V1PodAffinityTest)
{
    std::shared_ptr<V1PodAffinity> V1PodAffinity_ = std::make_shared<V1PodAffinity>();
    std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>> PreferredDuringSchedulingIgnoredDuringExecution;
    V1PodAffinity_->SetPreferredDuringSchedulingIgnoredDuringExecution(PreferredDuringSchedulingIgnoredDuringExecution);
    std::vector<std::shared_ptr<V1PodAffinityTerm>> RequiredDuringSchedulingIgnoredDuringExecution;
    V1PodAffinity_->SetRequiredDuringSchedulingIgnoredDuringExecution(RequiredDuringSchedulingIgnoredDuringExecution);
    auto json = V1PodAffinity_->ToJson();
    auto result = V1PodAffinity_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1PodAffinity_->PreferredDuringSchedulingIgnoredDuringExecutionIsSet());
    EXPECT_TRUE(V1PodAffinity_->RequiredDuringSchedulingIgnoredDuringExecutionIsSet());
}

TEST_F(KubeClientModelTest, V1PodAntiAffinityTest)
{
    std::shared_ptr<V1PodAntiAffinity> V1PodAntiAffinity_ = std::make_shared<V1PodAntiAffinity>();
    std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>> PreferredDuringSchedulingIgnoredDuringExecution;
    V1PodAntiAffinity_->SetPreferredDuringSchedulingIgnoredDuringExecution(
        PreferredDuringSchedulingIgnoredDuringExecution);
    std::vector<std::shared_ptr<V1PodAffinityTerm>> RequiredDuringSchedulingIgnoredDuringExecution;
    V1PodAntiAffinity_->SetRequiredDuringSchedulingIgnoredDuringExecution(
        RequiredDuringSchedulingIgnoredDuringExecution);
    auto json = V1PodAntiAffinity_->ToJson();
    auto result = V1PodAntiAffinity_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1PodAntiAffinity_->PreferredDuringSchedulingIgnoredDuringExecutionIsSet());
    EXPECT_TRUE(V1PodAntiAffinity_->RequiredDuringSchedulingIgnoredDuringExecutionIsSet());
}

TEST_F(KubeClientModelTest, V1PodListTest)
{
    std::shared_ptr<V1PodList> V1PodList_ = std::make_shared<V1PodList>();
    V1PodList_->SetApiVersion("0");
    std::vector<std::shared_ptr<V1Pod>> Items;
    V1PodList_->SetItems(Items);
    V1PodList_->SetKind("0");
    std::shared_ptr<V1ListMeta> Metadata;
    V1PodList_->SetMetadata(Metadata);
    auto json = V1PodList_->ToJson();
    auto result = V1PodList_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1PodList_->ApiVersionIsSet());
    EXPECT_EQ("0", V1PodList_->GetApiVersion());
    EXPECT_TRUE(V1PodList_->ItemsIsSet());
    EXPECT_TRUE(V1PodList_->KindIsSet());
    EXPECT_EQ("0", V1PodList_->GetKind());
    EXPECT_TRUE(V1PodList_->MetadataIsSet());
}

TEST_F(KubeClientModelTest, V1PodSecurityContextTest)
{
    std::shared_ptr<V1PodSecurityContext> V1PodSecurityContext_ = std::make_shared<V1PodSecurityContext>();
    V1PodSecurityContext_->SetFsGroup(0);
    V1PodSecurityContext_->SetFsGroupChangePolicy("0");
    V1PodSecurityContext_->SetRunAsGroup(0);
    V1PodSecurityContext_->SetRunAsNonRoot(0);
    V1PodSecurityContext_->SetRunAsUser(0);
    std::shared_ptr<V1SeccompProfile> seccompProfile =  std::make_shared<V1SeccompProfile>();
    seccompProfile->SetType("RuntimeDefault");
    seccompProfile->SetLocalhostProfile("test");
    V1PodSecurityContext_->SetSeccompProfile(seccompProfile);
    std::vector<int64_t> groupIDs{ 1000, 1002 };
    V1PodSecurityContext_->SetSupplementalGroups(groupIDs);
    auto json = V1PodSecurityContext_->ToJson();
    auto result = V1PodSecurityContext_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1PodSecurityContext_->FsGroupIsSet());
    EXPECT_EQ(0, V1PodSecurityContext_->GetFsGroup());
    EXPECT_TRUE(V1PodSecurityContext_->FsGroupChangePolicyIsSet());
    EXPECT_EQ("0", V1PodSecurityContext_->GetFsGroupChangePolicy());
    EXPECT_TRUE(V1PodSecurityContext_->RunAsGroupIsSet());
    EXPECT_EQ(0, V1PodSecurityContext_->GetRunAsGroup());
    EXPECT_TRUE(V1PodSecurityContext_->RunAsNonRootIsSet());
    EXPECT_EQ(0, V1PodSecurityContext_->IsRunAsNonRoot());
    EXPECT_TRUE(V1PodSecurityContext_->RunAsUserIsSet());
    EXPECT_EQ(0, V1PodSecurityContext_->GetRunAsUser());
    EXPECT_EQ(2, V1PodSecurityContext_->GetSupplementalGroups().size());
}

TEST_F(KubeClientModelTest, V1PodSpecTest)
{
    std::shared_ptr<V1PodSpec> V1PodSpec_ = std::make_shared<V1PodSpec>();
    V1PodSpec_->UnsetAffinity();
    V1PodSpec_->UnsetAutomountServiceAccountToken();
    V1PodSpec_->UnsetContainers();
    V1PodSpec_->UnsetDnsPolicy();
    V1PodSpec_->UnsetHostAliases();
    V1PodSpec_->UnsetHostNetwork();
    V1PodSpec_->UnsetImagePullSecrets();
    V1PodSpec_->UnsetInitContainers();
    V1PodSpec_->UnsetNodeSelector();
    V1PodSpec_->UnsetPriorityClassName();
    V1PodSpec_->UnsetRestartPolicy();
    V1PodSpec_->UnsetSchedulerName();
    V1PodSpec_->UnSetSecurityContext();
    V1PodSpec_->UnSetServiceAccountName();
    V1PodSpec_->UnsetTerminationGracePeriodSeconds();
    V1PodSpec_->UnsetVolumes();
    V1PodSpec_->UnsetHostPID();
    V1PodSpec_->UnsetTopologySpreadConstraints();
    std::shared_ptr<V1Affinity> Affinity = std::make_shared<V1Affinity>();
    V1PodSpec_->SetAffinity(Affinity);
    V1PodSpec_->SetAutomountServiceAccountToken(0);
    std::vector<std::shared_ptr<V1Container>> Containers;
    V1PodSpec_->SetContainers(Containers);
    V1PodSpec_->SetDnsPolicy("0");
    std::vector<std::shared_ptr<V1HostAlias>> HostAliases;
    V1PodSpec_->SetHostAliases(HostAliases);
    V1PodSpec_->SetHostNetwork(0);
    std::vector<std::shared_ptr<V1LocalObjectReference>> ImagePullSecrets;
    V1PodSpec_->SetImagePullSecrets(ImagePullSecrets);
    std::vector<std::shared_ptr<V1Container>> InitContainers;
    V1PodSpec_->SetInitContainers(InitContainers);
    std::map<std::string, std::string> NodeSelector;
    NodeSelector["node"] = "abc";
    V1PodSpec_->SetNodeSelector(NodeSelector);
    std::map<std::string, std::string> Overhead;
    V1PodSpec_->SetPriorityClassName("0");
    V1PodSpec_->SetRestartPolicy("0");
    V1PodSpec_->SetSchedulerName("0");
    std::shared_ptr<V1PodSecurityContext> SecurityContext;
    V1PodSpec_->SetSecurityContext(SecurityContext);
    V1PodSpec_->SetServiceAccountName("0");
    V1PodSpec_->SetTerminationGracePeriodSeconds(0);
    std::vector<std::shared_ptr<V1Volume>> Volumes;
    V1PodSpec_->SetVolumes(Volumes);
    V1PodSpec_->SetHostPID(true);
    auto V1TopologySpreadConstraint_ = std::make_shared<V1TopologySpreadConstraint>();
    V1TopologySpreadConstraint_->SetMaxSkew(1);
    V1TopologySpreadConstraint_->SetMinDomains(1);
    V1PodSpec_->SetTopologySpreadConstraints({V1TopologySpreadConstraint_});
    auto json = V1PodSpec_->ToJson();
    auto result = V1PodSpec_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1PodSpec_->AffinityIsSet());
    EXPECT_TRUE(V1PodSpec_->AutomountServiceAccountTokenIsSet());
    EXPECT_EQ(0, V1PodSpec_->IsAutomountServiceAccountToken());
    EXPECT_TRUE(V1PodSpec_->ContainersIsSet());
    EXPECT_TRUE(V1PodSpec_->DnsPolicyIsSet());
    EXPECT_EQ("0", V1PodSpec_->GetDnsPolicy());
    EXPECT_TRUE(V1PodSpec_->HostAliasesIsSet());
    EXPECT_TRUE(V1PodSpec_->HostNetworkIsSet());
    EXPECT_EQ(0, V1PodSpec_->IsHostNetwork());
    EXPECT_TRUE(V1PodSpec_->ImagePullSecretsIsSet());
    EXPECT_TRUE(V1PodSpec_->InitContainersIsSet());
    EXPECT_TRUE(V1PodSpec_->NodeSelectorIsSet());
    EXPECT_TRUE(V1PodSpec_->PriorityClassNameIsSet());
    EXPECT_EQ("0", V1PodSpec_->GetPriorityClassName());
    EXPECT_TRUE(V1PodSpec_->RestartPolicyIsSet());
    EXPECT_EQ("0", V1PodSpec_->GetRestartPolicy());
    EXPECT_TRUE(V1PodSpec_->SchedulerNameIsSet());
    EXPECT_EQ("0", V1PodSpec_->GetSchedulerName());
    EXPECT_TRUE(V1PodSpec_->SecurityContextIsSet());
    EXPECT_TRUE(V1PodSpec_->ServiceAccountNameIsSet());
    EXPECT_EQ("0", V1PodSpec_->GetServiceAccountName());
    EXPECT_TRUE(V1PodSpec_->TerminationGracePeriodSecondsIsSet());
    EXPECT_EQ(0, V1PodSpec_->GetTerminationGracePeriodSeconds());
    EXPECT_TRUE(V1PodSpec_->VolumesIsSet());
    EXPECT_TRUE(V1PodSpec_->TopologySpreadConstraintsIsSet());
}

TEST_F(KubeClientModelTest, V1PodStatusTest)
{
    std::shared_ptr<V1PodStatus> V1PodStatus_ = std::make_shared<V1PodStatus>();
    V1PodStatus_->UnsetContainerStatuses();
    V1PodStatus_->UnsetEphemeralContainerStatuses();
    V1PodStatus_->UnsetHostIP();
    V1PodStatus_->UnsetInitContainerStatuses();
    V1PodStatus_->UnsetMessage();
    V1PodStatus_->UnsetNominatedNodeName();
    V1PodStatus_->UnsetPhase();
    V1PodStatus_->UnSetPodIP();
    V1PodStatus_->UnsetQosClass();
    V1PodStatus_->UnsetReason();
    V1PodStatus_->UnsetStartTime();
    std::vector<std::shared_ptr<V1ContainerStatus>> ContainerStatuses;
    V1PodStatus_->SetContainerStatuses(ContainerStatuses);
    std::vector<std::shared_ptr<V1ContainerStatus>> EphemeralContainerStatuses;
    V1PodStatus_->SetEphemeralContainerStatuses(EphemeralContainerStatuses);
    V1PodStatus_->SetHostIP("0");
    std::vector<std::shared_ptr<V1ContainerStatus>> InitContainerStatuses;
    V1PodStatus_->SetInitContainerStatuses(InitContainerStatuses);
    V1PodStatus_->SetMessage("0");
    V1PodStatus_->SetNominatedNodeName("0");
    V1PodStatus_->SetPhase("0");
    V1PodStatus_->SetPodIP("0");
    V1PodStatus_->SetQosClass("0");
    V1PodStatus_->SetReason("0");
    V1PodStatus_->SetStartTime(utility::Datetime());
    auto json = V1PodStatus_->ToJson();
    auto result = V1PodStatus_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1PodStatus_->ContainerStatusesIsSet());
    EXPECT_TRUE(V1PodStatus_->EphemeralContainerStatusesIsSet());
    EXPECT_TRUE(V1PodStatus_->HostIPIsSet());
    EXPECT_EQ("0", V1PodStatus_->GetHostIP());
    EXPECT_TRUE(V1PodStatus_->InitContainerStatusesIsSet());
    EXPECT_TRUE(V1PodStatus_->MessageIsSet());
    EXPECT_EQ("0", V1PodStatus_->GetMessage());
    EXPECT_TRUE(V1PodStatus_->NominatedNodeNameIsSet());
    EXPECT_EQ("0", V1PodStatus_->GetNominatedNodeName());
    EXPECT_TRUE(V1PodStatus_->PhaseIsSet());
    EXPECT_EQ("0", V1PodStatus_->GetPhase());
    EXPECT_TRUE(V1PodStatus_->PodIPIsSet());
    EXPECT_EQ("0", V1PodStatus_->GetPodIP());
    EXPECT_TRUE(V1PodStatus_->QosClassIsSet());
    EXPECT_EQ("0", V1PodStatus_->GetQosClass());
    EXPECT_TRUE(V1PodStatus_->ReasonIsSet());
    EXPECT_EQ("0", V1PodStatus_->GetReason());
    EXPECT_TRUE(V1PodStatus_->StartTimeIsSet());
}

TEST_F(KubeClientModelTest, V1PodTemplateSpecTest)
{
    std::shared_ptr<V1PodTemplateSpec> V1PodTemplateSpec_ = std::make_shared<V1PodTemplateSpec>();
    std::shared_ptr<V1ObjectMeta> Metadata;
    V1PodTemplateSpec_->SetMetadata(Metadata);
    std::shared_ptr<V1PodSpec> Spec;
    V1PodTemplateSpec_->SetSpec(Spec);
    auto json = V1PodTemplateSpec_->ToJson();
    auto result = V1PodTemplateSpec_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1PodTemplateSpec_->MetadataIsSet());
    EXPECT_TRUE(V1PodTemplateSpec_->SpecIsSet());
}

TEST_F(KubeClientModelTest, V1PodTest)
{
    std::shared_ptr<V1Pod> V1Pod_ = std::make_shared<V1Pod>();
    V1Pod_->SetApiVersion("0");
    V1Pod_->SetKind("0");
    std::shared_ptr<V1ObjectMeta> Metadata;
    V1Pod_->SetMetadata(Metadata);
    std::shared_ptr<V1PodSpec> Spec;
    V1Pod_->SetSpec(Spec);
    std::shared_ptr<V1PodStatus> Status;
    V1Pod_->SetStatus(Status);
    auto json = V1Pod_->ToJson();
    auto result = V1Pod_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Pod_->ApiVersionIsSet());
    EXPECT_EQ("0", V1Pod_->GetApiVersion());
    EXPECT_TRUE(V1Pod_->KindIsSet());
    EXPECT_EQ("0", V1Pod_->GetKind());
    EXPECT_TRUE(V1Pod_->MetadataIsSet());
    EXPECT_TRUE(V1Pod_->SpecIsSet());
    EXPECT_TRUE(V1Pod_->StatusIsSet());
}

TEST_F(KubeClientModelTest, V1ProbeTest)
{
    std::shared_ptr<V1Probe> V1Probe_ = std::make_shared<V1Probe>();
    V1Probe_->UnsetFailureThreshold();
    V1Probe_->UnsetInitialDelaySeconds();
    V1Probe_->UnsetPeriodSeconds();
    V1Probe_->UnsetSuccessThreshold();
    V1Probe_->UnsetTcpSocket();
    V1Probe_->UnsetExec();
    V1Probe_->UnsetTerminationGracePeriodSeconds();
    V1Probe_->UnsetTimeoutSeconds();
    V1Probe_->SetFailureThreshold(0);
    V1Probe_->SetInitialDelaySeconds(0);
    V1Probe_->SetPeriodSeconds(0);
    V1Probe_->SetSuccessThreshold(0);
    std::shared_ptr<V1TCPSocketAction> TcpSocket;
    V1Probe_->SetTcpSocket(TcpSocket);
    std::shared_ptr<V1ExecAction> execAction;
    V1Probe_->SetExec(execAction);
    V1Probe_->SetTerminationGracePeriodSeconds(0);
    V1Probe_->SetTimeoutSeconds(0);
    auto json = V1Probe_->ToJson();
    auto result = V1Probe_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Probe_->FailureThresholdIsSet());
    EXPECT_EQ(0, V1Probe_->GetFailureThreshold());
    EXPECT_TRUE(V1Probe_->InitialDelaySecondsIsSet());
    EXPECT_EQ(0, V1Probe_->GetInitialDelaySeconds());
    EXPECT_TRUE(V1Probe_->PeriodSecondsIsSet());
    EXPECT_EQ(0, V1Probe_->GetPeriodSeconds());
    EXPECT_TRUE(V1Probe_->SuccessThresholdIsSet());
    EXPECT_EQ(0, V1Probe_->GetSuccessThreshold());
    EXPECT_TRUE(V1Probe_->TcpSocketIsSet());
    EXPECT_TRUE(V1Probe_->ExecIsSet());
    EXPECT_TRUE(V1Probe_->TerminationGracePeriodSecondsIsSet());
    EXPECT_EQ(0, V1Probe_->GetTerminationGracePeriodSeconds());
    EXPECT_TRUE(V1Probe_->TimeoutSecondsIsSet());
    EXPECT_EQ(0, V1Probe_->GetTimeoutSeconds());
}

TEST_F(KubeClientModelTest, V1ResourceFieldSelectorTest)
{
    std::shared_ptr<V1ResourceFieldSelector> V1ResourceFieldSelector_ = std::make_shared<V1ResourceFieldSelector>();
    V1ResourceFieldSelector_->SetContainerName("0");
    V1ResourceFieldSelector_->SetDivisor("0");
    V1ResourceFieldSelector_->SetResource("0");
    auto json = V1ResourceFieldSelector_->ToJson();
    auto result = V1ResourceFieldSelector_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ResourceFieldSelector_->ContainerNameIsSet());
    EXPECT_EQ("0", V1ResourceFieldSelector_->GetContainerName());
    EXPECT_TRUE(V1ResourceFieldSelector_->DivisorIsSet());
    EXPECT_EQ("0", V1ResourceFieldSelector_->GetDivisor());
    EXPECT_TRUE(V1ResourceFieldSelector_->ResourceIsSet());
    EXPECT_EQ("0", V1ResourceFieldSelector_->GetResource());
}

TEST_F(KubeClientModelTest, V1ResourceRequirementsTest)
{
    std::shared_ptr<V1ResourceRequirements> V1ResourceRequirements_ = std::make_shared<V1ResourceRequirements>();
    std::map<std::string, std::string> Limits;
    V1ResourceRequirements_->SetLimits(Limits);
    std::map<std::string, std::string> Requests;
    V1ResourceRequirements_->SetRequests(Requests);
    auto json = V1ResourceRequirements_->ToJson();
    auto result = V1ResourceRequirements_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ResourceRequirements_->LimitsIsSet());
    EXPECT_TRUE(V1ResourceRequirements_->RequestsIsSet());
}

TEST_F(KubeClientModelTest, V1SecretVolumeSourceTest)
{
    std::shared_ptr<V1SecretVolumeSource> V1SecretVolumeSource_ = std::make_shared<V1SecretVolumeSource>();
    V1SecretVolumeSource_->UnsetItems();
    V1SecretVolumeSource_->UnSetSecretName();
    V1SecretVolumeSource_->UnsetDefaultMode();
    V1SecretVolumeSource_->UnsetOptional();
    V1SecretVolumeSource_->SetDefaultMode(0);
    std::vector<std::shared_ptr<V1KeyToPath>> Items;
    V1SecretVolumeSource_->SetItems(Items);
    V1SecretVolumeSource_->SetOptional(0);
    V1SecretVolumeSource_->SetSecretName("0");
    auto json = V1SecretVolumeSource_->ToJson();
    auto result = V1SecretVolumeSource_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1SecretVolumeSource_->DefaultModeIsSet());
    EXPECT_EQ(0, V1SecretVolumeSource_->GetDefaultMode());
    EXPECT_TRUE(V1SecretVolumeSource_->ItemsIsSet());
    EXPECT_TRUE(V1SecretVolumeSource_->OptionalIsSet());
    EXPECT_EQ(0, V1SecretVolumeSource_->IsOptional());
    EXPECT_TRUE(V1SecretVolumeSource_->SecretNameIsSet());
    EXPECT_EQ("0", V1SecretVolumeSource_->GetSecretName());
}

TEST_F(KubeClientModelTest, V1SecurityContextTest)
{
    std::shared_ptr<V1SecurityContext> V1SecurityContext_ = std::make_shared<V1SecurityContext>();
    V1SecurityContext_->UnsetAllowPrivilegeEscalation();
    V1SecurityContext_->UnsetCapabilities();
    V1SecurityContext_->UnsetPrivileged();
    V1SecurityContext_->UnsetRunAsGroup();
    V1SecurityContext_->UnsetRunAsNonRoot();
    V1SecurityContext_->UnsetRunAsUser();
    V1SecurityContext_->UnSetSeccompProfile();
    V1SecurityContext_->SetAllowPrivilegeEscalation(0);
    std::shared_ptr<V1SeccompProfile> seccompProfile =  std::make_shared<V1SeccompProfile>();
    V1SecurityContext_->SetSeccompProfile(seccompProfile);
    std::shared_ptr<V1Capabilities> Capabilities;
    V1SecurityContext_->SetCapabilities(Capabilities);
    V1SecurityContext_->SetPrivileged(0);
    V1SecurityContext_->SetRunAsGroup(0);
    V1SecurityContext_->SetRunAsNonRoot(0);
    V1SecurityContext_->SetRunAsUser(0);
    auto json = V1SecurityContext_->ToJson();
    auto result = V1SecurityContext_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1SecurityContext_->AllowPrivilegeEscalationIsSet());
    EXPECT_EQ(0, V1SecurityContext_->IsAllowPrivilegeEscalation());
    EXPECT_TRUE(V1SecurityContext_->CapabilitiesIsSet());
    EXPECT_TRUE(V1SecurityContext_->PrivilegedIsSet());
    EXPECT_EQ(0, V1SecurityContext_->IsPrivileged());
    EXPECT_TRUE(V1SecurityContext_->RunAsGroupIsSet());
    EXPECT_EQ(0, V1SecurityContext_->GetRunAsGroup());
    EXPECT_TRUE(V1SecurityContext_->RunAsNonRootIsSet());
    EXPECT_EQ(0, V1SecurityContext_->IsRunAsNonRoot());
    EXPECT_TRUE(V1SecurityContext_->RunAsUserIsSet());
    EXPECT_EQ(0, V1SecurityContext_->GetRunAsUser());
}

TEST_F(KubeClientModelTest, V1TaintTest)
{
    std::shared_ptr<V1Taint> V1Taint_ = std::make_shared<V1Taint>();
    V1Taint_->UnSetEffect();
    V1Taint_->UnsetKey();
    V1Taint_->UnsetTimeAdded();
    V1Taint_->UnsetValue();
    V1Taint_->SetEffect("0");
    V1Taint_->SetKey("0");
    V1Taint_->SetTimeAdded(utility::Datetime());
    V1Taint_->SetValue("0");
    auto json = V1Taint_->ToJson();
    auto result = V1Taint_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Taint_->EffectIsSet());
    EXPECT_EQ("0", V1Taint_->GetEffect());
    EXPECT_TRUE(V1Taint_->KeyIsSet());
    EXPECT_EQ("0", V1Taint_->GetKey());
    EXPECT_TRUE(V1Taint_->TimeAddedIsSet());
    EXPECT_TRUE(V1Taint_->ValueIsSet());
    EXPECT_EQ("0", V1Taint_->GetValue());
}

TEST_F(KubeClientModelTest, V1TCPSocketActionTest)
{
    std::shared_ptr<V1TCPSocketAction> V1TCPSocketAction_ = std::make_shared<V1TCPSocketAction>();
    V1TCPSocketAction_->SetHost("0");
    V1TCPSocketAction_->SetPort(0);
    auto json = V1TCPSocketAction_->ToJson();
    auto result = V1TCPSocketAction_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1TCPSocketAction_->HostIsSet());
    EXPECT_EQ("0", V1TCPSocketAction_->GetHost());
    EXPECT_TRUE(V1TCPSocketAction_->PortIsSet());
    EXPECT_EQ(0, V1TCPSocketAction_->GetPort());
}

TEST_F(KubeClientModelTest, V1VolumeMountTest)
{
    std::shared_ptr<V1VolumeMount> V1VolumeMount_ = std::make_shared<V1VolumeMount>();
    V1VolumeMount_->UnsetName();
    V1VolumeMount_->UnsetMountPath();
    V1VolumeMount_->UnsetMountPropagation();
    V1VolumeMount_->UnsetReadOnly();
    V1VolumeMount_->UnsetSubPath();
    V1VolumeMount_->UnsetSubPathExpr();
    V1VolumeMount_->SetMountPath("0");
    V1VolumeMount_->SetMountPropagation("0");
    V1VolumeMount_->SetName("0");
    V1VolumeMount_->SetReadOnly(0);
    V1VolumeMount_->SetSubPath("0");
    V1VolumeMount_->SetSubPathExpr("0");
    auto json = V1VolumeMount_->ToJson();
    auto result = V1VolumeMount_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1VolumeMount_->MountPathIsSet());
    EXPECT_EQ("0", V1VolumeMount_->GetMountPath());
    EXPECT_TRUE(V1VolumeMount_->MountPropagationIsSet());
    EXPECT_EQ("0", V1VolumeMount_->GetMountPropagation());
    EXPECT_TRUE(V1VolumeMount_->NameIsSet());
    EXPECT_EQ("0", V1VolumeMount_->GetName());
    EXPECT_TRUE(V1VolumeMount_->ReadOnlyIsSet());
    EXPECT_EQ(0, V1VolumeMount_->IsReadOnly());
    EXPECT_TRUE(V1VolumeMount_->SubPathIsSet());
    EXPECT_EQ("0", V1VolumeMount_->GetSubPath());
    EXPECT_TRUE(V1VolumeMount_->SubPathExprIsSet());
    EXPECT_EQ("0", V1VolumeMount_->GetSubPathExpr());
}

TEST_F(KubeClientModelTest, V1VolumeTest)
{
    std::shared_ptr<V1Volume> V1Volume_ = std::make_shared<V1Volume>();
    V1Volume_->UnsetName();
    V1Volume_->UnsetConfigMap();
    V1Volume_->UnsetHostPath();
    V1Volume_->UnSetSecret();
    V1Volume_->UnsetEmptyDir();
    V1Volume_->UnsetProjected();
    V1Volume_->SetName("0");
    // configmap
    std::shared_ptr<V1ConfigMapVolumeSource> ConfigMap = std::make_shared<V1ConfigMapVolumeSource>();
    ConfigMap->SetDefaultMode(640);
    ConfigMap->SetName("config-map");
    ConfigMap->SetOptional(true);
    std::shared_ptr<V1KeyToPath> v1KeyToPath = std::make_shared<V1KeyToPath>();
    v1KeyToPath->SetMode(640);
    v1KeyToPath->SetPath("path");
    v1KeyToPath->SetKey("key1");
    std::vector<std::shared_ptr<V1KeyToPath>> v1KeyToPaths{v1KeyToPath};
    ConfigMap->SetItems(v1KeyToPaths);
    V1Volume_->SetConfigMap(ConfigMap);
    // empty dir
    std::shared_ptr<V1EmptyDirVolumeSource> EmptyDir = std::make_shared<V1EmptyDirVolumeSource>();
    EmptyDir->SetMedium("test");
    EmptyDir->SetSizeLimit("10Gi");
    V1Volume_->SetEmptyDir(EmptyDir);
    // host path
    std::shared_ptr<V1HostPathVolumeSource> HostPath = std::make_shared<V1HostPathVolumeSource>();
    HostPath->SetType("host");
    HostPath->SetPath("/home");
    V1Volume_->SetHostPath(HostPath);
    // secret
    std::shared_ptr<V1SecretVolumeSource> Secret = std::make_shared<V1SecretVolumeSource>();
    Secret->SetOptional(true);
    Secret->SetSecretName("secret-1");
    Secret->SetDefaultMode(500);
    Secret->SetItems(v1KeyToPaths);
    V1Volume_->SetSecret(Secret);
    // projected volume
    std::shared_ptr<V1ProjectedVolumeSource> projected = std::make_shared<V1ProjectedVolumeSource>();
    std::vector<std::shared_ptr<V1VolumeProjection>> projectionVolumes;
    std::shared_ptr<V1ObjectFieldSelector> objectFieldSelector = std::make_shared<V1ObjectFieldSelector>();
    objectFieldSelector->SetFieldPath("testPath");
    std::shared_ptr<V1DownwardAPIVolumeFile> v1DownwardApiVolumeFile = std::make_shared<V1DownwardAPIVolumeFile>();
    v1DownwardApiVolumeFile->SetFieldRef(objectFieldSelector);
    v1DownwardApiVolumeFile->SetPath("test-path");
    v1DownwardApiVolumeFile->SetMode(1);
    std::shared_ptr<V1ResourceFieldSelector> resourceFieldSelector = std::make_shared<V1ResourceFieldSelector>();
    resourceFieldSelector->SetContainerName("test");
    resourceFieldSelector->SetDivisor("divisor");
    resourceFieldSelector->SetResource("resource");
    v1DownwardApiVolumeFile->SetResourceFieldRef(resourceFieldSelector);
    std::shared_ptr<V1SecretKeySelector> secretKeySelector = std::make_shared<V1SecretKeySelector>();
    secretKeySelector->SetKey("test");
    secretKeySelector->SetName("divisor");
    v1DownwardApiVolumeFile->SetSecretKeyRef(secretKeySelector);
    std::vector<std::shared_ptr<V1DownwardAPIVolumeFile>> downwardAPIVolumeFiles;
    downwardAPIVolumeFiles.emplace_back(v1DownwardApiVolumeFile);
    std::shared_ptr<V1DownwardAPIProjection> downwardApiProjection = std::make_shared<V1DownwardAPIProjection>();
    downwardApiProjection->SetItems(downwardAPIVolumeFiles);
    std::shared_ptr<V1VolumeProjection> projectionVol = std::make_shared<V1VolumeProjection>();
    projectionVol->SetDownwardAPI(downwardApiProjection);
    projectionVolumes.emplace_back(projectionVol);
    std::shared_ptr<V1ProjectedVolumeSource> projectedVolumeSource = std::make_shared<V1ProjectedVolumeSource>();
    projectedVolumeSource->SetSources(projectionVolumes);
    V1Volume_->SetProjected(projectedVolumeSource);
    // download api
    std::shared_ptr<V1DownwardAPIVolumeSource> v1DownwardApiVolumeSource = std::make_shared<V1DownwardAPIVolumeSource>();
    v1DownwardApiVolumeFile->SetPath("down");
    v1DownwardApiVolumeFile->SetMode(500);
    v1DownwardApiVolumeFile->SetFieldRef(objectFieldSelector);
    v1DownwardApiVolumeFile->SetResourceFieldRef(resourceFieldSelector);
    v1DownwardApiVolumeFile->SetSecretKeyRef(secretKeySelector);
    V1Volume_->SetDownwardAPI(v1DownwardApiVolumeSource);
    // pvc
    std::shared_ptr<V1PersistentVolumeClaimVolumeSource> pvcVolume = std::make_shared<V1PersistentVolumeClaimVolumeSource>();
    pvcVolume->SetClaimName("pvc-1");
    pvcVolume->SetReadOnly(true);
    V1Volume_->SetPersistentVolumeClaim(pvcVolume);
    auto json = V1Volume_->ToJson();
    auto result = V1Volume_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Volume_->ConfigMapIsSet());
    EXPECT_TRUE(V1Volume_->EmptyDirIsSet());
    EXPECT_TRUE(V1Volume_->HostPathIsSet());
    EXPECT_TRUE(V1Volume_->NameIsSet());
    EXPECT_EQ("0", V1Volume_->GetName());
    EXPECT_TRUE(V1Volume_->SecretIsSet());
    EXPECT_TRUE(V1Volume_->ProjectedIsSet());
    EXPECT_TRUE(V1Volume_->DownwardAPIIsSet());
    EXPECT_TRUE(V1Volume_->PersistentVolumeClaimIsSet());
    EXPECT_EQ("pvc-1", V1Volume_->GetPersistentVolumeClaim()->GetClaimName());
}

TEST_F(KubeClientModelTest, V1WeightedPodAffinityTermTest)
{
    std::shared_ptr<V1WeightedPodAffinityTerm> V1WeightedPodAffinityTerm_ =
        std::make_shared<V1WeightedPodAffinityTerm>();
    std::shared_ptr<V1PodAffinityTerm> PodAffinityTerm;
    V1WeightedPodAffinityTerm_->SetPodAffinityTerm(PodAffinityTerm);
    V1WeightedPodAffinityTerm_->SetWeight(0);
    auto json = V1WeightedPodAffinityTerm_->ToJson();
    auto result = V1WeightedPodAffinityTerm_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1WeightedPodAffinityTerm_->PodAffinityTermIsSet());
    EXPECT_TRUE(V1WeightedPodAffinityTerm_->WeightIsSet());
    EXPECT_EQ(0, V1WeightedPodAffinityTerm_->GetWeight());
}

TEST_F(KubeClientModelTest, V1ContainerStateTest)
{
    std::shared_ptr<V1ContainerState> V1ContainerState_ = std::make_shared<V1ContainerState>();
    V1ContainerState_->UnsetWaiting();
    V1ContainerState_->UnsetTerminated();
    V1ContainerState_->UnsetRunning();
    std::shared_ptr<V1ContainerStateRunning> Running = std::make_shared<V1ContainerStateRunning>();
    Running->SetStartedAt(utility::Datetime());
    auto json = Running->ToJson();
    auto result = Running->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(Running->StartedAtIsSet());

    std::shared_ptr<V1ContainerStateTerminated> Terminated = std::make_shared<V1ContainerStateTerminated>();
    Terminated->SetContainerID("0");
    Terminated->SetExitCode(0);
    Terminated->SetFinishedAt(utility::Datetime());
    Terminated->SetStartedAt(utility::Datetime());
    json = Terminated->ToJson();
    result = Terminated->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(Terminated->ContainerIDIsSet());
    EXPECT_EQ("0", Terminated->GetContainerID());
    EXPECT_TRUE(Terminated->ExitCodeIsSet());
    EXPECT_EQ(0, Terminated->GetExitCode());
    EXPECT_TRUE(Terminated->FinishedAtIsSet());
    EXPECT_TRUE(Terminated->StartedAtIsSet());

    std::shared_ptr<V1ContainerStateWaiting> Waiting = std::make_shared<V1ContainerStateWaiting>();
    Waiting->SetMessage("0");
    Waiting->SetReason("0");
    json = Waiting->ToJson();
    result = Waiting->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(Waiting->MessageIsSet());
    EXPECT_EQ("0", Waiting->GetMessage());
    EXPECT_TRUE(Waiting->ReasonIsSet());
    EXPECT_EQ("0", Waiting->GetReason());

    V1ContainerState_->SetRunning(Running);
    V1ContainerState_->SetTerminated(Terminated);
    V1ContainerState_->SetWaiting(Waiting);
    json = V1ContainerState_->ToJson();
    result = V1ContainerState_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1ContainerState_->RunningIsSet());
    EXPECT_TRUE(V1ContainerState_->TerminatedIsSet());
    EXPECT_TRUE(V1ContainerState_->WaitingIsSet());
}

TEST_F(KubeClientModelTest, V1TolerationTest)
{
    auto V1Toleration_ = std::make_shared<V1Toleration>();
    V1Toleration_->SetEffect("0");
    V1Toleration_->SetKey("0");
    V1Toleration_->SetROperator("0");
    V1Toleration_->SetTolerationSeconds(0);
    V1Toleration_->SetValue("0");
    auto json = V1Toleration_->ToJson();
    auto result = V1Toleration_->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(V1Toleration_->EffectIsSet());
    EXPECT_EQ("0", V1Toleration_->GetEffect());
    EXPECT_TRUE(V1Toleration_->KeyIsSet());
    EXPECT_EQ("0", V1Toleration_->GetKey());
    EXPECT_TRUE(V1Toleration_->ROperatorIsSet());
    EXPECT_EQ("0", V1Toleration_->GetROperator());
    EXPECT_TRUE(V1Toleration_->TolerationSecondsIsSet());
    EXPECT_EQ(0, V1Toleration_->GetTolerationSeconds());
    EXPECT_TRUE(V1Toleration_->ValueIsSet());
    EXPECT_EQ("0", V1Toleration_->GetValue());
}


TEST_F(KubeClientModelTest, V1TopologySpreadConstraintTest)
{
    auto V1TopologySpreadConstraint_ = std::make_shared<V1TopologySpreadConstraint>();
    V1TopologySpreadConstraint_->UnsetMaxSkew();
    V1TopologySpreadConstraint_->UnsetMatchLabelKeys();
    V1TopologySpreadConstraint_->UnsetTopologyKey();
    V1TopologySpreadConstraint_->UnsetWhenUnsatisfiable();
    V1TopologySpreadConstraint_->UnsetLabelSelector();
    V1TopologySpreadConstraint_->UnsetMatchLabelKeys();
    V1TopologySpreadConstraint_->UnsetNodeAffinityPolicy();
    V1TopologySpreadConstraint_->UnsetNodeTaintsPolicy();
    V1TopologySpreadConstraint_->SetMaxSkew(1);
    V1TopologySpreadConstraint_->SetMinDomains(1);
    V1TopologySpreadConstraint_->SetTopologyKey("zone");
    V1TopologySpreadConstraint_->SetWhenUnsatisfiable("DoNotSchedule");
    auto labelSelector = std::make_shared<V1LabelSelector>();
    V1TopologySpreadConstraint_->SetLabelSelector(labelSelector);
    V1TopologySpreadConstraint_->SetMatchLabelKeys({"test"});
    V1TopologySpreadConstraint_->SetNodeAffinityPolicy("test");
    V1TopologySpreadConstraint_->SetNodeTaintsPolicy("test");
    auto json = V1TopologySpreadConstraint_->ToJson();
    auto topologySpreadConstraint = std::make_shared<V1TopologySpreadConstraint>();
    auto result = topologySpreadConstraint->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_EQ(1, topologySpreadConstraint->GetMaxSkew());
    EXPECT_TRUE(topologySpreadConstraint->MaxSkewIsSet());
    EXPECT_EQ(1, topologySpreadConstraint->GetMinDomains());
    EXPECT_TRUE(topologySpreadConstraint->MinDomainsIsSet());
    EXPECT_EQ("zone", topologySpreadConstraint->GetTopologyKey());
    EXPECT_TRUE(topologySpreadConstraint->TopologyKeyIsSet());
    EXPECT_EQ("DoNotSchedule", topologySpreadConstraint->GetWhenUnsatisfiable());
    EXPECT_TRUE(topologySpreadConstraint->WhenUnsatisfiableIsSet());
    EXPECT_TRUE(topologySpreadConstraint->LabelSelectorIsSet());
    EXPECT_TRUE(topologySpreadConstraint->MatchLabelKeysIsSet());
    EXPECT_EQ("test", topologySpreadConstraint->GetNodeAffinityPolicy());
    EXPECT_TRUE(topologySpreadConstraint->NodeAffinityPolicyIsSet());
    EXPECT_EQ("test", topologySpreadConstraint->GetNodeTaintsPolicy());
    EXPECT_TRUE(topologySpreadConstraint->NodeTaintsPolicyIsSet());
}

TEST_F(KubeClientModelTest, V1LeaseTest)
{
    auto v1LeaseSpec = std::make_shared<V1LeaseSpec>();
    v1LeaseSpec->UnsetAcquireTime();
    v1LeaseSpec->UnsetHolderIdentity();
    v1LeaseSpec->UnsetLeaseDurationSeconds();
    v1LeaseSpec->UnsetLeaseTransitions();
    v1LeaseSpec->UnsetRenewTime();
    v1LeaseSpec->SetAcquireTime(utility::Datetime());
    v1LeaseSpec->SetHolderIdentity("test");
    v1LeaseSpec->SetLeaseDurationSeconds(10);
    v1LeaseSpec->SetRenewTime(utility::Datetime());
    v1LeaseSpec->SetLeaseTransitions(10);
    auto json = v1LeaseSpec->ToJson();
    auto newLeaseSpec = std::make_shared<V1LeaseSpec>();
    auto result = newLeaseSpec->FromJson(json);
    EXPECT_TRUE(result);
    auto v1Lease = std::make_shared<V1Lease>();
    v1Lease->UnsetKind();
    v1Lease->UnsetApiVersion();
    v1Lease->UnsetMetadata();
    v1Lease->UnsetSpec();
    v1Lease->SetKind("V1Lease");
    v1Lease->SetApiVersion("v1");
    v1Lease->SetSpec(newLeaseSpec);
    auto v1ObjectMeta = std::make_shared<V1ObjectMeta>();
    v1Lease->SetMetadata(v1ObjectMeta);
    json = v1Lease->ToJson();
    auto newV1Lease = std::make_shared<V1Lease>();
    result = newV1Lease->FromJson(json);
    EXPECT_TRUE(result);
    EXPECT_TRUE(newV1Lease->KindIsSet());
    EXPECT_TRUE(newV1Lease->SpecIsSet());
    EXPECT_TRUE(newV1Lease->ApiVersionIsSet());
    EXPECT_TRUE(newV1Lease->MetadataIsSet());
    EXPECT_EQ("V1Lease", newV1Lease->GetKind());
    EXPECT_EQ("v1", newV1Lease->GetApiVersion());
    EXPECT_EQ("test", newV1Lease->GetSpec()->GetHolderIdentity());
    EXPECT_EQ("", newV1Lease->GetSpec()->GetRenewTime().ToString());
    EXPECT_EQ("", newV1Lease->GetSpec()->GetAcquireTime().ToString());
    EXPECT_EQ(10, newV1Lease->GetSpec()->GetLeaseDurationSeconds());
    EXPECT_EQ(10, newV1Lease->GetSpec()->GetLeaseTransitions());
}
}  // namespace functionsystem::kube_client::test
