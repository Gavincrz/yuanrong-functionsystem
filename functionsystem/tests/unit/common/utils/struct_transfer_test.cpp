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

#include "common/utils/struct_transfer.h"

#include "gtest/gtest.h"
#include "common/resource_view/view_utils.h"

namespace functionsystem::test {
const std::string DEFAULT_UNIT_ID = "test_node";
const std::string DEFAULT_NODE_UUID = "test_uuid";
const std::string DEFAULT_DISK_MOUNT_PATH = "/tmp/abc/";
const int DEFAULT_DISK_INDEX = 1;
const std::string DEFAULT_VENDOR = "NPU";
const std::string DEFAULT_CARD_TYPE = "NPU/Ascend310";
const std::string RUNTIME_ENV_PREFIX = "func-";
const std::vector<int> DEFAULT_VECTOR_RESOURCE = { 0, 100, 0 };
class StructTransferTest : public ::testing::Test {};

TEST_F(StructTransferTest, StaticFunction_TRUE)
{
    resources::InstanceInfo instanceInfo;

    auto *options = instanceInfo.mutable_createoptions();
    options->insert({ RESOURCE_OWNER_KEY, STATIC_FUNCTION_OWNER_VALUE });

    EXPECT_TRUE(IsStaticFunctionInstance(instanceInfo));
}

TEST_F(StructTransferTest, StaticFunction_FALSE)
{
    resources::InstanceInfo instanceInfo;
    EXPECT_FALSE(IsStaticFunctionInstance(instanceInfo));
}

void SetValidDiskAllocation(schedule_decision::ScheduleResult &result)
{
    schedule_framework::VectorResourceAllocation vectorAllocation;
    vectorAllocation.type = resource_view::DISK_RESOURCE_NAME;
    vectorAllocation.selectedIndices = { DEFAULT_DISK_INDEX };

    auto &allocCategory = (*vectorAllocation.allocationValues.mutable_values())[resource_view::DISK_RESOURCE_NAME];
    for (const auto &value: DEFAULT_VECTOR_RESOURCE) {
        (*allocCategory.mutable_vectors())[DEFAULT_NODE_UUID].add_values(value);
    }

    vectorAllocation.extendedInfo[resource_view::DISK_MOUNT_POINT] = DEFAULT_DISK_MOUNT_PATH;
    result.vectorAllocations.push_back(vectorAllocation);
}

void SetValidHeteroAllocation(schedule_decision::ScheduleResult &result)
{
    result.realIDs = { DEFAULT_DISK_INDEX };

    auto &allocCategory = (*result.allocatedVectors[DEFAULT_CARD_TYPE]
        .mutable_values())[resource_view::HETEROGENEOUS_MEM_KEY];
    for (const auto &value: DEFAULT_VECTOR_RESOURCE) {
        (*allocCategory.mutable_vectors())[DEFAULT_NODE_UUID].add_values(value);
    }
}

TEST_F(StructTransferTest, MergeScheduleResultToRequest)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    auto scheduleInstance = view_utils::Get1DInstanceWithNpuResource(1);
    *req->mutable_instance() = scheduleInstance;
    req->set_requestid(scheduleInstance.requestid());
    req->set_traceid("traceID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    schedule_decision::ScheduleResult result;
    result.id = DEFAULT_UNIT_ID;
    result.unitID = DEFAULT_UNIT_ID;
    SetValidDiskAllocation(result);
    SetValidHeteroAllocation(result);

    MergeScheduleResultToRequest(req, result);

    // validate basic info
    EXPECT_EQ(req->instance().functionagentid(), result.id);
    EXPECT_EQ(req->instance().unitid(), result.unitID);
    const auto& schedulerChain = req->instance().schedulerchain();
    EXPECT_EQ(schedulerChain.size(), 1);
    EXPECT_EQ(schedulerChain[0], result.id);

    const auto& createOptions = req->instance().createoptions();
    // validate heterogenous device id
    auto heteroDeviceIds = createOptions.find("func-NPU-DEVICE-IDS");
    ASSERT_NE(heteroDeviceIds, createOptions.end());
    EXPECT_EQ(heteroDeviceIds->second, "1");

    // validate heterogenous resource allocation
    const auto& resources = req->instance().resources().resources();
    auto heteroResource = resources.find(DEFAULT_CARD_TYPE);
    ASSERT_NE(heteroResource, resources.end());
    EXPECT_EQ(heteroResource->second.type(), resource_view::ValueType::Value_Type_VECTORS);
    EXPECT_EQ(heteroResource->second.name(), DEFAULT_CARD_TYPE);

    const auto& heteroVectors = heteroResource->second.vectors().values();
    auto heteroCategory = heteroVectors.find(resource_view::HETEROGENEOUS_MEM_KEY);
    ASSERT_NE(heteroCategory, heteroVectors.end());

    auto hetegroVectors = heteroCategory->second.vectors().find(DEFAULT_NODE_UUID);
    ASSERT_NE(hetegroVectors, heteroCategory->second.vectors().end());

    EXPECT_EQ(hetegroVectors->second.values_size(), DEFAULT_VECTOR_RESOURCE.size());
    for (int i = 0; i < hetegroVectors->second.values_size(); i++) {
        EXPECT_EQ(DEFAULT_VECTOR_RESOURCE[i], hetegroVectors->second.values(i));
    }

    // validate disk mount point
    auto envKey = RUNTIME_ENV_PREFIX + resource_view::DISK_MOUNT_POINT;
    auto diskMountPoint = createOptions.find(envKey);
    ASSERT_NE(diskMountPoint, createOptions.end());
    EXPECT_EQ(diskMountPoint->second, DEFAULT_DISK_MOUNT_PATH);

    // validate disk resource allocation
    auto diskResource = resources.find(resource_view::DISK_RESOURCE_NAME);
    ASSERT_NE(diskResource, resources.end());
    EXPECT_EQ(diskResource->second.type(), resource_view::ValueType::Value_Type_VECTORS);
    EXPECT_EQ(diskResource->second.name(), resource_view::DISK_RESOURCE_NAME);

    const auto& vectors = diskResource->second.vectors().values();
    auto diskCategory = vectors.find(resource_view::DISK_RESOURCE_NAME);
    ASSERT_NE(diskCategory, vectors.end());

    auto diskVectors = diskCategory->second.vectors().find(DEFAULT_NODE_UUID);
    ASSERT_NE(diskVectors, diskCategory->second.vectors().end());

    EXPECT_EQ(diskVectors->second.values_size(), DEFAULT_VECTOR_RESOURCE.size());
    for (int i = 0; i < diskVectors->second.values_size(); i++) {
        EXPECT_EQ(DEFAULT_VECTOR_RESOURCE[i], diskVectors->second.values(i));
    }

}

TEST_F(StructTransferTest, SetMonopolyAffinityOptScope)
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();

