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
#include "runtime_manager/metrics/metrics_actor.h"

#include <gtest/gtest.h>

#include "common/utils/exec_utils.h"
#include "mock_function_agent_actor.h"
#include "mocks/mock_local_sched_srv.h"
#include "runtime_manager/config/flags.h"
#include "runtime_manager/executor/runtime_executor.h"
#include "runtime_manager/manager/runtime_manager.h"
#include "runtime_manager/metrics/collector/base_metrics_collector.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

using namespace runtime_manager::test;
using namespace testing;
using namespace functionsystem::runtime_manager;

class TestMetricsActor : public runtime_manager::MetricsActor {
public:
    TestMetricsActor() : runtime_manager::MetricsActor("test_metrics_actor")
    {
    }

    std::string BuildUpdateMetricsRequest(const std::vector<litebus::Future<runtime_manager::Metrics>> &metricses)
    {
        return runtime_manager::MetricsActor::BuildUpdateMetricsRequest(metricses);
    }

    resources::ResourceUnit BuildResourceUnit(const std::vector<litebus::Future<runtime_manager::Metrics>> &metricses)
    {
        return runtime_manager::MetricsActor::BuildResourceUnit(metricses);
    }
};

class MetricsActorTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        port_ = GetPortEnv("LITEBUS_PORT", 8080);
    }

    void SetUp() override
    {
        metricsActor_ = std::make_shared<TestMetricsActor>();
        litebus::Spawn(metricsActor_);
        runtimeManager_ = std::make_shared<RuntimeManager>("RuntimeManagerActor");
        runtimeManager_->isUnitTestSituation_ = true;
        litebus::Spawn(runtimeManager_, true);
        runtimeManager_->connected_ = true;
        runtimeManager_->isUnitTestSituation_ = true;
        runtimeManager_->metricsClient_->actor_ = metricsActor_;
    }

    void TearDown() override
    {
        litebus::Terminate(metricsActor_->GetAID());
        litebus::Await(metricsActor_->GetAID());
        litebus::Terminate(runtimeManager_->GetAID());
        litebus::Await(runtimeManager_);
    }

    std::shared_ptr<TestMetricsActor> metricsActor_;
    inline static uint16_t port_;
    std::shared_ptr<RuntimeManager> runtimeManager_;
};

struct DiskInfo {
    std::string name;
    uint64_t size;
    std::string mountPoints;
};

void CheckDiskResourceMatch(const Resource& resource, const std::vector<DiskInfo>& expected)
{
    EXPECT_EQ(resource.extensions_size(), expected.size());
    EXPECT_TRUE(resource.vectors().values().count(metrics_type::DISK));
    EXPECT_EQ(resource.vectors().values().at(metrics_type::DISK).vectors().size(), 1);
    auto diskVector =  resource.vectors().values().at(metrics_type::DISK).vectors().begin()->second;
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto& ext = resource.extensions(i);
        EXPECT_TRUE(ext.has_disk());
        const auto& disk = ext.disk();
        const auto& exp = expected[i];

        EXPECT_EQ(diskVector.values(i), exp.size);
        EXPECT_EQ(disk.name(), exp.name);
        EXPECT_EQ(disk.size(), exp.size);
        EXPECT_EQ(disk.mountpoints(), exp.mountPoints);
    }
}

/**
 * Feature: MetricsActor
 * Description: Build UpdateMetricsRequest
 * Steps:
 * Expectation:
 */
