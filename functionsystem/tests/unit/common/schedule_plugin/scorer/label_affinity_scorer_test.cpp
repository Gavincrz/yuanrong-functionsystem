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

#include "common/schedule_plugin/scorer/label_affinity_scorer/label_affinity_scorer.h"

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
using namespace functionsystem::schedule_plugin::score;
using namespace functionsystem::test::schedule_plugin;

class LabelAffinityScorerTest : public Test {};

TEST_F(LabelAffinityScorerTest, InstancAffinityTest)
{
    LabelAffinityScorer relaxedScorerPlugin(true);

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
    affinity = Selector(true, { { Exist("key3") }});
    (*instanceAffinity->mutable_preferredantiaffinity()) = std::move(affinity);
    affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
    (*instanceAffinity->mutable_requiredaffinity()) = std::move(affinity);

    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(300);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;
    schedule_framework::NodeScore result(0);
    // 1.pod scope
    {
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 300);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 280);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 0);
    }

    // 2.node scope
    {
        auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
        instanceAffinity->set_scope(affinity::NODE);

        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();
        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::LOCAL;

        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 200);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 200);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 200);
    }

    // 3.no affinity
    {
        instance1.mutable_scheduleoption()->clear_affinity();

        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();
        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::LOCAL;

        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 1);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 1);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 1);
    }
}

TEST_F(LabelAffinityScorerTest, ResourceAffinityTest)
{
    LabelAffinityScorer relaxedScorerPlugin(true);

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
    affinity = Selector(true, { { Exist("key3") }});
    (*resourceAffinity->mutable_preferredantiaffinity()) = std::move(affinity);
    affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
    (*resourceAffinity->mutable_requiredaffinity()) = std::move(affinity);

    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(300);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;

    schedule_framework::NodeScore result(0);

    // 1.preferredaffinity  with priority
    {
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 10200);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 9190);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 0);
    }

    // 2.preferredaffinity without priority
    {
        auto resourceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
        auto affinity = Selector(false, { { Exist("key1") }, { Exist("key2") } });
        (*resourceAffinity->mutable_preferredaffinity()) = std::move(affinity);

        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();

        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 10200);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 9200);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 0);
    }
}

TEST_F(LabelAffinityScorerTest, PreemptAffinityTest)
{
    LabelAffinityScorer relaxedScorerPlugin(true);

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
    schedule_framework::NodeScore result(0);

    // 1.instance is preemptible
    {
        auto affinity = Selector(false, { { Exist("Preemptible") } });
        auto antiAffinity = Selector(false, { { Exist("NotPreemptible") } });
        (*affinity.mutable_condition()->mutable_subconditions())[0].set_weight(3);
        (*antiAffinity.mutable_condition()->mutable_subconditions())[0].set_weight(3);
        (*preemptAffinity->mutable_preferredaffinity()) = std::move(affinity);
        (*preemptAffinity->mutable_preferredantiaffinity()) = std::move(antiAffinity);

        preAllocated->allLocalLabels["NodeA"] = NodeA.nodelabels();
        preAllocated->allLocalLabels["NodeB"] = NodeB.nodelabels();
        preAllocated->allLocalLabels["NodeC"] = NodeC.nodelabels();
        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::NON_ROOT_DOMAIN;

        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 6);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 0);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 3);
    }

    // 2.instance is not preemptible
    {
        auto affinity = Selector(false, { { Exist("NotPreemptible") } });
        auto antiAffinity = Selector(false, { { Exist("Preemptible") } });
        (*affinity.mutable_condition()->mutable_subconditions())[0].set_weight(3);
        (*antiAffinity.mutable_condition()->mutable_subconditions())[0].set_weight(3);
        (*preemptAffinity->mutable_preferredaffinity()) = std::move(affinity);
        (*preemptAffinity->mutable_preferredantiaffinity()) = std::move(antiAffinity);

        preAllocated->allLocalLabels["NodeA"] = NodeA.nodelabels();
        preAllocated->allLocalLabels["NodeB"] = NodeB.nodelabels();
        preAllocated->allLocalLabels["NodeC"] = NodeC.nodelabels();
        preAllocated->schedulerLevel = resource_view::SCHEDULER_LEVEL::NON_ROOT_DOMAIN;
        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();

        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 0);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 6);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 3);
    }
}