    // 1.When instance affinity scope is not set --> scope == Node
    {
        auto instance = view_utils::Get1DInstance();
        instance.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
        auto createReq = CreateRequest();
        SetAffinityOpt(instance, createReq, scheduleReq);
        EXPECT_EQ(instance.scheduleoption().affinity().instance().scope(), affinity::NODE);
    }

    // 2.When instance affinity is set to POD scope --> scope == Node
    {
        auto instance = view_utils::Get1DInstance();
        instance.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
        auto createReq = CreateRequest();
        createReq.mutable_schedulingops()->mutable_scheduleaffinity()->mutable_instance()->set_scope(affinity::POD);
        SetAffinityOpt(instance, createReq, scheduleReq);
        EXPECT_EQ(instance.scheduleoption().affinity().instance().scope(), affinity::NODE);
    }

    // 3.When instance affinity is set to Node scope --> scope == Node
    {
        auto instance = view_utils::Get1DInstance();
        instance.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
        auto createReq = CreateRequest();
        createReq.mutable_schedulingops()->mutable_scheduleaffinity()->mutable_instance()->set_scope(affinity::NODE);
        SetAffinityOpt(instance, createReq, scheduleReq);
        EXPECT_EQ(instance.scheduleoption().affinity().instance().scope(), affinity::NODE);
    }
}

TEST_F(StructTransferTest, SetSharedAffinityOptScope)
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();

    // 1.When instance affinity scope is not set --> scope defaults to POD
    {
        auto instance = view_utils::Get1DInstance();
        auto createReq = CreateRequest();
        instance.mutable_scheduleoption()->set_schedpolicyname("shared");
        SetAffinityOpt(instance, createReq, scheduleReq);
        EXPECT_EQ(instance.scheduleoption().affinity().instance().scope(), affinity::POD);
    }

    // 2.When instance affinity is set to POD scope --> scope == POD
    {
        auto instance = view_utils::Get1DInstance();
        instance.mutable_scheduleoption()->set_schedpolicyname("shared");
        auto createReq = CreateRequest();
        createReq.mutable_schedulingops()->mutable_scheduleaffinity()->mutable_instance()->set_scope(affinity::POD);
        SetAffinityOpt(instance, createReq, scheduleReq);
        EXPECT_EQ(instance.scheduleoption().affinity().instance().scope(), affinity::POD);
    }

    // 3. When instance affinity is set to NODE scope --> scope == NODE
    {
        auto instance = view_utils::Get1DInstance();
        instance.mutable_scheduleoption()->set_schedpolicyname("shared");
        auto createReq = CreateRequest();
        createReq.mutable_schedulingops()->mutable_scheduleaffinity()->mutable_instance()->set_scope(affinity::NODE);
        SetAffinityOpt(instance, createReq, scheduleReq);
        EXPECT_EQ(instance.scheduleoption().affinity().instance().scope(), affinity::NODE);
    }
}

}  // namespace functionsystem::test