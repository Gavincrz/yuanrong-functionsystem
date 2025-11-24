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

#include "common/resource_view/resource_view.h"
#include "common/resource_view/view_utils.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "utils/future_test_helper.h"
#include "common/schedule_decision/preemption_controller/preemption_controller.h"


namespace functionsystem::test {
using namespace testing;
using namespace schedule_decision;

class PreemptionControllerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        resourceView_ = resource_view::ResourceView::CreateResourceView(
            "domain", resource_view::ResourceViewActor::Param{ false, false, 0 });
    }
    void TearDown() override
    {
        resourceView_ = nullptr;
    }

protected:
    std::shared_ptr<resource_view::ResourceView> resourceView_;
};

inline resource_view::InstanceInfo GetInstanceWithResource(const std::string &instanceID, int32_t priority, double cpu,
                                                           double memory)
{
    resource_view::InstanceInfo inst;
    auto id2 = "Test_ReqID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    inst.set_instanceid(instanceID);
    inst.set_requestid(id2);
    inst.mutable_scheduleoption()->set_priority(priority);
    resource_view::Resources rs = view_utils::GetCpuMemResources();
    rs.mutable_resources()->at(view_utils::RESOURCE_CPU_NAME).mutable_scalar()->set_value(cpu);
    rs.mutable_resources()->at(view_utils::RESOURCE_MEM_NAME).mutable_scalar()->set_value(memory);
    (*inst.mutable_resources()) = rs;
    (*inst.mutable_actualuse()) = rs;
    return inst;
}

// test for no available instance can be preempted
TEST_F(PreemptionControllerTest, NoAvailableInstanceCanBePreemptedTest)
{
    auto pod1 = view_utils::Get1DResourceUnit("pod1");
    auto pod2 = view_utils::Get1DResourceUnit("pod2");
    resourceView_->AddResourceUnit(pod1);
    resourceView_->AddResourceUnit(pod2);
    auto instance1 = view_utils::Get1DInstance();
    instance1.set_unitid("pod1");
    instance1.mutable_scheduleoption()->set_priority(5);
    instance1.mutable_scheduleoption()->set_preemptedallowed(true);
    auto instance2 = view_utils::Get1DInstance();
    instance2.set_unitid("pod1");
    instance2.mutable_scheduleoption()->set_priority(5);
    instance2.mutable_scheduleoption()->set_preemptedallowed(true);
    auto instance3 = view_utils::Get1DInstance();
    instance3.set_unitid("pod2");
    instance3.mutable_scheduleoption()->set_priority(5);
    resourceView_->AddInstances(
        { { instance1.instanceid(), { instance1, nullptr } }, { instance2.instanceid(), { instance2, nullptr } } });
    resourceView_->AddInstances({ { instance3.instanceid(), { instance3, nullptr } } });

    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 1, 100.1, 100.1);
    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto preContext = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto result = preemption.PreemptDecision(preContext, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::DOMAIN_SCHEDULER_NO_PREEMPTABLE_INSTANCE);
}

// test for invalid param
TEST_F(PreemptionControllerTest, InvalidParemTest)
{
    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 1, 100.1, 100.1);
    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto result = preemption.PreemptDecision(nullptr, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::PARAMETER_ERROR);
}

// test for unable to preempt with cross tenant
TEST_F(PreemptionControllerTest, UnableToPreemptWithCrossTenant)
{
    auto pod1 = view_utils::Get1DResourceUnit("pod1");
    resourceView_->AddResourceUnit(pod1);
    auto instance1 = view_utils::Get1DInstance();
    instance1.set_unitid("pod1");
    instance1.mutable_scheduleoption()->set_priority(1);
    instance1.add_labels("tenantId:123456");
    resourceView_->AddInstances({ { instance1.instanceid(), { instance1, nullptr } } });

    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 5, 100.1, 100.1);
    scheduledInstance.add_labels("tenantId:xxxxx");
    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto preContext = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto result = preemption.PreemptDecision(preContext, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::DOMAIN_SCHEDULER_NO_PREEMPTABLE_INSTANCE);
}