TEST_F(LabelAffinityScorerTest, TenantAffinityTest)
{
    LabelAffinityScorer relaxedScorerPlugin(true);

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

    preAllocated->ClearUnfeasible();
    schedule_framework::NodeScore result(0);

    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
    EXPECT_EQ(result.score, 100);
    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
    EXPECT_EQ(result.score, 0);
    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
    EXPECT_EQ(result.score, 0);
}

TEST_F(LabelAffinityScorerTest, DataAffinityTest)
{
    LabelAffinityScorer relaxedScorerPlugin(true);

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

    schedule_framework::NodeScore result(0);

    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
    EXPECT_EQ(result.score, 100);
    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
    EXPECT_EQ(result.score, 90);
    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
    EXPECT_EQ(result.score, 0);
}

TEST_F(LabelAffinityScorerTest, SkipPreferredScoreTest)
{
    LabelAffinityScorer relaxedScorerPlugin(true);
    LabelAffinityScorer strictScorerPlugin(false);

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
    auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    instanceAffinity->set_scope(affinity::POD);
    auto affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
    (*instanceAffinity->mutable_preferredaffinity()) = std::move(affinity);
    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(666);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;
    schedule_framework::NodeScore result(0);

    // 1.relaxed + not TopDownScheduling
    {
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 100);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 90);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 0);
    }

    // 2.strict + not TopDownScheduling
    {
        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();
        result = strictScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 666);
        result = strictScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 666);
        result = strictScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 666);
    }

    // set isTopDownScheduling
    (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->set_istopdownscheduling(true);


    // 3.strict + is TopDownScheduling
    {
        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();
        result = strictScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 100);
        result = strictScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 90);
        result = strictScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 0);
    }

    // 4.relaxed + is TopDownScheduling
    {
        (*preAllocated->pluginCtx)[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->clear_scheduledscore();
        result = strictScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 100);
        result = strictScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 90);
        result = strictScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 0);
    }
}

TEST_F(LabelAffinityScorerTest, MultiAffinityTest)
{
    LabelAffinityScorer relaxedScorerPlugin(true);

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
    auto affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
    // instanceAffinity
    auto instanceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    instanceAffinity->set_scope(affinity::POD);
    (*instanceAffinity->mutable_preferredaffinity()) = affinity;
    affinity = Selector(true, { { Exist("key1") }, { Exist("key2") } });
    (*instanceAffinity->mutable_requiredaffinity()) = affinity;

    // resourceAffinity
    auto resourceAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
    (*resourceAffinity->mutable_preferredaffinity()) = affinity;

    // dataAffinity
    auto dataAffinity = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_data();
    (*dataAffinity->mutable_preferredaffinity()) = affinity;

    auto preAllocated = std::make_shared<schedule_framework::PreAllocatedContext>();
    preAllocated->allLocalLabels[local1.id()] = local1.nodelabels();
    auto pluginCtx = messages::PluginContext();
    pluginCtx.mutable_affinityctx()->set_maxscore(400);
    std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
    ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
    preAllocated->pluginCtx = &map;


    schedule_framework::NodeScore result(0);

    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
    EXPECT_EQ(result.score, 400);
    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
    EXPECT_EQ(result.score, 360);
    result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
    EXPECT_EQ(result.score, 0);
}

TEST_F(LabelAffinityScorerTest, GroupScheduleAffinityTest)
{
    LabelAffinityScorer relaxedScorerPlugin(true);

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
    auto affinity = Selector(false, { { Exist("rgroup-111") } });

    {
        auto pluginCtx = messages::PluginContext();
        pluginCtx.mutable_affinityctx()->set_maxscore(100);
        std::map<std::string, messages::PluginContext> tmp{ { LABEL_AFFINITY_PLUGIN, pluginCtx } };
        ::google::protobuf::Map<std::string, messages::PluginContext> map(tmp.begin(), tmp.end());
        preAllocated->pluginCtx = &map;
        auto grouplb = instance1.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_grouplb();
        (*grouplb->mutable_preferredaffinity()) = affinity;
        schedule_framework::NodeScore result(0);

        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 100);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 0);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 0);
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
        schedule_framework::NodeScore result(0);

        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent1);
        EXPECT_EQ(result.score, 0);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent2);
        EXPECT_EQ(result.score, 100);
        result = relaxedScorerPlugin.Score(preAllocated, instance1, agent3);
        EXPECT_EQ(result.score, 100);
    }
}

}