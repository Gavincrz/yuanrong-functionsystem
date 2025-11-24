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

#ifndef UNIT_MOCKS_MOCK_INSTANCE_STATE_MACHINE_H
#define UNIT_MOCKS_MOCK_INSTANCE_STATE_MACHINE_H

#include <gmock/gmock.h>

#include "function_proxy/common/state_machine/instance_state_machine.h"

namespace functionsystem::test {

class MockInstanceStateMachine : public InstanceStateMachine {  // NOLINT
public:
    MockInstanceStateMachine(const std::string &nodeID, const std::shared_ptr<InstanceContext> &context)
        : InstanceStateMachine(nodeID, context, false)
    {
    }
    explicit MockInstanceStateMachine(const std::string &nodeID) : InstanceStateMachine(nodeID, nullptr, false)
    {
    }
    ~MockInstanceStateMachine() override
    {
    }

    MOCK_METHOD(litebus::Future<TransitionResult>, TransitionToImpl,
                (const InstanceState &newState, const int64_t version, const std::string &msg, bool persistence,
                 int32_t errCode));

    litebus::Future<TransitionResult> TransitionTo(const TransContext &context) override
    {
        return TransitionToImpl(context.newState, context.version, context.msg, context.persistence, context.errCode);
    }

    MOCK_METHOD(litebus::Future<Status>, DelInstance, (const std::string &instanceID));
    MOCK_METHOD(InstanceState, GetInstanceState, (), (override));
    MOCK_METHOD(litebus::Future<Status>, TryExitInstance,
                (const std::shared_ptr<litebus::Promise<Status>> &promise, const std::shared_ptr<KillContext> &killCtx,
                 bool isSynchronized), (override));
    MOCK_METHOD(void, ReleaseOwner, (), (override));
    MOCK_METHOD(std::string, GetRuntimeID, (), (override));
    MOCK_METHOD(resources::InstanceInfo, GetInstanceInfo, (), (override));
    MOCK_METHOD(void, AddStateChangeCallback,
                (const std::unordered_set<InstanceState> &statesConcerned,
                 const std::function<void(const resources::InstanceInfo &)> &callback, const std::string &key),
                (override));
    MOCK_METHOD(void, UpdateScheduleReq, (const std::shared_ptr<messages::ScheduleRequest> &scheduleReq), (override));
    MOCK_METHOD(void, SetScheduleTimes, (const int32_t &scheduleTimes));
    MOCK_METHOD(int32_t, GetScheduleTimes, ());
    MOCK_METHOD(int32_t, GetDeployTimes, ());
    MOCK_METHOD(std::shared_ptr<messages::ScheduleRequest>, GetScheduleRequest, (), (override));
    MOCK_METHOD(void, SetFunctionAgentIDAndHeteroConfig, (const schedule_decision::ScheduleResult &result), (override));

    MOCK_METHOD(void, SetDataSystemHost, (const std::string &ip), (override));

    MOCK_METHOD(void, SetRuntimeID, (const std::string &runtimeID), (override));

    MOCK_METHOD(void, SetStartTime, (const std::string &timeInfo), (override));

    MOCK_METHOD(void, SetRuntimeAddress, (const std::string &address), (override));

    MOCK_METHOD(void, IncreaseScheduleRound, (), (override));

    MOCK_METHOD(void, UpdateInstanceInfo, (const resources::InstanceInfo &instanceInfo), (override));

    MOCK_METHOD(void, SetVersion, (const int64_t version), (override));

    MOCK_METHOD(int64_t, GetVersion, (), (override));
    MOCK_METHOD(std::string, GetOwner, (), (override));

    MOCK_METHOD(bool, IsSaving, (), (override));
    MOCK_METHOD(int64_t, GetGracefulShutdownTime, (), (override));
    MOCK_METHOD(void, SetGracefulShutdownTime, (const int64_t time), (override));
    MOCK_METHOD(int32_t, GetLastSaveFailedState, (), (override));
    MOCK_METHOD(int32_t, GetLastSaveFailedErrCode, (), (override));
    MOCK_METHOD(void, ResetLastSaveFailedState, (), (override));
    MOCK_METHOD(litebus::Future<resources::InstanceInfo>, SyncInstanceFromMetaStore, (), (override));
    MOCK_METHOD(void, ExecuteStateChangeCallback, (const std::string &requestID, const InstanceState newState), (override));
    MOCK_METHOD(std::string, GetRequestID, (), (override));
    MOCK_METHOD(std::shared_ptr<InstanceContext>, GetInstanceContextCopy, (), (override));
    MOCK_METHOD(litebus::Future<std::string>, GetCancelFuture, (), (override));
    MOCK_METHOD(int64_t, GetModRevision, (), (override));
};
}  // namespace functionsystem::test

#endif  // UNIT_MOCKS_MOCK_INSTANCE_STATE_MACHINE_H
