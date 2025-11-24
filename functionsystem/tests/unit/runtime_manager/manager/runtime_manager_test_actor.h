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

#ifndef RUNTIME_MANAGER_MANAGER_RUNTIME_MANAGER_TEST_ACTOR_H
#define RUNTIME_MANAGER_MANAGER_RUNTIME_MANAGER_TEST_ACTOR_H

#include "actor/actor.hpp"
#include "common/proto/pb/message_pb.h"
namespace functionsystem::runtime_manager {
class RuntimeManagerTestActor : public litebus::ActorBase {
public:
    explicit RuntimeManagerTestActor(const std::string &name);

    ~RuntimeManagerTestActor() override = default;

    void StartInstance(const litebus::AID &to, const messages::StartInstanceRequest &request);

    void StartInstanceWithString(const litebus::AID &to, std::string request);

    void StopInstance(const litebus::AID &to, const messages::StopInstanceRequest &request);

    void HandlePrestartRuntimeExit(const pid_t pid){};

    void StartInstanceResponse(const litebus::AID &from, std::string && /* name */, std::string &&msg);

    void StopInstanceResponse(const litebus::AID &from, std::string && /* name */, std::string &&msg);

    void Register(const litebus::AID &from, std::string &&, std::string &&msg);

    void QueryInstanceStatusInfo(const litebus::AID &to, const messages::QueryInstanceStatusRequest &request);

    void QueryInstanceStatusInfoResponse(const litebus::AID &from, std::string &&, std::string &&msg);

    void QueryDebugInstanceInfos(const litebus::AID &to, const messages::QueryDebugInstanceInfosRequest &request);

    void QueryDebugInstanceInfosResponse(const litebus::AID &from, std::string &&, std::string &&msg);

    void CleanStatusResponse(const litebus::AID &from, std::string &&, std::string &&msg);

    void UpdateCredResponse(const litebus::AID &from, std::string &&, std::string &&msg);

    void ResetMessage();

    [[maybe_unused]] std::shared_ptr<messages::StartInstanceResponse> GetStartInstanceResponse();

    [[maybe_unused]] std::shared_ptr<messages::StopInstanceResponse> GetStopInstanceResponse();

    [[maybe_unused]] std::shared_ptr<messages::RegisterRuntimeManagerRequest> GetRegisterRuntimeManagerRequest()
    {
        return registerRuntimeManagerRequest_;
    }

    [[maybe_unused]] std::shared_ptr<messages::QueryInstanceStatusResponse> GetQueryInstanceStatusResponse()
    {
        return queryInstanceStatusResponse_;
    }

    [[maybe_unused]] std::shared_ptr<messages::QueryDebugInstanceInfosResponse> GetQueryDebugInstanceResponse()
    {
        return queryDebugInstanceInfosResponse_;
    }

    [[maybe_unused]] bool GetIsReceiveCleanStatusResponse()
    {
        return isReceiveCleanStatusResponse_;
    }

    [[maybe_unused]] void ResetIsReceiveCleanStatusResponse()
    {
        isReceiveCleanStatusResponse_ = false;
    }

    [[maybe_unused]] void SetRegisterRuntimeManagerResponse(const messages::RegisterRuntimeManagerResponse &response)
    {
        registerRuntimeManagerResponse_ = response;
    }

    [[maybe_unused]] bool GetIsReceiveStartInstanceResponse()
    {
        return isReceiveStartInstanceResponse_;
    }

    [[maybe_unused]] bool GetIsReceiveStopInstanceResponse()
    {
        return isReceiveStopInstanceResponse_;
    }

    [[maybe_unused]] bool GetIsReceiveQueryInstanceStatusInfoResponse()
    {
        return isReceiveQueryInstanceStatusInfoResponse_;
    }

    [[maybe_unused]] std::shared_ptr<messages::UpdateCredResponse> GetIsReceiveUpdateTokenResponse()
    {
        return updateTokenResponse_;
    }

    uint32_t GetReceiveTimes();

    uint32_t GetStartInstanceTimes()
    {
        return startInstanceTimes_;
    }

    void ResetStartInstanceTimes()
    {
        startInstanceTimes_ = 0;
    }

protected:
    void Init() override;

    void Finalize() override;

private:
    std::shared_ptr<messages::StartInstanceResponse> startInstanceResponse_;
    std::shared_ptr<messages::StopInstanceResponse> stopInstanceResponse_;
    std::shared_ptr<messages::RegisterRuntimeManagerRequest> registerRuntimeManagerRequest_;
    std::shared_ptr<messages::QueryInstanceStatusResponse> queryInstanceStatusResponse_;
    std::shared_ptr<messages::QueryDebugInstanceInfosResponse> queryDebugInstanceInfosResponse_;
    std::shared_ptr<messages::UpdateCredResponse> updateTokenResponse_;
    messages::RegisterRuntimeManagerResponse registerRuntimeManagerResponse_ = messages::RegisterRuntimeManagerResponse{};
    uint32_t receiveTimes_ = 0;
    uint32_t startInstanceTimes_ = 0;
    bool isReceiveCleanStatusResponse_{ false };
    bool isReceiveStartInstanceResponse_{ false };
    bool isReceiveStopInstanceResponse_{ false };
    bool isReceiveQueryInstanceStatusInfoResponse_{ false };
    bool isReceiveQueryDebugInstanceInfosResponse_{ false };
};
}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_MANAGER_RUNTIME_MANAGER_TEST_ACTOR_H