// test for preemption failed with resource capacity not enough
TEST_F(PreemptionControllerTest, PreemptionFailedWithCapNotEnough)
{
    auto pod1 = view_utils::Get1DResourceUnit("pod1");
    resourceView_->AddResourceUnit(pod1);
    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 5, 2000.1, 2000.1);
    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto preContext = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto result = preemption.PreemptDecision(preContext, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::DOMAIN_SCHEDULER_NO_PREEMPTABLE_INSTANCE);
}

// test for preemption failed with instance required anti affinity
TEST_F(PreemptionControllerTest, PreemptionFailedWithInstanceRequiredAntiAffinity)
{
    auto pod1 = view_utils::Get1DResourceUnit("pod1");
    resourceView_->AddResourceUnit(pod1);
    auto instance1 = view_utils::Get1DInstance();
    instance1.set_unitid("pod1");
    instance1.mutable_scheduleoption()->set_priority(1);
    instance1.mutable_scheduleoption()->set_preemptedallowed(true);
    instance1.add_labels("key1");
    resourceView_->AddInstances({ { instance1.instanceid(), { instance1, nullptr } } });

    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 5, 100.1, 100.1);
    scheduledInstance.add_labels("tenantId:xxxxx");
    auto instanceAffinity = scheduledInstance.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    (*instanceAffinity->mutable_requiredantiaffinity()) = Selector(false, { { Exist("key1") } });

    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto preContext = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto result = preemption.PreemptDecision(preContext, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::DOMAIN_SCHEDULER_NO_PREEMPTABLE_INSTANCE);
}

// test for preemption failed with instance required affinity
TEST_F(PreemptionControllerTest, PreemptionFailedWithInstanceRequiredAffinity)
{
    auto pod1 = view_utils::Get1DResourceUnit("pod1");
    resourceView_->AddResourceUnit(pod1);
    auto instance1 = view_utils::Get1DInstance();
    instance1.set_unitid("pod1");
    instance1.mutable_scheduleoption()->set_priority(1);
    instance1.mutable_scheduleoption()->set_preemptedallowed(true);
    instance1.add_labels("key1");
    resourceView_->AddInstances({ { instance1.instanceid(), { instance1, nullptr } } });

    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 5, 100.1, 100.1);
    scheduledInstance.add_labels("tenantId:xxxxx");
    auto instanceAffinity = scheduledInstance.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    (*instanceAffinity->mutable_requiredaffinity()) = Selector(false, { { Exist("key1") } });

    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto preContext = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto result = preemption.PreemptDecision(preContext, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::DOMAIN_SCHEDULER_NO_PREEMPTABLE_INSTANCE);
}

