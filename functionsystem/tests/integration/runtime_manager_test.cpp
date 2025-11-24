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

#include <fcntl.h>
#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <csignal>
#include <unistd.h>

#include <async/async.hpp>
#include <exec/exec.hpp>
#include <utils/os_utils.hpp>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/utils/files.h"
#include "mocks/mock_function_agent_service_actor.h"
#include "utils.h"
#include "utils/port_helper.h"
#include "common/utils/exec_utils.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {

const std::string RUNTIME_MANAGER_NODE_ID = "dggpsmk100001-8008";
const std::string RUNTIME_MANAGER_IP = "127.0.0.1";
const std::string RUNTIME_MANAGER_RUNTIME_INITIAL_PORT = "500";
const std::string RUNTIME_MANAGER_PORT_NUM = "2000";
const std::string RUNTIME_MANAGER_IS_NEW_RUNTIME_PATH = "false";
const std::string RUNTIME_MANAGER_PYTHON_DEPENDENCY_PATH = "";
const std::string RUNTIME_MANAGER_RUNTIME_DIR = "/tmp";
const std::string RUNTIME_MANAGER_RUNTIME_LOGS_DIR = "";
const std::string RUNTIME_MANAGER_RUNTIME_LD_LIBRARY_PATH = "/tmp";
const std::string RUNTIME_MANAGER_LOG_CONFIG =
    R"(--log_config={"filepath": "/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})";
const std::string RUNTIME_MANAGER_PROC_METRICS_CPU = "2000";
const std::string RUNTIME_MANAGER_PROC_METRICS_MEMORY = "2000";
const std::string testDeployDir = "/tmp/layer/func/bucket-test-log1/yr-test-integration-runtime-manager";
const std::string funcObj = testDeployDir + "/" + "funcObj";
const std::string KILL_PROCESS_TIMEOUT_SECONDS = "1";
const std::string TEST_MONITOR_DISK_PATH = "/diskMonitorTestDir";
const std::string TEST_TENANT_ID = "tenant001";

