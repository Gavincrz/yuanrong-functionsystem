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

#include "common/scheduler_framework/framework/framework_impl.h"
#include <gtest/gtest.h>

#include <string>

#include "common/resource_view/resource_type.h"
#include "common/resource_view/view_utils.h"
#include "common/scheduler_framework/framework/framework.h"
#include "common/scheduler_framework/framework/policy.h"
#include "framework_impl_test.h"

namespace functionsystem::test {
using namespace ::testing;
std::unique_ptr<Framework> fwk = nullptr;
class FrameworkImplTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        fwk = std::make_unique<FrameworkImpl>();
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        fwk = nullptr;
    }
};

const std::string DEFAULT_RESOURCE_NAME = "CPU";
const std::string DEFAULT_RESOURCE_ID = "default_resource_unit_test_id";
const double DEFAULT_SCALA_VALUE = 100.1;
const double DEFAULT_SCALA_LIMIT = 1000.1;

const std::string DEFAULT_SCALA_RESOURCE_STRING = "{ name:" + DEFAULT_RESOURCE_NAME +
                                                  ", value:" + std::to_string(DEFAULT_SCALA_VALUE) +
                                                  " , limit:" + std::to_string(DEFAULT_SCALA_LIMIT) + " }";
const std::string DEFAULT_SCALA_RESOURCES_STRING = "{ { name:" + DEFAULT_RESOURCE_NAME +
                                                   ", value:" + std::to_string(DEFAULT_SCALA_VALUE) +
                                                   " , limit:" + std::to_string(DEFAULT_SCALA_LIMIT) + " } }";

resource_view::Resource MakeScalaResource(const std::string &name = DEFAULT_RESOURCE_NAME,
                                          const double &value = DEFAULT_SCALA_VALUE,
                                          const double &limit = DEFAULT_SCALA_LIMIT)
{
    resource_view::Resource res;
    res.set_name(name);
    res.set_type(resource_view::ValueType::Value_Type_SCALAR);
    res.mutable_scalar()->set_value(value);
    res.mutable_scalar()->set_limit(limit);
    return res;
}

// only cpu resource
resource_view::ResourceUnit MakeDefaultTestResourceUnit()
{
    auto res = MakeScalaResource();
    resource_view::ResourceUnit unit;
    unit.set_id(DEFAULT_RESOURCE_ID);
    auto rs = unit.mutable_capacity()->mutable_resources();
    (*rs)[DEFAULT_RESOURCE_NAME] = res;
    return unit;
}

// only cpu resource
resource_view::ResourceUnit MakeMultiFragmentTestResourceUnit(int32_t ids)
{
    resource_view::ResourceUnit unit;
    unit.set_id("domain");
    for (int32_t i = 0; i < ids ; i++) {
        resource_view::ResourceUnit frag;
        auto id = std::to_string(i);
        frag.set_id(id);
        (*unit.mutable_fragment())[id] = std::move(frag);
    }
    return unit;
}

std::shared_ptr<MockFilterPlugin> GetFilteredMockPlugin(std::string pluginName, std::set<std::string> &feasible)
{
    auto mockFilter = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter, GetPluginName()).WillOnce(Return(pluginName));
    EXPECT_CALL(*mockFilter, Filter(_, _, _))
        .WillRepeatedly(DoAll(
            Invoke([feasible](const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                              const resource_view::ResourceUnit &resourceUnit) -> Filtered {
                if (feasible.find(resourceUnit.id()) != feasible.end()) {
                    return {};
                }
                return Filtered{ Status(StatusCode::ERR_RESOURCE_NOT_ENOUGH, "no available cpu/mem"), false };
            })));
    return mockFilter;
}

// only cpu resource
resource_view::InstanceInfo MakeDefaultTestInstanceInfo()
{
    resource_view::InstanceInfo instance;
    return instance;
}

