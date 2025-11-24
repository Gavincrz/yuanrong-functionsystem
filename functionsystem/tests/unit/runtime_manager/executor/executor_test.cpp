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

#include "runtime_manager/executor/executor.h"

#include "common/utils/files.h"
#include "gtest/gtest.h"
#include "port/port_manager.h"
#include "runtime_manager/executor/runtime_executor.h"
#include "runtime_manager/metrics/mock_function_agent_actor.h"
#include "utils/os_utils.hpp"

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {
const int INITIAL_PORT = 600;
const int PORT_NUM = 800;
const std::string testDeployDir = "/tmp/layer/func/bucket-test-log1/yr-test-runtime-executor";
const std::string funcObj = testDeployDir + "/" + "funcObj";
class ExecutorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        PortManager::GetInstance().InitPortResource(INITIAL_PORT, PORT_NUM);
        (void)litebus::os::Mkdir(testDeployDir);
        (void)TouchFile(funcObj);
        (void)system(
            "echo \"testDeployDir in runtime_executor_test\""
            "> /tmp/layer/func/bucket-test-log1/yr-test-runtime-executor/funcObj");
        auto mockFuncAgentActor = std::make_shared<runtime_manager::test::MockFunctionAgentActor>();
        executor_ = std::make_shared<RuntimeExecutor>("RuntimeExecutorTestActor", mockFuncAgentActor->GetAID());
        litebus::Spawn(executor_);
        litebus::os::SetEnv("YR_BARE_MENTAL", "1");
    }

    void TearDown() override
    {
        (void)litebus::os::Rmdir(testDeployDir);
        litebus::Terminate(executor_->GetAID());
        litebus::Await(executor_->GetAID());
    }

protected:
    std::shared_ptr<Executor> executor_;
};

TEST_F(ExecutorTest, GetRuntimeInstanceInfosTest)
{
    auto map = executor_->GetRuntimeInstanceInfos();
    EXPECT_EQ(map.size(), size_t(0));
}

}