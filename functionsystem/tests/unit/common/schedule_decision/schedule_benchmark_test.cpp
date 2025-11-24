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

#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"
#include "common/schedule_decision/scheduler.h"
#include "common/schedule_decision/schedule_queue_actor.h"
#include "common/schedule_decision/scheduler/priority_scheduler.h"
#include "mocks/mock_resource_view.h"
#include "mocks/mock_schedule_performer.h"
#include "utils/future_test_helper.h"
#include "async/async.hpp"

#include "common/schedule_plugin/common/plugin_utils.h"
#include "common/scheduler_framework/framework/framework.h"
#include "common/scheduler_framework/framework/policy.h"
#include "common/scheduler_framework/framework/framework_impl.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/resource_view/resource_type.h"

#include "common/schedule_plugin/prefilter/default_prefilter/default_prefilter.h"
#include "common/schedule_plugin/filter/default_filter/default_filter.h"
#include "common/schedule_plugin/filter/default_heterogeneous_filter/default_heterogeneous_filter.h"
#include "common/schedule_plugin/filter/resource_selector_filter/resource_selector_filter.h"
#include "common/schedule_plugin/scorer/default_heterogeneous_scorer/default_heterogeneous_scorer.h"
#include "common/schedule_plugin/scorer/default_scorer/default_scorer.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace schedule_decision;

