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

#include "common/resource_view/resource_tool.h"

#include <gtest/gtest.h>

#include <iostream>

#include "common/resource_view/resource_view.h"
#include "common/resource_view/scala_resource_tool.h"
#include "view_utils.h"

using namespace functionsystem::resource_view;
using namespace functionsystem::test::view_utils;

namespace functionsystem::test {

class ResourceToolTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
    }

    void SetUp()
    {
    }

    void TearDown()
    {
    }

private:
};

TEST_F(ResourceToolTest, ScalaValueValidate)
{
    auto r = GetCpuResource();
    EXPECT_TRUE(ScalaValueValidate(r));

    r.mutable_scalar()->set_value(-1);
    EXPECT_FALSE(ScalaValueValidate(r));

    r.clear_scalar();
    EXPECT_FALSE(ScalaValueValidate(r));
}

TEST_F(ResourceToolTest, ScalaValueIsEmpty)
{
    auto r = GetCpuResource();
    EXPECT_FALSE(ScalaValueIsEmpty(r));

    r.mutable_scalar()->set_value(0);
    EXPECT_TRUE(ScalaValueIsEmpty(r));

    r.clear_scalar();
    EXPECT_TRUE(ScalaValueIsEmpty(r));
}

TEST_F(ResourceToolTest, ScalaValueIsEqual)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    EXPECT_TRUE(ScalaValueIsEqual(r1, r2));

    r1.mutable_scalar()->set_value(0);
    EXPECT_FALSE(ScalaValueIsEqual(r1, r2));
}

TEST_F(ResourceToolTest, ScalaValueAdd)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    auto r3 = ScalaValueAdd(r1, r2);
    EXPECT_TRUE(r3.scalar().value() == (SCALA_VALUE1 * 2));
    EXPECT_TRUE(IsValid(r3));
    EXPECT_EQ(r3.name(), r1.name());
    EXPECT_EQ(r3.type(), r1.type());

    r1.mutable_scalar()->set_value(-1);
    auto r4 = ScalaValueAdd(r1, r2);
    EXPECT_TRUE(r4.scalar().value() == (SCALA_VALUE1 - 1.0));
}

TEST_F(ResourceToolTest, ScalaValueSub)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    auto r3 = ScalaValueSub(r1, r2);
    EXPECT_TRUE(r3.scalar().value() == 0);
    EXPECT_TRUE(IsValid(r3));
    EXPECT_EQ(r3.name(), r1.name());
    EXPECT_EQ(r3.type(), r1.type());

    r2.mutable_scalar()->set_value(10);
    auto r4 = ScalaValueSub(r1, r2);
    EXPECT_TRUE(r4.scalar().value() == (SCALA_VALUE1 - 10));

    r2.mutable_scalar()->set_value(200);
    auto r5 = ScalaValueSub(r1, r2);
    EXPECT_TRUE(r5.scalar().value() == (SCALA_VALUE1 - 200));
}

TEST_F(ResourceToolTest, ScalaValueLess)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    EXPECT_FALSE(ScalaValueLess(r1, r2));

    r1.mutable_scalar()->set_value(1);
    EXPECT_TRUE(ScalaValueLess(r1, r2));

    r1.mutable_scalar()->set_value(200);
    EXPECT_FALSE(ScalaValueLess(r1, r2));

    r1.mutable_scalar()->set_value(-1);
    EXPECT_TRUE(ScalaValueLess(r1, r2));
}

TEST_F(ResourceToolTest, IsValidType)
{
    auto r1 = GetCpuResource();
    EXPECT_TRUE(IsValidType(r1));

    r1.set_type(ValueType::Value_Type_SET);
    EXPECT_TRUE(IsValidType(r1));

    r1.set_type(ValueType::Value_Type_END);
    EXPECT_FALSE(IsValidType(r1));
}

TEST_F(ResourceToolTest, IsValid)
{
    auto r1 = GetCpuResource();
    EXPECT_TRUE(IsValid(r1));

    Resource r2 = r1;
    r2.clear_name();
    EXPECT_FALSE(IsValid(r2));

    Resource r3 = r1;
    r3.clear_type();
    EXPECT_EQ(r3.type(), ValueType::Value_Type_SCALAR);
    EXPECT_TRUE(IsValid(r3));

    Resource r4 = r1;
    r4.mutable_scalar()->set_value(-1.0);
    EXPECT_FALSE(IsValid(r4));

    Resource r5 = r1;
    r5.clear_scalar();
    EXPECT_FALSE(IsValid(r4));
}

