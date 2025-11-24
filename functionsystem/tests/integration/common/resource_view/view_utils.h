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

#ifndef UNIT_COMMON_RESOURCE_VIEW_RESOURCE_TEST_H
#define UNIT_COMMON_RESOURCE_VIEW_RESOURCE_TEST_H

#include <iostream>

#include "async/uuid_generator.hpp"
#include "common/resource_view/resource_tool.h"
#include "common/resource_view/resource_type.h"

namespace functionsystem::test::view_utils {

extern const std::string RESOURCE_CPU_NAME;
extern const std::string RESOURCE_MEM_NAME;
extern const double SCALA_VALUE0;
extern const double SCALA_VALUE1;
extern const double SCALA_VALUE2;

extern const double INST_SCALA_VALUE;

extern const std::string CPU_SCALA_RESOURCE_STRING;
extern const std::string CPU_SCALA_RESOURCES_STRING;

inline resource_view::Resource GetResource(const std::string &name)
{
    resource_view::Resource r;
    r.set_name(name);
    r.set_type(resource_view::ValueType::Value_Type_SCALAR);
    r.mutable_scalar()->set_value(SCALA_VALUE1);
    r.mutable_scalar()->set_limit(SCALA_VALUE2);
    return r;
}

inline resource_view::Resource Get0Resource(const std::string &name)
{
    resource_view::Resource r;
    r.set_name(name);
    r.set_type(resource_view::ValueType::Value_Type_SCALAR);
    r.mutable_scalar()->set_value(SCALA_VALUE0);
    r.mutable_scalar()->set_limit(SCALA_VALUE0);
    return r;
}

inline resource_view::Resource Get0CpuResource()
{
    return Get0Resource(RESOURCE_CPU_NAME);
}

inline resource_view::Resource GetCpuResource()
{
    return GetResource(RESOURCE_CPU_NAME);
}

inline resource_view::Resource Get0MemResource()
{
    return Get0Resource(RESOURCE_MEM_NAME);
}

inline resource_view::Resource GetMemResource()
{
    return GetResource(RESOURCE_MEM_NAME);
}

inline resource_view::Resources Get0CpuResources()
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[Get0CpuResource().name()] = Get0CpuResource();
    return rs;
}

inline resource_view::Resources GetCpuResources()
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[GetCpuResource().name()] = GetCpuResource();
    return rs;
}

inline resource_view::Resources Get0MemResources()
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[Get0MemResource().name()] = Get0MemResource();
    return rs;
}

inline resource_view::Resources GetMemResources()
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[GetMemResource().name()] = GetMemResource();
    return rs;
}

inline resource_view::Resources Get0CpuMemResources()
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[Get0CpuResource().name()] = Get0CpuResource();
    (*rs.mutable_resources())[Get0MemResource().name()] = Get0MemResource();
    return rs;
}

inline resource_view::Resources GetCpuMemResources()
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[GetCpuResource().name()] = GetCpuResource();
    (*rs.mutable_resources())[GetMemResource().name()] = GetMemResource();
    return rs;
}

inline resource_view::Resources GetCpuMemWithOtherEmptyResources()
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[GetCpuResource().name()] = GetCpuResource();
    (*rs.mutable_resources())[GetMemResource().name()] = GetMemResource();
    (*rs.mutable_resources())["OtherResource"] = Get0Resource("OtherResource");
    return rs;
}

inline resource_view::InstanceInfo Get1DInstance()
{
    resource_view::InstanceInfo inst;
    auto id = "Test_InstID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto id2 = "Test_ReqID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    inst.set_instanceid(id);
    inst.set_requestid(id2);
    resource_view::Resources rs = GetCpuMemResources();
    rs.mutable_resources()->at(RESOURCE_CPU_NAME).mutable_scalar()->set_value(INST_SCALA_VALUE);
    rs.mutable_resources()->at(RESOURCE_MEM_NAME).mutable_scalar()->set_value(INST_SCALA_VALUE);
    (*inst.mutable_resources()) = rs;
    (*inst.mutable_actualuse()) = rs;
    return inst;
}