TEST_F(MetricsActorTest, BuildUpdateMetricsRequest)
{
    runtimeManager_->metricsClient_->StartUpdateResource();
    runtimeManager_->metricsClient_->StartDiskUsageMonitor();
    runtimeManager_->metricsClient_->StartRuntimeMemoryLimitMonitor();

    const char *argv[] = { "./runtime-manager", "--node_id=node1"};
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    metricsActor_->SetConfig(flags);

    // stub instanceInfos
    messages::RuntimeInstanceInfo runtimeInstanceInfo;
    runtimeInstanceInfo.set_runtimeid("runtimeID1");
    runtimeInstanceInfo.set_instanceid("id-1");
    runtimeInstanceInfo.set_requestid("requestID1");
    auto cpuLimit = 300.0; // MB
    auto memoryLimit = 128.0; // MB
    const pid_t testPid = 1001;
    metricsActor_->AddInstance(runtimeInstanceInfo, testPid, cpuLimit, memoryLimit);
    EXPECT_AWAIT_TRUE([&]() -> bool { return metricsActor_->instanceInfos_.size() >= 1; });

    // parse disk resources
    std::string diskResources = R"(
            [{"name": "disk1", "size": "100G", "mountPoints": "/tmp/disk1/"},
             {"name": "disk2", "size": "200G", "mountPoints": "/tmp/disk2a/"}]
        )";
    metricsActor_->ResolveDiskResourceMetricsCollector(diskResources);
    auto iter = metricsActor_->filter_.find("system-disk");
    EXPECT_NE(iter, metricsActor_->filter_.end());
    auto diskMetrics = iter->second->GetMetrics().Get();

    // given
    auto given = std::vector<litebus::Future<runtime_manager::Metrics>>{
        {runtime_manager::Metrics{100.0, 120.0, {}, {}, runtime_manager::metrics_type::CPU}},
        {runtime_manager::Metrics{110.0, 130.0, {}, {}, runtime_manager::metrics_type::MEMORY}},
        {runtime_manager::Metrics{120.0, 140.0, {"id-1"}, {}, runtime_manager::metrics_type::CPU}},
        {runtime_manager::Metrics{140.0, 160.0, {"id-1"}, {}, runtime_manager::metrics_type::MEMORY}},
        {runtime_manager::Metrics{180.0, 200.0, {"id-2"}, {}, runtime_manager::metrics_type::CPU}},
        {runtime_manager::Metrics{220.0, 240.0, {"id-2"}, {}, runtime_manager::metrics_type::MEMORY}},
        {diskMetrics}
    };

    // got
    auto got = metricsActor_->BuildUpdateMetricsRequest(given);

    // want
    // 1.validate cpu/memory resource
    messages::UpdateResourcesRequest req;
    EXPECT_TRUE(req.ParseFromString(got) == 1);
    auto unit = req.resourceunit();
    std::cout << unit.ShortDebugString() << std::endl;
    EXPECT_EQ(unit.capacity().resources().find("CPU")->second.scalar().value(), 120.0);
    EXPECT_EQ(unit.actualuse().resources().find("CPU")->second.scalar().value(), 100.0);
    EXPECT_EQ(unit.allocatable().resources().find("CPU")->second.scalar().value(), 120.0);

    EXPECT_EQ(unit.capacity().resources().find("Memory")->second.scalar().value(), 130.0);
    EXPECT_EQ(unit.actualuse().resources().find("Memory")->second.scalar().value(), 110.0);
    EXPECT_EQ(unit.allocatable().resources().find("Memory")->second.scalar().value(), 130.0);

    // 2.validate instance info
    EXPECT_EQ(unit.instances().find("id-1")->second.actualuse().resources().find("CPU")->second.scalar().value(),
              120.0);
    EXPECT_EQ(unit.instances().find("id-1")->second.actualuse().resources().find("Memory")->second.scalar().value(),
              140.0);
    EXPECT_EQ(unit.instances().find("id-2")->second.actualuse().resources().find("CPU")->second.scalar().value(),
              180.0);
    EXPECT_EQ(unit.instances().find("id-2")->second.actualuse().resources().find("Memory")->second.scalar().value(),
              220.0);

    // 3.validate disk resource
    std::vector<DiskInfo> expectedDisks = {
        {"disk1", 100, "/tmp/disk1/"},
        {"disk2", 200, "/tmp/disk2a/"},
    };
    auto diskResourceIter = unit.capacity().resources().find(metrics_type::DISK);
    ASSERT_NE(diskResourceIter, unit.capacity().resources().end());
    CheckDiskResourceMatch(diskResourceIter->second, expectedDisks);

    diskResourceIter = unit.actualuse().resources().find(metrics_type::DISK);
    ASSERT_NE(diskResourceIter, unit.actualuse().resources().end());
    CheckDiskResourceMatch(diskResourceIter->second, expectedDisks);

    diskResourceIter = unit.allocatable().resources().find(metrics_type::DISK);
    ASSERT_NE(diskResourceIter, unit.allocatable().resources().end());
    CheckDiskResourceMatch(diskResourceIter->second, expectedDisks);

    runtimeManager_->metricsClient_->StopUpdateResource();
    runtimeManager_->metricsClient_->StopDiskUsageMonitor();
    runtimeManager_->metricsClient_->StopRuntimeMemoryLimitMonitor();
}