TEST_F(ResourceToolTest, IsEmpty)
{
    auto r1 = GetCpuResource();
    EXPECT_FALSE(IsEmpty(r1));

    Resource r4 = r1;
    r4.mutable_scalar()->set_value(0);
    EXPECT_TRUE(IsEmpty(r4));
}

TEST_F(ResourceToolTest, IsValids)
{
    auto r = GetCpuMemResources();
    EXPECT_TRUE(IsValid(r));

    r.mutable_resources()->at(RESOURCE_CPU_NAME).clear_name();
    EXPECT_FALSE(IsValid(r));

    r.mutable_resources()->at(RESOURCE_CPU_NAME).set_type(ValueType::Value_Type_SCALAR);
    r.mutable_resources()->at(RESOURCE_CPU_NAME).mutable_scalar()->set_value(-1);
    EXPECT_FALSE(IsValid(r));

    r.mutable_resources()->at(RESOURCE_CPU_NAME).clear_scalar();
    EXPECT_FALSE(IsValid(r));

    r.clear_resources();
    EXPECT_FALSE(IsValid(r));
}

TEST_F(ResourceToolTest, IsEmptys)
{
    auto r = GetCpuMemResources();
    EXPECT_FALSE(IsEmpty(r));

    r.mutable_resources()->at(RESOURCE_CPU_NAME).mutable_scalar()->set_value(0);
    r.mutable_resources()->at(RESOURCE_MEM_NAME).mutable_scalar()->set_value(0);
    EXPECT_TRUE(IsEmpty(r));

    r.mutable_resources()->erase(RESOURCE_CPU_NAME);
    r.mutable_resources()->at(RESOURCE_MEM_NAME).mutable_scalar()->set_value(1.0);
    EXPECT_FALSE(IsEmpty(r));
}

TEST_F(ResourceToolTest, LessEqual)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    EXPECT_TRUE(r1 <= r2);

    r1.mutable_scalar()->set_value(100.0);
    EXPECT_TRUE(r1 <= r2);

    r1.mutable_scalar()->set_value(200.1);
    EXPECT_FALSE(r1 <= r2);
}

TEST_F(ResourceToolTest, IsVectorValid)
{
    auto r1 = GetNpuResourceWithSpecificNpuNumber({20,20,20,20,20,20,20,20});
    EXPECT_TRUE(IsValid(r1));

    Resource r2 = r1;
    r2.clear_name();
    EXPECT_FALSE(IsValid(r2));

    Resource r3 = r1;
    r3.clear_type();
    EXPECT_FALSE(IsValid(r3));

    Resource r4 = GetNpuResourceWithSpecificNpuNumber({-20,20,20,20,20,20,20,20});
    EXPECT_TRUE(IsValid(r4));
}

TEST_F(ResourceToolTest, VectorAdd)
{
    std::string uuid = "uuid";
    auto r1 = GetNpuResourceWithSpecificNpuNumber({20,20,20,20,20,20,20,20}, {0}, {0}, "NPU/310", uuid);
    auto r2 =GetNpuResourceWithSpecificNpuNumber({20,20,20,20,20,20,20,20}, {0}, {0}, "NPU/310", uuid);
    auto r3 = r1 + r2;

    auto res1 = r3.vectors().values()
                    .at(resource_view::HETEROGENEOUS_MEM_KEY).vectors().begin()->second.values();
    for (int i=0; i<8; i++) {
        EXPECT_EQ(res1.at(i), 40);
    }
}

TEST_F(ResourceToolTest, VectorEqual)
{
    std::string uuid = "uuid";
    auto r1 = GetNpuResourceWithSpecificNpuNumber({20,20,20,20,20,20,20,20}, {0}, {0}, "NPU/310", uuid);
    auto r2 =GetNpuResourceWithSpecificNpuNumber({20,20,20,20,20,20,20,20}, {0}, {0}, "NPU/310", uuid);
    EXPECT_TRUE(r1 == r2);

    r2 = GetNpuResourceWithSpecificNpuNumber({100,20,20,20,20,20,20,20}, {0}, {0}, "NPU/310", uuid);
    EXPECT_FALSE(r1 == r2);
    r2 = GetNpuResourceWithSpecificNpuNumber({100,20,20,20,20,20,20,20});
    EXPECT_FALSE(r1 == r2);
}

