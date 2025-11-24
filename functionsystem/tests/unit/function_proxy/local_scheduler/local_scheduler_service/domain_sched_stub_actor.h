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

#ifndef UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_DOMAIN_SCHEDULER_STUB_ACTOR_H
#define UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_DOMAIN_SCHEDULER_STUB_ACTOR_H

#include <actor/actor.hpp>
#include <async/future.hpp>

#include <gmock/gmock.h>

#include "common/proto/pb/message_pb.h"

namespace functionsystem::test {
class DomainSchedStubActor : public litebus::ActorBase {
public:
    explicit DomainSchedStubActor(const std::string &name) : ActorBase(name) {
        YRLOG_INFO("start domain stub: {}", name);
    }
    ~DomainSchedStubActor() = default;

    void Register(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("receive register from: {}", std::string(from));
        RegisterCall();
        auto registeredMsg = MockRegister();
        Send(from, "Registered", std::move(registeredMsg));
    }
    MOCK_METHOD0(MockRegister, std::string());

    void ForwardSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto forwardSchedRsp = MockForwardSchedule();
        MockForwardScheduleWithParam(from, name, msg);
        Send(from, "ResponseForwardSchedule", std::move(forwardSchedRsp));
    }
    MOCK_METHOD0(MockForwardSchedule, std::string());
    MOCK_METHOD3(MockForwardScheduleWithParam, void(const litebus::AID, std::string, std::string));
    MOCK_METHOD(void, RegisterCall, ());

    void NotifyWorkerStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto request = std::make_shared<messages::NotifyWorkerStatusRequest>();
        if(!request->ParseFromString(msg)) {
            YRLOG_ERROR("invalid request message from {}", std::string(from));
            return;
        }
        messages::NotifyWorkerStatusResponse response;
        response.set_workerip(MockNotifyWorkerStatus());
        response.set_healthy(request->healthy());
        Send(from, "ResponseNotifyWorkerStatus", response.SerializeAsString());
    }
    MOCK_METHOD0(MockNotifyWorkerStatus, std::string());

    void DeletePod(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto deletePodRequest = std::make_shared<messages::DeletePodRequest>();
        if (!deletePodRequest->ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse request for DeletePod.");
            return;
        }
        auto resp = std::make_shared<messages::DeletePodResponse>();
        resp->set_code(MockDeletePodResponse());
        resp->set_requestid(deletePodRequest->requestid());
        Send(from, "DeletePodResponse", resp->SerializeAsString());
    }
    MOCK_METHOD0(MockDeletePodResponse, int32_t());

    void TryCancelSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto cancelRequest = std::make_shared<messages::CancelSchedule>();
        if (!cancelRequest->ParseFromString(msg)) {
            YRLOG_ERROR("invalid request message from {} for cancel schedule", std::string(from));
            return;
        }
        auto resp = std::make_shared<messages::CancelScheduleResponse>();
        resp->set_msgid(cancelRequest->msgid());
        auto status = std::make_shared<messages::FunctionSystemStatus>();
        resp->mutable_status()->set_code(static_cast<common::ErrorCode>(MockCancelScheduleResponse()));
        Send(from, "TryCancelResponse", resp->SerializeAsString());
    }
    MOCK_METHOD0(MockCancelScheduleResponse, int32_t());

protected:
    void Init() override
    {
        Receive("Register", &DomainSchedStubActor::Register);
        Receive("ForwardSchedule", &DomainSchedStubActor::ForwardSchedule);
        Receive("NotifyWorkerStatus", &DomainSchedStubActor::NotifyWorkerStatus);
        Receive("DeletePod", &DomainSchedStubActor::DeletePod);
        Receive("TryCancelSchedule", &DomainSchedStubActor::TryCancelSchedule);
    }
    void Finalize() override
    {
    }
};
}

#endif  // UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_DOMAIN_SCHEDULER_STUB_ACTOR_H
