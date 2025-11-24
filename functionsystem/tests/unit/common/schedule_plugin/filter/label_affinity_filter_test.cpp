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

#include "common/schedule_plugin/filter/label_affinity_filter/label_affinity_filter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/view_utils.h"
#include "common/schedule_plugin/common/plugin_utils.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::schedule_plugin::filter;
using namespace functionsystem::test::schedule_plugin;

class LabelAffinityFilterTest : public Test {};

TEST_F(LabelAffinityFilterTest, InstancRequiredAffinityInPodScopeTest)
{
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } , { "key2", "value2" }});
    auto agent2 = NewResourceUnit("agent2", { { "key1", "value1" } });
    auto agent3 = NewResourceUnit("agent3", { { "key2", "value2" } });
    auto agent4 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    agent2.set_ownerid(local1.id());
    agent3.set_ownerid(local1.id());
    agent4.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);
    AddFragmentToUnit(local1, agent4);

    auto instance1 = view_utils::Get1DInstance();

    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(100);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    // 1.without priority
    {
        instance1.mutable_scheduleoption()->clear_affinity();
        auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
        instanceAffinity->set_scope(affinity::POD);
        auto affinity = Selector(false, { { Exist("key1") }, { Exist("key2") } });
        (*instanceAffinity->mutable_requiredaffinity()) = std::move(affinity);

        preAllocated->ClearUnfeasible();
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent4);
        EXPECT_EQ(result.status.IsOk(), false);
    }

    // 2.with priority
    {
        instance1.mutable_scheduleoption()->clear_affinity();
        auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
        instanceAffinity->set_scope(affinity::POD);
        auto affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
        (*instanceAffinity->mutable_requiredaffinity()) = std::move(affinity);

        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledresult();
        preAllocated->ClearUnfeasible();
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        // agent3: Affinity score is not optimal
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
        // agent4: Affinity label filtering failed
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent4);
        EXPECT_EQ(result.status.IsOk(), false);
    }
}

TEST_F(LabelAffinityFilterTest, InstancRequiredAffinityInNodeScopeTest)
{
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } , { "key2", "value2" }});
    auto agent2 = NewResourceUnit("agent2", { { "key1", "value1" } });
    auto agent3 = NewResourceUnit("agent3", { { "key2", "value2" } });
    auto agent4 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(agent1.id());
    agent2.set_ownerid(agent2.id());
    agent3.set_ownerid(agent3.id());
    agent4.set_ownerid(agent4.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);
    AddFragmentToUnit(local1, agent4);

    auto instance1 = view_utils::Get1DInstance();

    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(100);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    instance1.mutable_scheduleoption()->clear_affinity();
    auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    instanceAffinity->set_scope(affinity::NODE);
    auto affinity = Selector(false, { { Exist("key1") }, { Exist("key2") } });
    (*instanceAffinity->mutable_requiredaffinity()) = std::move(affinity);

    (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledresult();
    preAllocated->ClearUnfeasible();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::LOCAL;
    schedule_framework::Filtered result;

    result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
    EXPECT_EQ(result.status.IsOk(), true);
    EXPECT_EQ(result.availableForRequest, -1);
    result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
    EXPECT_EQ(result.status.IsOk(), true);
    result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
    EXPECT_EQ(result.status.IsOk(), true);
    result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent4);
    EXPECT_EQ(result.status.IsOk(), true);
}

TEST_F(LabelAffinityFilterTest, InstancPreferredAffinityTest)
{
    LabelAffinityFilter relaxedNonRootFilterPlugin(true, false);
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } });
    auto agent2 = NewResourceUnit("agent2", { { "key2", "value2" } });
    auto agent3 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(agent1.id());
    agent2.set_ownerid(agent2.id());
    agent3.set_ownerid(agent3.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);

    auto instance1 = view_utils::Get1DInstance();
    auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    instanceAffinity->set_scope(affinity::POD);
    auto affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
    (*instanceAffinity->mutable_preferredaffinity()) = std::move(affinity);
    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(100);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    // 1.relaxed filter
    {
        schedule_framework::Filtered result;

        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }

    // 2.strict filter + with priority + pod scope
    {
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }

    // 3.strict filter + without priority + pod scope
    {
        instance1.mutable_scheduleoption()->clear_affinity();
        auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
        auto affinity = Selector(false, { { Exist("key1") }, { Exist("key2") } });
        (*instanceAffinity->mutable_preferredaffinity()) = std::move(affinity);

        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }

    // 4.strict filter + without priority + node scope
    {
        instance1.mutable_scheduleoption()->clear_affinity();
        auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
        instanceAffinity->set_scope(affinity::NODE);

        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::LOCAL;

        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }
}