inline resource_view::ResourceUnit Get1DResourceUnit(const std::string &id)
{
    resource_view::ResourceUnit unit;
    unit.set_id(id);
    (*unit.mutable_capacity()) = GetCpuMemResources();
    (*unit.mutable_allocatable()) = GetCpuMemResources();
    (*unit.mutable_actualuse()) = Get0CpuMemResources();

    ::resources::Value::Counter cnter;
    cnter.mutable_items()->insert( { "0-system-faascontroller-0", 1 } );
    unit.mutable_nodelabels()->insert({ RESOURCE_OWNER_KEY, cnter });

    auto bucketIndexes = unit.mutable_bucketindexs();
    resource_view::BucketIndex bucketIndex;
    auto buckets = bucketIndex.mutable_buckets();
    resource_view::Bucket bucket;
    bucket.mutable_total()->set_monopolynum(1);
    resource_view::BucketInfo bucketInfo;
    bucketInfo.set_monopolynum(1);
    bucket.mutable_allocatable()->insert({id, bucketInfo});
    buckets->insert({"1000.100000", bucket});
    bucketIndexes->insert({"1.000000", bucketIndex});

    return unit;
}

inline resource_view::ResourceUnit Get1DResourceUnit()
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    return Get1DResourceUnit("Test_ResID_" + uuid.ToString());
}

inline std::tuple<std::string, std::string> GetMinimumUnitBucketIndex(
    const resource_view::Resources &res)
{
    auto cpu = res.resources().at(RESOURCE_CPU_NAME).scalar().value();
    auto mem = res.resources().at(RESOURCE_MEM_NAME).scalar().value();
    auto proportion = mem / cpu;
    return std::tuple(std::to_string(proportion), std::to_string(mem));
}

inline resource_view::ResourceUnit Get1DResourceUnitWithInstances()
{
    resource_view::ResourceUnit unit;
    unit = Get1DResourceUnit();

    resource_view::InstanceInfo inst1 = Get1DInstance();
    resource_view::InstanceInfo inst2 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = unit.id();
    (*inst2.mutable_schedulerchain()->Add()) = unit.id();

    (*unit.mutable_allocatable()) = unit.allocatable() - inst1.resources();
    (*unit.mutable_allocatable()) = unit.allocatable() - inst2.resources();

    (*unit.mutable_instances())[inst1.instanceid()] = inst1;
    (*unit.mutable_instances())[inst2.instanceid()] = inst2;
    // Indicates the bottom resource unit. The ID of the bucket index is this unit ID.
    auto [unitPropotion, unitMem] = GetMinimumUnitBucketIndex(unit.allocatable());
    auto &bucketIndex = (*unit.mutable_bucketindexs());
    auto &bucket = *bucketIndex[unitPropotion].mutable_buckets();
    bucket[unitMem].mutable_total()->set_sharednum(bucket[unitMem].total().sharednum() + 1);
    (*bucket[unitMem].mutable_allocatable())[unit.id()].set_sharednum(1);
    return unit;
}