class RuntimeManagerTest : public testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        if (!litebus::os::ExistPath("/tmp/cpp/bin")) {
            litebus::os::Mkdir("/tmp/cpp/bin");
        }

        auto fd = open("/tmp/cpp/bin/runtime", O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);

        std::ofstream outfile;
        outfile.open("/tmp/cpp/bin/runtime");
        outfile << "sleep 2" << std::endl;
        outfile.close();
    }

    void SetUp() override
    {
        auto outputPath = litebus::os::GetEnv("BIN_PATH");
        binPath_ = outputPath.Get() + "/runtime_manager";
        litebus::os::SetEnv("YR_BARE_MENTAL", "1");
        (void)litebus::os::Mkdir(testDeployDir);
        (void)TouchFile(funcObj);
        (void)system(
            "echo \"testDeployDir in integration runtime_executor_test\""
            "> /tmp/layer/func/bucket-test-log1/yr-test-integration-runtime-executor/funcObj");
        litebus::os::Rmdir(TEST_MONITOR_DISK_PATH);
    }

    void TearDown() override
    {
        (void)litebus::os::Rmdir(testDeployDir);
    }

    std::shared_ptr<MockFunctionAgentServiceActor> StartFunctionAgent()
    {
        auto functionAgent = std::make_shared<MockFunctionAgentServiceActor>();
        litebus::Spawn(functionAgent);
        return functionAgent;
    }

    static std::shared_ptr<litebus::Exec> StartRuntimeManager(
        const std::string &binPath, const std::shared_ptr<MockFunctionAgentServiceActor> &functionAgent)
    {
        YRLOG_INFO("start runtime manager");
        const std::vector<std::string> args = { " ",
                                                "--node_id=" + RUNTIME_MANAGER_NODE_ID,
                                                "--ip=" + RUNTIME_MANAGER_IP,
                                                "--host_ip=" + RUNTIME_MANAGER_IP,
                                                "--port=" + std::to_string(FindAvailablePort()),
                                                "--runtime_initial_port=" + RUNTIME_MANAGER_RUNTIME_INITIAL_PORT,
                                                "--port_num=" + RUNTIME_MANAGER_PORT_NUM,
                                                "--runtime_dir=" + RUNTIME_MANAGER_RUNTIME_DIR,
                                                "--agent_address=" + functionAgent->GetAID().Url(),
                                                "--runtime_ld_library_path=" + RUNTIME_MANAGER_RUNTIME_LD_LIBRARY_PATH,
                                                "--proc_metrics_cpu=" + RUNTIME_MANAGER_PROC_METRICS_CPU,
                                                "--proc_metrics_memory=" + RUNTIME_MANAGER_PROC_METRICS_MEMORY,
                                                "--kill_process_timeout_seconds=" + KILL_PROCESS_TIMEOUT_SECONDS,
                                                "--disk_usage_monitor_path=" + TEST_MONITOR_DISK_PATH,
                                                "--disk_usage_limit=1",
                                                "--disk_usage_monitor_duration=200",
                                                "--disk_usage_monitor_notify_failure_enable=true",
                                                RUNTIME_MANAGER_LOG_CONFIG };
        auto runtimeProcess = CreateProcess(binPath, args);
        EXPECT_TRUE(runtimeProcess.IsOK());
        return runtimeProcess.Get();
    }

    messages::StartInstanceRequest GenStartInstanceRequest()
    {
        messages::StartInstanceRequest startRequest;
        startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
        auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
        runtimeInfo->set_requestid("test_requestID");
        runtimeInfo->set_instanceid("test_instanceID");
        runtimeInfo->set_traceid("test_traceID");

        auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
        runtimeConfig->set_language("cpp");
        auto userEnvs = runtimeConfig->mutable_userenvs();
        userEnvs->insert({ "user_env1", "user_env1_value" });
        userEnvs->insert({ "user_env2", "user_env2_value" });

        auto deployConfig = runtimeInfo->mutable_deploymentconfig();
        deployConfig->set_objectid("test_objectID");
        deployConfig->set_bucketid("test_bucketID");
        deployConfig->set_deploydir(testDeployDir);
        deployConfig->set_storagetype("s3");
        return startRequest;
    }

    void PrepareWorkingDir()
    {
        (void)litebus::os::Mkdir(unzipedAppWorkingDir_);
        const std::string entrypointPath = unzipedAppWorkingDir_ + "script.py";
        (void)litebus::os::Rm(entrypointPath);
        TouchFile(entrypointPath);
        std::ofstream outfile;
        outfile.open(entrypointPath);
        outfile << "import sys" << std::endl;
        outfile << "import os" << std::endl;
        outfile << "import time" << std::endl;
        outfile << R"(print("Python executable path:", sys.executable))" << std::endl;
        outfile << R"(print("Python module search path (sys.path):", sys.path))" << std::endl; // print PYTHONPATH
        outfile << R"(print("Environment Variables:"))" << std::endl;
        outfile << R"(for key, value in os.environ.items():)" << std::endl;
        outfile << R"(    print(f"{key}={value}"))" << std::endl << std::endl;
        outfile << R"(time.sleep(60))" << std::endl; // sleep 60s
        outfile.close();
    }

    void DestroyWorkingDir()
    {
        (void)litebus::os::Rmdir(unzipedAppWorkingDir_);
    }

    messages::StartInstanceRequest GenStartJobRequest()
    {
        messages::StartInstanceRequest startRequest;
        startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
        auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
        runtimeInfo->set_requestid("test_requestID");
        runtimeInfo->set_instanceid("test_instanceID");
        runtimeInfo->set_traceid("test_traceID");

        auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
        runtimeConfig->set_language("posix-custom-runtime");
        auto userEnvs = runtimeConfig->mutable_userenvs();
        userEnvs->insert({ "user_env1", "user_env1_value" });
        userEnvs->insert({ "user_env2", "user_env2_value" });
        runtimeInfo->mutable_runtimeconfig()->mutable_posixenvs()->insert(
            { "LD_LIBRARY_PATH", "${LD_LIBRARY_PATH}:/opt/buildtools/python3.9/lib/" });
        // both UNZIPPED_WORKING_DIR and YR_WORKING_DIR are required.
        runtimeInfo->mutable_runtimeconfig()->mutable_posixenvs()->insert({"UNZIPPED_WORKING_DIR", unzipedAppWorkingDir_});
        runtimeInfo->mutable_runtimeconfig()->mutable_posixenvs()->insert({"YR_WORKING_DIR", workingDirFile_});
        runtimeInfo->mutable_runtimeconfig()->mutable_posixenvs()->insert({YR_TENANT_ID, TEST_TENANT_ID});
        runtimeInfo->mutable_runtimeconfig()->set_entryfile("python3 script.py");  // ray job entrypoint

        auto deployConfig = runtimeInfo->mutable_deploymentconfig();
        deployConfig->set_objectid("test_objectID");
        deployConfig->set_bucketid("test_bucketID");
        deployConfig->set_deploydir(testDeployDir);
        deployConfig->set_storagetype("s3");
        return startRequest;
    }

    messages::StopInstanceRequest GenStopInstanceRequest()
    {
        messages::StopInstanceRequest stopRequest;
        stopRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
        stopRequest.set_requestid("test_requestID");
        stopRequest.set_runtimeid("test_runtimeID");
        stopRequest.set_traceid("test_traceID");
        return stopRequest;
    }

    std::string binPath_;
    std::string unzipedAppWorkingDir_ = "/home/sn/function/package/xxxst/working_dir/yyy";
    std::string workingDirFile_ = "file:///home/sn/function/package/file.zip";
};