TEST_F(LabelAffinityFilterTest, TopDomnSchedulingSkipPreferredOptimalScoreTest)
{
    LabelAffinityFilter strictRootFilterPlugin(false, true);
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } });
    auto agent2 = NewResourceUnit("agent2", { { "key2", "value2" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    agent2.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent2);

    auto instance1 = view_utils::Get1DInstance();
    auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    instanceAffinity->set_scope(affinity::POD);
    auto affinity = Selector(false, { { Exist("key1") } });
    (*instanceAffinity->mutable_preferredaffinity()) = std::move(affinity);
    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(100);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    // 1.strict Non-root + not topdownscheduling
    {
        schedule_framework::Filtered result;
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
    }

    // 2.set istopdownscheduling in root scorer
    {
        schedule_framework::Filtered result;
        EXPECT_FALSE((*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].affinityctx().istopdownscheduling());
        result = strictRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_TRUE((*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].affinityctx().istopdownscheduling());
    }

    // 3.strict Non-root + is topdownscheduling
    {
        schedule_framework::Filtered result;
        result = strictRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
    }
}

TEST_F(LabelAffinityFilterTest, ResourceRequiredAffinityTest)
{
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } , { "key2", "value2" }});
    auto agent2 = NewResourceUnit("agent2", { { "key1", "value1" } });
    auto agent3 = NewResourceUnit("agent3", { { "key2", "value2" } });
    auto agent4 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    agent2.set_ownerid(local1.id());
    agent3.set_ownerid(local1.id());
    agent4.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);
    AddFragmentToUnit(local1, agent4);

    auto instance1 = view_utils::Get1DInstance();

    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(100);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    // 1.with priority
    {
        instance1.mutable_scheduleoption()->clear_affinity();
        auto resourceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
        auto affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
        (*resourceAffinity->mutable_requiredaffinity()) = std::move(affinity);

        preAllocated->ClearUnfeasible();
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        // agent3: Affinity score is not optimal
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
        EXPECT_STREQ(result.required.c_str(), "resource { aff { condition { subConditions { expressions { key: \"key1\" op { exists { } } } weight: 100 } subConditions { expressions { key: \"key2\" op { exists { } } } weight: 90 } orderPriority: true } } antiAff { } }");
        // agent4: Affinity label filtering failed
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent4);
        EXPECT_EQ(result.status.IsOk(), false);
    }

    // 2.without priority
    {
        instance1.mutable_scheduleoption()->clear_affinity();
        auto resourceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
        auto affinity = Selector(false, { { Exist("key1") }, { Exist("key2") } });
        (*resourceAffinity->mutable_requiredaffinity()) = std::move(affinity);

        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();
        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledresult();
        preAllocated->ClearUnfeasible();
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        EXPECT_STREQ(result.required.c_str(), "resource { aff { condition { subConditions { expressions { key: \"key1\" op { exists { } } } weight: 100 } subConditions { expressions { key: \"key2\" op { exists { } } } weight: 100 } } } antiAff { } }");
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }
}

TEST_F(LabelAffinityFilterTest, ResourcePreferredAffinityTest)
{
    LabelAffinityFilter relaxedNonRootFilterPlugin(true, false);
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } });
    auto agent2 = NewResourceUnit("agent2", { { "key2", "value2" } });
    auto agent3 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    agent2.set_ownerid(local1.id());
    agent3.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);

    auto instance1 = view_utils::Get1DInstance();
    auto resourceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
    auto affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
    (*resourceAffinity->mutable_preferredaffinity()) = std::move(affinity);
    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(100);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    // 1.relaxed filter
    {
        schedule_framework::Filtered result;

        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }

    // 2.strict filter + with priority
    {
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
        EXPECT_STREQ(result.required.c_str(), "");
    }

    // 3.strict filter + without priority
    {
        instance1.mutable_scheduleoption()->clear_affinity();
        auto resourceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
        auto affinity = Selector(false, { { Exist("key1") }, { Exist("key2") } });
        (*resourceAffinity->mutable_preferredaffinity()) = std::move(affinity);

        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
        EXPECT_STREQ(result.required.c_str(), "");
    }
}

