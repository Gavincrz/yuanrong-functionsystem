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
#ifndef FUNCTIONCORE_CPP_INSTANCE_CTRL_HELPER_H
#define FUNCTIONCORE_CPP_INSTANCE_CTRL_HELPER_H

#include <gmock/gmock.h>

#include <async/async.hpp>

#include "function_proxy/local_scheduler/instance_control/instance_ctrl_actor.h"

namespace functionsystem::test {
static internal::ForwardKillResponse ProcRequest(const std::string &msg, const common::ErrorCode &code,
                                          const std::string &errMsg)
{
    internal::ForwardKillResponse resp;

    internal::ForwardKillRequest req;
    if (msg.empty() || !req.ParseFromString(msg)) {
        resp = GenForwardKillResponse(req.requestid(), common::ErrorCode::ERR_INNER_SYSTEM_ERROR, "req parse error");
    } else {
        resp = GenForwardKillResponse(req.requestid(), code, errMsg);
    }
    return resp;
}

class MockInstanceCtrlActor : public local_scheduler::InstanceCtrlActor {
public:
    MockInstanceCtrlActor(const std::string &name, const std::string &nodeID,
                          const local_scheduler::InstanceCtrlConfig &config)
        : InstanceCtrlActor(name, nodeID, config)
    {
    }

    ~MockInstanceCtrlActor() = default;

    void SendForwardCustomSignalRequest(const litebus::AID &server)
    {
        Send(server, "ForwardCustomSignalRequest", MockGetForwardCustomSignalRequest().SerializeAsString());
    }
    MOCK_METHOD0(MockGetForwardCustomSignalRequest, internal::ForwardKillRequest(void));

    void ForwardCustomSignalRequest(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto ret = MockForwardCustomSignalRequest(from, name, msg);
        if (ret.first) {
            Send(from, "ForwardCustomSignalResponse",
                 ProcRequest(msg, ret.second.code(), ret.second.message()).SerializeAsString());
        }
    }
    MOCK_METHOD3(MockForwardCustomSignalRequest,
                 std::pair<bool, internal::ForwardKillResponse>(const litebus::AID &, const std::string &,
                                                                const std::string &));

    void ForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockForwardCustomSignalResponse(from, name, msg);
    }
    MOCK_METHOD3(MockForwardCustomSignalResponse, void(const litebus::AID &, const std::string &, const std::string &));

    litebus::Future<Status> SendForwardCustomSignalResponse(const KillResponse &killResponse, const litebus::AID &from,
                                                            const std::string &requestID)
    {
        return MockSendForwardCustomSignalResponse(killResponse, from, requestID);
    }
    MOCK_METHOD3(MockSendForwardCustomSignalResponse,
                 litebus::Future<Status>(const KillResponse &killResponse, const litebus::AID &from,
                                         const std::string &requestID));

    virtual litebus::Future<CallResultAck> SendCallResult(const std::string &srcInstance,
                                                          const std::string &dstInstance, const std::string &dstProxyID,
                                                          const std::shared_ptr<functionsystem::CallResult> &callResult)
    {
        return MockSendCallResult(srcInstance, dstInstance, dstProxyID, callResult);
    }

    MOCK_METHOD4(MockSendCallResult,
                 litebus::Future<CallResultAck>(const std::string &, const std::string &, const std::string &,
                                                const std::shared_ptr<functionsystem::CallResult> &));

    MOCK_METHOD(void, HandleRuntimeHeartbeatLost, (const std::string &, const std::string &), (override));

    MOCK_METHOD(void, HandleInstanceHealthChange, (const std::string &, const StatusCode &), (override));

protected:
    void Init() override
    {
        Receive("ForwardCustomSignalRequest", &MockInstanceCtrlActor::ForwardCustomSignalRequest);
        Receive("ForwardCustomSignalResponse", &MockInstanceCtrlActor::ForwardCustomSignalResponse);
    }
};

class InstanceCtrlHelper {
public:
    InstanceCtrlHelper() = default;
    virtual ~InstanceCtrlHelper() = default;

    virtual std::pair<bool, internal::ForwardKillResponse> MockForwardCustomSignalRequestSuccess(
        const litebus::AID &from, const std::string &name, const std::string &msg)
    {
        return { true, ProcRequest(msg, common::ErrorCode::ERR_NONE, "") };
    }

    virtual std::pair<bool, internal::ForwardKillResponse> MockForwardCustomSignalRequestFail(const litebus::AID &from,
                                                                                              const std::string &name,
                                                                                              const std::string &msg)
    {
        return { true, ProcRequest(msg, common::ErrorCode::ERR_INNER_SYSTEM_ERROR, "forward custom signal fail") };
    }
};
}  // namespace functionsystem::test

#endif  // FUNCTIONCORE_CPP_INSTANCE_CTRL_HELPER_H