TEST_F(FrameworkImplTest, RegisterUnRegisterPlugin)  // NOLINT
{
    auto Filter = std::make_shared<MockFilterPlugin>();
    auto Score = std::make_shared<MockScorePlugin>();
    auto Pre = std::make_shared<MockPreFilterPolicy>();

    EXPECT_CALL(*Filter, GetPluginName()).WillRepeatedly(Return("MockFilterPolicy"));
    EXPECT_CALL(*Score, GetPluginName()).WillRepeatedly(Return("MockScorePolicy"));
    EXPECT_CALL(*Pre, GetPluginName()).WillRepeatedly(Return("MockPrePolicy"));

    bool retFilter = fwk->RegisterPolicy(Filter);
    bool retScore = fwk->RegisterPolicy(Score);
    bool retPre = fwk->RegisterPolicy(Pre);
    EXPECT_TRUE(retFilter);
    EXPECT_TRUE(retScore);
    EXPECT_TRUE(retPre);

    retFilter = fwk->RegisterPolicy(Filter);
    retScore = fwk->RegisterPolicy(Score);
    retPre = fwk->RegisterPolicy(Pre);
    EXPECT_FALSE(retFilter);
    EXPECT_FALSE(retScore);
    EXPECT_FALSE(retPre);
    retFilter = fwk->UnRegisterPolicy(Filter->GetPluginName());
    retScore = fwk->UnRegisterPolicy(Score->GetPluginName());
    retPre = fwk->UnRegisterPolicy(Pre->GetPluginName());
    EXPECT_TRUE(retFilter);
    EXPECT_TRUE(retScore);
    EXPECT_TRUE(retPre);

    retFilter = fwk->UnRegisterPolicy(Filter->GetPluginName());
    retScore = fwk->UnRegisterPolicy(Score->GetPluginName());
    retPre = fwk->UnRegisterPolicy(Pre->GetPluginName());
    EXPECT_FALSE(retFilter);
    EXPECT_FALSE(retScore);
    EXPECT_FALSE(retPre);
}

// no valid PreFilter
TEST_F(FrameworkImplTest, InvalidPreFilterTest)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeDefaultTestResourceUnit();
    auto result = fw->SelectFeasible(ctx, instance, resource, 1);
    EXPECT_EQ(result.code, static_cast<int32_t>(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG));
}

// PreFilter return empty result
TEST_F(FrameworkImplTest, PreFilterEmptyTest)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeDefaultTestResourceUnit();
    // PreFilter return empty result
    auto mockPrefilter = std::make_shared<MockPreFilterPolicy>();
    EXPECT_CALL(*mockPrefilter, GetPluginName()).WillRepeatedly(Return("MockPreFilterPolicy"));
    EXPECT_CALL(*mockPrefilter, PrefilterMatched(_)).WillOnce(Return(true));
    std::set<std::string> selected{};
    EXPECT_CALL(*mockPrefilter, PreFilter(_, _, _))
        .WillOnce(Return(std::make_shared<SetPreFilterResult>(selected, Status::OK())));
    fw->RegisterPolicy(mockPrefilter);
    auto result = fw->SelectFeasible(ctx, instance, resource, 1);
    EXPECT_EQ(result.code, static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH));
    EXPECT_NE(result.reason.find("no available resource that meets the request requirements"), std::string::npos);
}

// PreFilter return err
TEST_F(FrameworkImplTest, PreFilterErrTest)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeDefaultTestResourceUnit();
    // PreFilter return empty result
    auto mockPrefilter = std::make_shared<MockPreFilterPolicy>();
    EXPECT_CALL(*mockPrefilter, GetPluginName()).WillRepeatedly(Return("MockPreFilterPolicy"));
    fw->RegisterPolicy(mockPrefilter);

    EXPECT_CALL(*mockPrefilter, PrefilterMatched(_)).WillOnce(Return(true));
    std::set<std::string> selected{};
    EXPECT_CALL(*mockPrefilter, PreFilter(_, _, _))
        .WillOnce(Return(std::make_shared<SetPreFilterResult>(selected, Status(StatusCode::ERR_PARAM_INVALID))));
    auto result = fw->SelectFeasible(ctx, instance, resource, 1);
    EXPECT_EQ(result.code, static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
}