TEST_F(LabelAffinityFilterTest, AllowPreemptPreferredAffinityTest)
{
    LabelAffinityFilter relaxedNonRootFilterPlugin(true, false);
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "Preemptible", "true" } });
    auto NodeA = NewResourceUnit("NodeA", {});
    agent1.set_ownerid(NodeA.id());
    AddFragmentToUnit(NodeA, agent1);

    auto agent2 = NewResourceUnit("agent2", { { "NotPreemptible", "true" } });
    auto NodeB = NewResourceUnit("NodeB", {});
    agent2.set_ownerid(NodeB.id());
    AddFragmentToUnit(NodeB, agent2);

    auto agent3 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto NodeC = NewResourceUnit("NodeC", {});
    agent3.set_ownerid(NodeC.id());
    AddFragmentToUnit(NodeC, agent3);

    auto instance1 = view_utils::Get1DInstance();
    auto preemptAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_preempt();
    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx();
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    auto affinity = Selector(false, { { Exist("Preemptible") } });
    auto antiAffinity = Selector(false, { { Exist("NotPreemptible") } });
    (*affinity.mutable_condition()->mutable_subconditions())[0].set_weight(3);
    (*antiAffinity.mutable_condition()->mutable_subconditions())[0].set_weight(3);
    (*preemptAffinity->mutable_preferredaffinity()) = std::move(affinity);
    (*preemptAffinity->mutable_preferredantiaffinity()) = std::move(antiAffinity);

    // 1.instance is preemptible + relaxed filter
    {
        preAllocated->allLocalLabels["NodeA"] = NodeA.nodelabels();
        preAllocated->allLocalLabels["NodeB"] = NodeB.nodelabels();
        preAllocated->allLocalLabels["NodeC"] = NodeC.nodelabels();
        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::NON_ROOT_DOMAIN;
        schedule_framework::Filtered result;

        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }

    // 2.instance is preemptible + strict filter
    {
        preAllocated->allLocalLabels["NodeA"] = NodeA.nodelabels();
        preAllocated->allLocalLabels["NodeB"] = NodeB.nodelabels();
        preAllocated->allLocalLabels["NodeC"] = NodeC.nodelabels();
        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::NON_ROOT_DOMAIN;
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }
}

TEST_F(LabelAffinityFilterTest, NotAllowPreemptPreferredAffinityTest)
{
    LabelAffinityFilter relaxedNonRootFilterPlugin(true, false);
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "Preemptible", "true" } });
    auto NodeA = NewResourceUnit("NodeA", {});
    agent1.set_ownerid(NodeA.id());
    AddFragmentToUnit(NodeA, agent1);

    auto agent2 = NewResourceUnit("agent2", { { "NotPreemptible", "true" } });
    auto NodeB = NewResourceUnit("NodeB", {});
    agent2.set_ownerid(NodeB.id());
    AddFragmentToUnit(NodeB, agent2);

    auto agent3 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto NodeC = NewResourceUnit("NodeC", {});
    agent3.set_ownerid(NodeC.id());
    AddFragmentToUnit(NodeC, agent3);

    auto instance1 = view_utils::Get1DInstance();
    auto preemptAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_preempt();
    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx();
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    auto affinity = Selector(false, { { Exist("NotPreemptible") } });
    auto antiAffinity = Selector(false, { { Exist("Preemptible") } });
    (*affinity.mutable_condition()->mutable_subconditions())[0].set_weight(3);
    (*antiAffinity.mutable_condition()->mutable_subconditions())[0].set_weight(3);
    (*preemptAffinity->mutable_preferredaffinity()) = std::move(affinity);
    (*preemptAffinity->mutable_preferredantiaffinity()) = std::move(antiAffinity);

    // 1.instance is not preemptible + relaxed filter
    {
        preAllocated->allLocalLabels["NodeA"] = NodeA.nodelabels();
        preAllocated->allLocalLabels["NodeB"] = NodeB.nodelabels();
        preAllocated->allLocalLabels["NodeC"] = NodeC.nodelabels();
        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::NON_ROOT_DOMAIN;
        schedule_framework::Filtered result;

        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }

    // 4.instance is not preemptible + strict filter
    {
        preAllocated->allLocalLabels["NodeA"] = NodeA.nodelabels();
        preAllocated->allLocalLabels["NodeB"] = NodeB.nodelabels();
        preAllocated->allLocalLabels["NodeC"] = NodeC.nodelabels();
        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::NON_ROOT_DOMAIN;
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }
}

