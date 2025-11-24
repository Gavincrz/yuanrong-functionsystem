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

#include "agent_service_test_actor.h"

#include "common/logs/logging.h"

namespace functionsystem::function_agent::test {
void MockActor::SendRequestToAgentServiceActor(const litebus::AID &to, std::string &&name, std::string &&msg)
{
    // in case send wrong msg by wrong actor
    if (actorMessageList_.count(name) > 0) {
        Send(to, std::move(name), std::move(msg));
    }
}

void MockRuntimeManagerActor::Init()
{
    ActorBase::Receive("StartInstance", &MockRuntimeManagerActor::StartInstance);
    ActorBase::Receive("StopInstance", &MockRuntimeManagerActor::StopInstance);
    ActorBase::Receive("QueryInstanceStatusInfo", &MockRuntimeManagerActor::QueryInstanceStatusInfo);
    ActorBase::Receive("CleanStatus", &MockRuntimeManagerActor::CleanStatus);
    ActorBase::Receive("UpdateCred", &MockRuntimeManagerActor::UpdateCred);
    ActorBase::Receive("QueryDebugInstanceInfos", &MockRuntimeManagerActor::QueryDebugInstanceInfos);
}

void MockHealthCheckActor::Init()
{
    ActorBase::Receive("UpdateInstanceStatusResponse", &MockHealthCheckActor::UpdateInstanceStatusResponse);
}

void MockMetricsActor::Init()
{
    ActorBase::Receive("UpdateRuntimeStatusResponse", &MockMetricsActor::UpdateRuntimeStatusResponse);
}

void MockRegisterHelperActor::Init()
{
    ActorBase::Receive("Registered", &MockRegisterHelperActor::Registered);
}

void MockRuntimeManagerActor::StartInstance(const litebus::AID &from, std::string &&, std::string &&msg)
{
    receiveStartInstanceRequest_ = true;
    promiseOfStartInstanceRequest.SetValue(msg);
    if (isNeedToResponse_) {
        Send(from, "StartInstanceResponse", MockStartInstanceResponse());
    }
}

void MockRuntimeManagerActor::CleanStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    receiveCleanStatusRequest_ = true;
    if (isNeedToResponse_) {
        Send(from, "CleanStatusResponse", "");
    }
}

void MockRuntimeManagerActor::StopInstance(const litebus::AID &from, std::string &&, std::string &&)
{
    receiveStopInstanceRequest_ = true;
    if (isNeedToResponse_) {
        Send(from, "StopInstanceResponse", MockStopInstanceResponse());
    }
}

void MockHealthCheckActor::UpdateInstanceStatusResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    if (!updateInstanceStatusResponse_->ParseFromString(msg)) {
        YRLOG_DEBUG("failed to parse response messages");
        return;
    }
    YRLOG_DEBUG("received UpdateInstanceStatusResponse from {}, {}", std::string(from), msg);
}

void MockMetricsActor::UpdateRuntimeStatusResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    if (!updateRuntimeStatusResponse_->ParseFromString(msg)) {
        YRLOG_DEBUG("failed to parse response messages");
        return;
    }
    YRLOG_DEBUG("received UpdateRuntimeStatusResponse from {}, {}", std::string(from), msg);
}

void MockRuntimeManagerActor::QueryInstanceStatusInfo(const litebus::AID &from, std::string &&, std::string &&msg)
{
    receiveQueryInstanceStatusInfo_ = true;
    messages::QueryInstanceStatusRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_DEBUG("failed to parse response messages");
        return;
    }
    messages::QueryInstanceStatusResponse response;
    response.set_requestid(req.requestid());
    Send(from, "QueryInstanceStatusInfoResponse", std::move(response.SerializeAsString()));
}

