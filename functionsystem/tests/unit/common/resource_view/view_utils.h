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
extern const std::string RESOURCE_NPU_NAME;
extern const double SCALA_VALUE0;
extern const double SCALA_VALUE1;
extern const double SCALA_VALUE2;

extern const double INST_SCALA_VALUE;

extern const std::string CPU_SCALA_RESOURCE_STRING;
extern const std::string CPU_SCALA_RESOURCES_STRING;

const std::string DEFAULT_NPU_TYPE = "NPU/310";

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

inline resource_view::Resource GetResourceWithValue(const std::string &name, double value)
{
    resource_view::Resource r;
    r.set_name(name);
    r.set_type(resource_view::ValueType::Value_Type_SCALAR);
    r.mutable_scalar()->set_value(value);
    r.mutable_scalar()->set_limit(value);
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

inline resource_view::Resource GetNameResourceWithValue(const std::string &name, double value)
{
    return GetResourceWithValue(name, value);
}

inline resource_view::Resource Get0MemResource()
{
    return Get0Resource(RESOURCE_MEM_NAME);
}

inline resource_view::Resource GetMemResource()
{
    return GetResource(RESOURCE_MEM_NAME);
}

inline resource_view::Resource GetNpuResource(const std::string cardType = DEFAULT_NPU_TYPE)
{
    resource_view::Resource r;
    r.set_name(cardType);
    r.set_type(resource_view::ValueType::Value_Type_VECTORS);
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID().ToString();

    auto categories = r.mutable_vectors()->mutable_values();

    auto &vectors1 = (*categories)[resource_view::HETEROGENEOUS_MEM_KEY];
    auto &vector1 = (*vectors1.mutable_vectors())[uuid];
    for (int i=0; i<8; i++) {
        vector1.mutable_values()->Add(100);
    }
    auto &vectors2 = (*categories)[resource_view::HETEROGENEOUS_LATENCY_KEY];
    auto &vector2 = (*vectors2.mutable_vectors())[uuid];
    for (int i=0; i<8; i++) {
        vector2.mutable_values()->Add(0);
    }

    auto &vectors3 = (*categories)[resource_view::HETEROGENEOUS_STREAM_KEY];
    auto &vector3 = (*vectors3.mutable_vectors())[uuid];
    for (int i=0; i<8; i++) {
        vector3.mutable_values()->Add(100);
    }

    auto &vectors4 = (*categories)[resource_view::IDS_KEY];
    auto &vector4 = (*vectors4.mutable_vectors())[uuid];
    for (int i=0; i<8; i++) {
        vector4.mutable_values()->Add(i+100);
    }

    auto &vectors5 = (*categories)[resource_view::HEALTH_KEY];
    auto &vector5 = (*vectors5.mutable_vectors())[uuid];
    for (int i=0; i<8; i++) {
        vector5.mutable_values()->Add(0);
    }

    auto heterogeneousInfo = r.mutable_heterogeneousinfo();
    std::string ips = "0.0.0.0,0.0.0.1,0.0.0.2,0.0.0.3,"
                      "0.0.0.4,0.0.0.5,0.0.0.6,0.0.0.7";
    (*heterogeneousInfo)[resource_view::DEV_CLUSTER_IPS_KEY] = ips;
    return r;
}

inline resource_view::Resources GetCpuMemNpuResources(const std::string cardType = DEFAULT_NPU_TYPE)
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[GetCpuResource().name()] = GetCpuResource();
    (*rs.mutable_resources())[GetMemResource().name()] = GetMemResource();
    (*rs.mutable_resources())[cardType] = GetNpuResource(cardType);
    return rs;
}

inline resource_view::Resources GetCpuMemNpuResourcesWithValue(double cpu, double mem,
                                                               const std::string cardType = DEFAULT_NPU_TYPE)
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[RESOURCE_CPU_NAME] = GetNameResourceWithValue(RESOURCE_CPU_NAME, cpu);
    (*rs.mutable_resources())[RESOURCE_MEM_NAME] = GetNameResourceWithValue(RESOURCE_MEM_NAME, mem);
    (*rs.mutable_resources())[cardType] = GetNpuResource(cardType);
    return rs;
}