TEST_F(LabelAffinityFilterTest, DataPreferredAffinityTest)
{
    LabelAffinityFilter relaxedNonRootFilterPlugin(true, false);
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } });
    auto agent2 = NewResourceUnit("agent2", { { "key2", "value2" } });
    auto agent3 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    agent2.set_ownerid(local1.id());
    agent3.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);

    auto instance1 = view_utils::Get1DInstance();
    auto dataAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_data();
    auto affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
    (*dataAffinity->mutable_preferredaffinity()) = std::move(affinity);
    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(100);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    // 1.relaxed filter
    {
        schedule_framework::Filtered result;

        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }

    // 2.strict filter + with priority
    {
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }

    // 3.strict filter + without priority
    {
        instance1.mutable_scheduleoption()->clear_affinity();
        auto dataAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_data();
        auto affinity = Selector(false, { { Exist("key1") }, { Exist("key2") } });
        (*dataAffinity->mutable_preferredaffinity()) = std::move(affinity);

        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }
}

TEST_F(LabelAffinityFilterTest, PendingAffinityTest)
{
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } });
    auto agent2 = NewResourceUnit("agent2", { { "key2", "value1" } });
    auto agent3 = NewResourceUnit("agent3", { { "key1", "value1" } , { "key3", "value3" } });
    auto agent4 = NewResourceUnit("agent4", { { "key4", "value4" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    agent2.set_ownerid(local1.id());
    agent3.set_ownerid(local1.id());
    agent4.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);
    AddFragmentToUnit(local1, agent4);

    auto instance1 = view_utils::Get1DInstance();

    {
        auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
        preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
        auto pluginCtx = messages::PluginContext();
        pluginCtx.mutable_affinityctx()->set_maxscore(100);
        std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
        ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
        preAllocated->pluginCtx = &map;

        instance1.mutable_scheduleoption()->clear_affinity();
        auto pendingAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_pending();
        auto affinity1 = Selector(false, { { Exist("key1"), NotExist("key3") }});
        auto affinity2 = Selector(true, { { NotExist("key4") } });
        pendingAffinity->add_resources()->mutable_requiredaffinity()->CopyFrom(affinity1);
        pendingAffinity->add_resources()->mutable_requiredaffinity()->CopyFrom(affinity2);

        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();
        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledresult();
        preAllocated->ClearUnfeasible();
        schedule_framework::Filtered result;

        // ！（true && true） && !（..）
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), false);
        // ！（false && ..） && !（true）
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        // ！（true && false） && !（true）
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
        // ！（false && ..） && !（false）
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent4);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
    }
}

TEST_F(LabelAffinityFilterTest, TenantPreferredAffinityTest)
{
    LabelAffinityFilter relaxedNonRootFilterPlugin(true, false);
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "tenantId", "tenantA" } });
    auto agent2 = NewResourceUnit("agent2", { { "tenantId", "tenantB" } });
    auto agent3 = NewResourceUnit("agent3", {});
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    agent2.set_ownerid(local1.id());
    agent3.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);

    auto instance1 = view_utils::Get1DInstance();
    auto tenantAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_tenant();
    auto tenantRequiredAntiAffinity = Selector(false, { { NotIn("tenantId", { "tenantA" }), Exist("tenantId") } });
    (*tenantAffinity->mutable_requiredantiaffinity()) = std::move(tenantRequiredAntiAffinity);
    auto tenantPreferrdAffinity = Selector(true, { { In("tenantId", { "tenantA" }) } });
    (*tenantAffinity->mutable_preferredaffinity()) = std::move(tenantPreferrdAffinity);

    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(100);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    // 1.relaxed filter
    {
        preAllocated->ClearUnfeasible();
        schedule_framework::Filtered result;

        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }

    // 2.strict filter
    {
        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();
        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledresult();
        preAllocated->ClearUnfeasible();
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }
}