std::shared_ptr<MockPreFilterPolicy> DefaultPrefilter(const resource_view::ResourceUnit &resource)
{
    auto mockPrefilter = std::make_shared<MockPreFilterPolicy>();
    EXPECT_CALL(*mockPrefilter, GetPluginName()).WillOnce(Return("MockPreFilterPolicy"));
    EXPECT_CALL(*mockPrefilter, PrefilterMatched(_)).WillOnce(Return(true));
    EXPECT_CALL(*mockPrefilter, PreFilter(_, _, _))
        .WillOnce(Return(
            std::make_shared<ProtoMapPreFilterResult<resource_view::ResourceUnit>>(resource.fragment(), Status::OK())));
    EXPECT_EQ(mockPrefilter->GetPluginType(), PolicyType::PRE_FILTER_POLICY);
    return mockPrefilter;
}

// nothing filtered
TEST_F(FrameworkImplTest, FilterNothing)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(5);

    auto mockPrefilter = DefaultPrefilter(resource);
    std::set<std::string> feasibleOne{"1", "2", "3"};
    auto mockFilter = GetFilteredMockPlugin("MockFilterPolicy1", feasibleOne);
    std::set<std::string> feasibleTwo{"4"};
    auto mockFilter2 = GetFilteredMockPlugin("MockFilterPolicy2", feasibleTwo);
    fw->RegisterPolicy(mockPrefilter);
    fw->RegisterPolicy(mockFilter);
    fw->RegisterPolicy(mockFilter2);
    auto result = fw->SelectFeasible(ctx, instance, resource, 0);
    EXPECT_EQ(result.code, static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH));
    fw->UnRegisterPolicy("MockFilterPolicy1");
    fw->UnRegisterPolicy("MockFilterPolicy2");
}

// fatal err happened on filter plugin
TEST_F(FrameworkImplTest, FilterFatalErrTest)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(5);

    auto mockPrefilter = DefaultPrefilter(resource);
    fw->RegisterPolicy(mockPrefilter);

    auto mockFilter = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter, GetPluginName()).WillOnce(Return("pluginName"));
    std::set<std::string> feasible{"3"};
    EXPECT_CALL(*mockFilter, Filter(_, _, _)).WillRepeatedly(DoAll(Invoke(
        [feasible](const std::shared_ptr<ScheduleContext> &ctx,
                   const resource_view::InstanceInfo &instance,
                   const resource_view::ResourceUnit &resourceUnit) -> Filtered {
            if (feasible.find(resourceUnit.id()) != feasible.end()) {
                return Filtered{Status(StatusCode::ERR_PARAM_INVALID), true};
            }
            return {};
        })));
    fw->RegisterPolicy(mockFilter);
    auto result = fw->SelectFeasible(ctx, instance, resource, 0);
    EXPECT_EQ(result.code, static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
}

// expected larger than relaxed
TEST_F(FrameworkImplTest, ExpectedLargerThanRelaxedTest)
{
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(5);

    auto mockPrefilter = DefaultPrefilter(resource);
    auto mockFilter = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter, GetPluginName()).WillRepeatedly(Return("mockFilter"));
    EXPECT_CALL(*mockFilter, Filter(_, _, _)).WillRepeatedly(Return(Filtered{}));
    auto mockScore = std::make_shared<MockScorePlugin>();
    EXPECT_CALL(*mockScore, GetPluginName()).WillRepeatedly(Return("mockScore"));
    EXPECT_CALL(*mockScore, Score(_, _, _)).WillRepeatedly(Return(NodeScore{ "", 0 }));

    auto fw = std::make_unique<FrameworkImpl>(2);
    fw->RegisterPolicy(mockPrefilter);
    fw->RegisterPolicy(mockFilter);
    fw->RegisterPolicy(mockScore);
    auto result = fw->SelectFeasible(ctx, instance, resource, 4);
    EXPECT_EQ(result.sortedFeasibleNodes.size(), (size_t)4);
}

// relaxed -1
TEST_F(FrameworkImplTest, RelaxedUnlimitedTest)
{
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(5);

    auto mockPrefilter = DefaultPrefilter(resource);
    auto mockFilter = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter, GetPluginName()).WillRepeatedly(Return("mockFilter"));
    EXPECT_CALL(*mockFilter, Filter(_, _, _)).WillRepeatedly(Return(Filtered{}));
    auto mockScore = std::make_shared<MockScorePlugin>();
    EXPECT_CALL(*mockScore, GetPluginName()).WillRepeatedly(Return("mockScore"));
    EXPECT_CALL(*mockScore, Score(_, _, _)).WillRepeatedly(Return(NodeScore{ "", 0 }));

    auto fw = std::make_unique<FrameworkImpl>(-1);
    fw->RegisterPolicy(mockPrefilter);
    fw->RegisterPolicy(mockFilter);
    fw->RegisterPolicy(mockScore);
    auto result = fw->SelectFeasible(ctx, instance, resource, 0);
    EXPECT_EQ(result.sortedFeasibleNodes.size(), (size_t)5);
}