/**
 * Feature: MetricsActor
 * Description: Build Resource Unit
 * Steps:
 * Expectation:
 */
TEST_F(MetricsActorTest, BuildResourceUnit)
{
    const char *argv[] = { "./runtime-manager", "--node_id=node1"};
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    metricsActor_->SetConfig(flags);

    // parse disk resources
    std::string diskResources = R"(
                    [{"name": "disk1", "size": "100G", "mountPoints": "/tmp/disk1/"},
                     {"name": "disk2", "size": "200G", "mountPoints": "/tmp/disk2a/"}]
                )";
    metricsActor_->ResolveDiskResourceMetricsCollector(diskResources);
    auto iter = metricsActor_->filter_.find("system-disk");
    EXPECT_NE(iter, metricsActor_->filter_.end());
    auto diskMetrics = iter->second->GetMetrics().Get();

    // given
    DevClusterMetrics npuMetrics{ "a1b2c3d4", 4, {}, { { resource_view::IDS_KEY, { 85, 90, 78, 92 } } } };
    auto given = std::vector<litebus::Future<runtime_manager::Metrics>>{
        { runtime_manager::Metrics{
            4.0, 4.0, {}, {}, runtime_manager::metrics_type::NPU, collector_type::SYSTEM, { npuMetrics } } },
        { runtime_manager::Metrics{ 100.0, 120.0, {}, {}, runtime_manager::metrics_type::CPU,} },
        { runtime_manager::Metrics{ 110.0, 130.0, {}, {}, runtime_manager::metrics_type::MEMORY } },
        { runtime_manager::Metrics{ 120.0, 140.0, { "id-1" }, {}, runtime_manager::metrics_type::CPU } },
        { runtime_manager::Metrics{ 140.0, 160.0, { "id-1" }, {}, runtime_manager::metrics_type::MEMORY } },
        { runtime_manager::Metrics{ 180.0, 200.0, { "id-2" }, {}, runtime_manager::metrics_type::CPU } },
        { runtime_manager::Metrics{ 220.0, 240.0, { "id-2" }, {}, runtime_manager::metrics_type::MEMORY } },
        {diskMetrics},
    };

    // got
    auto got = metricsActor_->BuildResourceUnit(given);
    EXPECT_EQ(metricsActor_->cardIDs_.size(), 4);

    // want
    auto unit = got;
    // 1.validate cpu/memory resource
    EXPECT_EQ(unit.capacity().resources().find("CPU")->second.scalar().value(), 120.0);
    EXPECT_EQ(unit.actualuse().resources().find("CPU")->second.scalar().value(), 100.0);
    EXPECT_EQ(unit.allocatable().resources().find("CPU")->second.scalar().value(), 120.0);
    EXPECT_EQ(unit.capacity().resources().find("Memory")->second.scalar().value(), 130.0);
    EXPECT_EQ(unit.actualuse().resources().find("Memory")->second.scalar().value(), 110.0);
    EXPECT_EQ(unit.allocatable().resources().find("Memory")->second.scalar().value(), 130.0);

    // 2.validate instance info
    EXPECT_EQ(unit.instances().find("id-1")->second.actualuse().resources().find("CPU")->second.scalar().value(),
              120.0);
    EXPECT_EQ(unit.instances().find("id-1")->second.actualuse().resources().find("Memory")->second.scalar().value(),
              140.0);
    EXPECT_EQ(unit.instances().find("id-2")->second.actualuse().resources().find("CPU")->second.scalar().value(),
              180.0);
    EXPECT_EQ(unit.instances().find("id-2")->second.actualuse().resources().find("Memory")->second.scalar().value(),
              220.0);


    // 3.validate disk resource
    std::vector<DiskInfo> expectedDisks = {
        {"disk1", 100, "/tmp/disk1/"},
        {"disk2", 200, "/tmp/disk2a/"},
    };
    auto diskResourceIter = unit.capacity().resources().find(metrics_type::DISK);
    ASSERT_NE(diskResourceIter, unit.capacity().resources().end());
    CheckDiskResourceMatch(diskResourceIter->second, expectedDisks);

    diskResourceIter = unit.actualuse().resources().find(metrics_type::DISK);
    ASSERT_NE(diskResourceIter, unit.actualuse().resources().end());
    CheckDiskResourceMatch(diskResourceIter->second, expectedDisks);

    diskResourceIter = unit.allocatable().resources().find(metrics_type::DISK);
    ASSERT_NE(diskResourceIter, unit.allocatable().resources().end());
    CheckDiskResourceMatch(diskResourceIter->second, expectedDisks);
}