void MockRuntimeManagerActor::QueryDebugInstanceInfos(const litebus::AID &from, std::string &&, std::string &&msg)
{
    receiveQueryDebugInstanceInfos_ = true;
    messages::QueryDebugInstanceInfosRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_DEBUG("failed to parse request messages");
        return;
    }
    messages::QueryDebugInstanceInfosResponse response;
    response.set_requestid(req.requestid());
    // stub data
    auto* info = response.add_debuginstanceinfos();
    info->set_pid(1);
    info->set_status("R");
    info->set_instanceid("test_instanceID");
    info->set_debugserver("127.0.0.1:12324");
    Send(from, "QueryDebugInstanceInfosResponse", std::move(response.SerializeAsString()));
}

void MockRuntimeManagerActor::UpdateCred(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_DEBUG("received UpdateCred from {}", std::string(from));
    messages::UpdateCredRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_DEBUG("failed to parse response messages");
        return;
    }
    messages::UpdateCredResponse response;
    response.set_requestid(req.requestid());
    Send(from, "UpdateCredResponse", std::move(response.SerializeAsString()));
}

void MockFunctionAgentMgrActor::Init()
{
    ActorBase::Receive("DeployInstanceResponse", &MockFunctionAgentMgrActor::DeployInstanceResponse);
    ActorBase::Receive("KillInstanceResponse", &MockFunctionAgentMgrActor::KillInstanceResponse);
    ActorBase::Receive("UpdateResources", &MockFunctionAgentMgrActor::UpdateResources);
    ActorBase::Receive("UpdateInstanceStatus", &MockFunctionAgentMgrActor::UpdateInstanceStatus);
    ActorBase::Receive("QueryInstanceStatusInfoResponse", &MockFunctionAgentMgrActor::QueryInstanceStatusInfoResponse);
    ActorBase::Receive("UpdateAgentStatus", &MockFunctionAgentMgrActor::UpdateAgentStatus);
    ActorBase::Receive("Register", &MockFunctionAgentMgrActor::Register);
    ActorBase::Receive("CleanStatusResponse", &MockFunctionAgentMgrActor::CleanStatusResponse);
    ActorBase::Receive("UpdateCredResponse", &MockFunctionAgentMgrActor::UpdateCredResponse);
    ActorBase::Receive("SetNetworkIsolationResponse", &MockFunctionAgentMgrActor::SetNetworkIsolationResponse);
    ActorBase::Receive("QueryDebugInstanceInfosResponse", &MockFunctionAgentMgrActor::QueryDebugInstanceInfosResponse);
    ActorBase::Receive("StaticFunctionScheduleRequest", &MockFunctionAgentMgrActor::StaticFunctionScheduleRequest);
    ActorBase::Receive("NotifyFunctionStatusChangeResp", &MockFunctionAgentMgrActor::NotifyFunctionStatusChangeResp);
}

void MockFunctionAgentMgrActor::DeployInstanceResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    (void)deployInstanceResponse_->ParseFromString(msg);
    auto deployInstanceResp = std::make_shared<messages::DeployInstanceResponse>();
    deployInstanceResp->ParseFromString(msg);
    deployInstanceResponseMap_[deployInstanceResp->requestid()] = deployInstanceResp;
    YRLOG_DEBUG("received deploy instance response from {}, requestID: {}, code: {}, message: {}", std::string(from),
                deployInstanceResponse_->requestid(), deployInstanceResponse_->code(),
                deployInstanceResponse_->message());
}

void MockFunctionAgentMgrActor::KillInstanceResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    (void)killInstanceResponse_->ParseFromString(msg);
    YRLOG_DEBUG("received kill instance response from {}", std::string(from));
}

void MockFunctionAgentMgrActor::UpdateResources(const litebus::AID &from, std::string &&, std::string &&)
{
    YRLOG_DEBUG("received UpdateResources request from {}", std::string(from));
    receivedUpdateResource_ = true;
}

