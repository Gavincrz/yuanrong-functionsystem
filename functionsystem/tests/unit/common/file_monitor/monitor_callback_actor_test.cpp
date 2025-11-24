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

#include "common/file_monitor/monitor_callback_actor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "async/async.hpp"
#include "utils/os_utils.hpp"

namespace functionsystem::test {

using namespace std;
using namespace functionsystem;

class MonitorCallbackActorTest : public ::testing::Test {
    void SetUp() override
    {
        std::string mcName = "MonitorCallBack_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        monitorCallBackActor_ = std::make_shared<functionsystem::MonitorCallBackActor>(mcName, "agent001");
        litebus::Spawn(monitorCallBackActor_);
    }

    void TearDown() override
    {
        monitorCallBackActor_ = nullptr;
    }

protected:
    std::shared_ptr<MonitorCallBackActor> monitorCallBackActor_{ nullptr };
};

TEST_F(MonitorCallbackActorTest, AddDelWatchTest)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_subdirectoryconfig()->set_quota(-1);
    auto future = litebus::Async(monitorCallBackActor_->GetAID(), &MonitorCallBackActor::AddToMonitorMap, "ins001",
                                 "/tmp/dir1", request);
    EXPECT_TRUE(future.Get().IsOk());
    auto future1 =
        litebus::Async(monitorCallBackActor_->GetAID(), &MonitorCallBackActor::DeleteFromMonitorMap, "ins001");
    EXPECT_TRUE(future1.Get() == "/tmp/dir1");
}

}  // namespace functionsystem::test