inline resource_view::ResourceUnit Get2DResourceUnitWithInstances()
{
    resource_view::ResourceUnit unit1;
    auto id = "Test_ResID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit1.set_id(id);
    (*unit1.mutable_capacity()) = GetCpuMemResources();
    (*unit1.mutable_allocatable()) = GetCpuMemResources();
    (*unit1.mutable_actualuse()) = Get0CpuMemResources();
    resource_view::GenerateMinimumUnitBucketInfo(unit1);

    resource_view::ResourceUnit unit2;
    id = "Test_ResID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit2.set_id(id);
    (*unit2.mutable_capacity()) = GetCpuMemResources();
    (*unit2.mutable_allocatable()) = GetCpuMemResources();
    (*unit2.mutable_actualuse()) = Get0CpuMemResources();
    resource_view::GenerateMinimumUnitBucketInfo(unit2);

    resource_view::ResourceUnit unit3;
    id = "Test_ResID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit3.set_id(id);
    (*unit3.mutable_capacity()) = unit1.capacity() + unit2.capacity();
    (*unit3.mutable_allocatable()) = unit1.allocatable() + unit2.allocatable();
    (*unit3.mutable_actualuse()) = unit1.actualuse() + unit2.actualuse();

    // make unit1 instances
    resource_view::InstanceInfo inst1 = Get1DInstance();
    resource_view::InstanceInfo inst2 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = unit3.id();
    (*inst1.mutable_schedulerchain()->Add()) = unit1.id();
    (*inst2.mutable_schedulerchain()->Add()) = unit3.id();
    (*inst2.mutable_schedulerchain()->Add()) = unit1.id();

    (*unit1.mutable_allocatable()) = unit1.allocatable() - inst1.resources();
    (*unit1.mutable_allocatable()) = unit1.allocatable() - inst2.resources();

    (*unit1.mutable_instances())[inst1.instanceid()] = inst1;
    (*unit1.mutable_instances())[inst2.instanceid()] = inst2;

    (*unit3.mutable_instances())[inst1.instanceid()] = inst1;
    (*unit3.mutable_instances())[inst2.instanceid()] = inst2;

    // make unit2 instances
    inst1 = Get1DInstance();
    inst2 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = unit3.id();
    (*inst1.mutable_schedulerchain()->Add()) = unit1.id();
    (*inst2.mutable_schedulerchain()->Add()) = unit3.id();
    (*inst2.mutable_schedulerchain()->Add()) = unit1.id();

    (*unit2.mutable_allocatable()) = unit1.allocatable() - inst1.resources();
    (*unit2.mutable_allocatable()) = unit1.allocatable() - inst2.resources();

    (*unit2.mutable_instances())[inst1.instanceid()] = inst1;
    (*unit2.mutable_instances())[inst2.instanceid()] = inst2;

    (*unit3.mutable_instances())[inst1.instanceid()] = inst1;
    (*unit3.mutable_instances())[inst2.instanceid()] = inst2;

    // make unit3 resources
    (*unit3.mutable_capacity()) = unit1.capacity() + unit2.capacity();
    (*unit3.mutable_allocatable()) = unit1.allocatable() + unit2.allocatable();
    (*unit3.mutable_actualuse()) = unit1.actualuse() + unit2.actualuse();
    unit3.mutable_fragment()->insert({ unit1.id(), unit1 });
    unit3.mutable_fragment()->insert({ unit2.id(), unit2 });
    // unit1 and unit2 indicates the bottom resource unit.
    auto [unit1Propotion, unit1Mem] = GetMinimumUnitBucketIndex(unit1.allocatable());
    auto &bucketIndex = (*unit3.mutable_bucketindexs());
    auto &bucket = *bucketIndex[unit1Propotion].mutable_buckets();
    bucket[unit1Mem].mutable_total()->set_sharednum(bucket[unit1Mem].total().sharednum() + 1);
    (*bucket[unit1Mem].mutable_allocatable())[unit1.id()].set_sharednum(1);

    auto [unit2Propotion, unit2Mem] = GetMinimumUnitBucketIndex(unit2.allocatable());
    auto &bucket2 = *bucketIndex[unit2Propotion].mutable_buckets();
    bucket2[unit2Mem].mutable_total()->set_sharednum(bucket2[unit2Mem].total().sharednum() + 1);
    (*bucket2[unit2Mem].mutable_allocatable())[unit1.id()].set_sharednum(1);
    return unit3;
}

inline resource_view::ResourceUnit Change1DResourceUnitWithInstances(resource_view::ResourceUnit unit)
{
    resource_view::InstanceInfo inst1 = Get1DInstance();
    inst1.mutable_resources()->mutable_resources()->erase(RESOURCE_CPU_NAME);
    (*inst1.mutable_schedulerchain()->Add()) = unit.id();

    // remove orgin bucket index.
    auto [originPropotion, originMem] = GetMinimumUnitBucketIndex(unit.allocatable());
    auto &originBucketIndex = (*unit.mutable_bucketindexs());
    auto &originBucket = *originBucketIndex[originPropotion].mutable_buckets();
    originBucket[originMem].mutable_total()->set_sharednum(0);
    (*originBucket[originMem].mutable_allocatable())[unit.id()].set_sharednum(0);

    unit.mutable_instances()->clear();
    unit.clear_allocatable();
    (*unit.mutable_allocatable()) = unit.capacity() - inst1.resources();

    (*unit.mutable_instances())[inst1.instanceid()] = inst1;

    // update bucket index
    auto [unitPropotion, unitMem] = GetMinimumUnitBucketIndex(unit.allocatable());
    auto &bucketIndex = (*unit.mutable_bucketindexs());
    auto &bucket = *bucketIndex[unitPropotion].mutable_buckets();
    bucket[unitMem].mutable_total()->set_sharednum(bucket[unitMem].total().sharednum() + 1);
    (*bucket[unitMem].mutable_allocatable())[unit.id()].set_sharednum(1);

    return unit;
}

}  // namespace functionsystem::test::view_utils
#endif  // UNIT_COMMON_RESOURCE_VIEW_RESOURCE_TEST_H