TEST_F(ResourceToolTest, VectorNotEqual)
{
    std::string uuid = "uuid";
    auto r1 = GetNpuResourceWithSpecificNpuNumber({20,20,20,20,20,20,20,20}, {0}, {0}, "NPU/310", uuid);
    auto r2 =GetNpuResourceWithSpecificNpuNumber({20,20,20,20,20,20,20,20}, {0}, {0}, "NPU/310", uuid);
    EXPECT_FALSE(r1 != r2);

    r2 =GetNpuResourceWithSpecificNpuNumber({100,20,20,20,20,0,20,20}, {0}, {0}, "NPU/310", uuid);
    EXPECT_TRUE(r1 != r2);
}

TEST_F(ResourceToolTest, VectorSub)
{
    std::string uuid = "uuid";
    auto r1 = GetNpuResourceWithSpecificNpuNumber({30,30,30,40,50,30,20,60}, {0}, {0}, "NPU/310", uuid);
    auto r2 =GetNpuResourceWithSpecificNpuNumber({20,20,20,20,20,20,20,20}, {0}, {0}, "NPU/310", uuid);
    auto r3 = r1 - r2;
    auto res1 = r3.vectors().values()
                    .at(resource_view::HETEROGENEOUS_MEM_KEY).vectors().begin()->second.values();

    EXPECT_EQ(res1.at(0), 10);
    EXPECT_EQ(res1.at(3), 20);
    EXPECT_EQ(res1.at(4), 30);
    EXPECT_EQ(res1.at(6), 0);

    r1 = GetNpuResourceWithSpecificNpuNumber({100,100,100,100,100,100,100,100}, {0}, {0}, "NPU/310", uuid);

    auto r4 = r2 - r1;
    auto res2 = r4.vectors().values()
                    .at(resource_view::HETEROGENEOUS_MEM_KEY).vectors().begin()->second.values();
    EXPECT_EQ(res2.at(0), -80);
    EXPECT_EQ(res2.at(3), -80);
    EXPECT_EQ(res2.at(4), -80);
    EXPECT_EQ(res2.at(6), -80);
}

TEST_F(ResourceToolTest, Equal)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    EXPECT_TRUE(r1 == r2);

    r1.mutable_scalar()->set_value(100);
    EXPECT_FALSE(r1 == r2);

    r2.mutable_scalar()->set_value(0);
    EXPECT_FALSE(r1 == r2);
}

TEST_F(ResourceToolTest, NotEqual)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    EXPECT_FALSE(r1 != r2);

    r1.mutable_scalar()->set_value(100);
    EXPECT_TRUE(r1 != r2);

    r2.mutable_scalar()->set_value(0);
    EXPECT_TRUE(r1 != r2);
}

TEST_F(ResourceToolTest, Add)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    EXPECT_EQ((r1 + r2).scalar().value(), SCALA_VALUE1 * 2);
}

TEST_F(ResourceToolTest, Sub)
{
    auto r1 = GetCpuResource();
    auto r2 = GetCpuResource();
    EXPECT_EQ((r1 - r2).scalar().value(), 0);

    r2.mutable_scalar()->set_value(200);
    EXPECT_EQ((r1 - r2).scalar().value(), SCALA_VALUE1 - 200);
}

TEST_F(ResourceToolTest, Lesss)
{
    auto r1 = GetCpuMemResources();
    auto r2 = GetCpuMemResources();
    EXPECT_TRUE(r1 <= r2);

    r2.mutable_resources()->at(RESOURCE_CPU_NAME).mutable_scalar()->set_value(200);
    EXPECT_TRUE(r1 <= r2);

    r2.mutable_resources()->at(RESOURCE_CPU_NAME).mutable_scalar()->set_value(100);
    EXPECT_FALSE(r1 <= r2);

    r1.mutable_resources()->erase(RESOURCE_CPU_NAME);
    EXPECT_TRUE(r1 <= r2);
    EXPECT_FALSE(r2 <= r1);
}

TEST_F(ResourceToolTest, Equals)
{
    auto r1 = GetCpuMemResources();
    auto r2 = GetCpuMemResources();
    EXPECT_TRUE(r1 == r2);

    r2.mutable_resources()->at(RESOURCE_CPU_NAME).mutable_scalar()->set_value(200);
    EXPECT_FALSE(r1 == r2);

    r1.mutable_resources()->erase(RESOURCE_CPU_NAME);
    EXPECT_FALSE(r1 == r2);
    EXPECT_FALSE(r2 == r1);
}