TEST_F(RuntimeManagerTest, StartInstance)
{
    auto functionAgent = StartFunctionAgent();
    auto runtimeProcess = StartRuntimeManager(binPath_, functionAgent);
    auto startRequest = GenStartInstanceRequest();

    // If the resource is not null, runtime-manager initialization is complete.
    EXPECT_AWAIT_TRUE([&]() -> bool { return functionAgent->resource_ != nullptr; });
    litebus::Async(functionAgent->GetAID(), &MockFunctionAgentServiceActor::StartInstance, startRequest);

    auto startInstanceResponseMsg = functionAgent->startInstanceResponseMsg_.GetFuture();
    EXPECT_AWAIT_READY(startInstanceResponseMsg);  // wait response
    messages::StartInstanceResponse res;
    res.ParseFromString(startInstanceResponseMsg.Get());
    EXPECT_TRUE(res.code() == StatusCode::SUCCESS);

    auto updateInstanceStatusMsg = functionAgent->updateInstanceStatusMsg_.GetFuture();
    EXPECT_AWAIT_READY(updateInstanceStatusMsg);  // wait response
    messages::UpdateInstanceStatusRequest req;
    req.ParseFromString(updateInstanceStatusMsg.Get());
    EXPECT_TRUE(req.instancestatusinfo().status() == 0);
    EXPECT_TRUE(req.instancestatusinfo().instanceid() == "test_instanceID");

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
    KillProcess(runtimeProcess->GetPid(), SIGKILL);
    (void)runtimeProcess->GetStatus().Get();
}

TEST_F(RuntimeManagerTest, StopInstance)
{
    auto functionAgent = StartFunctionAgent();
    auto runtimeProcess = StartRuntimeManager(binPath_, functionAgent);

    litebus::AID aid = functionAgent->GetAID();

    auto startRequest = GenStartInstanceRequest();
    EXPECT_GT(litebus::Async(aid, &MockFunctionAgentServiceActor::StartInstance, startRequest).Get(), 0);

    auto startInstanceResponseMsg = functionAgent->startInstanceResponseMsg_.GetFuture();
    messages::StartInstanceResponse startInstanceResponse;
    startInstanceResponse.ParseFromString(startInstanceResponseMsg.Get());
    EXPECT_EQ(startInstanceResponse.code(), StatusCode::SUCCESS);
    auto resRuntimeId = startInstanceResponse.startruntimeinstanceresponse().runtimeid();

    auto stopRequest = GenStopInstanceRequest();
    stopRequest.set_runtimeid(resRuntimeId);
    EXPECT_GT(litebus::Async(aid, &MockFunctionAgentServiceActor::StopInstance, stopRequest).Get(), 0);

    auto stopInstanceResponseMsg = functionAgent->stopInstanceResponseMsg_.GetFuture();
    messages::StopInstanceResponse stopInstanceResponse;
    stopInstanceResponse.ParseFromString(stopInstanceResponseMsg.Get());
    EXPECT_EQ(stopInstanceResponse.code(), StatusCode::SUCCESS);
    EXPECT_EQ(stopInstanceResponse.message(), "stop instance success");
    EXPECT_EQ(stopInstanceResponse.runtimeid(), resRuntimeId);
    EXPECT_EQ(stopInstanceResponse.requestid(), "test_requestID");

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
    KillProcess(runtimeProcess->GetPid(), SIGKILL);
    (void)runtimeProcess->GetStatus().Get();
}

