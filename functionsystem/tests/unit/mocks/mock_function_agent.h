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

#ifndef UT_MOCKS_MOCK_FUNCTION_AGENT_H
#define UT_MOCKS_MOCK_FUNCTION_AGENT_H

#include <gmock/gmock.h>

#include "common/heartbeat/heartbeat_client.h"
#include "function_agent/agent_service_actor.h"

namespace functionsystem::test {

class MockFunctionAgent : public function_agent::AgentServiceActor {
public:
    MockFunctionAgent(const std::string &name, const std::string &agentID, const litebus::AID &localSchedulerAID,
                      const S3Config &s3Config, const messages::CodePackageThresholds &codePackageThresholds,
                      uint32_t pingTimeoutMs = 0, const std::string &alias = "")
        : function_agent::AgentServiceActor(name, agentID,
                                            function_agent::AgentServiceActor::Config{
                                                localSchedulerAID, s3Config, codePackageThresholds, pingTimeoutMs },
                                            alias)
    {
    }
    ~MockFunctionAgent() = default;

    void RegisterToLocalScheduler(const litebus::AID &server)
    {
        std::string registerMsg = MockRegister();
        Send(server, "Register", std::move(registerMsg));
    }
    MOCK_METHOD0(MockRegister, std::string());

    void Registered(const litebus::AID &from, std::string &&name, std::string &&msg) override
    {
        MockRegistered(from, name, msg);
    }
    MOCK_METHOD3(MockRegistered, void(const litebus::AID, std::string, std::string));

    void DeployInstance(const litebus::AID &from, std::string &&name, std::string &&msg) override
    {
        std::pair<bool, std::string> ret = MockDeployInstance(from, name, msg);
        if (ret.first) {
            Send(from, "DeployInstanceResponse", std::move(ret.second));
        }
    }
    MOCK_METHOD3(MockDeployInstance, std::pair<bool, std::string>(litebus::AID, std::string, std::string));

    void KillInstance(const litebus::AID &from, std::string &&name, std::string &&msg) override
    {
        std::pair<bool, std::string> ret = MockKillInstance(from, name, msg);
        if (ret.first) {
            Send(from, "KillInstanceResponse", std::move(ret.second));
        }
    }
    MOCK_METHOD3(MockKillInstance, std::pair<bool, std::string>(litebus::AID, std::string, std::string));

    void UpdateResources(const litebus::AID &server, const messages::UpdateResourcesRequest &request)
    {
        Send(server, "UpdateResources", request.SerializeAsString());
    }

    void UpdateInstanceStatus(const litebus::AID &server, const messages::UpdateInstanceStatusRequest &request)
    {
        Send(server, "UpdateInstanceStatus", request.SerializeAsString());
    }

    void UpdateInstanceStatusResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockUpdateInstanceStatusResponse(from, name, msg);
    }
    MOCK_METHOD3(MockUpdateInstanceStatusResponse, void(const litebus::AID, std::string, std::string));

    void UpdateAgentStatus(const litebus::AID &to, const messages::UpdateAgentStatusRequest &request)
    {
        Send(to, "UpdateAgentStatus", request.SerializeAsString());
    }

    void UpdateAgentStatusResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockUpdateAgentStatusResponse(from, name, msg);
    }
    MOCK_METHOD3(MockUpdateAgentStatusResponse, void(const litebus::AID, std::string, std::string));

    void CleanStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        std::pair<bool, std::string> ret = MockCleanStatusResponse(from, name, msg);
        if (ret.first) {
            Send(from, "CleanStatusResponse", std::move(ret.second));
        }
    }
    MOCK_METHOD3(MockCleanStatusResponse, std::pair<bool, std::string>(litebus::AID, std::string, std::string));

    void UpdateCred(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        std::pair<bool, std::string> ret = MockUpdateTokenResponse(from, name, msg);
        if (ret.first) {
            Send(from, "UpdateCredResponse", std::move(ret.second));
        }
    }
    MOCK_METHOD3(MockUpdateTokenResponse, std::pair<bool, std::string>(litebus::AID, std::string, std::string));

    void QueryDebugInstanceInfos(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::QueryDebugInstanceInfosRequest req;
        req.ParseFromString(msg);
        messages::QueryDebugInstanceInfosResponse rsp = MockQueryDebugInstanceInfos();
        rsp.set_requestid(req.requestid());
        Send(from, "QueryDebugInstanceInfosResponse", std::move(rsp.SerializeAsString()));
    }
    MOCK_METHOD0(MockQueryDebugInstanceInfos, messages::QueryDebugInstanceInfosResponse());

    void StaticFunctionScheduleRequest(const litebus::AID &server, const messages::ScheduleRequest &request)
    {
        Send(server, "StaticFunctionScheduleRequest", request.SerializeAsString());
    }

    void StaticFunctionScheduleResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockStaticFunctionScheduleResponse(from, name, msg);
    }
    MOCK_METHOD3(MockStaticFunctionScheduleResponse, void(const litebus::AID, std::string, std::string));

    void NotifyFunctionStatusChange(const litebus::AID &from, std::string &&name, std::string &&msg) override
    {
        std::pair<bool, std::string> ret = MockNotifyFunctionStatusChange(from, name, msg);
        if (ret.first) {
            Send(from, "NotifyFunctionStatusChangeResp", std::move(ret.second));
        }
    }
    MOCK_METHOD3(MockNotifyFunctionStatusChange, std::pair<bool, std::string>(litebus::AID, std::string, std::string));

protected:
    void Init() override
    {
        Receive("Registered", &MockFunctionAgent::Registered);
        Receive("DeployInstance", &MockFunctionAgent::DeployInstance);
        Receive("KillInstance", &MockFunctionAgent::KillInstance);
        Receive("UpdateInstanceStatusResponse", &MockFunctionAgent::UpdateInstanceStatusResponse);
        Receive("UpdateAgentStatusResponse", &MockFunctionAgent::UpdateAgentStatusResponse);
        Receive("CleanStatus", &MockFunctionAgent::CleanStatus);
        Receive("UpdateCred", &MockFunctionAgent::UpdateCred);
        Receive("QueryDebugInstanceInfos", &MockFunctionAgent::QueryDebugInstanceInfos);
        Receive("StaticFunctionScheduleResponse", &MockFunctionAgent::StaticFunctionScheduleResponse);
        Receive("NotifyFunctionStatusChange", &MockFunctionAgent::NotifyFunctionStatusChange);
    }
};

}  // namespace functionsystem::test

#endif