TEST_F(ResourceToolTest, NotEquals)
{
    auto r1 = GetCpuMemResources();
    auto r2 = GetCpuMemResources();
    EXPECT_FALSE(r1 != r2);

    r2.mutable_resources()->at(RESOURCE_CPU_NAME).mutable_scalar()->set_value(200);
    EXPECT_TRUE(r1 != r2);

    r1.mutable_resources()->erase(RESOURCE_CPU_NAME);
    EXPECT_TRUE(r1 != r2);
    EXPECT_TRUE(r2 != r1);
}

TEST_F(ResourceToolTest, Adds)
{
    auto r1 = GetCpuMemResources();
    auto r2 = GetCpuMemResources();
    auto r3 = r1 + r2;
    EXPECT_EQ(r3.resources().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(r3.resources().at(RESOURCE_CPU_NAME).scalar().value(), (SCALA_VALUE1 * 2));
    EXPECT_EQ(r3.resources().at(RESOURCE_MEM_NAME).scalar().value(), (SCALA_VALUE1 * 2));
    EXPECT_TRUE(IsValid(r3));
}

TEST_F(ResourceToolTest, Subs)
{
    auto r1 = GetCpuMemResources();
    auto r2 = GetCpuMemResources();
    auto r3 = r1 - r2;
    EXPECT_EQ(r3.resources().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(r3.resources().at(RESOURCE_CPU_NAME).scalar().value(), 0);
    EXPECT_EQ(r3.resources().at(RESOURCE_MEM_NAME).scalar().value(), 0);
    EXPECT_TRUE(IsValid(r3));

    r2.mutable_resources()->at(RESOURCE_CPU_NAME).mutable_scalar()->set_value(200);
    auto r4 = r1 - r2;
    EXPECT_EQ(r4.resources().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(r4.resources().at(RESOURCE_CPU_NAME).scalar().value(), SCALA_VALUE1 - 200);
    EXPECT_EQ(r4.resources().at(RESOURCE_MEM_NAME).scalar().value(), 0);
    EXPECT_FALSE(IsValid(r4));
}

TEST_F(ResourceToolTest, ScalaResource2StringSuccess)
{
    auto res = GetCpuResource();
    auto ret = ScalaValueToString(res);

    EXPECT_EQ(ret, CPU_SCALA_RESOURCE_STRING);
}

TEST_F(ResourceToolTest, ScalaResource2StringSuccess2)
{
    auto res = GetCpuResource();
    auto ret = ToString(res);

    EXPECT_EQ(ret, CPU_SCALA_RESOURCE_STRING);
}

TEST_F(ResourceToolTest, ScalaResources2StringSuccess)
{
    auto res = GetCpuResource();
    Resources resources;
    (*resources.mutable_resources())[RESOURCE_CPU_NAME] = res;
    auto ret = ToString(resources);

    EXPECT_EQ(ret, CPU_SCALA_RESOURCES_STRING);
}

resources::Value::Counter GetSimpleCounter(std::unordered_map<std::string, uint64_t> kvs)
{
    resources::Value::Counter cnter;
    for (auto &it : kvs) {
        (*cnter.mutable_items())[it.first] = it.second;
    }
    return cnter;
}

// Add two counters will works like, the order doesn't matter
//   {"A": 3, "B": 2        , "D": 1}
// + {"A": 1,         "C": 4, "D": 1}
// = {"A": 4, "B": 2, "C": 4, "D": 2}
TEST_F(ResourceToolTest, CounterAdd)
{
    auto cnt1 = GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "A", 3 }, { "B", 2 }, { "D", 1 } });
    auto cnt2 = GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "A", 1 }, { "C", 4 }, { "D", 1 } });

    auto sum = cnt1 + cnt2;
    EXPECT_EQ(sum.items().size(), static_cast<uint32_t>(4));
    EXPECT_TRUE(sum.items().contains("A"));
    EXPECT_EQ(sum.items().at("A"), static_cast<uint64_t>(4));
    EXPECT_TRUE(sum.items().contains("B"));
    EXPECT_EQ(sum.items().at("B"), static_cast<uint64_t>(2));
    EXPECT_TRUE(sum.items().contains("C"));
    EXPECT_EQ(sum.items().at("C"), static_cast<uint64_t>(4));
    EXPECT_TRUE(sum.items().contains("D"));
    EXPECT_EQ(sum.items().at("D"), static_cast<uint64_t>(2));
}