#if 0
/**
 * Feature: MetricsActorTest MonitorDiskUsageTest
 * Description: monitor disk usage
 * Steps:
 * 1. Add config
 * 2. Start monitoring
 * 3. Read into file
 * Expectation:
 * 3. Report error to function agent
 */
TEST_F(MetricsActorTest, MonitorDiskUsageTest)
{
    auto mockFuncAgentActor = std::make_shared<MockFunctionAgentActor>();
    litebus::Spawn(mockFuncAgentActor);
    mockFuncAgentActor->needAutoSendResp_ = false;
    litebus::Async(metricsActor_->GetAID(), &TestMetricsActor::UpdateAgentInfo, mockFuncAgentActor->GetAID());

    litebus::os::Rmdir("/diskMonitorTestDir");
    litebus::os::Rmdir("/home/snuser/testdir");
    litebus::os::Rmdir("/tmp/MetricsActorTest/testdir");
    std::string dir = "/diskMonitorTestDir";
    litebus::os::Mkdir(dir);
    std::string portOption = "--port=" + std::to_string(port_);
    const char *argv[] = { "./runtime_manager",
                           "--node_id=node1",
                           "--ip=127.0.0.1",
                           "--host_ip=127.0.0.1",
                           "--runtime_ld_library_path=/home/sn/runtime",
                           portOption.c_str(),
                           "--agent_address=127.0.0.1:8081",
                           "--runtime_initial_port=20000",
                           "--disk_usage_monitor_path=/diskMonitorTestDir1;;/diskMonitorTestDir",
                           "--disk_usage_limit=1",
                           "--runtime_home_dir=/home/snuser",
                           "--snuser_disk_usage_limit=1",
                           "--tmp_disk_usage_limit=500",
                           "--disk_usage_monitor_duration=50",
                           "--disk_usage_monitor_notify_failure_enable=false",
    };
    runtime_manager::Flags flags;
    ASSERT_TRUE(flags.ParseFlags(15, argv).IsNone());

    metricsActor_->SetConfig(flags);
    litebus::Async(metricsActor_->GetAID(), &TestMetricsActor::StartDiskUsageMonitor);
    // write into file
    ExecuteCommand("dd if=/dev/zero of=" + dir + "/test.txt bs=2M count=1");
    EXPECT_AWAIT_TRUE([&]() -> bool { return mockFuncAgentActor->requestArray_.size() >= 1; });
    auto request = mockFuncAgentActor->requestArray_[mockFuncAgentActor->requestArray_.size()-1];
    EXPECT_TRUE(request->message().find("diskMonitorTestDir") != std::string::npos);
    mockFuncAgentActor->SendMsg(metricsActor_->GetAID(), request->requestid());
    mockFuncAgentActor->requestArray_.clear();
    litebus::os::Rmdir(dir);
    // write snuser file
    dir = "/home/snuser/testdir";
    litebus::os::Rmdir(dir);
    litebus::os::Mkdir(dir);
    ExecuteCommand("dd if=/dev/zero of=" + dir + "/test.txt bs=2M count=1");
    EXPECT_AWAIT_TRUE([&]() -> bool { return mockFuncAgentActor->requestArray_.size() >= 1; });
    request = mockFuncAgentActor->requestArray_[mockFuncAgentActor->requestArray_.size()-1];
    EXPECT_TRUE(request->message().find("snuser dir") != std::string::npos);
    mockFuncAgentActor->SendMsg(metricsActor_->GetAID(), request->requestid());
    mockFuncAgentActor->requestArray_.clear();
    litebus::os::Rmdir(dir);
    // write tmp file
    dir = "/tmp/MetricsActorTest/testdir";
    litebus::os::Rmdir(dir);
    litebus::os::Mkdir(dir);
    ExecuteCommand("dd if=/dev/zero of=" + dir + "/test.txt bs=500M count=1");
    EXPECT_AWAIT_TRUE([&]() -> bool { return mockFuncAgentActor->requestArray_.size() >= 1; });
    request = mockFuncAgentActor->requestArray_[mockFuncAgentActor->requestArray_.size()-1];
    EXPECT_TRUE(request->message().find("tmp dir") != std::string::npos);
    mockFuncAgentActor->SendMsg(metricsActor_->GetAID(), request->requestid());
    mockFuncAgentActor->requestArray_.clear();
    litebus::os::Rmdir(dir);

    litebus::Terminate(mockFuncAgentActor->GetAID());
    litebus::Await(mockFuncAgentActor->GetAID());
    litebus::os::Rmdir(dir);
}
#endif

