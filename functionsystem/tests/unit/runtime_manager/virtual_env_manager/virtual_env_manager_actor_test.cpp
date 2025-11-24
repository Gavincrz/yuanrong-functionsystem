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

#include <gtest/gtest.h>

#include "function_agent/agent_service/agent_service_test_actor.h"
#include "manager/runtime_manager.h"
#include "mocks/mock_cmdtool.h"
#include "virtual_env_manager/virtual_env_mgr_actor.h"

namespace functionsystem::test {
using namespace functionsystem::runtime_manager;
const std::vector<std::string> envRecycleResult{ "Remove all packages in environment" };
class VirtualEnvMgrActorTest : public testing::Test {
public:
    void SetUp() override
    {
        actor_ = std::make_shared<VirtualEnvMgrActor>(RUNTIME_MANAGER_VIRTUAL_ENV_MGR_ACTOR_NAME, 0);
        actor_->SetRecycleUnusedEnvsInterval(0);
        cmdTool_ = std::make_shared<MockCmdTools>();
        litebus::Spawn(actor_);
    }

    void TearDown() override
    {
        litebus::Terminate(actor_->GetAID());
        litebus::Await(actor_->GetAID());
    }

protected:
    std::shared_ptr<VirtualEnvMgrActor> actor_;
    std::shared_ptr<MockCmdTools> cmdTool_;
};

TEST_F(VirtualEnvMgrActorTest, AddEnvReferInfos)
{
    EXPECT_CALL(*cmdTool_.get(), GetCmdResultWithError).WillRepeatedly(testing::Return(envRecycleResult));

    std::string envName = "env-1";
    std::string runtimeId = "runtimeId-1";

    actor_->AddEnvReferInfos(envName, runtimeId);

    ASSERT_EQ(actor_->GetEnvReferInfos().size(), 1);
    for (const auto &info : actor_->GetEnvReferInfos()) {
        ASSERT_EQ(info.first, envName);
        ASSERT_TRUE(info.second.runtimeIds.count(runtimeId));
    }
}

TEST_F(VirtualEnvMgrActorTest, RmEnvReferInfosOnSameEnvs)
{
    EXPECT_CALL(*cmdTool_.get(), GetCmdResultWithError).WillRepeatedly(testing::Return(envRecycleResult));

    std::string envName1 = "env-1";
    std::string runtimeId1 = "runtimeId-1";
    actor_->AddEnvReferInfos(envName1, runtimeId1);

    std::string runtimeId2 = "runtimeId-2";
    actor_->AddEnvReferInfos(envName1, runtimeId2);
    actor_->RmEnvReferInfos(runtimeId1);

    actor_->RecycleUnusedEnvs();

    ASSERT_EQ(actor_->GetEnvReferInfos().size(), 1);
}
}  // namespace functionsystem::test