inline resource_view::Resource GetNpuResourceWithSpecificNpuNumber(const std::vector<double> &usage,
                                                                   const std::vector<double> &latency =
                                                                       { 0,0,0,0,0,0,0,0 },
                                                                   const std::vector<double> &stream =
                                                                       {100,100,100,100,100,100,100,100},
                                                                   const std::string cardType = DEFAULT_NPU_TYPE,
                                                                   const std::string &uuid = "")
{
    resource_view::Resource r;
    r.set_name(cardType);
    r.set_type(resource_view::ValueType::Value_Type_VECTORS);
    std::string myUUID;
    if (uuid.empty()) {
        myUUID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    }else {
        myUUID = uuid;
    }
    auto categories = r.mutable_vectors()->mutable_values();

    auto &vectors1 = (*categories)[resource_view::HETEROGENEOUS_MEM_KEY];
    auto &vector1 = (*vectors1.mutable_vectors())[myUUID];
    for (double i : usage) {
        vector1.mutable_values()->Add(i);
    }
    auto &vectors2 = (*categories)[resource_view::HETEROGENEOUS_LATENCY_KEY];
    auto &vector2 = (*vectors2.mutable_vectors())[myUUID];
    for (double i : latency) {
        vector2.mutable_values()->Add(i);
    }

    auto &vectors3 = (*categories)[resource_view::HETEROGENEOUS_STREAM_KEY];
    auto &vector3 = (*vectors3.mutable_vectors())[myUUID];
    for (double i : stream) {
        vector3.mutable_values()->Add(i);
    }

    auto &vectors4 = (*categories)[resource_view::IDS_KEY];
    auto &vector4 = (*vectors4.mutable_vectors())[myUUID];
    for (int i=0; i<8; i++) {
        vector4.mutable_values()->Add(i+100);
    }

    auto &vectors5 = (*categories)[resource_view::HEALTH_KEY];
    auto &vector5 = (*vectors5.mutable_vectors())[myUUID];
    for (int i=0; i<8; i++) {
        vector5.mutable_values()->Add(0);
    }

    auto heterogeneousInfo = r.mutable_heterogeneousinfo();
    std::string ips = "0.0.0.0,0.0.0.1,0.0.0.2,0.0.0.3,"
                      "0.0.0.4,0.0.0.5,0.0.0.6,0.0.0.7";
    (*heterogeneousInfo)[resource_view::DEV_CLUSTER_IPS_KEY] = ips;
    return r;
}

inline resource_view::Resources GetCpuMemNpuResourcesWithSpecificNpuNumber(const std::vector<double> &usage,
                                                                           const std::vector<double> &latency =
                                                                               { 0,0,0,0,0,0,0,0 },
                                                                           const std::vector<double> &stream =
                                                                               {100,100,100,100,100,100,100,100},
                                                                           const std::string cardType = DEFAULT_NPU_TYPE,
                                                                           const std::string &uuid = "")
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[GetCpuResource().name()] = GetCpuResource();
    (*rs.mutable_resources())[GetMemResource().name()] = GetMemResource();
    (*rs.mutable_resources())[cardType] = GetNpuResourceWithSpecificNpuNumber(usage, latency, stream, cardType, uuid);
    return rs;
}
inline resource_view::ResourceUnit Get1DResourceUnitWithSpecificNpuNumber(const std::vector<double> &usage,
                                                                          const std::string cardType = DEFAULT_NPU_TYPE)
{
    resource_view::ResourceUnit unit;
    auto id = "Test_ResID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit.set_id(id);
    (*unit.mutable_capacity()) = GetCpuMemNpuResources(cardType);
    auto key = (*unit.mutable_capacity()->mutable_resources())[cardType].vectors().values().
               at(resource_view::HETEROGENEOUS_MEM_KEY).vectors().begin()->first;
    (*unit.mutable_allocatable()) = GetCpuMemNpuResourcesWithSpecificNpuNumber(usage,
                                                                               { 0,0,0,0,0,0,0,0 },
                                                                               {100,100,100,100,100,100,100,100},
                                                                               cardType, key);
    (*unit.mutable_actualuse()) = GetCpuMemNpuResourcesWithSpecificNpuNumber({ 0,0,0,0,0,0,0,0 },
                                                                             { 0,0,0,0,0,0,0,0 },
                                                                             {100,100,100,100,100,100,100,100},
                                                                             cardType, key);
    return unit;
}

inline resource_view::ResourceUnit Get1DResourceUnitWithSpecificNpuNumber(const std::vector<double> &hbm,
                                                                          const std::vector<double> &latency,
                                                                          const std::vector<double> &stream,
                                                                          const std::string cardType = DEFAULT_NPU_TYPE)
{
    resource_view::ResourceUnit unit;
    auto id = "Test_ResID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit.set_id(id);
    (*unit.mutable_capacity()) = GetCpuMemNpuResources(cardType);
    auto key = (*unit.mutable_capacity()->mutable_resources())[cardType].vectors().values().
               at(resource_view::HETEROGENEOUS_MEM_KEY).vectors().begin()->first;
    (*unit.mutable_allocatable()) = GetCpuMemNpuResourcesWithSpecificNpuNumber(hbm,
                                                                               latency,
                                                                               stream,
                                                                               cardType, key);
    (*unit.mutable_actualuse()) = GetCpuMemNpuResourcesWithSpecificNpuNumber({ 0,0,0,0,0,0,0,0 },
                                                                             { 0,0,0,0,0,0,0,0 },
                                                                             {100,100,100,100,100,100,100,100},
                                                                             cardType, key);
    return unit;
}

