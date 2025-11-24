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

#ifndef FUNCTIONSYSTEM_TEST_UNIT_SCALER_TEST_ACTOR_H
#define FUNCTIONSYSTEM_TEST_UNIT_SCALER_TEST_ACTOR_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "function_master/scaler/scaler_actor.h"

namespace functionsystem::test {

class ScalerTestActor : public litebus::ActorBase {
public:
    explicit ScalerTestActor(const std::string &name) : ActorBase(name)
    {
        response_ = std::make_shared<messages::CreateAgentResponse>();
    }
    ~ScalerTestActor() override = default;

    void Init() override
    {
        Receive("CreateAgentResponse", &ScalerTestActor::CreateAgentResponse);
        Receive("UpdateNodeTaintsResponse", &ScalerTestActor::UpdateTaintsResponse);
        Receive("UpdateLocalStatus", &ScalerTestActor::UpdateLocalStatus);
        Receive("DeletePodResponse", &ScalerTestActor::DeletePodResponse);
    }

    void CreateAgent(const litebus::AID &to, std::string msg)
    {
        Send(to, "CreateAgent", std::move(msg));
    }

    void UpdateNodeTaints(const litebus::AID &to, std::string msg)
    {
        Send(to, "UpdateNodeTaints", std::move(msg));
    }

    void DeletePodRequest(const litebus::AID &to, std::string msg)
    {
        Send(to, "DeletePod", std::move(msg));
    }

    void CreateAgentResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto createAgentResponse = std::make_shared<messages::CreateAgentResponse>();
        if (!createAgentResponse->ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse response for CreateAgent.");
            return;
        }
        response_ = createAgentResponse;
    }

    void UpdateTaintsResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto updateTaintResponse = std::make_shared<messages::UpdateNodeTaintResponse>();
        if (!updateTaintResponse->ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse response for UpdateTaints");
            return;
        }
        taintResponse_ = updateTaintResponse;
    }

    void UpdateLocalStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto localStatus = std::make_shared<messages::UpdateLocalStatusRequest>();
        if (!localStatus->ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse response for updateLocalStatus");
            return;
        }
        localStatus_ = localStatus;
    }

    messages::CreateAgentResponse GetResponse()
    {
        if (response_) {
            return *response_;
        }
        return {};
    }

    messages::UpdateNodeTaintResponse GetTaintResponse()
    {
        if (taintResponse_) {
            return *taintResponse_;
        }
        return {};
    }

    messages::UpdateLocalStatusRequest GetLocalStatusRequest()
    {
        if (localStatus_) {
            return *localStatus_;
        }
        return {};
    }

    void DeletePodResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto deletePodResponse = std::make_shared<messages::DeletePodResponse>();
        if (!deletePodResponse->ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse request for DeletePod.");
            return;
        }
        MockDeletePodResponse(deletePodResponse);
    }
    MOCK_METHOD1(MockDeletePodResponse, void(const std::shared_ptr<messages::DeletePodResponse>));

private:
    std::shared_ptr<messages::CreateAgentResponse> response_;
    std::shared_ptr<messages::UpdateNodeTaintResponse> taintResponse_;
    std::shared_ptr<messages::UpdateLocalStatusRequest> localStatus_;
};

}  // namespace functionsystem::test

#endif  // FUNCTIONSYSTEM_TEST_UNIT_SCALER_TEST_ACTOR_H
