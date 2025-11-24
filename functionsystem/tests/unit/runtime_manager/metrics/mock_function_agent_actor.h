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

#ifndef TEST_UNIT_RUNTIME_MANAGER_METRICS_MOCK_FUNCTION_AGENT_ACTOR_H
#define TEST_UNIT_RUNTIME_MANAGER_METRICS_MOCK_FUNCTION_AGENT_ACTOR_H

#include <gmock/gmock.h>

#include "actor/actor.hpp"
#include "async/future.hpp"

#include "common/proto/pb/message_pb.h"

namespace functionsystem::runtime_manager::test {
class MockFunctionAgentActor : public litebus::ActorBase {
public:
    MockFunctionAgentActor() : litebus::ActorBase("MockFunctionAgentActor")
    {
    }

    void UpdateRuntimeStatus(const litebus::AID &from, std::string &&name, std::string &&msg);

    void UpdateInstanceStatus(const litebus::AID &from, std::string &&name, std::string &&msg);

    void UpdateResources(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        updateUpdateResourcesMsg_.SetValue(msg);
    }

    void SendMsg(const litebus::AID &to, const std::string &requestID);

    MOCK_METHOD(std::string, GetUpdateRuntimeStatusResponse, ());
    litebus::Promise<std::string> updateInstanceStatusMsg;
    litebus::Promise<std::string> updateUpdateResourcesMsg_;
    std::vector<std::shared_ptr<messages::UpdateRuntimeStatusRequest>> requestArray_;
    bool needAutoSendResp_{ true };
protected:
    void Init() override;
};
}  // namespace functionsystem::runtime_manager::test

#endif  // TEST_UNIT_RUNTIME_MANAGER_METRICS_MOCK_FUNCTION_AGENT_ACTOR_H