// expected less than relaxed
TEST_F(FrameworkImplTest, ExpectedLessThanRelaxedTest)
{
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(5);

    auto mockPrefilter = DefaultPrefilter(resource);
    auto mockFilter = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter, GetPluginName()).WillRepeatedly(Return("mockFilter"));
    EXPECT_CALL(*mockFilter, Filter(_, _, _)).WillRepeatedly(Return(Filtered{}));
    auto mockScore = std::make_shared<MockScorePlugin>();
    EXPECT_CALL(*mockScore, GetPluginName()).WillRepeatedly(Return("mockScore"));
    EXPECT_CALL(*mockScore, Score(_, _, _)).WillRepeatedly(Return(NodeScore{ "", 0 }));
    auto fw = std::make_unique<FrameworkImpl>(3);
    fw->RegisterPolicy(mockPrefilter);
    fw->RegisterPolicy(mockFilter);
    fw->RegisterPolicy(mockScore);
    auto result = fw->SelectFeasible(ctx, instance, resource, 2);
    EXPECT_EQ(result.sortedFeasibleNodes.size(), (size_t)3);
}

// sorted result returned
TEST_F(FrameworkImplTest, ScoreSortedTest)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(5);

    auto mockPrefilter = DefaultPrefilter(resource);
    auto mockFilter = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter, GetPluginName()).WillRepeatedly(Return("mockFilter"));
    EXPECT_CALL(*mockFilter, Filter(_, _, _)).WillRepeatedly(Return(Filtered{}));

    std::map<std::string, int64_t> scoreList = { { "0", 5 }, { "1", 10 }, { "2", 100 }, { "3", 50 }, { "4", 0 } };
    std::map<std::string, int64_t> scoreLis1 = { { "0", 30 }, { "1", 10 }, { "2", 20 }, { "3", 100 }, { "4", 0 } };

    auto mockScore = std::make_shared<MockScorePlugin>();
    EXPECT_CALL(*mockScore, GetPluginName()).WillRepeatedly(Return("mockScore"));
    EXPECT_CALL(*mockScore, Score(_, _, _))
        .WillRepeatedly(
            DoAll(Invoke([&](const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                             const resource_view::ResourceUnit &resourceUnit) -> NodeScore {
                auto nodeScore = NodeScore(scoreList[resourceUnit.id()]);
                if (resourceUnit.id() == "3") {
                    nodeScore.heteroProductName = "NPU/910B4";
                }
                return nodeScore;
            })));

    auto mockScore1 = std::make_shared<MockScorePlugin>();
    EXPECT_CALL(*mockScore1, GetPluginName()).WillRepeatedly(Return("mockScore1"));
    EXPECT_CALL(*mockScore1, Score(_, _, _))
        .WillRepeatedly(
            DoAll(Invoke([&](const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                             const resource_view::ResourceUnit &resourceUnit) -> NodeScore {
                return NodeScore(scoreLis1[resourceUnit.id()]);
            })));
    fw->RegisterPolicy(mockPrefilter);
    fw->RegisterPolicy(mockFilter);
    fw->RegisterPolicy(mockScore);
    fw->RegisterPolicy(mockScore1);
    EXPECT_EQ(mockFilter->GetPluginType(), PolicyType::FILTER_POLICY);
    EXPECT_EQ(mockScore->GetPluginType(), PolicyType::SCORE_POLICY);

    auto result = fw->SelectFeasible(ctx, instance, resource, 0);
    EXPECT_EQ(result.sortedFeasibleNodes.size(), (size_t)5);
    auto top = result.sortedFeasibleNodes.top();
    EXPECT_EQ(top.name, "3");
    EXPECT_EQ(top.score, scoreList[top.name] + scoreLis1[top.name]);
    EXPECT_EQ(top.heteroProductName, "NPU/910B4");
    result.sortedFeasibleNodes.pop();

    auto second = result.sortedFeasibleNodes.top();
    EXPECT_EQ(second.name, "2");
    EXPECT_EQ(second.score, scoreList[second.name] + scoreLis1[second.name]);
    EXPECT_EQ(second.heteroProductName, "");
    result.sortedFeasibleNodes.pop();
}

