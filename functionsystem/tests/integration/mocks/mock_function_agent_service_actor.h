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

#ifndef TEST_INTEGRATION_MOCKS_MOCK_FUNCTION_AGENT_H
#define TEST_INTEGRATION_MOCKS_MOCK_FUNCTION_AGENT_H

#include <gmock/gmock.h>

#include <actor/actor.hpp>
#include <async/async.hpp>

#include "common/proto/pb/message_pb.h"
#include "common/register/register_helper.h"
#include "common/status/status.h"

namespace functionsystem::test {

const std::string MOCK_AGENT_SERVICE_NAME = "AgentServiceActor";

class MockFunctionAgentServiceActor : public litebus::ActorBase {
public:
    MockFunctionAgentServiceActor()
        : ActorBase(MOCK_AGENT_SERVICE_NAME), registerHelper_(std::make_shared<RegisterHelper>(MOCK_AGENT_SERVICE_NAME))
    {
        registerHelper_->SetRegisterCallback(
            std::bind(&MockFunctionAgentServiceActor::RegisterHandler, this, std::placeholders::_1));
    }

    void Init() override
    {
        Receive("UpdateResources", &MockFunctionAgentServiceActor::UpdateResources);
        Receive("StartInstanceResponse", &MockFunctionAgentServiceActor::StartInstanceResponse);
        Receive("UpdateInstanceStatus", &MockFunctionAgentServiceActor::UpdateInstanceStatus);
        Receive("StopInstanceResponse", &MockFunctionAgentServiceActor::StopInstanceResponse);
        Receive("UpdateRuntimeStatus", &MockFunctionAgentServiceActor::UpdateRuntimeStatus);
    }

    void RegisterHandler(const std::string &message)
    {
        messages::RegisterRuntimeManagerResponse rsp;
        messages::RegisterRuntimeManagerRequest req;
        if (!req.ParseFromString(message)) {
            YRLOG_ERROR("failed to parse RuntimeManager register message");
            return;
        }
        runtimeManagerAID_ = litebus::AID(req.name(), req.address());
        registerHelper_->SetHeartbeatObserveDriver(
            req.name(), req.address(), 12000, [](const litebus::AID &, HeartbeatConnection) {
                YRLOG_WARN("heartbeat timeouts");
            }, MOCK_AGENT_SERVICE_NAME);
        // send Registered message back to runtime_manager
        rsp.set_code((int32_t)StatusCode::SUCCESS);
        registerHelper_->SendRegistered(req.name(), req.address(), rsp.SerializeAsString());
    }

    void UpdateResources(const litebus::AID & /* from */, std::string && /* name */, std::string &&msg)
    {
        resource_ = std::make_shared<messages::UpdateResourcesRequest>();
        EXPECT_TRUE(resource_->ParseFromString(msg));
    }

    void UpdateInstanceStatus(const litebus::AID &from, std::string && /* name */, std::string &&msg)
    {
        YRLOG_DEBUG("receive UpdateInstanceStatus msg from {}", std::string(from));
        updateInstanceStatusMsg_.SetValue(msg);
        messages::UpdateInstanceStatusRequest req;
        req.ParseFromString(msg);
        auto requestID = req.requestid();
        messages::UpdateInstanceStatusResponse res;
        res.set_requestid(requestID);
        Send(from, "UpdateInstanceStatusResponse", res.SerializeAsString());
    }

    void UpdateRuntimeStatus(const litebus::AID &from, std::string && /* name */, std::string &&msg)
    {
        YRLOG_DEBUG("receive UpdateRuntimeStatus msg from {}", std::string(from));
        updateRuntimeStatusMsg_.SetValue(msg);
        messages::UpdateInstanceStatusRequest req;
        req.ParseFromString(msg);
        auto requestID = req.requestid();
        messages::UpdateRuntimeStatusResponse rsp;
        rsp.set_requestid(req.requestid());
        rsp.set_status(static_cast<int32_t>(StatusCode::SUCCESS));
        rsp.set_message("update runtime status success");
        Send(from, "UpdateRuntimeStatusResponse", rsp.SerializeAsString());
    }

    int StartInstance(const messages::StartInstanceRequest &request)
    {
        YRLOG_INFO("Send StartInstance request to {}", std::string(runtimeManagerAID_));
        return Send(runtimeManagerAID_, "StartInstance", request.SerializeAsString());
    }

    void StartInstanceResponse(const litebus::AID &from, std::string && /* name */, std::string &&msg)
    {
        YRLOG_INFO("Receive StartInstance response from {}", std::string(from));
        startInstanceResponseMsg_.SetValue(msg);
    }

    int StopInstance(const messages::StopInstanceRequest &request)
    {
        YRLOG_INFO("Send StopInstance request to {}", std::string(runtimeManagerAID_));
        return Send(runtimeManagerAID_, "StopInstance", request.SerializeAsString());
    }

    void StopInstanceResponse(const litebus::AID &from, std::string && /* name */, std::string &&msg)
    {
        YRLOG_INFO("Receive StopInstance response from {}", std::string(from));
        stopInstanceResponseMsg_.SetValue(msg);
    }

    // receive resource when runtime-manager initialize done
    std::shared_ptr<messages::UpdateResourcesRequest> resource_;

    litebus::AID runtimeManagerAID_;
    litebus::Promise<std::string> startInstanceResponseMsg_;
    litebus::Promise<std::string> updateInstanceStatusMsg_;
    litebus::Promise<std::string> stopInstanceResponseMsg_;
    litebus::Promise<std::string> updateRuntimeStatusMsg_;
    std::shared_ptr<RegisterHelper> registerHelper_;
};

}  // namespace functionsystem::test

#endif  // TEST_INTEGRATION_MOCKS_MOCK_FUNCTION_AGENT_H