TEST_F(LabelAffinityFilterTest, ResourceGroupAffinityTest)
{
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "rgroup", "value1" } , { "key2", "value2" }});
    auto agent2 = NewResourceUnit("agent2", { { "rgroup", "value1" } });
    auto agent3 = NewResourceUnit("agent3", { { "rgroup", "value2" } });
    auto agent4 = NewResourceUnit("agent3", { { "rgroup", "value3" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    agent2.set_ownerid(local1.id());
    agent3.set_ownerid(local1.id());
    agent4.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);
    AddFragmentToUnit(local1, agent4);

    auto instance1 = view_utils::Get1DInstance();

    {
        auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
        preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
        auto pluginCtx = messages::PluginContext();
        pluginCtx.mutable_affinityctx()->set_maxscore(100);
        std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
        ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
        preAllocated->pluginCtx = &map;

        instance1.mutable_scheduleoption()->clear_affinity();
        auto rgAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()
                                   ->mutable_rgroup();
        auto affinity1 = Selector(false, { { In("rgroup", {"value1"}) }});
        (*rgAffinity->mutable_requiredaffinity()) = std::move(affinity1);
        preAllocated->ClearUnfeasible();
        schedule_framework::Filtered result;

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        EXPECT_EQ(result.availableForRequest, -1);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }
}

TEST_F(LabelAffinityFilterTest, GroupSchedulePolicyAffinityTest)
{
    LabelAffinityFilter relaxedNonRootFilterPlugin(true, false);
    LabelAffinityFilter strictNonRootFilterPlugin(false, false);

    auto agent1 = NewResourceUnit("agent1", { { "key1", "value1" } });
    auto agent2 = NewResourceUnit("agent2", { { "key2", "value2" } });
    auto agent3 = NewResourceUnit("agent3", { { "key3", "value3" } });
    auto local1 = NewResourceUnit("local1", {});
    agent1.set_ownerid(local1.id());
    agent2.set_ownerid(local1.id());
    agent3.set_ownerid(local1.id());
    AddFragmentToUnit(local1, agent1);
    AddFragmentToUnit(local1, agent2);
    AddFragmentToUnit(local1, agent3);

    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allocatedLabels[agent1.id()] = ToLabelKV("rgroup-111");
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto instance1 = view_utils::Get1DInstance();
    auto affinity = Selector(false, { { Exist("rgroup-111") }});
    // 1.pack affinity filter
    {
        auto pluginCtx = messages::PluginContext();
        pluginCtx.mutable_affinityctx()->set_maxscore(100);
        std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
        ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
        preAllocated->pluginCtx = &map;
        auto grouplbAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_grouplb();
        (*grouplbAffinity->mutable_preferredaffinity()) = affinity;
        schedule_framework::Filtered result;
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), false);
    }
    // 2.spread affinity filter
    {
        auto pluginCtx = messages::PluginContext();
        pluginCtx.mutable_affinityctx()->set_maxscore(100);
        std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
        ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
        preAllocated->pluginCtx = &map;
        instance1.mutable_scheduleoption()->clear_affinity();
        auto grouplbAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_grouplb();
        (*grouplbAffinity->mutable_preferredantiaffinity()) = affinity;

        schedule_framework::Filtered result;
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }
    // 3.strict spread
    {
        auto pluginCtx = messages::PluginContext();
        pluginCtx.mutable_affinityctx()->set_maxscore(100);
        std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
        ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
        preAllocated->pluginCtx = &map;
        instance1.mutable_scheduleoption()->clear_affinity();
        auto grouplbAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_grouplb();
        (*grouplbAffinity->mutable_requiredantiaffinity()) = affinity;

        schedule_framework::Filtered result;
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), false);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = relaxedNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);

        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent1);
        EXPECT_EQ(result.status.IsOk(), false);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent2);
        EXPECT_EQ(result.status.IsOk(), true);
        result = strictNonRootFilterPlugin.Filter(preAllocated, instance1, agent3);
        EXPECT_EQ(result.status.IsOk(), true);
    }
}
}