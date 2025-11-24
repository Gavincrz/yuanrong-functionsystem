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

#ifndef FUNCTION_AGENT_AGENT_SERVICE_TEST_ACTOR_H
#define FUNCTION_AGENT_AGENT_SERVICE_TEST_ACTOR_H

#include <gmock/gmock.h>
#include <litebus.hpp>
#include <async/future.hpp>
#include <unordered_set>
#include <iostream>
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::function_agent::test {

class MockActor : public litebus::ActorBase {
public:
    explicit MockActor(const std::string &name) : ActorBase(name){}
    ~MockActor() override = default;
    virtual void SendRequestToAgentServiceActor(const litebus::AID &to, std::string &&name, std::string &&msg);
    std::unordered_set<std::string> actorMessageList_;
};

class MockFunctionAgentMgrActor : public MockActor {
public:
    explicit MockFunctionAgentMgrActor(const std::string &name)
        : MockActor(name),
          deployInstanceResponse_(std::make_shared<messages::DeployInstanceResponse>()),
          killInstanceResponse_(std::make_shared<messages::KillInstanceResponse>()),
          queryInstanceStatusResponse_(std::make_shared<messages::QueryInstanceStatusResponse>()),
          updateAgentStatusResponse_(std::make_shared<messages::UpdateAgentStatusResponse>()),
          updateTokenResponse_(std::make_shared<messages::UpdateCredResponse>()),
          setNetworkIsolationResponse_(std::make_shared<messages::SetNetworkIsolationResponse>()),
          queryDebugInstanceInfosResponse_(std::make_shared<messages::QueryDebugInstanceInfosResponse>()),
          scheduleRequest_(std::make_shared<messages::ScheduleRequest>()){};
    ~MockFunctionAgentMgrActor() override = default;

    MOCK_METHOD(std::string, MockUpdateResourceResponse, ());
    MOCK_METHOD(std::string, MockUpdateAgentStatusResponse, ());
    MOCK_METHOD(std::string, MockRegisteredResponse, ());
    MOCK_METHOD(std::string, MockStaticFunctionScheduleResponse, ());

    // Simulates the Function Agent Manager Actor to receive DeployInstanceResponse messages.
    void DeployInstanceResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the Function Agent Manager Actor to receive KillInstanceResponse messages.
    void KillInstanceResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the Function Agent Manager Actor to receive UpdateResources messages.
    void UpdateResources(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the Function Agent Manager Actor to receive UpdateInstanceStatus messages.
    void UpdateInstanceStatus(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the Function Agent Manager Actor to receive QueryInstanceStatusInfoResponse messages.
    void QueryInstanceStatusInfoResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the Function Agent Manager Actor to receive UpdateAgentStatus messages.
    void UpdateAgentStatus(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the Function Agent Manager Actor to receive Register messages.
    void Register(const litebus::AID &from, std::string &&name, std::string &&msg);
    void CleanStatusResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    void UpdateCredResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    void SetNetworkIsolationResponse(const litebus::AID &, std::string &&, std::string &&msg);
    // Simulates the Function Agent Manager Actor to receive QueryDebugInstanceInfosResponse messages.
    void QueryDebugInstanceInfosResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    void StaticFunctionScheduleRequest(const litebus::AID &from, std::string &&name, std::string &&msg);
    void NotifyFunctionStatusChangeResp(const litebus::AID &from, std::string &&, std::string &&msg);

    [[maybe_unused]] [[nodiscard]] bool GetReceivedScheduleRequest() const
    {
        return receivedScheduleRequest_;
    }

    [[maybe_unused]] void ResetReceivedScheduleRequest()
    {
        receivedScheduleRequest_ = false;
    }

    [[maybe_unused]] void ResetReceivedUpdateResource()
    {
        receivedUpdateResource_ = false;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceivedUpdateResource() const
    {
        return receivedUpdateResource_;
    }

    [[maybe_unused]] void ResetReceivedUpdateAgentStatus()
    {
        receiveUpdateAgentStatus_ = false;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceivedUpdateAgentStatus() const
    {
        return receiveUpdateAgentStatus_;
    }

    [[maybe_unused]] void ResetReceivedRegisterRequest()
    {
        receivedRegisterRequest_ = false;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceivedRegisterRequest() const
    {
        return receivedRegisterRequest_;
    }

    [[maybe_unused]] void ResetDeployInstanceResponse()
    {
        deployInstanceResponse_ = std::make_shared<messages::DeployInstanceResponse>();
        deployInstanceResponseMap_ = {};
    }

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::DeployInstanceResponse> GetDeployInstanceResponse() const
    {
        return deployInstanceResponse_;
    }

    [[maybe_unused]] [[nodiscard]] std::map<std::string, std::shared_ptr<messages::DeployInstanceResponse>> GetDeployInstanceResponseMap() const
    {
        return deployInstanceResponseMap_;
    }

    [[maybe_unused]] void ResetReceivedCleanStatusResponse()
    {
        receivedCleanStatusResponse_ = false;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceivedCleanStatusResponse() const
    {
        return receivedCleanStatusResponse_;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceivedUpdateInstanceStatus() const
    {
        return receiveUpdateInstanceStatus_;
    }

    [[maybe_unused]] void ResetKillInstanceResponse()
    {
        killInstanceResponse_ = std::make_shared<messages::KillInstanceResponse>();
    }

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::KillInstanceResponse> GetKillInstanceResponse() const
    {
        return killInstanceResponse_;
    }

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::QueryInstanceStatusResponse>
        GetQueryInstanceStatusResponse() const
    {
        return queryInstanceStatusResponse_;
    }

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::UpdateAgentStatusResponse>
        GetUpdateAgentStatusRequest() const
    {
        return updateAgentStatusResponse_;
    }

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::UpdateCredResponse> GetUpdateTokenResponse() const
    {
        return updateTokenResponse_;
    }

    std::shared_ptr<messages::SetNetworkIsolationResponse> GetSetNetworkIsolationResponse()
    {
        return setNetworkIsolationResponse_;
    }

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::QueryDebugInstanceInfosResponse>
        GetQueryDebugInstanceInfosResponse() const
    {
        return queryDebugInstanceInfosResponse_;
    }

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::ScheduleRequest> GetScheduleRequest() const
    {
        return scheduleRequest_;
    }

protected:
    // litebus virtual functions
    void Init() override;

private:
    bool receivedCleanStatusResponse_{ false };
    bool receivedRegisterRequest_{ false };
    bool receivedUpdateResource_{ false };
    bool receiveUpdateAgentStatus_{ false };
    bool receiveUpdateInstanceStatus_{ false };
    bool receivedScheduleRequest_{ false };
    std::shared_ptr<messages::DeployInstanceResponse> deployInstanceResponse_;
    std::map<std::string, std::shared_ptr<messages::DeployInstanceResponse>> deployInstanceResponseMap_;
    std::shared_ptr<messages::KillInstanceResponse> killInstanceResponse_;
    std::shared_ptr<messages::QueryInstanceStatusResponse> queryInstanceStatusResponse_;
    std::shared_ptr<messages::UpdateAgentStatusResponse> updateAgentStatusResponse_;
    std::shared_ptr<messages::UpdateCredResponse> updateTokenResponse_;
    std::shared_ptr<messages::SetNetworkIsolationResponse> setNetworkIsolationResponse_;
    std::shared_ptr<messages::QueryDebugInstanceInfosResponse> queryDebugInstanceInfosResponse_;
    std::shared_ptr<messages::ScheduleRequest> scheduleRequest_;
};

class MockRuntimeManagerActor : public MockActor {
public:
    explicit MockRuntimeManagerActor(const std::string &name): MockActor(name){}

    ~MockRuntimeManagerActor() override = default;

    MOCK_METHOD(std::string, MockStartInstanceResponse, ());
    MOCK_METHOD(std::string, MockStopInstanceResponse, ());

    // Simulates the runtime manager to receive and handle StartInstance messages.
    void StartInstance(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the runtime manager to receive and handle StopInstance messages.
    void StopInstance(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the runtime manager to receive and QueryInstanceStatusInfo messages.
    void QueryInstanceStatusInfo(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the runtime manager to receive and InstanceStatusResponse messages.
    void UpdateInstanceStatusResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the runtime manager to receive and UpdateRuntimeStatusResponse messages.
    void UpdateRuntimeStatusResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    // Simulates the runtime manager to receive and QueryDebugInstanceInfos messages.
    void QueryDebugInstanceInfos(const litebus::AID &from, std::string &&name, std::string &&msg);
    void CleanStatus(const litebus::AID &from, std::string &&name, std::string &&msg);
    void UpdateCred(const litebus::AID &from, std::string &&name, std::string &&msg);

    [[maybe_unused]] void ResetReceivedStartInstanceRequest()
    {
        receiveStartInstanceRequest_ = false;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceivedStartInstanceRequest() const
    {
        return receiveStartInstanceRequest_;
    }

    [[maybe_unused]] void ResetReceivedStopInstanceRequest()
    {
        receiveStopInstanceRequest_ = false;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceivedStopInstanceRequest() const
    {
        return receiveStopInstanceRequest_;
    }

    [[maybe_unused]] void ResetReceiveCleanStatusRequest()
    {
        receiveCleanStatusRequest_ = false;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceiveCleanStatusRequest() const
    {
        return receiveCleanStatusRequest_;
    }

    [[maybe_unused]] [[nodiscard]] std::string GetRuntimeManagerID() const
    {
        return runtimeManagerID;
    }

    [[maybe_unused]] void SetIsNeedToResponse(bool isNeedToResponse)
    {
        isNeedToResponse_ = isNeedToResponse;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceiveQueryInstanceStatusInfo() const
    {
        return receiveQueryInstanceStatusInfo_;
    }

    [[maybe_unused]] [[nodiscard]] bool GetReceiveQueryDebugInstanceInfos() const
    {
        return receiveQueryDebugInstanceInfos_;
    }

    litebus::Promise<std::string> promiseOfStartInstanceRequest;
protected:
    // litebus virtual functions
    void Init() override;

private:
    bool isNeedToResponse_ { true };
    bool receiveCleanStatusRequest_{ false };
    bool receiveStartInstanceRequest_{ false };
    bool receiveStopInstanceRequest_{ false };
    bool receiveQueryInstanceStatusInfo_{ false };
    bool receiveQueryDebugInstanceInfos_{ false };
    std::string runtimeManagerID {"testRuntimeManagerID"};
};

class MockHealthCheckActor : public MockActor {
public:
    explicit MockHealthCheckActor(const std::string &name)
        : MockActor(name),
          updateInstanceStatusResponse_(std::make_shared<messages::UpdateInstanceStatusResponse>()){}

    ~MockHealthCheckActor() override = default;

    // Simulates the runtime manager to receive and InstanceStatusResponse messages.
    void UpdateInstanceStatusResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    [[maybe_unused]] void ResetUpdateInstanceStatusResponse()
    {
        updateInstanceStatusResponse_ = std::make_shared<messages::UpdateInstanceStatusResponse>();
    }

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::UpdateInstanceStatusResponse>
    GetUpdateInstanceStatusResponse() const
    {
        return updateInstanceStatusResponse_;
    }

protected:
    // litebus virtual functions
    void Init() override;

private:
    std::shared_ptr<messages::UpdateInstanceStatusResponse> updateInstanceStatusResponse_;
};

class MockMetricsActor : public MockActor {
public:
    explicit MockMetricsActor(const std::string &name)
        : MockActor(name),
          updateRuntimeStatusResponse_(std::make_shared<messages::UpdateRuntimeStatusResponse>()){}

    ~MockMetricsActor() override = default;

    // Simulates the runtime manager to receive and UpdateRuntimeStatusResponse messages.
    void UpdateRuntimeStatusResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    [[maybe_unused]] [[nodiscard]] std::shared_ptr<messages::UpdateRuntimeStatusResponse>
    GetUpdateRuntimeStatusResponse() const
    {
        return updateRuntimeStatusResponse_;
    }

    [[maybe_unused]] void ResetUpdateRuntimeStatusResponse()
    {
        updateRuntimeStatusResponse_ = std::make_shared<messages::UpdateRuntimeStatusResponse>();
    }

protected:
    // litebus virtual functions
    void Init() override;

private:
    std::shared_ptr<messages::UpdateRuntimeStatusResponse> updateRuntimeStatusResponse_;
};

class MockRegisterHelperActor : public MockActor {
public:
    explicit MockRegisterHelperActor(const std::string &name) : MockActor(name){}

    void Registered(const litebus::AID &from, std::string &&name, std::string &&msg);

    [[maybe_unused]] [[nodiscard]] bool GetReceivedRegisterRuntimeManagerResponse() const
    {
        YRLOG_INFO("return receivedRegisterRuntimeManagerResponse_ {}", receivedRegisterRuntimeManagerResponse_);
        return receivedRegisterRuntimeManagerResponse_;
    }

    [[maybe_unused]] void ResetReceivedRegisterRuntimeManagerResponse()
    {
        YRLOG_INFO("reset receivedRegisterRuntimeManagerResponse_ to false");
        receivedRegisterRuntimeManagerResponse_ = false;
    }

    messages::Registered registeredMsg_;
protected:
    // litebus virtual functions
    void Init() override;

private:
    bool receivedRegisterRuntimeManagerResponse_{ false };
};

}  // namespace functionsystem::function_agent::test

#endif  // FUNCTION_AGENT_AGENT_SERVICE_TEST_ACTOR_H