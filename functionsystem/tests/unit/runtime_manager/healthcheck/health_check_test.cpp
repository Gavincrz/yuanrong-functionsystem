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

#include "runtime_manager/healthcheck/health_check.h"

#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>

#include "common/proto/pb/message_pb.h"
#include "common/utils/files.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {

class FunctionAgent : public litebus::ActorBase {
public:
    FunctionAgent() : ActorBase("MockFunctionAgent")
    {
    }
    ~FunctionAgent() override = default;

    void Init() override
    {
        Receive("UpdateInstanceStatus", &FunctionAgent::UpdateInstanceStatus);
    }

    void UpdateInstanceStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::UpdateInstanceStatusRequest req;
        req.ParseFromString(msg);
        messages::UpdateInstanceStatusResponse res;
        res.set_requestid(req.requestid());
        res.set_status(0);
        Send(from, "UpdateInstanceStatusResponse", res.SerializeAsString());
        MockUpdateInstanceStatus(from, name, msg);
    }
    MOCK_METHOD3(MockUpdateInstanceStatus, void(litebus::AID from, std::string name, std::string msg));
};

class HealthCheckTest : public testing::Test {
public:
    void SetUp() override
    {
        (void)system("dmesg -C");
    }

    void TearDown() override
    {
    }
};

/**
 * Feature: HealthCheckWithNormalReturn
 * Description: Update Process status when process normal exit
 * Steps:
 * Expectation:
 */
TEST_F(HealthCheckTest, HealthCheckWithNormalReturn)
{
    auto client = std::make_shared<runtime_manager::HealthCheck>();
    client->SetMaxSendFrequency(10);
    auto functionAgent = std::make_shared<FunctionAgent>();
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*functionAgent.get(), MockUpdateInstanceStatus(testing::_, testing::_, testing::_))
        .WillOnce(FutureArg<2>(&msgValue));
    litebus::Spawn(functionAgent);

    auto execPtr =
        litebus::Exec::CreateExec("echo HealthCheckWithNormalReturn", litebus::None(), litebus::ExecIO::CreatePipeIO(),
                                  litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO(), {}, {}, false);

    client->AddRuntimeRecord(functionAgent->GetAID(), execPtr->GetPid(), "Instance-ID", "runtime-ID", "runtime-ID");

    messages::UpdateInstanceStatusRequest req;
    EXPECT_TRUE(req.ParseFromString(msgValue.Get()));
    auto info = req.instancestatusinfo();
    EXPECT_EQ(0, execPtr->GetStatus().Get().Get());
    EXPECT_EQ(info.instanceid(), "Instance-ID");
    EXPECT_EQ(info.instancemsg(), "runtime had been returned");

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
}

/**
 * Feature: HealthCheckWithKill
 * Description: Update Process status when process was killed
 * Steps:
 * Expectation:
 */
TEST_F(HealthCheckTest, HealthCheckWithKill)
{
    auto client = std::make_shared<runtime_manager::HealthCheck>();
    auto functionAgent = std::make_shared<FunctionAgent>();
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*functionAgent.get(), MockUpdateInstanceStatus(testing::_, testing::_, testing::_))
        .WillOnce(FutureArg<2>(&msgValue));
    litebus::Spawn(functionAgent);

    std::string captureStr;
    auto func = [&](const std::string runtimeID){
        captureStr = runtimeID;
    };
    client->RegisterHandleLogPrefixExit(func);

    auto execPtr =
        litebus::Exec::CreateExec("sleep 10", litebus::None(), litebus::ExecIO::CreatePipeIO(),
                                  litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO(), {}, {}, false);
    litebus::TimerTools::AddTimer(500, "kill Process", [execPtr]() { kill(execPtr->GetPid(), SIGKILL); });

    client->AddRuntimeRecord(functionAgent->GetAID(), execPtr->GetPid(), "Instance-ID", "runtime-ID", "runtime-ID");

    messages::UpdateInstanceStatusRequest req;
    EXPECT_TRUE(req.ParseFromString(msgValue.Get()));

    auto info = req.instancestatusinfo();
    EXPECT_EQ(0, execPtr->GetStatus().Get().Get());
    EXPECT_EQ(info.instanceid(), "Instance-ID");
    EXPECT_EQ(captureStr, "runtime-ID");
    EXPECT_TRUE(info.instancemsg().find("exitState:0 exitStatus:0") != std::string::npos);

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
}

