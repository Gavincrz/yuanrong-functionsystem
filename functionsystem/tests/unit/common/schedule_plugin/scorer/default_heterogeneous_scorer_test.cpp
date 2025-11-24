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

#include "common/schedule_plugin/scorer/default_heterogeneous_scorer/default_heterogeneous_scorer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"
#include "common/resource_view/resource_tool.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::schedule_plugin::score;
using namespace functionsystem::schedule_framework;

void AddPreAllocated(const resource_view::InstanceInfo &ins,
                     const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                     const std::string &selected,
                     schedule_framework::NodeScore &score)
{
    auto backupIns = ins;
    const auto &required = ins.resources().resources();
    for (auto &req : required) {
        auto resourceNameFields = litebus::strings::Split(req.first, "/");
        if (resourceNameFields.size() == HETERO_RESOURCE_FIELD_NUM) {
            backupIns.mutable_resources()->mutable_resources()->erase(req.first);
        }
    }
    auto *resources = backupIns.mutable_resources()->mutable_resources();
    for (const auto &allocated : score.allocatedVectors) {
        auto *vectors = (*resources)[allocated.first].mutable_vectors();
        (*resources)[allocated.first].set_name(allocated.first);
        (*resources)[allocated.first].set_type(resource_view::ValueType::Value_Type_VECTORS);
        for (const auto &value : allocated.second.values()) {
            (*vectors->mutable_values())[value.first] = value.second;
        }
    }
    (*backupIns.mutable_schedulerchain()->Add()) = selected;
    backupIns.set_unitid(selected);
    context->allocated[selected].resource = context->allocated[selected].resource.resources().size() == 0
                                                ? backupIns.resources()
                                                : context->allocated[selected].resource + backupIns.resources();
    context->allocatedLabels[selected] = context->allocatedLabels[selected] + ToLabelKVs(ins.labels());
    context->preAllocatedSelectedFunctionAgentMap[ins.instanceid()] = selected;
    context->preAllocatedSelectedFunctionAgentSet.insert(selected);
}

class DefaultHeterogeneousScorerTest : public Test {};

// Score heterogeneous(hbm+latency+stream) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringHBMAndLatencyAndStreamInHeteroPod) {
    auto unit = view_utils::Get1DResourceUnitWithSpecificNpuNumber({15,20,40,0,20,30,0,0}, "NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    // 1.Non-regex
    auto instance = view_utils::Get1DInstanceWithNpuResource(30, 20, 1, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 33); // int(30/30*100 + 0 + 0) / 3;
    EXPECT_EQ(score.realIDs[0], 5);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    // 2.regex
    instance = view_utils::Get1DInstanceWithNpuResource(30, 20, 1, "NPU/Ascend910.*");
    preAllocated = std::make_shared<PreAllocatedContext>();
    score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 33); // int(30/30*100 + 0 + 0) / 3;
    EXPECT_EQ(score.realIDs[0], 5);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringCountInHeteroPod) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    // 1.Non-regex
    auto instance = view_utils::Get1DInstanceWithNpuResource(6, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    // 2.regex
    instance = view_utils::Get1DInstanceWithNpuResource(6, "NPU/Ascend910.*");
    preAllocated = std::make_shared<PreAllocatedContext>();
    score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");
}

// Score non-heterogeneous requests for pod without heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestNonHeteroScoringInNonHeteroPod) {
    auto instance = view_utils::Get1DInstance();
    auto unit = view_utils::Get1DResourceUnit();

    DefaultHeterogeneousScorer scorer;
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
}

// Score non-heterogeneous requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestNonHeteroScoringInHeteroPod) {
    auto instance = view_utils::Get1DInstance();
    auto unit = view_utils::Get1DResourceUnitWithNpu();

    DefaultHeterogeneousScorer scorer;
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 0);
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase1) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 0);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 1);
}


// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase2) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.7, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 1);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 0);
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase3) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.7, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 1);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(0.6, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 2);
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase4) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.4, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 0);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 0);
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase5) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.8, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 1);
    EXPECT_EQ(score.realIDs.size(), 1);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(5, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 2);
    EXPECT_EQ(score.realIDs.size(), 5);

    AddPreAllocated(instance3, preAllocated, unit.id(), score);
    auto instance4 = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance4, unit);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.realIDs.size(), 1);
}

}