TEST_F(FrameworkImplTest, AddScoreTest)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(1);

    auto mockPrefilter = DefaultPrefilter(resource);
    auto mockFilter = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter, GetPluginName()).WillRepeatedly(Return("mockFilter"));
    EXPECT_CALL(*mockFilter, Filter(_, _, _)).WillRepeatedly(Return(Filtered{}));

    std::map<std::string, int64_t> scoreList = { { "0", 5 } };
    std::map<std::string, int64_t> scoreLis1 = { { "0", 30 } };

    auto mockScore = std::make_shared<MockScorePlugin>();
    EXPECT_CALL(*mockScore, GetPluginName()).WillRepeatedly(Return("mockScore"));
    auto nodeScore = NodeScore(5);
    nodeScore.heteroProductName = "NPU/910B4";
    nodeScore.realIDs = { 0 };
    nodeScore.vectorAllocations.push_back(schedule_framework::VectorResourceAllocation{});
    nodeScore.vectorAllocations.push_back(schedule_framework::VectorResourceAllocation{});
    EXPECT_CALL(*mockScore, Score(_, _, _)).WillOnce(Return(nodeScore));

    auto mockScore1 = std::make_shared<MockScorePlugin>();
    EXPECT_CALL(*mockScore1, GetPluginName()).WillRepeatedly(Return("mockScore1"));
    auto nodeScore1 = NodeScore(30);
    nodeScore1.heteroProductName = "NPU/310";
    nodeScore1.realIDs = { 1 };
    nodeScore1.vectorAllocations.push_back(schedule_framework::VectorResourceAllocation{});
    EXPECT_CALL(*mockScore1, Score(_, _, _)).WillOnce(Return(nodeScore1));;
    fw->RegisterPolicy(mockPrefilter);
    fw->RegisterPolicy(mockFilter);
    fw->RegisterPolicy(mockScore);
    fw->RegisterPolicy(mockScore1);
    EXPECT_EQ(mockFilter->GetPluginType(), PolicyType::FILTER_POLICY);
    EXPECT_EQ(mockScore->GetPluginType(), PolicyType::SCORE_POLICY);

    auto result = fw->SelectFeasible(ctx, instance, resource, 0);
    EXPECT_EQ(result.sortedFeasibleNodes.size(), (size_t)1);
    auto top = result.sortedFeasibleNodes.top();
    EXPECT_EQ(top.name, "0");
    EXPECT_EQ(top.score, scoreList[top.name] + scoreLis1[top.name]);
    // validate heterogeneous info -- overwrite mode
    EXPECT_EQ(top.heteroProductName, "NPU/310");
    EXPECT_EQ(top.realIDs, std::vector<int>{1});
    // validate vector allocation info -- append mode
    EXPECT_EQ(top.vectorAllocations.size(), 3);
}

