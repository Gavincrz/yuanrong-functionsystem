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

#include "common/schedule_plugin/filter/disk_filter/disk_filter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::schedule_plugin::filter;
using namespace functionsystem::schedule_framework;

const std::string DEFAULT_NODE_ID = "test_node";

class DiskFilterTest : public Test {};

// Test disk scheduling success due to sufficient resources
TEST(DiskFilterTest, SuccessWhenSufficientDiskResource) {

    DiskFilter filter;

    // Subcase 1: Exact match scenario
    // Instance requests exactly the available disk capacity (100)
    {
        auto instance = view_utils::Get1DInstanceWithDiskResource(100);
        auto unit = view_utils::Get1DResourceUnitWithDisk({ 100 });

        auto preAllocated = std::make_shared<PreAllocatedContext>();
        resource_view::Resources rs = view_utils::GetDiskResources({ 0 });
        preAllocated->allocated[unit.id()].resource = std::move(rs);

        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::SUCCESS);
        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 1);
    }

    // Subcase 2: Partial match scenario
    // Resource view contains two disks:
    // - First disk capacity (40) is insufficient for request (100)
    // - Second disk capacity (200) can satisfy the request
    {
        auto instance = view_utils::Get1DInstanceWithDiskResource(100);
        auto unit = view_utils::Get1DResourceUnitWithDisk({ 40, 200 });

        auto preAllocated = std::make_shared<PreAllocatedContext>();
        resource_view::Resources rs = view_utils::GetDiskResources({ 0, 0 });
        preAllocated->allocated[unit.id()].resource = std::move(rs);

        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::SUCCESS);
        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 1);
    }

    // Subcase 3: Non-disk request
    {
        auto instance = view_utils::Get1DInstance();
        auto unit = view_utils::Get1DResourceUnitWithDisk({ 100 });

        auto preAllocated = std::make_shared<PreAllocatedContext>();
        resource_view::Resources rs = view_utils::GetDiskResources({ 0 });
        preAllocated->allocated[unit.id()].resource = std::move(rs);

        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::SUCCESS);
        EXPECT_FALSE(filter.Filter(preAllocated, instance, unit).isFatalErr);
        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 0);
    }
}


// Test disk scheduling failure due to insufficient resources
TEST(DiskFilterTest, FailedWhenInsufficientDiskResource)
{
    DiskFilter filter;

    // Subcase 1: Initial empty disk resources (request 200)
    {
        auto instance = view_utils::Get1DInstanceWithDiskResource(200);
        auto unit = view_utils::Get1DResourceUnit();

        auto preAllocated = std::make_shared<PreAllocatedContext>();
        resource_view::Resources rs = view_utils::GetDiskResources({ 0 });
        preAllocated->allocated[unit.id()].resource = std::move(rs);

        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::DISK_SCHEDULE_FAILED);
        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 0);
    }

    // Subcase 2: Initial disk resources insufficient (request 200 > available 100)
    {
        auto instance = view_utils::Get1DInstanceWithDiskResource(200);
        auto unit = view_utils::Get1DResourceUnitWithDisk({ 100 });

        auto preAllocated = std::make_shared<PreAllocatedContext>();
        resource_view::Resources rs = view_utils::GetDiskResources({ 0 });
        preAllocated->allocated[unit.id()].resource = std::move(rs);

        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::DISK_SCHEDULE_FAILED);
        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 0);
    }

    // Subcase 3: Disk insufficiency caused by pre-allocated resources
    // (request 200 > remaining 0 && request 200 > remaining 100)
    {
        auto instance = view_utils::Get1DInstanceWithDiskResource(200);
        auto unit = view_utils::Get1DResourceUnitWithDisk({ 250, 200 }, DEFAULT_NODE_ID);

        auto preAllocated = std::make_shared<PreAllocatedContext>();
        resource_view::Resources rs = view_utils::GetDiskResources({ 250, 100 }, DEFAULT_NODE_ID);
        preAllocated->allocated[unit.id()].resource = std::move(rs);

        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::DISK_SCHEDULE_FAILED);
        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 0);
    }


    // Subcase 4: Zero-disk request
    {
        auto instance = view_utils::Get1DInstanceWithDiskResource(0);
        auto unit = view_utils::Get1DResourceUnitWithDisk({ 100 });

        auto preAllocated = std::make_shared<PreAllocatedContext>();
        resource_view::Resources rs = view_utils::GetDiskResources({ 0 });
        preAllocated->allocated[unit.id()].resource = std::move(rs);

        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::PARAMETER_ERROR);
        EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 0);
    }
}

// Abnormal scenario: Invalid context
TEST(DiskFilterTest, InvalidContext) {
    auto instance = view_utils::Get1DInstanceWithDiskResource(100);
    auto unit = view_utils::Get1DResourceUnitWithDisk({ 100 });

    DiskFilter filter;
    auto status = filter.Filter(nullptr, instance, unit);
    EXPECT_EQ(status.status, StatusCode::PARAMETER_ERROR);
    EXPECT_TRUE(status.isFatalErr);
}

}  // namespace functionsystem::test