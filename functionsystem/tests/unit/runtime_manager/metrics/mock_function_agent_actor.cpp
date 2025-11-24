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

#include "mock_function_agent_actor.h"

#include "common/status/status.h"
#include "common/logs/logging.h"

namespace functionsystem::runtime_manager::test {
void MockFunctionAgentActor::Init()
{
    Receive("UpdateRuntimeStatus", &MockFunctionAgentActor::UpdateRuntimeStatus);
    Receive("UpdateInstanceStatus", &MockFunctionAgentActor::UpdateInstanceStatus);
    Receive("UpdateResources", &MockFunctionAgentActor::UpdateResources);
}

void MockFunctionAgentActor::UpdateRuntimeStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::UpdateRuntimeStatusRequest>();
    if (!req->ParseFromString(msg)) {
        return;
    }
    if (needAutoSendResp_) {
        Send(from, "UpdateRuntimeStatusResponse", GetUpdateRuntimeStatusResponse());
    } else {
        requestArray_.push_back(req);
    }
}

void MockFunctionAgentActor::UpdateInstanceStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    updateInstanceStatusMsg.SetValue(msg);
    auto req = std::make_shared<messages::UpdateInstanceStatusRequest>();
    if (!req->ParseFromString(msg)) {
        return;
    }
    if(needAutoSendResp_) {
        messages::UpdateInstanceStatusResponse res;
        res.set_status(static_cast<int32_t>(StatusCode::SUCCESS));
        res.set_requestid(req->requestid());
        res.set_message("No error occurs");
        YRLOG_DEBUG(res.ShortDebugString());
        Send(from, "UpdateInstanceStatusResponse", res.SerializeAsString());
    }
}

void MockFunctionAgentActor::SendMsg(const litebus::AID &to, const std::string &requestID)
{
    auto rsp = std::make_shared<messages::UpdateRuntimeStatusRequest>();
    rsp->set_requestid(requestID);
    rsp->set_status(static_cast<int32_t>(StatusCode::SUCCESS));
    rsp->set_message("update runtime status success");
    Send(to, "UpdateRuntimeStatusResponse", rsp->SerializeAsString());
}
}  // namespace functionsystem::runtime_manager::test