TEST_F(HealthCheckTest, HealthCheckWithRuntimeMemoryExceedLimit)
{
    auto client = std::make_shared<runtime_manager::HealthCheck>();
    auto functionAgent = std::make_shared<FunctionAgent>();
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*functionAgent.get(), MockUpdateInstanceStatus(testing::_, testing::_, testing::_))
        .WillOnce(FutureArg<2>(&msgValue));
    litebus::Spawn(functionAgent);

    // stub health check data
    auto execPtr =
        litebus::Exec::CreateExec("sleep 1", litebus::None(), litebus::ExecIO::CreatePipeIO(),
                                  litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO(), {}, {}, false);
    runtime_manager::Flags flags;
    const char *argv[] = { "/runtime_manager", "--node_id=", "--ip=127.0.0.1","--host_ip=127.0.0.1", "--port=32233",
        "--memory_detection_interval=200", "--oom_kill_enable=true" };
    flags.ParseFlags(std::size(argv), argv);
    client->SetConfig(flags);
    const std::string instanceID = "Instance-ID";
    const std::string requestID = "Request-ID";
    const std::string runtimeID = "runtime-ID";
    client->AddRuntimeRecord(functionAgent->GetAID(), execPtr->GetPid(), instanceID, runtimeID, runtimeID);

    // simulate inner oom kill
    litebus::TimerTools::AddTimer(500, "Notify InAdvance", [client, execPtr, &requestID, &instanceID, &runtimeID]() {
        client->NotifyOomKillInstanceInAdvance(requestID, instanceID, runtimeID);
        EXPECT_AWAIT_TRUE([&]() -> bool { return client->actor_->oomMap_.count(execPtr->GetPid()) > 0; });

        client->StopHeathCheckByPID(execPtr);
    });

    messages::UpdateInstanceStatusRequest req;
    EXPECT_TRUE(req.ParseFromString(msgValue.Get()));

    auto info = req.instancestatusinfo();
    EXPECT_EQ(0, execPtr->GetStatus().Get().Get());
    EXPECT_EQ(info.instanceid(), instanceID);
    EXPECT_EQ(info.type(), EXIT_TYPE::RUNTIME_MEMORY_EXCEED_LIMIT);
    EXPECT_EQ(info.status(), -1);
    EXPECT_TRUE(info.instancemsg().find("runtime memory exceed limit") != std::string::npos);

    // cleaned after inner oom kill
    EXPECT_AWAIT_TRUE([&]() -> bool {
        return client->actor_->oomMap_.count(execPtr->GetPid()) == 0
               && client->actor_->instanceID2PidMap_.count(instanceID) == 0;
    });
    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
}

/**
 * Feature: HealthCheckWhenRuntimeExceptionExitWithExceptionLog
 * Description: Update Process status when process was exit with exception log
 * Steps:
 * 1. runtime exit
 * 2. existing exception log
 * 3. existing std log
 * Expectation:
 * update instance status message contains exception log
 */