TEST_F(MetricsActorTest, MonitorDiskUsageErrorTest)
{
    auto mockFuncAgentActor = std::make_shared<MockFunctionAgentActor>();
    litebus::Spawn(mockFuncAgentActor);
    mockFuncAgentActor->needAutoSendResp_ = false;
    litebus::Async(metricsActor_->GetAID(), &TestMetricsActor::UpdateAgentInfo, mockFuncAgentActor->GetAID());

    litebus::os::Rmdir("/diskMonitorTestDir");
    litebus::os::Rmdir("/home/snuser/testdir");
    litebus::os::Rmdir("/tmp/MetricsActorTest/testdir");
    std::string dir = "/diskMonitorTestDir";
    litebus::os::Mkdir(dir);
    std::string portOption = "--port=" + std::to_string(port_);
    const char *argv[] = { "./runtime_manager",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--runtime_ld_library_path=/home/sn/runtime",
        portOption.c_str(),
        "--agent_address=127.0.0.1:8081",
        "--runtime_initial_port=20000",
        "--disk_usage_monitor_path=/diskMonitorTestDir1;;/diskMonitorTestDir",
        "--disk_usage_limit=1",
        "--runtime_home_dir=/home/snuser",
        "--snuser_disk_usage_limit=1",
        "--tmp_disk_usage_limit=500",
        "--disk_usage_monitor_duration=50",
        "--disk_usage_monitor_notify_failure_enable=false",
    };
    runtime_manager::Flags flags;
    ASSERT_TRUE(flags.ParseFlags(15, argv).IsNone());

    metricsActor_->SetConfig(flags);
    litebus::Async(metricsActor_->GetAID(), &TestMetricsActor::StartDiskUsageMonitor);
    ExecuteCommand("mv /usr/bin/du /usr/bin/du1");
    dir = "/tmp/MetricsActorTest/testdir";
    litebus::os::Rmdir(dir);
    litebus::os::Mkdir(dir);
    ExecuteCommand("dd if=/dev/zero of=" + dir + "/test.txt bs=500M count=1");
    EXPECT_EQ(mockFuncAgentActor->requestArray_.size(), 0);
    litebus::os::Rmdir(dir);
    ExecuteCommand("mv /usr/bin/du1 /usr/bin/du");
    litebus::Terminate(mockFuncAgentActor->GetAID());
    litebus::Await(mockFuncAgentActor->GetAID());
    litebus::os::Rmdir(dir);
}

