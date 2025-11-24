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

#include "runtime_manager/debug/debug_server_mgr.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "port/port_manager.h"
#include "runtime_manager/manager/runtime_manager_test_actor.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"

namespace functionsystem::test {
using namespace functionsystem::runtime_manager;
using namespace ::testing;

class DebugServerMgrTest : public testing::Test {
public:
    void SetUp() override
    {
        auto optionEnv = litebus::os::GetEnv("PATH");
        std::string env;
        if (optionEnv.IsSome()) {
            env_ = optionEnv.Get();
        }
    }

    void TearDown() override
    {
    }

protected:
    std::shared_ptr<DebugServerMgr> debugServerMgr_;
    std::shared_ptr<DebugServerMgrActor> debugServerMgrActor_;
    std::string env_;
};

TEST_F(DebugServerMgrTest, GdbServerNotInPath)
{
    auto optionEnv = litebus::os::GetEnv("PATH");
    std::string env;
    if (optionEnv.IsSome()) {
        env = optionEnv.Get(); // origin env
    }

    litebus::os::SetEnv("PATH", "/");
    auto debugServerMgrActor_ =
        std::make_shared<DebugServerMgrActor>(GenerateRandomName(RUNTIME_MANAGER_DEBUG_SERVER_MGR_ACTOR_NAME));
    debugServerMgr_ = std::make_shared<DebugServerMgr>(debugServerMgrActor_);

    // does not have debug server program in PATH
    auto runtimeID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto instanceID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto future = debugServerMgr_->CreateServer(runtimeID, "12888", instanceID, "cpp");
    auto status = future.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::RUNTIME_MANAGER_DEBUG_SERVER_NOTFOUND);

    future = debugServerMgr_->DestroyServer(runtimeID);
    status = future.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::RUNTIME_MANAGER_DEBUG_SERVER_NOTFOUND);
    litebus::os::SetEnv("PATH", env);
}

TEST_F(DebugServerMgrTest, CreateAndDestroyServer)
{
    litebus::os::SetEnv("PATH", litebus::os::Join("/tmp/", env_, ':'));
    (void) litebus::os::Rm("/tmp/gdbserver");
    auto fd = open("/tmp/gdbserver", O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    dprintf(fd, "#!/bin/bash\n");
    dprintf(fd, "sleep 2s\n");
    close(fd);
    auto debugServerMgrActor_ =
        std::make_shared<DebugServerMgrActor>(GenerateRandomName(RUNTIME_MANAGER_DEBUG_SERVER_MGR_ACTOR_NAME));
    debugServerMgr_ = std::make_shared<DebugServerMgr>(debugServerMgrActor_);

    const char *argv[] = { "./runtime-manager", "--runtime_logs_dir=/tmp/", "--host_ip=127.0.0.1" };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    debugServerMgr_->SetConfig(flags);
    auto runtimeID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto instanceID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto future = debugServerMgr_->CreateServer(runtimeID, "12888", instanceID, "cpp");
    auto status = future.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::SUCCESS);
    auto outFile = litebus::os::Join(debugServerMgrActor_->logDir_, runtimeID + "-cpp-debugserver.txt");
    EXPECT_AWAIT_TRUE([=]() { return FileExists(outFile); });
    future = debugServerMgr_->DestroyServer(runtimeID);
    status = future.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::SUCCESS);
    auto runtimeID2 = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    future = debugServerMgr_->DestroyServer(runtimeID2);
    status = future.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::RUNTIME_MANAGER_DEBUG_SERVER_NOTFOUND);

    (void)litebus::os::Rm("/tmp/gdbserver");
    if (FileExists(outFile)) {
        EXPECT_TRUE(litebus::os::Rm(outFile).IsNone());
    }
}

TEST_F(DebugServerMgrTest, QueryDebugInstanceInfos)
{
    auto debugServerMgrActor_ =
        std::make_shared<DebugServerMgrActor>(GenerateRandomName(RUNTIME_MANAGER_DEBUG_SERVER_MGR_ACTOR_NAME));
    debugServerMgr_ = std::make_shared<DebugServerMgr>(debugServerMgrActor_);

    const char *argv[] = { "./runtime-manager", "--runtime_logs_dir=/tmp/", "--host_ip=127.0.0.1" };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    debugServerMgr_->SetConfig(flags);

    std::vector<pid_t> testPids;
    for (int i = 0; i < 10; ++i) {
        pid_t pid = fork();
        if (pid == 0) {  // child process
            while (true) {
                sleep(1);  // keep alive
            }
            _exit(0);          // Prevents child processes from executing code outside the loop
        } else if (pid > 0) {  // parent process
            testPids.push_back(pid);
            auto runtimeID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
            debugServerMgr_->actor_->runtime2DebugServerPID_[runtimeID] = pid;
            debugServerMgr_->actor_->runtime2PID_[runtimeID] = pid;
            debugServerMgr_->actor_->pid2runtimeID_[pid] = runtimeID;
        }
    }

    // query interface
    auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    messages::QueryDebugInstanceInfosResponse response = debugServerMgr_->QueryDebugInstanceInfos(requestID).Get();
    YRLOG_DEBUG("response: {}", response.ShortDebugString());
    EXPECT_TRUE(response.debuginstanceinfos_size() > 0);

    debugServerMgr_->DestroyAllServers();
}
}