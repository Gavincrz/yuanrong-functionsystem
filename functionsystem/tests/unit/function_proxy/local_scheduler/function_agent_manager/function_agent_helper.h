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

#ifndef UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_FUNCTION_AGENT_MANAGER_FUNCTION_AGENT_HELPER_H
#define UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_FUNCTION_AGENT_MANAGER_FUNCTION_AGENT_HELPER_H

#include <gmock/gmock.h>

#include <async/async.hpp>

#include "common/heartbeat/heartbeat_client.h"
#include "common/logs/logging.h"
#include "common/utils/generate_message.h"

namespace functionsystem::test {

class FunctionAgentHelper {
public:
    FunctionAgentHelper() = default;
    virtual ~FunctionAgentHelper() = default;

    virtual std::pair<bool, std::string> MockDeployInstance(const litebus::AID, std::string name, std::string msg)
    {
        messages::DeployInstanceResponse resp;

        messages::DeployInstanceRequest req;
        if (msg.empty() || !req.ParseFromString(msg)) {
            resp = GenDeployInstanceResponse(StatusCode::FAILED, "parameter error", req.requestid());
        } else {
            resp = GenDeployInstanceResponse(StatusCode::SUCCESS, "deploy success", req.requestid());
        }
        return { true, resp.SerializeAsString() };
    }

    virtual std::pair<bool, std::string> MockKillInstance(const litebus::AID, std::string name, std::string msg)
    {
        messages::KillInstanceResponse resp;

        messages::KillInstanceRequest req;
        if (msg.empty() || !req.ParseFromString(msg)) {
            resp = GenKillInstanceResponse(StatusCode::FAILED, "parameter error", req.requestid());
        } else {
            resp = GenKillInstanceResponse(StatusCode::SUCCESS, "kill success", req.requestid());
        }
        return { true, resp.SerializeAsString() };
    }

    static messages::Register GetRegisterMsg(const std::string &name, const std::string &address)
    {
        messages::Register registerMsg;
        registerMsg.set_name(name);
        registerMsg.set_address(address);
        return registerMsg;
    }

    [[maybe_unused]] std::string GetMsg() const
    {
        return msg_;
    }

private:
    std::string msg_;
};

}  // namespace functionsystem::test

#endif  // UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_FUNCTION_AGENT_MANAGER_FUNCTION_AGENT_HELPER_H