TEST_F(MetricsActorTest, OomMonitor_ExceedControlLimit_Trigger_OomKillInstance)
{
    std::string portOption = "--port=" + std::to_string(port_);
    const char *argv[] = { "./runtime_manager",
                           "--node_id=node1",
                           "--ip=127.0.0.1",
                           "--proxy_ip=",
                           "--host_ip=127.0.0.1",
                           "--runtime_ld_library_path=/home/sn/runtime",
                           portOption.c_str(),
                           "--agent_address=127.0.0.1:8081",
                           "--runtime_initial_port=20000",
                           "--memory_detection_interval=200",
                           "--oom_kill_enable=true",
                           "--oom_kill_control_limit=-1",
                           "--oom_consecutive_detection_count=2",
    };
    runtime_manager::Flags flags;
    auto ret = flags.ParseFlags(std::size(argv), argv);
    ASSERT_TRUE(ret.IsNone()) << ret.Get();
    runtimeManager_->SetConfig(flags);

    // MockFunctionAgentActor receive the "UpdateInstanceStatus" request
    auto mockFuncAgentActor = std::make_shared<MockFunctionAgentActor>();
    litebus::Spawn(mockFuncAgentActor);
    mockFuncAgentActor->needAutoSendResp_ = true;
    metricsActor_->agentAid_ = mockFuncAgentActor->GetAID();

    std::string runtimeID = "runtime01";
    std::string instanceID = "instance01";
    std::string requestID = "request01";
    pid_t testPid = 32767;
    // stub metrics data 1
    Metrics metrics1;
    metrics1.usage = 130.0; // MB count 1
    metrics1.limit = 128.0; // MB
    metrics1.instanceID = instanceID;
    metrics1.metricsType = metrics_type::MEMORY;
    metrics1.collectorType = collector_type::INSTANCE;
    // stub metrics data 2
    Metrics metrics2;
    metrics2.usage = 126.0; // MB < 128-1, count 0
    metrics2.limit = 128.0; // MB
    metrics2.instanceID = instanceID;
    metrics2.metricsType = metrics_type::MEMORY;
    metrics2.collectorType = collector_type::INSTANCE;
    // stub metrics data 3
    Metrics metrics3;
    metrics3.usage = 131.0; // MB count 1, simulate memory growth
    metrics3.limit = 128.0; // MB
    metrics3.instanceID = instanceID;
    metrics3.metricsType = metrics_type::MEMORY;
    metrics3.collectorType = collector_type::INSTANCE;
    // stub metrics data 4
    Metrics metrics4;
    metrics4.usage = 140.0; // MB count 2, trigger
    metrics4.limit = 128.0; // MB
    metrics4.instanceID = instanceID;
    metrics4.metricsType = metrics_type::MEMORY;
    metrics4.collectorType = collector_type::INSTANCE;

    // stub instanceInfos
    messages::RuntimeInstanceInfo runtimeInstanceInfo;
    runtimeInstanceInfo.set_runtimeid(runtimeID);
    runtimeInstanceInfo.set_instanceid(instanceID);
    runtimeInstanceInfo.set_requestid(requestID);
    auto deployDir = "/dcache/func/layer/function-package1/";
    auto cpuLimit = 300.0; // MB
    auto memoryLimit = 128.0; // MB
    runtimeInstanceInfo.mutable_deploymentconfig()->set_deploydir(deployDir);
    metricsActor_->AddInstance(runtimeInstanceInfo, testPid, cpuLimit, memoryLimit);

    // set OomKillInstance callback correctly
    metricsActor_->runtimeMemoryExceedLimitCallback_ =
        [this](const std::string &instanceID, const std::string &runtimeID, const std::string &requestID) {
            runtimeManager_->OomKillInstance(instanceID, runtimeID, requestID);
        };
    // stub executor data
    const std::shared_ptr<RuntimeExecutor> executor =
        std::static_pointer_cast<RuntimeExecutor>(runtimeManager_->FindExecutor(EXECUTOR_TYPE::RUNTIME)->executor_);
    executor->runtime2PID_[runtimeID] = testPid;
    // stub health check data
    runtimeManager_->healthCheckClient_->AddRuntimeRecord(mockFuncAgentActor->GetAID(), testPid, instanceID,
                                                          runtimeID, runtimeID);

    metricsActor_->RuntimeMemoryMetricsProcess({metrics1, metrics2, metrics3, metrics4, metrics4});

    // expected runtime killed
    EXPECT_AWAIT_TRUE([&]() -> bool { return executor->runtime2PID_.find(runtimeID) == executor->runtime2PID_.end(); });
    EXPECT_AWAIT_TRUE(
        [&]() -> bool { return runtimeManager_->healthCheckClient_->actor_->oomNotifyMap_.count(requestID) == 0; });

    litebus::Terminate(mockFuncAgentActor->GetAID());
    litebus::Await(mockFuncAgentActor->GetAID());
}

/**
 * Feature: MetricsActorTest CustomResourceTest
 * Description: input custom resource
 * Steps:
 * 1. Add config
 * 2. parse config
 * Expectation:
 * 3. string json format
 * 4. filter expected
 */