inline resource_view::ResourceUnit Get1DResourceUnitWithNpu(const std::string cardType = DEFAULT_NPU_TYPE)
{
    resource_view::ResourceUnit unit;
    auto id = "Test_ResID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit.set_id(id);
    auto rs = GetCpuMemNpuResources(cardType);
    (*unit.mutable_capacity()).CopyFrom(rs);
    (*unit.mutable_allocatable()).CopyFrom(rs);
    (*unit.mutable_actualuse()) = GetCpuMemNpuResourcesWithSpecificNpuNumber({ 0,0,0,0,0,0,0,0 },
                                                                             { 0,0,0,0,0,0,0,0 },
                                                                             {100,100,100,100,100,100,100,100},
                                                                             cardType);
    return unit;
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

inline resource_view::Resource GetDiskResource(
    const std::vector<int> vectorValue = { 100, 100, 100 },
    std::string nodeid = litebus::uuid_generator::UUID::GetRandomUUID().ToString())
{
    resource_view::Resource r;
    r.set_name(resource_view::DISK_RESOURCE_NAME);
    r.set_type(resource_view::ValueType::Value_Type_VECTORS);

    auto categories = r.mutable_vectors()->mutable_values();

    auto &vectors1 = (*categories)[resource_view::DISK_RESOURCE_NAME];
    auto &vector1 = (*vectors1.mutable_vectors())[nodeid];
    for (int i = 0; i < static_cast<int>(vectorValue.size()) ;i++) {
        auto value = vectorValue[i];
        vector1.mutable_values()->Add(value);
        resource_view::ResourceExtension extension;
        extension.mutable_disk()->set_size(value);
        extension.mutable_disk()->set_name("disk"+ std::to_string(i));
        extension.mutable_disk()->set_mountpoints("/tmp/abc" + std::to_string(i) + "/");

        (*r.mutable_extensions()->Add()) = extension;
    }
    return r;
}

inline resource_view::Resources GetDiskResources(
    const std::vector<int> vectorValue = { 100, 100, 100 },
    std::string nodeid = litebus::uuid_generator::UUID::GetRandomUUID().ToString())
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[resource_view::DISK_RESOURCE_NAME] = GetDiskResource(vectorValue, nodeid);
    return rs;
}


inline resource_view::ResourceUnit Get1DResourceUnitWithDisk(
    const std::vector<int> vectorValue = { 100, 100, 100 },
    std::string nodeid = litebus::uuid_generator::UUID::GetRandomUUID().ToString())
{
    resource_view::ResourceUnit unit;
    auto id = "Test_ResID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit.set_id(id);
    (*unit.mutable_capacity()) = GetDiskResources(vectorValue, nodeid);
    (*unit.mutable_allocatable()) = GetDiskResources(vectorValue, nodeid);
    std::vector<int> actualuse(vectorValue.size(), 0);
    (*unit.mutable_actualuse()) = GetDiskResources(actualuse, nodeid);
    return unit;
}

inline resource_view::Resources GetCpuMemWithOtherEmptyResources()
{
    resource_view::Resources rs;
    (*rs.mutable_resources())[GetCpuResource().name()] = GetCpuResource();
    (*rs.mutable_resources())[GetMemResource().name()] = GetMemResource();
    (*rs.mutable_resources())["OtherResource"] = Get0Resource("OtherResource");
    return rs;
}

inline resource_view::InstanceInfo GetInstanceWithResourceAndPriority(int32_t priority,
                                                                      double cpu, double memory)
{
    resource_view::InstanceInfo inst;
    auto id = "Test_InstID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto id2 = "Test_ReqID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    inst.set_instanceid(id);
    inst.set_requestid(id2);
    inst.mutable_scheduleoption()->set_priority(priority);
    inst.mutable_scheduleoption()->set_preemptedallowed(true);
    resource_view::Resources rs = view_utils::GetCpuMemResources();
    rs.mutable_resources()->at(view_utils::RESOURCE_CPU_NAME).mutable_scalar()->set_value(cpu);
    rs.mutable_resources()->at(view_utils::RESOURCE_MEM_NAME).mutable_scalar()->set_value(memory);
    (*inst.mutable_resources()) = rs;
    (*inst.mutable_actualuse()) = rs;
    return inst;
}