class ScheduleBenchmarkTest : public Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        if (scheduleQueueActor_ != nullptr) {
            litebus::Terminate(scheduleQueueActor_->GetAID());
            litebus::Await(scheduleQueueActor_->GetAID());
        }
        scheduleQueueActor_ = nullptr;
        mockResourceView_ = nullptr;
        instanceSchedulerPerformer_ = nullptr;
        mockGroupPerformer_ = nullptr;
        aggregatedSchedulePerformer_ = nullptr;
        scheduler_ = nullptr;
    }

    void SetUpForTest(int32_t relaxed, const std::string &aggregatedStrategy = "no_aggregate")
    {
        auto fw = std::make_shared<functionsystem::schedule_framework::FrameworkImpl>(relaxed);
        // prefilter plugin
        fw->RegisterPolicy(std::make_shared<functionsystem::schedule_plugin::prefilter::DefaultPreFilter>());

        // filter plugin
        fw->RegisterPolicy(std::make_shared<functionsystem::schedule_plugin::filter::DefaultFilter>());
        fw->RegisterPolicy(std::make_shared<functionsystem::schedule_plugin::filter::DefaultHeterogeneousFilter>());
        fw->RegisterPolicy(std::make_shared<functionsystem::schedule_plugin::filter::ResourceSelectorFilter>());
        // strict root

        // scorer plugin
        fw->RegisterPolicy(std::make_shared<functionsystem::schedule_plugin::score::DefaultHeterogeneousScorer>());
        fw->RegisterPolicy(std::make_shared<functionsystem::schedule_plugin::scorer::DefaultScorer>());

        // resource view
        mockResourceView_ = MockResourceView::CreateMockResourceView();
        EXPECT_CALL(*mockResourceView_, AddResourceUpdateHandler).WillOnce(Return());

        // instance scheduler
        instanceSchedulerPerformer_ = std::make_shared<schedule_decision::InstanceSchedulePerformer>(
                schedule_decision::AllocateType::PRE_ALLOCATION);
        instanceSchedulerPerformer_->RegisterScheduleFramework(fw);
        instanceSchedulerPerformer_->BindResourceView(mockResourceView_);

        // group scheduler
        mockGroupPerformer_ = std::make_shared<MockGroupSchedulePerformer>();

        // aggregated scheduler
        aggregatedSchedulePerformer_ = std::make_shared<schedule_decision::AggregatedSchedulePerformer>(PRE_ALLOCATION);
        aggregatedSchedulePerformer_->RegisterScheduleFramework(fw);
        aggregatedSchedulePerformer_->BindResourceView(mockResourceView_);

        // fairnessSchedule
        auto fairnessSchedule =
                std::make_shared<PriorityScheduler>(schedule_decision::ScheduleRecorder::CreateScheduleRecorder(),
                                                    10, PriorityPolicyType::FAIRNESS, aggregatedStrategy);
        fairnessSchedule->RegisterSchedulePerformer(instanceSchedulerPerformer_, mockGroupPerformer_,
                                                    aggregatedSchedulePerformer_);

        // scheduleQueueActor_
        scheduleQueueActor_ = std::make_shared<schedule_decision::ScheduleQueueActor>("ScheduleQueueActor");
        scheduleQueueActor_->RegisterResourceView(mockResourceView_);
        scheduleQueueActor_->RegisterScheduler(fairnessSchedule);
        litebus::Spawn(scheduleQueueActor_);
        // Since RG is not used here, virtualAid is temporarily set to primaryAid.
        scheduler_ = std::make_shared<schedule_decision::Scheduler>(scheduleQueueActor_->GetAID(),
                                                                    scheduleQueueActor_->GetAID());
    }


    inline resource_view::BucketInfo GetBucketInfo(int32_t monoNum, int32_t sharedNum)
    {
        resource_view::BucketInfo bucketInfo;
        bucketInfo.set_monopolynum(monoNum);
        bucketInfo.set_sharednum(sharedNum);
        return bucketInfo;
    }

    std::shared_ptr<messages::ScheduleRequest> GetInstanceReq(int32_t priority,
                                                              double cpu, double memory,
                                                              const std::string &policy)
    {
        auto req = std::make_shared<messages::ScheduleRequest>();
        auto scheduleInstance = view_utils::GetInstanceWithResourceAndPriority(priority, cpu, memory);
        scheduleInstance.mutable_scheduleoption()->set_schedpolicyname(policy);
        *req->mutable_instance() = scheduleInstance;
        req->set_requestid(scheduleInstance.requestid());
        req->set_traceid("traceID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        return req;
    }

    resource_view::ResourceUnit MakeMultiFragmentTestResourceUnit(int32_t ids, double cpu, double mem)
    {
        resource_view::ResourceUnit unit;
        unit.set_id("domain");
        auto bucketIndexes = unit.mutable_bucketindexs();
        std::vector<std::pair<std::string, resource_view::BucketInfo>> bucketInfos;
        int totalNum = 0;

        for (int32_t i = 0; i < ids ; i++) {
            resource_view::ResourceUnit frag = functionsystem::test::schedule_plugin::GetAgentResourceUnit(cpu, mem, 1);
            auto id = std::to_string(i);
            frag.set_id(id);
            (*unit.mutable_fragment())[id] = std::move(frag);

            bucketInfos.push_back({ id, GetBucketInfo(1, 0) });
            totalNum += 1;
        }

        auto proportionStr = std::to_string(mem / cpu);
        auto memStr = std::to_string(mem);
        std::vector<std::pair<std::string, resource_view::Bucket>>buckets =
            { std::make_pair(memStr, functionsystem::test::schedule_plugin::GetBucket(GetBucketInfo(totalNum, 0), bucketInfos)) };
        bucketIndexes->insert({ proportionStr, functionsystem::test::schedule_plugin::GetBucketIndex(buckets) });

        return unit;
    }

    struct TestResult {
        int numAgents;
        double avg;
        double median;
        double p90;
        double p95;
        double p99;
        double variance;
        double std_dev;
        double min;
        double max;
        int request_count;
        int cyclyTime;
    };

    struct RunResult {
        int successCount;
        double time;
    };

    RunResult RunTestWithFixedResource(int numReqs)
    {
        scheduleQueueActor_->SetNewResourceAvailable();
        std::vector<std::shared_ptr<messages::ScheduleRequest>> reqs;
        for (int i = 0; i < numReqs; ++i) {
            reqs.push_back(GetInstanceReq(0, 300.0, 128.0, "monopoly"));
        }

        std::vector<litebus::Future<ScheduleResult>> results;
        auto start = std::chrono::high_resolution_clock::now();
        int successCount = 0;
        for (int i = 0; i < numReqs; ++i) {
            results.push_back(scheduler_->ScheduleDecision(reqs[i],  litebus::Future<std::string>()));
        }
        for (auto &future: results) {
            auto result = future.Get();
            if (result.code == 0) {
                successCount++;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration<double, std::milli>(end - start).count();
        return RunResult{ successCount, ms};
    }

    static TestResult ComputeTestStatistics(const std::vector<double>& times, const std::vector<int>& successCounts,
                                            int numAgents)
    {
        TestResult res;
        res.numAgents = numAgents;
        res.request_count = *std::min_element(successCounts.begin(), successCounts.end());

        res.avg = res.request_count / (calculateAverage(times) / 1000);
        res.median = res.request_count / (calculateMedian(times) / 1000);

        res.p90 = res.request_count / (calculatePercentile(times, 90) / 1000);
        res.p95 = res.request_count / (calculatePercentile(times, 95) / 1000);
        res.p99 = res.request_count / (calculatePercentile(times, 99) / 1000);

        res.min = res.request_count / (*std::min_element(times.begin(), times.end()) / 1000);
        res.max = res.request_count / (*std::max_element(times.begin(), times.end()) / 1000);
        res.variance = calculateVariance(times);
        res.std_dev = std::sqrt(res.variance);

        res.cyclyTime = times.size();

        return res;
    }

    static double calculateAverage(const std::vector<double>& data)
    {
        double sum = 0;
        for (double d : data) sum += d;
        return sum / data.size();
    }

    static double calculateMedian(std::vector<double> data)
    {
        std::sort(data.begin(), data.end());
        size_t size = data.size();
        size_t mid = size / 2;
        return (size % 2) ? data[mid] : (data[mid-1] + data[mid])/2;
    }

    static double calculatePercentile(const std::vector<double>& data, double percentile)
    {
        std::vector<double> sorted(data);
        std::sort(sorted.begin(), sorted.end());
        size_t index = static_cast<size_t>( (percentile/100) * (sorted.size()-1) );
        return sorted[index];
    }

    static double calculateVariance(const std::vector<double>& data)
    {
        double avg = calculateAverage(data);
        double var = 0;
        for (double d : data) var += (d - avg)*(d - avg);
        return var / data.size();
    }

    // Output results to the terminal if no filename is specified, otherwise save to a file
    static void ProcessData(const std::vector<TestResult>& results, const std::string& fileName = "")
    {
        std::ofstream file;
        std::ostream* output = &std::cout;

        if (!fileName.empty()) {
            file.open(fileName);
            if (!file.is_open()) {
                std::cerr << "无法创建或打开文件！" << std::endl;
                return;
            }
            output = &file;
        }

        *output << std::fixed << std::setprecision(2);

        if (fileName.empty()) {
            for (const auto& res : results) {
                *output << "Agent: " << res.numAgents
                        << " | 请求数: " << res.request_count
                        << " | 调度次数: " << res.cyclyTime
                        << " | 平均 RPS: " << res.avg
                        << " | 最小 RPS: " << res.min
                        << " | 最大 RPS: " << res.max
                        << " | p50 RPS: " << res.median
                        << " | p90 RPS: " << res.p90
                        << " | p95 RPS: " << res.p95
                        << " | p99 RPS: " << res.p99
                        << std::endl;
            }
        } else {
            *output << "Benchmark Results:\n";
            for (const auto& res : results) {
                *output << std::string(70, '*') << std::endl;
                *output << "* 拉起的agent数量：" << res.numAgents
                        << " -- 调度请求数: " << res.request_count
                        << " -- 执行调度次数: " << res.cyclyTime << " *" << std::endl;
                *output << std::string(70, '*') << std::endl;
                *output << "平均 RPS: " << res.avg << std::endl;
                *output << "最小 RPS: " << res.min << std::endl;
                *output << "最大 RPS: " << res.max << std::endl;
                *output << "p50 RPS: " << res.median << std::endl;
                *output << "P90 RPS: " << res.p90 << std::endl;
                *output << "P95 RPS: " << res.p95 << std::endl;
                *output << "P99 RPS: " << res.p99 << std::endl;
            }
        }

        if (file.is_open()) {
            file.close();
        }
    }

protected:
    std::shared_ptr<schedule_decision::ScheduleQueueActor> scheduleQueueActor_;
    std::shared_ptr<MockResourceView> mockResourceView_;
    std::shared_ptr<schedule_decision::InstanceSchedulePerformer> instanceSchedulerPerformer_;
    std::shared_ptr<MockGroupSchedulePerformer> mockGroupPerformer_;
    std::shared_ptr<schedule_decision::AggregatedSchedulePerformer> aggregatedSchedulePerformer_;
    std::shared_ptr<schedule_decision::Scheduler> scheduler_;
};

/**
 * Test scheduling performance with varying agent counts.
 * - Disabled relaxed mode.
 * - Aggregation strategy: "no_aggregate".
 * Parameters(Set the parameter range for performance testing as needed):
 *   - agentCounts: {1, 100, 1000, 2000, 10000}  (number of agents to test).
 *   - verificationAttempts: 3 (number of test repetitions per agent count).
 * Output: Statistics for scheduling performance across agent counts.
 */
TEST_F(ScheduleBenchmarkTest, BenchmarkVaryAgentCountsNoRelaxNoAggregate)
{
    SetUpForTest(-1);
    const std::vector<int> agentCounts = {1, 100};
    int verificationAttempts = 1;
    std::vector<TestResult> results;

    for (int totalAgent : agentCounts) {
        std::vector<double> times;
        std::vector<int> successCounts;

        auto resource = MakeMultiFragmentTestResourceUnit(totalAgent, 300.0, 128.0);
        resource_view::ResourceViewInfo resourceViewInfo;
        resourceViewInfo.resourceUnit = resource;
        EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillRepeatedly(Return(resourceViewInfo));

        auto totalReqs = totalAgent;
        for (int i = 0; i < verificationAttempts; ++i) {
            auto result = RunTestWithFixedResource(totalReqs);
            times.push_back(result.time);
            successCounts.push_back(result.successCount);
        }

        TestResult res = ComputeTestStatistics(times, successCounts, totalAgent);
        results.push_back(res);
    }
    // Output results to the terminal if no filename is specified, otherwise save to a file
    ProcessData(results);
}

/**
 * Test scheduling performance with varying request counts and fixed agent count.
 * - Disabled relaxed mode.
 * - Aggregation strategy: "no_aggregate".
 * Parameters(Set the parameter range for performance testing as needed):
 *   - reqCounts: {1, 100, 1000, 2000, 10000}  (number of requests to test).
 *   - agentCount: 10000 (fixed number of agents).
 *   - verificationAttempts: 3 (number of test repetitions per request count).
 * Output: Statistics for scheduling performance across request counts.
 */
TEST_F(ScheduleBenchmarkTest, BenchmarkVaryRequestCountsNoRelaxNoAggregate)
{
    SetUpForTest(-1);
    const std::vector<int> req_counts = {1, 100};
    int totalAgent = 100;
    int verificationAttempts = 1;
    std::vector<TestResult> results;

    auto resource = MakeMultiFragmentTestResourceUnit(totalAgent, 300.0, 128.0);
    resource_view::ResourceViewInfo resourceViewInfo;
    resourceViewInfo.resourceUnit = resource;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillRepeatedly(Return(resourceViewInfo));

    for (int totalreq : req_counts) {
        std::vector<double> times;
        std::vector<int> successCounts;

        for (int i = 0; i < verificationAttempts; ++i) {
            auto result = RunTestWithFixedResource(totalreq);
            times.push_back(result.time);
            successCounts.push_back(result.successCount);
        }

        TestResult res = ComputeTestStatistics(times, successCounts, totalAgent);
        results.push_back(res);
    }
    // Output results to the terminal if no filename is specified, otherwise save to a file
    ProcessData(results);
}

/**
 * Test scheduling performance with varying agent counts in relaxed mode.
 * - Enabled relaxed mode (relaxed = 1).
 * - Aggregation strategy: "no_aggregate".
 * Parameters(Set the parameter range for performance testing as needed):
 *   - agentCounts: {1, 100, 1000, 2000, 10000} (number of agents to test).
 *   - verificationAttempts: 50 (number of test repetitions per agent count).
 * Output: Statistics for scheduling performance across agent counts.
 */
TEST_F(ScheduleBenchmarkTest, BenchmarkVaryAgentCountsWithRelaxNoAggregate)
{
    SetUpForTest(1);
    const std::vector<int> agentCounts = {1, 100};
    int verificationAttempts = 1;
    std::vector<TestResult> results;

    for (int totalAgent : agentCounts) {
        std::vector<double> times;
        std::vector<int> successCounts;

        auto resource = MakeMultiFragmentTestResourceUnit(totalAgent, 300.0, 128.0);
        resource_view::ResourceViewInfo resourceViewInfo;
        resourceViewInfo.resourceUnit = resource;
        EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillRepeatedly(Return(resourceViewInfo));

        auto totalReqs = totalAgent;
        for (int i = 0; i < verificationAttempts; ++i) {
            auto result = RunTestWithFixedResource(totalReqs);
            times.push_back(result.time);
            successCounts.push_back(result.successCount);
        }

        TestResult res = ComputeTestStatistics(times, successCounts, totalAgent);
        results.push_back(res);
    }
    // Output results to the terminal if no filename is specified, otherwise save to a file
    ProcessData(results);
}

/**
 * Test scheduling performance with varying request counts and fixed agent count in relaxed mode.
 * - Enabled relaxed mode (relaxed = 1).
 * - Aggregation strategy: "no_aggregate".
 * Parameters(Set the parameter range for performance testing as needed):
 *   - reqCounts: {1, 100, 1000, 2000, 10000} (number of requests to test).
 *   - agentCount: 10000 (fixed number of agents).
 *   - verificationAttempts: 50 (number of test repetitions per request count).
 * Output: Statistics for scheduling performance across request counts.
 */
TEST_F(ScheduleBenchmarkTest, BenchmarkVaryRequestCountsWithRelaxNoAggregate)
{
    SetUpForTest(1);
    const std::vector<int> req_counts = {1, 100};
    int totalAgent = 100;
    int verificationAttempts = 1;
    std::vector<TestResult> results;

    auto resource = MakeMultiFragmentTestResourceUnit(totalAgent, 300.0, 128.0);
    resource_view::ResourceViewInfo resourceViewInfo;
    resourceViewInfo.resourceUnit = resource;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillRepeatedly(Return(resourceViewInfo));

    for (int totalreq : req_counts) {
        std::vector<double> times;
        std::vector<int> successCounts;

        for (int i = 0; i < verificationAttempts; ++i) {
            auto result = RunTestWithFixedResource(totalreq);
            times.push_back(result.time);
            successCounts.push_back(result.successCount);
        }

        TestResult res = ComputeTestStatistics(times, successCounts, totalAgent);
        results.push_back(res);
    }
    // Output results to the terminal if no filename is specified, otherwise save to a file
    ProcessData(results);
}

/**
 * Test scheduling performance with varying agent counts in relaxed mode and relaxed aggregation.
 * - Enabled relaxed mode (relaxed = 1).
 * - Aggregation strategy: "relaxed".
 * Parameters(Set the parameter range for performance testing as needed):
 *   - agentCounts: {1, 100, 1000, 2000, 10000} (number of agents to test).
 *   - verificationAttempts: 50 (number of test repetitions per agent count).
 * Output: Statistics for scheduling performance across agent counts.
 */
TEST_F(ScheduleBenchmarkTest, BenchmarkVaryAgentCountsWithRelaxAndAggregate)
{
    SetUpForTest(1, "relaxed");
    const std::vector<int> agentCounts = {1, 100, 1000, 2000, 10000};
    int verificationAttempts = 1;
    std::vector<TestResult> results;

    for (int totalAgent : agentCounts) {
        std::vector<double> times;
        std::vector<int> successCounts;

        auto resource = MakeMultiFragmentTestResourceUnit(totalAgent, 300.0, 128.0);
        resource_view::ResourceViewInfo resourceViewInfo;
        resourceViewInfo.resourceUnit = resource;
        EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillRepeatedly(Return(resourceViewInfo));

        auto totalReqs = totalAgent;
        for (int i = 0; i < verificationAttempts; ++i) {
            auto result = RunTestWithFixedResource(totalReqs);
            times.push_back(result.time);
            successCounts.push_back(result.successCount);
        }

        TestResult res = ComputeTestStatistics(times, successCounts, totalAgent);
        results.push_back(res);
    }
    // Output results to the terminal if no filename is specified, otherwise save to a file
    ProcessData(results);
}

/**
 * Test scheduling performance with varying request counts and fixed agent count in relaxed mode and relaxed aggregation.
 * - Enabled relaxed mode (relaxed = 1).
 * - Aggregation strategy: "relaxed".
 * Parameters(Set the parameter range for performance testing as needed):
 *   - reqCounts: {1, 100, 1000, 2000, 10000} (number of requests to test).
 *   - agentCount: 10000 (fixed number of agents).
 *   - verificationAttempts: 50 (number of test repetitions per request count).
 * Output: Statistics for scheduling performance across request counts.
 */
TEST_F(ScheduleBenchmarkTest, BenchmarkVaryRequestCountsWithRelaxAndAggregate)
{
    SetUpForTest(1, "relaxed");
    const std::vector<int> req_counts = {1, 100, 1000, 2000, 10000};
    int totalAgent = 100;
    int verificationAttempts = 1;
    std::vector<TestResult> results;

    auto resource = MakeMultiFragmentTestResourceUnit(totalAgent, 300.0, 128.0);
    resource_view::ResourceViewInfo resourceViewInfo;
    resourceViewInfo.resourceUnit = resource;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillRepeatedly(Return(resourceViewInfo));

    for (int totalreq : req_counts) {
        std::vector<double> times;
        std::vector<int> successCounts;

        for (int i = 0; i < verificationAttempts; ++i) {
            auto result = RunTestWithFixedResource(totalreq);
            times.push_back(result.time);
            successCounts.push_back(result.successCount);
        }

        TestResult res = ComputeTestStatistics(times, successCounts, totalAgent);
        results.push_back(res);
    }
    // Output results to the terminal if no filename is specified, otherwise save to a file
    ProcessData(results);
}

}  // namespace functionsystem::test