// test for preemption successful with resource priority affinity
// instance which scheduled with resource priority(poolA, poolB)
// unit1 -> instanceA(can not preempted)  instanceB(can be preempted)  poolA
// unit2 -> instanceC(can be preempted) poolA
// unit3 -> instanceD(can be preempted) poolB
// instanceC request resource > instanceB request resource
// instanceD request resource > instanceA request resource
// expected： unit1 is selected & instanceB is selected to be preempted
TEST_F(PreemptionControllerTest, PreemptionSuccessfulWithResourcePriorityAffinity)
{
    auto pod1 = view_utils::Get1DResourceUnit("unit1");
    resources::Value::Counter cnter;
    (*cnter.mutable_items())["value"] = 1;
    (*pod1.mutable_nodelabels())["poolA"] = cnter;

    auto pod2 = view_utils::Get1DResourceUnit("unit2");
    (*pod2.mutable_nodelabels())["poolA"] = cnter;

    auto pod3 = view_utils::Get1DResourceUnit("unit3");
    resources::Value::Counter cnter2;
    (*cnter.mutable_items())["value2"] = 1;
    (*pod2.mutable_nodelabels())["poolB"] = cnter2;

    resourceView_->AddResourceUnit(pod1);
    resourceView_->AddResourceUnit(pod2);
    resourceView_->AddResourceUnit(pod3);
    auto instance1 = GetInstanceWithResource("instance1", 6, 30.0, 30.0);
    instance1.set_unitid("unit1");
    instance1.mutable_scheduleoption()->set_preemptedallowed(true);

    auto instance2 = GetInstanceWithResource("instance2", 1, 30.0, 30.0);
    instance2.set_unitid("unit1");
    instance2.mutable_scheduleoption()->set_preemptedallowed(true);

    auto instance3 = GetInstanceWithResource("instance3", 1, 50.0, 50.0);
    instance3.set_unitid("unit2");
    instance3.mutable_scheduleoption()->set_preemptedallowed(true);

    auto instance4 = GetInstanceWithResource("instance4", 1, 50.0, 50.0);
    instance4.set_unitid("unit3");
    instance4.mutable_scheduleoption()->set_preemptedallowed(true);

    resourceView_->AddInstances(
        { { instance1.instanceid(), { instance1, nullptr } }, { instance2.instanceid(), { instance2, nullptr } } });
    resourceView_->AddInstances({ { instance3.instanceid(), { instance3, nullptr } } });
    resourceView_->AddInstances({ { instance4.instanceid(), { instance4, nullptr } } });

    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 5, 60.0, 60.0);

    auto resourceAffinity = scheduledInstance.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
    (*resourceAffinity->mutable_preferredaffinity()) = Selector(true, { { Exist("poolA") }, { Exist("poolB") } });
    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto preContext = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto result = preemption.PreemptDecision(preContext, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(result.unitID, "unit1");
    EXPECT_EQ(result.preemptedInstances.size(), size_t(1));
    EXPECT_EQ(result.preemptedInstances[0].instanceid(), instance2.instanceid());
}

// test for preemption successful with resource priority affinity
// instance which scheduled with resource priority(poolA, poolB)
// unit1 -> instance1(can be preempted)  instance2(can be preempted)  poolA
// unit2 -> instance3(can be preempted) poolB
// instance1 request resource == instance2 request resource
// instance3 request resource == instance1 request resource
// expected： unit1 is selected & instance1, instance2 is selected to be preempted
TEST_F(PreemptionControllerTest, PreemptionSuccessfulWithResourcePriorityAffinityMultiInstancePreempted)
{
    auto pod1 = view_utils::Get1DResourceUnit("unit1");
    resources::Value::Counter cnter;
    (*cnter.mutable_items())["value"] = 1;
    (*pod1.mutable_nodelabels())["runtimepool1"] = cnter;

    auto pod2 = view_utils::Get1DResourceUnit("unit2");
    resources::Value::Counter cnter2;
    (*cnter.mutable_items())["value2"] = 1;
    (*pod2.mutable_nodelabels())["runtimepool2"] = cnter2;

    resourceView_->AddResourceUnit(pod1);
    resourceView_->AddResourceUnit(pod2);
    auto instance1 = view_utils::Get1DInstance();
    instance1.set_unitid("unit1");
    instance1.mutable_scheduleoption()->set_priority(1);
    instance1.mutable_scheduleoption()->set_preemptedallowed(true);
    auto instance2 = view_utils::Get1DInstance();
    instance2.set_unitid("unit1");
    instance2.mutable_scheduleoption()->set_priority(1);
    instance2.mutable_scheduleoption()->set_preemptedallowed(true);
    auto instance3 = view_utils::Get1DInstance();
    instance3.set_unitid("unit2");
    instance3.mutable_scheduleoption()->set_priority(1);
    instance3.mutable_scheduleoption()->set_preemptedallowed(true);
    resourceView_->AddInstances(
        { { instance1.instanceid(), { instance1, nullptr } }, { instance2.instanceid(), { instance2, nullptr } } });
    resourceView_->AddInstances({ { instance3.instanceid(), { instance3, nullptr } } });

    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 5, 100.1, 100.1);

    auto resourceAffinity = scheduledInstance.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
    (*resourceAffinity->mutable_preferredaffinity()) =
        Selector(true, { { Exist("runtimepool1") }, { Exist("runtimepool2") } });
    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto preContext = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto result = preemption.PreemptDecision(preContext, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(result.unitID, "unit1");
    EXPECT_EQ(result.preemptedInstances.size(), 2);
    EXPECT_TRUE(result.preemptedInstances[0].instanceid() == instance1.instanceid() ||
                result.preemptedInstances[0].instanceid() == instance2.instanceid());
}