// Sub two counters will works like
//   {"A": 3, "B": 2,         "D": 2}
// - {"A": 1,         "C": 4, "D": 2}
// = {"A": 2, "B": 2,               }
TEST_F(ResourceToolTest, CounterSub)
{
    auto cnt1 = GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "A", 3 }, { "B", 2 }, { "D", 2 } });
    auto cnt2 = GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "A", 1 }, { "C", 4 }, { "D", 2 } });

    auto sum = cnt1 - cnt2;
    EXPECT_EQ(sum.items().size(), static_cast<uint32_t>(2));
    EXPECT_TRUE(sum.items().contains("A"));
    EXPECT_EQ(sum.items().at("A"), static_cast<uint64_t>(2));
    EXPECT_TRUE(sum.items().contains("B"));
    EXPECT_EQ(sum.items().at("B"), static_cast<uint64_t>(2));
}

MapCounter GetSimpleMapCounter(std::unordered_map<std::string, resources::Value::Counter> kvs)
{
    MapCounter mc;
    for (auto &it : kvs) {
        mc[it.first] = it.second;
    }
    return mc;
}

// add 3 agents: {x:{y:1}}, {x:{z:1}}, {x:{z:1}}
// expected sum: {x:{y:1,z:2}}
TEST_F(ResourceToolTest, MapCounterAdd)
{
    // 3 agents: {x:{y:1}}, {x:{z:1}}, {x:{z:1}}
    auto mc1 = GetSimpleMapCounter(std::unordered_map<std::string, resources::Value::Counter>{
        { "x", GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "y", 1 } }) } });
    auto mc2 = GetSimpleMapCounter(std::unordered_map<std::string, resources::Value::Counter>{
        { "x", GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "z", 1 } }) } });
    auto mc3 = GetSimpleMapCounter(std::unordered_map<std::string, resources::Value::Counter>{
        { "x", GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "z", 1 } }) } });
    
    // expected sum: {x:{y:1,z:2}}
    auto sum = mc1 + mc2 + mc3;
    EXPECT_EQ(sum.size(), static_cast<uint32_t>(1));
    EXPECT_TRUE(sum.contains("x"));
    EXPECT_EQ(sum.at("x").items().size(), static_cast<uint32_t>(2));
    EXPECT_TRUE(sum.at("x").items().contains("y"));
    EXPECT_TRUE(sum.at("x").items().contains("z"));
    EXPECT_EQ(sum.at("x").items().at("y"), static_cast<uint64_t>(1));
    EXPECT_EQ(sum.at("x").items().at("z"), static_cast<uint64_t>(2));
}

//   { x : { y : 1 , z : 2 } }
// - { x : { y : 1 , z : 1 } }
// = { x : {         z : 1 } }
TEST_F(ResourceToolTest, MapCounterSub)
{
    auto mc1 = GetSimpleMapCounter(std::unordered_map<std::string, resources::Value::Counter>{
        { "x", GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "y", 1 }, { "z", 2 } }) } });
    auto mc2 = GetSimpleMapCounter(std::unordered_map<std::string, resources::Value::Counter>{
        { "x", GetSimpleCounter(std::unordered_map<std::string, uint64_t>{ { "y", 1 }, { "z", 1 } }) } });
    
    auto sub = mc1 - mc2;
    EXPECT_EQ(sub.size(), static_cast<uint32_t>(1));
    EXPECT_TRUE(sub.contains("x"));
    EXPECT_EQ(sub.at("x").items().size(), static_cast<uint32_t>(1));
    EXPECT_FALSE(sub.at("x").items().contains("y"));
    EXPECT_TRUE(sub.at("x").items().contains("z"));
    EXPECT_EQ(sub.at("x").items().at("z"), static_cast<uint64_t>(1));
}

TEST_F(ResourceToolTest, HasHeterogeneousResourceTest)
{
    resource_view::InstanceInfo instance;
    EXPECT_FALSE(HasHeterogeneousResource(instance));

    (*instance.mutable_resources()->mutable_resources())["123/123/123"] = GetResource("123/123/123");
    EXPECT_TRUE(HasHeterogeneousResource(instance));
}