void KillChildProcesses(pid_t parentPid)
{
    namespace fs = std::filesystem;
    std::vector<pid_t> childPids;

    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;

        const std::string pidDir = entry.path().filename().string();
        if (!std::all_of(pidDir.begin(), pidDir.end(), ::isdigit)) continue;

        pid_t pid = std::stoi(pidDir);
        std::string statPath = entry.path().string() + "/stat";

        std::ifstream statFile(statPath);
        if (!statFile.is_open()) continue;

        std::string statContent;
        std::getline(statFile, statContent);
        statFile.close();

        // Get the ppid (parent process ID) from the stat file
        size_t startPos = statContent.find(')') + 2;
        size_t spacePos = statContent.find(' ', startPos);
        std::istringstream iss(statContent.substr(spacePos + 1));
        int ppid;
        iss >> ppid;

        if (ppid == parentPid) {
            childPids.push_back(pid);
        }
    }

    // Kill child process
    for (pid_t childPid : childPids) {
        if (kill(childPid, SIGKILL) == 0) {
            std::cout << "Killed child process: " << childPid << std::endl;
        } else {
            perror(("Failed to kill child process: " + std::to_string(childPid)).c_str());
        }
    }
}

/*
TEST_F(RuntimeManagerTest, RayJobStartAndKill)
{
    PrepareWorkingDir();

    auto functionAgent = StartFunctionAgent();
    auto runtimeProcess = StartRuntimeManager(binPath_, functionAgent);
    auto startRequest = GenStartJobRequest();

    litebus::Async(functionAgent->GetAID(), &MockFunctionAgentServiceActor::StartInstance, startRequest);

    auto startInstanceResponseMsg = functionAgent->startInstanceResponseMsg_.GetFuture();
    messages::StartInstanceResponse res;
    res.ParseFromString(startInstanceResponseMsg.Get());
    EXPECT_TRUE(res.code() == StatusCode::SUCCESS);
    auto resRuntimeId = res.startruntimeinstanceresponse().runtimeid();

    YRLOG_DEBUG("kill job");
    KillChildProcesses(runtimeProcess->GetPid());

    YRLOG_DEBUG("receive update job status msg");
    auto updateInstanceStatusMsg = functionAgent->updateInstanceStatusMsg_.GetFuture();
    messages::UpdateInstanceStatusRequest req;
    req.ParseFromString(updateInstanceStatusMsg.Get());
    EXPECT_EQ(req.instancestatusinfo().status(), 9); // SIGKILL
    EXPECT_TRUE(req.instancestatusinfo().instanceid() == "test_instanceID");

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
    KillProcess(runtimeProcess->GetPid(), SIGKILL);
    runtimeProcess->GetStatus().Get();
    DestroyWorkingDir();
}
 */

TEST_F(RuntimeManagerTest, StartInstanceWithDiskMonitor)
{
    litebus::os::Mkdir(TEST_MONITOR_DISK_PATH);

    auto functionAgent = StartFunctionAgent();
    auto runtimeProcess = StartRuntimeManager(binPath_, functionAgent);

    // disk usage execeed limit must caused by instance
    auto startRequest = GenStartInstanceRequest();
    litebus::Async(functionAgent->GetAID(), &MockFunctionAgentServiceActor::StartInstance, startRequest);
    auto startInstanceResponseMsg = functionAgent->startInstanceResponseMsg_.GetFuture();
    messages::StartInstanceResponse res;
    res.ParseFromString(startInstanceResponseMsg.Get());
    EXPECT_TRUE(res.code() == StatusCode::SUCCESS);

    // write into file
    ExecuteCommand("dd if=/dev/zero of=" + TEST_MONITOR_DISK_PATH + "/test.txt bs=2M count=1");
    auto updateInstanceStatusMsg = functionAgent->updateInstanceStatusMsg_.GetFuture();
    messages::UpdateInstanceStatusRequest req;
    req.ParseFromString(updateInstanceStatusMsg.Get());
    EXPECT_AWAIT_TRUE([&]() -> bool {
        return req.instancestatusinfo().status() == static_cast<int32_t>(INSTANCE_DISK_USAGE_EXCEED_LIMIT);
    });

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
    KillProcess(runtimeProcess->GetPid(), SIGKILL);
    litebus::os::Mkdir(TEST_MONITOR_DISK_PATH);
}

}  // namespace functionsystem::test