TEST_F(MetricsActorTest, CustomResourceTest)
{
    std::string portOption = "--port=" + std::to_string(port_);
    const char *argv[] = { "./runtime_manager",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--proxy_ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--runtime_ld_library_path=/home/sn/runtime",
        portOption.c_str(),
        "--agent_address=127.0.0.1:8081",
        "--runtime_initial_port=20000",
        "--custom_resources={\"CustomResource\": 5}",
    };
    runtime_manager::Flags flags;
    ASSERT_TRUE(flags.ParseFlags(10, argv).IsNone());

    EXPECT_EQ(flags.GetCustomResources(), "{\"CustomResource\": 5}");
    metricsActor_->ResolveCustomResourceMetricsCollector(flags.GetCustomResources());
    auto iter = metricsActor_->filter_.find("system-CustomResource");
    EXPECT_NE(iter, metricsActor_->filter_.end());
    auto metrics = iter->second->GetMetrics().Get();
    EXPECT_EQ(metrics.limit, 5.0);
    EXPECT_EQ(metrics.collectorType, runtime_manager::collector_type::SYSTEM);
    EXPECT_EQ(metrics.metricsType, "CustomResource");

    metricsActor_->ResolveCustomResourceMetricsCollector(
        "{\"CustomResource\": 5, \"CustomResource\": 6, \"CustomResource222\": xxx}");
    iter = metricsActor_->filter_.find("system-CustomResource");
    EXPECT_NE(iter, metricsActor_->filter_.end());
    metrics = iter->second->GetMetrics().Get();
    EXPECT_EQ(metrics.limit, 5.0);

    iter = metricsActor_->filter_.find("system-CustomResource222");
    EXPECT_EQ(iter, metricsActor_->filter_.end());
}

/*
 * Verifies disk resource parsing handles both valid and invalid config entries:
 * - disk1 (valid config)
 * - disk2 (invalid mount point)
 */
TEST_F(MetricsActorTest, DiskResourceTestWithMixedConfig)
{
    std::string portOption = "--port=" + std::to_string(port_);

    const char* disk_json = R"([{"name":"disk1","size":"100G","mountPoints":"/tmp/disk1/"},
        {"name":"disk2","size":"200G","mountPoints":"/tmp/invalidDisk"}])";
    std::string disk_arg = "--disk_resources=" + std::string(disk_json);
    const char *argv[] = { "./runtime_manager",
                           "--node_id=node1",
                           "--ip=127.0.0.1",
                           "--proxy_ip=127.0.0.1",
                           "--host_ip=127.0.0.1",
                           "--runtime_ld_library_path=/home/sn/runtime",
                           portOption.c_str(),
                           "--agent_address=127.0.0.1:8081",
                           "--runtime_initial_port=20000",
                           disk_arg.c_str(),

    };
    runtime_manager::Flags flags;
    ASSERT_TRUE(flags.ParseFlags(10, argv).IsNone());

    EXPECT_EQ(flags.GetDiskResources(), std::string(disk_json));
    metricsActor_->ResolveDiskResourceMetricsCollector(flags.GetDiskResources());
    auto iter = metricsActor_->filter_.find("system-disk");
    EXPECT_NE(iter, metricsActor_->filter_.end());
    auto metrics = iter->second->GetMetrics().Get();
    EXPECT_EQ(metrics.collectorType, runtime_manager::collector_type::SYSTEM);
    EXPECT_EQ(metrics.metricsType, "disk");

    auto devMetrics = metrics.devClusterMetrics.Get();

    EXPECT_EQ(devMetrics.intsInfo.count(resource_view::DISK_RESOURCE_NAME), 1);
    EXPECT_EQ(devMetrics.intsInfo.at(resource_view::DISK_RESOURCE_NAME).size(), 1);
    EXPECT_EQ(devMetrics.intsInfo.at(resource_view::DISK_RESOURCE_NAME)[0], 100);

    EXPECT_EQ(devMetrics.extensionInfo.size(), 1);
    const auto& diskExt = devMetrics.extensionInfo[0].disk();
    EXPECT_EQ(diskExt.name(), "disk1");
    EXPECT_EQ(diskExt.size(), 100);
    EXPECT_EQ(diskExt.mountpoints(), "/tmp/disk1/");
}
}  // namespace functionsystem::test