TEST_F(HealthCheckTest, HealthCheckWhenRuntimeExceptionExitWithExceptionLog)
{
    auto functionAgent = std::make_shared<FunctionAgent>();
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*functionAgent.get(), MockUpdateInstanceStatus(testing::_, testing::_, testing::_))
        .WillOnce(FutureArg<2>(&msgValue));
    litebus::Spawn(functionAgent);

    const std::string &exceptionLog = "/home/snuser/health_check/exception";
    if (!litebus::os::ExistPath(exceptionLog)) {
        litebus::os::Mkdir(exceptionLog);
    }
    const std::string &runtimeBackTraceLog = exceptionLog + "/BackTrace_runtime-ID.log";
    auto fd = open(runtimeBackTraceLog.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    EXPECT_NE(fd, -1);
    close(fd);

    std::ofstream outfile;
    outfile.open(runtimeBackTraceLog.c_str());
    outfile << "runtime ID backtrace log. This is a Test." << std::endl;
    outfile.close();

    const std::string &stdLog = "/home/snuser/health_check/instances/";
    if (!litebus::os::ExistPath(stdLog)) {
        litebus::os::Mkdir(stdLog);
    }
    const std::string &runtimeStdLog = stdLog + "_user_func_std.log";
    fd = open(runtimeStdLog.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    EXPECT_NE(fd, -1);
    close(fd);

    outfile.open(runtimeStdLog.c_str());
    outfile << "runtime ID Std log. This is a Test." << std::endl;
    outfile.close();

    auto execPtr =
        litebus::Exec::CreateExec("sleep 10", litebus::None(), litebus::ExecIO::CreatePipeIO(),
                                  litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO(), {}, {}, false);
    litebus::TimerTools::AddTimer(500, "kill Process", [execPtr]() { kill(execPtr->GetPid(), SIGXCPU); });

    auto client = std::make_shared<runtime_manager::HealthCheck>();
    runtime_manager::Flags flags;
    const char *argv[] = { "/runtime_manager", "--node_id=", "--ip=127.0.0.1","--host_ip=127.0.0.1", "--port=32233",
        "--runtime_initial_port=500", "--runtime_std_log_dir=instances", "--runtime_logs_dir=/home/snuser/health_check" };
    flags.ParseFlags(std::size(argv), argv);
    client->SetConfig(flags);
    client->AddRuntimeRecord(functionAgent->GetAID(), execPtr->GetPid(), "Instance-ID", "runtime-ID", "runtime-ID");

    messages::UpdateInstanceStatusRequest req;
    EXPECT_TRUE(req.ParseFromString(msgValue.Get()));
    auto info = req.instancestatusinfo();
    EXPECT_EQ(0, execPtr->GetStatus().Get().Get());
    EXPECT_EQ(info.instanceid(), "Instance-ID");
    EXPECT_EQ(info.instancemsg(), "runtime ID backtrace log. This is a Test.\n");

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
    (void)litebus::os::Rm(runtimeBackTraceLog);
    (void)litebus::os::Rm(runtimeStdLog);
}

/**
 * Feature: HealthCheckWhenRuntimeExceptionExitWithStdLog
 * Description: Update Process status when process was exit with std log
 * Steps:
 * 1. runtime exit
 * 2. existing std log
 * Expectation:
 * update instance status message contains std log
 */
TEST_F(HealthCheckTest, HealthCheckWhenRuntimeExceptionExitWithStdLog)
{
    auto functionAgent = std::make_shared<FunctionAgent>();
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*functionAgent.get(), MockUpdateInstanceStatus(testing::_, testing::_, testing::_))
        .WillOnce(FutureArg<2>(&msgValue));
    litebus::Spawn(functionAgent);

    const std::string &stdLog = "/home/snuser/health_check/instances";
    if (!litebus::os::ExistPath(stdLog)) {
        litebus::os::Mkdir(stdLog);
    }
    const std::string &runtimeStdLog = stdLog + "/runtime-ID-user_func_std.log";
    auto fd = open(runtimeStdLog.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    EXPECT_NE(fd, -1);
    close(fd);

    std::ofstream outfile;
    outfile.open(runtimeStdLog.c_str());
    outfile << "|runtime-ID|ERROR|runtime ID Std log. This is a Test." << std::endl;
    outfile.close();

    auto execPtr =
        litebus::Exec::CreateExec("sleep 10", litebus::None(), litebus::ExecIO::CreatePipeIO(),
                                  litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO(), {}, {}, false);
    litebus::TimerTools::AddTimer(500, "kill Process", [execPtr]() { kill(execPtr->GetPid(), SIGXCPU); });

    auto client = std::make_shared<runtime_manager::HealthCheck>();
    runtime_manager::Flags flags;
    const char *argv[] = { "/runtime_manager", "--node_id=", "--ip=127.0.0.1","--host_ip=127.0.0.1", "--port=32233",
        "--runtime_initial_port=500", "--runtime_std_log_dir=instances", "--runtime_logs_dir=/home/snuser/health_check" };
    flags.ParseFlags(std::size(argv), argv);
    client->SetConfig(flags);
    client->AddRuntimeRecord(functionAgent->GetAID(), execPtr->GetPid(), "Instance-ID", "runtime-ID", "runtime-ID");

    messages::UpdateInstanceStatusRequest req;
    EXPECT_TRUE(req.ParseFromString(msgValue.Get()));
    auto info = req.instancestatusinfo();
    EXPECT_EQ(0, execPtr->GetStatus().Get().Get());
    EXPECT_EQ(info.instanceid(), "Instance-ID");
    EXPECT_TRUE(info.instancemsg().find("with exitState(0) exitStatus(0)") != std::string::npos);

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
    (void)litebus::os::Rm(runtimeStdLog);
}

TEST_F(HealthCheckTest, HealthCheckWhenRuntimeExit)
{
    auto functionAgent = std::make_shared<FunctionAgent>();
    litebus::Future<std::string> msgValue;
    EXPECT_CALL(*functionAgent.get(), MockUpdateInstanceStatus(testing::_, testing::_, testing::_))
        .WillOnce(FutureArg<2>(&msgValue));
    litebus::Spawn(functionAgent);

    const std::string &stdLog = "/home/snuser/health_check/instances";
    if (!litebus::os::ExistPath(stdLog)) {
        litebus::os::Mkdir(stdLog);
    }
    const std::string &runtimeStdLog = stdLog + "/runtime-ID-user_func_std.log";
    auto fd = open(runtimeStdLog.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    EXPECT_NE(fd, -1);
    close(fd);

    std::ofstream outfile;
    outfile.open(runtimeStdLog.c_str());
    outfile << "|runtime-ID|ERROR|runtime ID Std log. This is a Test." << std::endl;
    outfile.close();

    auto execPtr =
        litebus::Exec::CreateExec("echo hello; exit 204;", litebus::None(), litebus::ExecIO::CreatePipeIO(),
                                  litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO(), {}, {}, false);

    auto client = std::make_shared<runtime_manager::HealthCheck>();
    runtime_manager::Flags flags;
    client->SetConfig(flags);
    const char *argv[] = { "/runtime_manager", "--node_id=", "--ip=127.0.0.1","--host_ip=127.0.0.1", "--port=32233",
        "--runtime_initial_port=500", "--runtime_std_log_dir=instances", "--runtime_logs_dir=/home/snuser/health_check" };
    flags.ParseFlags(std::size(argv), argv);
    client->SetConfig(flags);
    client->AddRuntimeRecord(functionAgent->GetAID(), execPtr->GetPid(), "Instance-ID", "runtime-ID", "runtime-ID");

    messages::UpdateInstanceStatusRequest req;
    EXPECT_TRUE(req.ParseFromString(msgValue.Get()));
    auto info = req.instancestatusinfo();

    EXPECT_TRUE(info.instancemsg().find("with exitState(1) exitStatus(204)") != std::string::npos);
    EXPECT_EQ(0, execPtr->GetStatus().Get().Get());
    EXPECT_EQ(info.instanceid(), "Instance-ID");

    litebus::Terminate(functionAgent->GetAID());
    litebus::Await(functionAgent->GetAID());
    (void)litebus::os::Rm(runtimeStdLog);
}
}  // namespace functionsystem::test