// test for preemption successful with instance priority affinity
// instance which scheduled with instance preferred affinity(c 80, a 100, b 90)
// unit1 -> instanceA(can be preempted)  instanceB(can be preempted)
// unit2 -> instanceC(can be preempted)
// unit3 -> instanceD(can be preempted)
// expected： unit1 is selected & instanceB is selected to be preempted
TEST_F(PreemptionControllerTest, PreemptionSuccessfulWithInstancePreferredAffinity)
{
    auto pod1 = view_utils::Get1DResourceUnit("unit1");
    auto pod2 = view_utils::Get1DResourceUnit("unit2");
    auto pod3 = view_utils::Get1DResourceUnit("unit3");

    resourceView_->AddResourceUnit(pod1);
    resourceView_->AddResourceUnit(pod2);
    resourceView_->AddResourceUnit(pod3);
    auto instance1 = GetInstanceWithResource("instance1", 1, 30.0, 30.0);
    instance1.set_unitid("unit1");
    instance1.add_labels("instance1");
    instance1.mutable_scheduleoption()->set_preemptedallowed(true);

    auto instance2 = GetInstanceWithResource("instance2", 1, 30.0, 30.0);
    instance2.set_unitid("unit1");
    instance2.add_labels("instance2");
    instance2.mutable_scheduleoption()->set_preemptedallowed(true);

    auto instance3 = GetInstanceWithResource("instance3", 1, 50.0, 50.0);
    instance3.set_unitid("unit2");
    instance3.add_labels("instance3");
    instance3.mutable_scheduleoption()->set_preemptedallowed(true);

    auto instance4 = GetInstanceWithResource("instance4", 1, 50.0, 50.0);
    instance4.set_unitid("unit3");
    instance4.add_labels("instance4");
    instance4.mutable_scheduleoption()->set_preemptedallowed(true);

    resourceView_->AddInstances(
        { { instance1.instanceid(), { instance1, nullptr } }, { instance2.instanceid(), { instance2, nullptr } } });
    resourceView_->AddInstances({ { instance3.instanceid(), { instance3, nullptr } } });
    resourceView_->AddInstances({ { instance4.instanceid(), { instance4, nullptr } } });

    auto scheduledInstance = GetInstanceWithResource("scheduledInstance", 5, 60.0, 60.0);

    auto resourceAffinity = scheduledInstance.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    (*resourceAffinity->mutable_preferredaffinity()) =
         Selector(true, { { Exist("instance1") }, { Exist("instance2") }, { Exist("instance3") } });
    auto preemption = PreemptionController();
    auto unit = resourceView_->GetResourceView().Get();
    auto preContext = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto result = preemption.PreemptDecision(preContext, scheduledInstance, *unit);
    EXPECT_EQ(result.status.StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(result.unitID, "unit1");
    EXPECT_EQ(result.preemptedInstances.size(), 1);
    EXPECT_EQ(result.preemptedInstances[0].instanceid(), instance2.instanceid());
}
}  // namespace functionsystem::test