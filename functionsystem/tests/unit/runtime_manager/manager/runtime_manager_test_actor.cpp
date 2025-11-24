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

#include "runtime_manager_test_actor.h"

#include "common/constants/actor_name.h"
#include "common/heartbeat/heartbeat_client.h"
#include "common/logs/logging.h"
#include "utils/port_helper.h"

namespace functionsystem::runtime_manager {
RuntimeManagerTestActor::RuntimeManagerTestActor(const std::string &name)
    : ActorBase(name),
      startInstanceResponse_(std::make_shared<messages::StartInstanceResponse>()),
      stopInstanceResponse_(std::make_shared<messages::StopInstanceResponse>()),
      registerRuntimeManagerRequest_(std::make_shared<messages::RegisterRuntimeManagerRequest>()),
      queryInstanceStatusResponse_(std::make_shared<messages::QueryInstanceStatusResponse>()),
      updateTokenResponse_(std::make_shared<messages::UpdateCredResponse>())
{
}

void RuntimeManagerTestActor::StartInstance(const litebus::AID &to, const messages::StartInstanceRequest &request)
{
    Send(to, "StartInstance", request.SerializeAsString());
}

void RuntimeManagerTestActor::StartInstanceWithString(const litebus::AID &to, std::string request)
{
    Send(to, "StartInstance", std::move(request));
}

void RuntimeManagerTestActor::StopInstance(const litebus::AID &to, const messages::StopInstanceRequest &request)
{
    Send(to, "StopInstance", request.SerializeAsString());
}
void RuntimeManagerTestActor::Init()
{
    ActorBase::Receive("StartInstanceResponse", &RuntimeManagerTestActor::StartInstanceResponse);
    ActorBase::Receive("StopInstanceResponse", &RuntimeManagerTestActor::StopInstanceResponse);
    ActorBase::Receive("Register", &RuntimeManagerTestActor::Register);
    Receive("QueryInstanceStatusInfoResponse", &RuntimeManagerTestActor::QueryInstanceStatusInfoResponse);
    Receive("CleanStatusResponse", &RuntimeManagerTestActor::CleanStatusResponse);
    Receive("UpdateCredResponse", &RuntimeManagerTestActor::UpdateCredResponse);
    Receive("QueryDebugInstanceInfosResponse", &RuntimeManagerTestActor::QueryDebugInstanceInfosResponse);
}
void RuntimeManagerTestActor::StartInstanceResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto response = std::make_shared<messages::StartInstanceResponse>();
    response->ParseFromString(msg);
    isReceiveStartInstanceResponse_ = true;
    startInstanceResponse_ = response;
    startInstanceTimes_ ++;
}
void RuntimeManagerTestActor::StopInstanceResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto response = std::make_shared<messages::StopInstanceResponse>();
    response->ParseFromString(msg);
    isReceiveStopInstanceResponse_ = true;
    stopInstanceResponse_ = response;
}

void RuntimeManagerTestActor::Register(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto request = std::make_shared<messages::RegisterRuntimeManagerRequest>();
    request->ParseFromString(msg);
    receiveTimes_ += 1;
    registerRuntimeManagerRequest_ = request;
    Send(from, "Registered", registerRuntimeManagerResponse_.SerializeAsString());
}
void RuntimeManagerTestActor::Finalize()
{
    ActorBase::Finalize();
}

[[maybe_unused]] void RuntimeManagerTestActor::ResetMessage()
{
    startInstanceResponse_ = std::make_shared<messages::StartInstanceResponse>();
    stopInstanceResponse_ = std::make_shared<messages::StopInstanceResponse>();
    registerRuntimeManagerRequest_ = std::make_shared<messages::RegisterRuntimeManagerRequest>();
    queryInstanceStatusResponse_  = std::make_shared<messages::QueryInstanceStatusResponse>();
    updateTokenResponse_ = std::make_shared<messages::UpdateCredResponse>();
    queryDebugInstanceInfosResponse_ = std::make_shared<messages::QueryDebugInstanceInfosResponse>();
}

[[maybe_unused]] std::shared_ptr<messages::StartInstanceResponse> RuntimeManagerTestActor::GetStartInstanceResponse()
{
    return startInstanceResponse_;
}
[[maybe_unused]] std::shared_ptr<messages::StopInstanceResponse> RuntimeManagerTestActor::GetStopInstanceResponse()
{
    return stopInstanceResponse_;
}
uint32_t RuntimeManagerTestActor::GetReceiveTimes()
{
    return receiveTimes_;
}

void RuntimeManagerTestActor::QueryInstanceStatusInfo(const litebus::AID &to, const messages::QueryInstanceStatusRequest &request)
{
    Send(to, "QueryInstanceStatusInfo", request.SerializeAsString());
}

void RuntimeManagerTestActor::QueryInstanceStatusInfoResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto response = std::make_shared<messages::QueryInstanceStatusResponse>();
    isReceiveQueryInstanceStatusInfoResponse_ = true;
    response->ParseFromString(msg);
    queryInstanceStatusResponse_ = response;
}

void RuntimeManagerTestActor::QueryDebugInstanceInfos(const litebus::AID &to,
                                                      const messages::QueryDebugInstanceInfosRequest &request)
{
    Send(to, "QueryDebugInstanceInfos", request.SerializeAsString());
}

void RuntimeManagerTestActor::QueryDebugInstanceInfosResponse(const litebus::AID &from, std::string &&,
                                                              std::string &&msg)
{
    auto response = std::make_shared<messages::QueryDebugInstanceInfosResponse>();
    isReceiveQueryDebugInstanceInfosResponse_ = true;
    response->ParseFromString(msg);
    queryDebugInstanceInfosResponse_ = response;
    YRLOG_DEBUG("received QueryDebugInstanceInfosResponse: {}", response->ShortDebugString());
}

void RuntimeManagerTestActor::CleanStatusResponse(const litebus::AID &, std::string &&, std::string &&)
{
    isReceiveCleanStatusResponse_ = true;
}

void RuntimeManagerTestActor::UpdateCredResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    updateTokenResponse_->ParseFromString(msg);
}

}  // namespace functionsystem::runtime_manager