// multi filter return diff available
TEST_F(FrameworkImplTest, AvailableForRequestTest)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(5);
    auto mockPrefilter = DefaultPrefilter(resource);

    auto mockFilter = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter, GetPluginName()).WillRepeatedly(Return("mockFilter"));
    std::map<std::string, int32_t> available = {{"0", 1}, {"1", 5}, {"2", -1}, {"3", -1}, {"4", 1} };
    std::map<std::string, int32_t> available2 = {{"0", -1}, {"1", 6}, {"2", 0}, {"3", -1}, {"4", 1} };
    std::map<std::string, int32_t> available3 = {{"0", 3}, {"1", 2}, {"2", 2}, {"3", 1}, {"4", -1} };
    EXPECT_CALL(*mockFilter, Filter(_, _, _)).WillRepeatedly(DoAll(Invoke(
        [&](const std::shared_ptr<ScheduleContext> &ctx,
                   const resource_view::InstanceInfo &instance,
                   const resource_view::ResourceUnit &resourceUnit) -> Filtered {
            return {Status::OK(), false, available[resourceUnit.id()]};
        })));

    auto mockFilter1 = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter1, GetPluginName()).WillRepeatedly(Return("mockFilter1"));
    EXPECT_CALL(*mockFilter1, Filter(_, _, _)).WillRepeatedly(DoAll(Invoke(
        [&](const std::shared_ptr<ScheduleContext> &ctx,
                    const resource_view::InstanceInfo &instance,
                    const resource_view::ResourceUnit &resourceUnit) -> Filtered {
            return {Status::OK(), false, available2[resourceUnit.id()]};
        })));

    auto mockFilter2 = std::make_shared<MockFilterPlugin>();
    EXPECT_CALL(*mockFilter2, GetPluginName()).WillRepeatedly(Return("mockFilter2"));
    EXPECT_CALL(*mockFilter2, Filter(_, _, _)).WillRepeatedly(DoAll(Invoke(
        [&](const std::shared_ptr<ScheduleContext> &ctx,
                     const resource_view::InstanceInfo &instance,
                     const resource_view::ResourceUnit &resourceUnit) -> Filtered {
            return {Status::OK(), false, available3[resourceUnit.id()]};
        })));

    std::map<std::string, int64_t> scoreList = { { "0", 100 }, { "1", 90 }, { "2", 80 }, { "3", 70 }, { "4", 60 } };
    auto mockScore = std::make_shared<MockScorePlugin>();
    EXPECT_CALL(*mockScore, GetPluginName()).WillRepeatedly(Return("mockScore"));
    EXPECT_CALL(*mockScore, Score(_, _, _))
        .WillRepeatedly(
            DoAll(Invoke([&](const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                             const resource_view::ResourceUnit &resourceUnit) -> NodeScore {
                return NodeScore(scoreList[resourceUnit.id()]);
            })));
    fw->RegisterPolicy(mockPrefilter);
    fw->RegisterPolicy(mockFilter);
    fw->RegisterPolicy(mockFilter1);
    fw->RegisterPolicy(mockFilter2);
    fw->RegisterPolicy(mockScore);

    auto result = fw->SelectFeasible(ctx, instance, resource, 0);
    EXPECT_EQ(result.sortedFeasibleNodes.size(), (size_t)5);
    auto top = result.sortedFeasibleNodes.top();
    EXPECT_EQ(top.name, "0");
    EXPECT_EQ(top.availableForRequest, 1);
    result.sortedFeasibleNodes.pop();

    top = result.sortedFeasibleNodes.top();
    EXPECT_EQ(top.name, "1");
    EXPECT_EQ(top.availableForRequest, 2);
    result.sortedFeasibleNodes.pop();

    top = result.sortedFeasibleNodes.top();
    EXPECT_EQ(top.name, "2");
    EXPECT_EQ(top.availableForRequest, 2);
    result.sortedFeasibleNodes.pop();

    top = result.sortedFeasibleNodes.top();
    EXPECT_EQ(top.name, "3");
    EXPECT_EQ(top.availableForRequest, 1);
    result.sortedFeasibleNodes.pop();

    top = result.sortedFeasibleNodes.top();
    EXPECT_EQ(top.name, "4");
    EXPECT_EQ(top.availableForRequest, 1);
    result.sortedFeasibleNodes.pop();
}

// multi filter return diff available
TEST_F(FrameworkImplTest, UnitStatusTest)
{
    auto fw = std::make_unique<FrameworkImpl>(-1);
    auto ctx = std::make_shared<ScheduleContext>();
    auto instance = MakeDefaultTestInstanceInfo();
    auto resource = MakeMultiFragmentTestResourceUnit(5);
    (*resource.mutable_fragment())["0"].set_status(1);
    (*resource.mutable_fragment())["1"].set_status(2);
    (*resource.mutable_fragment())["2"].set_status(3);
    (*resource.mutable_fragment())["3"].set_status(1);
    (*resource.mutable_fragment())["4"].set_status(2);
    auto mockPrefilter = DefaultPrefilter(resource);
    fw->RegisterPolicy(mockPrefilter);
    auto result = fw->SelectFeasible(ctx, instance, resource, 0);
    EXPECT_EQ(result.code, static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH));
    EXPECT_NE(result.reason.find("no available resource that meets the request requirements"), std::string::npos);
}

}  // namespace functionsystem::test
