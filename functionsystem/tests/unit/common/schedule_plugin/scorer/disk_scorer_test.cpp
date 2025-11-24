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

#include "common/schedule_plugin/scorer/disk_scorer/disk_scorer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::schedule_plugin::score;
using namespace functionsystem::schedule_framework;

const std::string DEFAULT_NODE_ID = "test_node";

class DiskScorerTest : public Test {};

// Tests disk scoring when 50 out of 200 disk units are pre-allocated:
// 1. Verifies score calculation: (1 - requested/remaining) * 100
// 2. Checks if resource allocation selects correct mount point ("/tmp/abc2/")
// 3. Validates output vector allocation metadata
TEST(DiskScorerTest, ScoreWithPartiallyAllocatedDisk) {
    DiskScorer scorer;
    auto instance = view_utils::Get1DInstanceWithDiskResource(100);
    auto unit = view_utils::Get1DResourceUnitWithDisk({ 20, 100, 200 }, DEFAULT_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources rs = view_utils::GetDiskResources({ 0, 0, 50 }, DEFAULT_NODE_ID);
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 33); // (1-100/(200-50))*100
    EXPECT_EQ(score.vectorAllocations.size(), 1);
    auto vectorAllocation = score.vectorAllocations[0];
    EXPECT_EQ(vectorAllocation.selectedIndices.size(), 1);
    EXPECT_EQ(vectorAllocation.selectedIndices[0], 2);
    EXPECT_EQ(vectorAllocation.selectedIndices.size(), 1);
    EXPECT_EQ(vectorAllocation.type, resource_view::DISK_RESOURCE_NAME);
    auto diskMountPoint = vectorAllocation.extendedInfo.find(resource_view::DISK_MOUNT_POINT);
    ASSERT_NE(diskMountPoint, vectorAllocation.extendedInfo.end());
    EXPECT_EQ(diskMountPoint->second, "/tmp/abc2/");
}

//  Test disk scoring with floating-point disk requirement(100.5)
// Verifies score calculation: (1 - requested/remaining) * 100
TEST(DiskScorerTest, ScoreWithFloatDiskReq) {
    DiskScorer scorer;
    auto instance = view_utils::Get1DInstanceWithDiskResource(100.5);
    (*instance.mutable_resources()->mutable_resources())[resource_view::DISK_RESOURCE_NAME]
        .mutable_scalar()->set_value(100.5);
    auto unit = view_utils::Get1DResourceUnitWithDisk({ 20, 100, 200 }, DEFAULT_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources rs = view_utils::GetDiskResources({ 0, 0, 50 }, DEFAULT_NODE_ID);
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 33); // int((1-100.5/(200-50))*100)
    EXPECT_EQ(score.vectorAllocations.size(), 1);
    auto vectorAllocation = score.vectorAllocations[0];
    EXPECT_EQ(vectorAllocation.selectedIndices.size(), 1);
    EXPECT_EQ(vectorAllocation.selectedIndices[0], 2);
    EXPECT_EQ(vectorAllocation.selectedIndices.size(), 1);
    EXPECT_EQ(vectorAllocation.type, resource_view::DISK_RESOURCE_NAME);

    std::vector<double> expectAlloc{ 0, 0, 100.5 };
    auto allocCategory = vectorAllocation.allocationValues.values().find(resource_view::DISK_RESOURCE_NAME);
    ASSERT_NE(allocCategory, vectorAllocation.allocationValues.values().end());
    EXPECT_EQ(allocCategory->second.vectors_size(), 1);
    auto valuesAlloc = allocCategory->second.vectors().begin()->second;
    EXPECT_EQ(valuesAlloc.values_size(), expectAlloc.size());
    for (size_t i = 0; i< expectAlloc.size(); i++) {
        EXPECT_EQ(expectAlloc[i], valuesAlloc.values(i));
    }
}

// abnormal scenario: Tests disk scoring with insufficient disk resource
TEST(DiskScorerTest, ScoreWithInsufficientDisk) {
    DiskScorer scorer;
    auto instance = view_utils::Get1DInstanceWithDiskResource(100);
    auto unit = view_utils::Get1DResourceUnitWithDisk({ 20, 100, 200 }, DEFAULT_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources rs = view_utils::GetDiskResources({ 20, 100, 200 }, DEFAULT_NODE_ID);
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, -1);
    EXPECT_TRUE(score.vectorAllocations.empty());
}

// Tests disk scoring with  Non-disk req
TEST(DiskScorerTest, ScoreWithNonDiskReq) {
    DiskScorer scorer;
    auto instance = view_utils::Get1DInstance();
    auto unit = view_utils::Get1DResourceUnitWithDisk({ 20, 100, 200 }, DEFAULT_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources rs = view_utils::GetDiskResources({ 20, 100, 200 }, DEFAULT_NODE_ID);
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 0);
}

// abnormal scenario: Tests disk scoring with  Non-disk resource
TEST(DiskScorerTest, ScoreWithNonDiskResource) {
    DiskScorer scorer;
    auto instance = view_utils::Get1DInstanceWithDiskResource(100);
    auto unit = view_utils::Get1DResourceUnit();

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 0);
}

// Abnormal scenario: Invalid context
TEST(DiskScorerTest, InvalidContext) {
    auto instance = view_utils::Get1DInstanceWithDiskResource(100);
    auto unit = view_utils::Get1DResourceUnitWithDisk({ 100 });

    DiskScorer scorer;
    auto score = scorer.Score(nullptr, instance, unit);
    EXPECT_EQ(score.score, 0);
}

}