void MockFunctionAgentMgrActor::UpdateInstanceStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    YRLOG_DEBUG("received UpdateInstanceStatus request from {}", std::string(from));
    receiveUpdateInstanceStatus_ = true;
    messages::UpdateInstanceStatusRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_DEBUG("failed to parse response messages");
        return;
    }
    messages::UpdateInstanceStatusResponse response;
    response.set_requestid(req.requestid());
    // send UpdateInstanceStatusResponse back to AgentServiceActor
    Send(from, "UpdateInstanceStatusResponse", response.SerializeAsString());
}

void MockFunctionAgentMgrActor::QueryInstanceStatusInfoResponse(const litebus::AID &from, std::string &&,
                                                                std::string &&msg)
{
    (void)queryInstanceStatusResponse_->ParseFromString(msg);
    YRLOG_DEBUG("received QueryInstanceStatusInfo response from {}", std::string(from));
}

void MockFunctionAgentMgrActor::QueryDebugInstanceInfosResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    (void)queryDebugInstanceInfosResponse_->ParseFromString(msg);
    YRLOG_DEBUG("received QueryDebugInstanceInfos response from {}", std::string(from));
}

void MockFunctionAgentMgrActor::UpdateAgentStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    YRLOG_DEBUG("received UpdateAgentStatus request from {}", std::string(from));
    receiveUpdateAgentStatus_ = true;
    messages::UpdateAgentStatusRequest request;
    (void)request.ParseFromString(msg);
    updateAgentStatusResponse_->set_requestid(request.requestid());
    updateAgentStatusResponse_->set_status(request.status());
    // send UpdateAgentStatusResponse back to AgentServiceActor
    Send(from, "UpdateAgentStatusResponse", MockUpdateAgentStatusResponse());
}

void MockFunctionAgentMgrActor::Register(const litebus::AID &from, std::string &&, std::string &&)
{
    YRLOG_DEBUG("received Register request from {}", std::string(from));
    receivedRegisterRequest_ = true;
    Send(from, "Registered", MockRegisteredResponse());
}
void MockFunctionAgentMgrActor::CleanStatusResponse(const litebus::AID &, std::string &&, std::string &&)
{
    receivedCleanStatusResponse_ = true;
}

void MockFunctionAgentMgrActor::UpdateCredResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    YRLOG_DEBUG("received UpdateCredResponse from {}", std::string(from));
    (void)updateTokenResponse_->ParseFromString(msg);
}

void MockRegisterHelperActor::Registered(const litebus::AID &from, std::string &&, std::string &&msg)
{
    YRLOG_INFO("set receivedRegisterRuntimeManagerResponse_ to true");
    receivedRegisterRuntimeManagerResponse_ = true;
    registeredMsg_.ParseFromString(msg);
    YRLOG_DEBUG("received Registered message from {}", std::string(from));
}

void MockFunctionAgentMgrActor::SetNetworkIsolationResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    (void)setNetworkIsolationResponse_->ParseFromString(msg);
    YRLOG_DEBUG("received SetNetworkIsolationResponse(requestid:{}) from {}", setNetworkIsolationResponse_->requestid(), std::string(from));
}

void MockFunctionAgentMgrActor::StaticFunctionScheduleRequest(const litebus::AID &from, std::string &&, std::string &&msg)
{
    YRLOG_INFO("set receivedScheduleRequest to true");
    receivedScheduleRequest_ = true;
    scheduleRequest_->ParseFromString(msg);
    YRLOG_DEBUG("{}|received static function schedule message from {}", scheduleRequest_->requestid(), std::string(from));
    Send(from, "StaticFunctionScheduleResponse", MockStaticFunctionScheduleResponse());
}

void MockFunctionAgentMgrActor::NotifyFunctionStatusChangeResp(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::StaticFunctionChangeResponse resp;
    if (msg.empty() || !resp.ParseFromString(msg)) {
        YRLOG_WARN("invalid request body, failed to get response of notify instance healthy change from {}.",
                   from.HashString());
        return;
    }
    YRLOG_INFO("{}|notify instance status change instance({}) successfully ", resp.requestid(), resp.instanceid());
}
}  // namespace functionsystem::function_agent::test