TEST_F(ResourceToolTest, HasInstanceAffinityTest)
{
    resource_view::InstanceInfo instance;
    EXPECT_FALSE(HasInstanceAffinity(instance));

    auto instanceAffinity = instance.mutable_scheduleoption()->mutable_affinity()->mutable_instance();
    instanceAffinity->mutable_preferredaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasInstanceAffinity(instance));

    instanceAffinity->clear_preferredaffinity();
    EXPECT_FALSE(HasInstanceAffinity(instance));

    instanceAffinity->mutable_preferredantiaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasInstanceAffinity(instance));

    instanceAffinity->clear_preferredantiaffinity();
    EXPECT_FALSE(HasInstanceAffinity(instance));

    instanceAffinity->mutable_requiredantiaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasInstanceAffinity(instance));

    instanceAffinity->clear_requiredantiaffinity();
    EXPECT_FALSE(HasInstanceAffinity(instance));

    instanceAffinity->mutable_requiredaffinity()->mutable_condition()->mutable_subconditions()->Add(GetEmptySelector());
    EXPECT_TRUE(HasInstanceAffinity(instance));
}

TEST_F(ResourceToolTest, HasResourceAffinityTest)
{
    resource_view::InstanceInfo instance;
    EXPECT_FALSE(HasResourceAffinity(instance));

    auto resourceAffinity = instance.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
    resourceAffinity->mutable_preferredaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasResourceAffinity(instance));

    resourceAffinity->clear_preferredaffinity();
    EXPECT_FALSE(HasResourceAffinity(instance));

    resourceAffinity->mutable_preferredantiaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasResourceAffinity(instance));

    resourceAffinity->clear_preferredantiaffinity();
    EXPECT_FALSE(HasResourceAffinity(instance));

    resourceAffinity->mutable_requiredantiaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasResourceAffinity(instance));

    resourceAffinity->clear_requiredantiaffinity();
    EXPECT_FALSE(HasResourceAffinity(instance));

    resourceAffinity->mutable_requiredaffinity()->mutable_condition()->mutable_subconditions()->Add(GetEmptySelector());
    EXPECT_TRUE(HasResourceAffinity(instance));
}

TEST_F(ResourceToolTest, HasInnerAffinityTest)
{
    resource_view::InstanceInfo instance;
    EXPECT_FALSE(HasInnerAffinity(instance));

    auto innerAffinity = instance.mutable_scheduleoption()->mutable_affinity()->mutable_inner();
    innerAffinity->mutable_data()->mutable_preferredaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasInnerAffinity(instance));

    innerAffinity->clear_data();
    EXPECT_FALSE(HasInnerAffinity(instance));

    innerAffinity->mutable_preempt()->mutable_preferredaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasInnerAffinity(instance));

    innerAffinity->clear_preempt();
    EXPECT_FALSE(HasInnerAffinity(instance));

    innerAffinity->mutable_preempt()
        ->mutable_preferredantiaffinity()
        ->mutable_condition()
        ->mutable_subconditions()
        ->Add(GetEmptySelector());
    EXPECT_TRUE(HasInnerAffinity(instance));

    innerAffinity->clear_preempt();
    EXPECT_FALSE(HasInnerAffinity(instance));

    innerAffinity->mutable_preempt()->mutable_preferredaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasInnerAffinity(instance));

    innerAffinity->clear_preempt();
    EXPECT_FALSE(HasInnerAffinity(instance));

    innerAffinity->mutable_tenant()->mutable_preferredaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasInnerAffinity(instance));

    innerAffinity->clear_tenant();
    EXPECT_FALSE(HasInnerAffinity(instance));

    innerAffinity->mutable_tenant()->mutable_requiredantiaffinity()->mutable_condition()->mutable_subconditions()->Add(
        GetEmptySelector());
    EXPECT_TRUE(HasInnerAffinity(instance));
}

TEST_F(ResourceToolTest, ValidateInstanceDiskResource)
{
    resource_view::InstanceInfo instance;
    EXPECT_FALSE(HasDiskResource(instance));

    resource_view::Resources rs = view_utils::GetDiskResources();
    (*instance.mutable_resources()) = rs;
    EXPECT_TRUE(HasDiskResource(instance));
}

TEST_F(ResourceToolTest, ValidateResourceunitDiskResource)
{
    resource_view::ResourceUnit unit;
    EXPECT_FALSE(HasDiskResource(unit));

    resource_view::Resources rs = view_utils::GetDiskResources();
    (*unit.mutable_allocatable()) = rs;
    EXPECT_TRUE(HasDiskResource(unit));
}

}  // namespace functionsystem::test