inline resource_view::InstanceInfo Get1DInstance()
{
    return GetInstanceWithResourceAndPriority(0, INST_SCALA_VALUE, INST_SCALA_VALUE);
}

inline resource_view::InstanceInfo Get1DInstanceWithDiskResource(int diskSize = 100)
{
    resource_view::InstanceInfo ins = view_utils::Get1DInstance();
    (*ins.mutable_resources()->mutable_resources())[resource_view::DISK_RESOURCE_NAME]
        .mutable_scalar()->set_value(diskSize);
    return ins;
}

inline resource_view::InstanceInfo Get1DInstanceWithNpuResource(int hbm, int latency, int stream,
                                                                std::string cardType = DEFAULT_NPU_TYPE)
{
    resource_view::InstanceInfo ins = view_utils::Get1DInstance();
    auto resKeyPre = cardType;
    auto resKeyHbm = resKeyPre + "/" + resource_view::HETEROGENEOUS_MEM_KEY;
    (*ins.mutable_resources()->mutable_resources())[resKeyHbm].mutable_scalar()->set_value(hbm);
    (*ins.mutable_resources()->mutable_resources())[resKeyHbm].set_name(resKeyHbm);
    (*ins.mutable_resources()->mutable_resources())[resKeyHbm].set_type(resources::Value_Type_SCALAR);
    auto resKeyLatency = resKeyPre + "/" + resource_view::HETEROGENEOUS_LATENCY_KEY;
    (*ins.mutable_resources()->mutable_resources())[resKeyLatency].mutable_scalar()->set_value(latency);
    (*ins.mutable_resources()->mutable_resources())[resKeyLatency].set_name(resKeyLatency);
    (*ins.mutable_resources()->mutable_resources())[resKeyLatency].set_type(resources::Value_Type_SCALAR);
    auto resKeyStream = resKeyPre + "/" + resource_view::HETEROGENEOUS_STREAM_KEY;
    (*ins.mutable_resources()->mutable_resources())[resKeyStream].mutable_scalar()->set_value(stream);
    (*ins.mutable_resources()->mutable_resources())[resKeyStream].set_name(resKeyStream);
    (*ins.mutable_resources()->mutable_resources())[resKeyStream].set_type(resources::Value_Type_SCALAR);
    return ins;
}

inline resource_view::InstanceInfo Get1DInstanceWithNpuResource(double cardNum, std::string cardType = DEFAULT_NPU_TYPE)
{
    resource_view::InstanceInfo ins = view_utils::Get1DInstance();
    auto resKeyPre = cardType;
    auto resKey = resKeyPre + "/" + resource_view::HETEROGENEOUS_CARDNUM_KEY;
    (*ins.mutable_resources()->mutable_resources())[resKey].mutable_scalar()->set_value(cardNum);
    (*ins.mutable_resources()->mutable_resources())[resKey].set_name(resKey);
    (*ins.mutable_resources()->mutable_resources())[resKey].set_type(resources::Value_Type_SCALAR);
    return ins;
}

inline resource_view::ResourceUnit Get1DResourceUnit()
{
    resource_view::ResourceUnit unit;
    auto id = "Test_ResID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit.set_id(id);
    (*unit.mutable_capacity()) = GetCpuMemResources();
    (*unit.mutable_allocatable()) = GetCpuMemResources();
    (*unit.mutable_actualuse()) = Get0CpuMemResources();

    return unit;
}

inline resource_view::ResourceUnit Get1DResourceUnit(const std::string &id)
{
    resource_view::ResourceUnit unit;
    unit.set_id(id);
    (*unit.mutable_capacity()) = GetCpuMemResources();
    (*unit.mutable_allocatable()) = GetCpuMemResources();
    (*unit.mutable_actualuse()) = Get0CpuMemResources();
    return unit;
}

inline resource_view::ResourceUnitChanges Get1DResourceUnitChanges()
{
    auto unit = Get1DResourceUnit();
    resource_view::Addition addition;
    (*addition.mutable_resourceunit()) = unit;

    resource_view::ResourceUnitChange change;
    change.set_resourceunitid(unit.id());
    *change.mutable_addition() = addition;

    resource_view::ResourceUnitChanges changes;
    *changes.add_changes() = change;
    changes.set_startrevision(0);
    changes.set_endrevision(1);
    return changes;
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

inline affinity::SubCondition GetEmptySelector()
{
    affinity::SubCondition subCondition;
    return subCondition;
}

}  // namespace functionsystem::test::view_utils
#endif  // UNIT_COMMON_RESOURCE_VIEW_RESOURCE_TEST_H
