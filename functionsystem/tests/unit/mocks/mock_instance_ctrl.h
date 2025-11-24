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

#ifndef UT_MOCKS_MOCK_INSTANCE_CTRL_H
#define UT_MOCKS_MOCK_INSTANCE_CTRL_H

#include <gmock/gmock.h>

#include <async/async.hpp>

#include "common/logs/logging.h"
#include "function_proxy/local_scheduler/instance_control/instance_ctrl.h"
#include "gmock/gmock-function-mocker.h"

using std::string;

namespace functionsystem::test {

class MockInstanceCtrl : public local_scheduler::InstanceCtrl {
public:
    explicit MockInstanceCtrl(std::shared_ptr<local_scheduler::InstanceCtrlActor> &&actor)
        : InstanceCtrl(std::move(actor))
    {
    }
    MockInstanceCtrl()
        : InstanceCtrl(std::make_shared<local_scheduler::InstanceCtrlActor>("name", "node",
                                                                            local_scheduler::InstanceCtrlConfig()))
    {
    }
    ~MockInstanceCtrl() override = default;

    MOCK_METHOD(litebus::Future<messages::ScheduleResponse>, Schedule,
                (const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                 const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise),
                (override));
    MOCK_METHOD(litebus::Future<KillResponse>, Kill,
                (const std::string &srcInstanceID, const std::shared_ptr<KillRequest> &killReq), (override));
    MOCK_METHOD(litebus::Future<ExitResponse>, Exit,
                (const std::string &srcInstanceID, const std::shared_ptr<ExitRequest> &exitReq), (override));
    MOCK_METHOD(litebus::Future<Status>, SyncInstances, (const std::shared_ptr<resource_view::ResourceUnit> &view),
                (override));
    MOCK_METHOD(litebus::Future<Status>, SyncAgent,
                ((const std::unordered_map<std::string, messages::FuncAgentRegisInfo> &agentMap)), (override));
    MOCK_METHOD(litebus::Future<Status>, UpdateInstanceStatus, (const std::shared_ptr<InstanceExitStatus> &info),
                (override));
    MOCK_METHOD(litebus::Future<KillResponse>, KillInstancesOfJob, (const std::shared_ptr<KillRequest> &killReq),
                (const, override));
    MOCK_METHOD(litebus::Future<CallResultAck>, CallResult,
                (const std::string &from, const std::shared_ptr<functionsystem::CallResult> &callResult),
                (const, override));
    MOCK_METHOD(void, SetAbnormal, (), (override));
    MOCK_METHOD(litebus::AID, GetActorAID, (), (override));
    MOCK_METHOD(litebus::Future<litebus::Option<FunctionMeta>>, GetFuncMeta, (const std::string &funcKey), (override));
    MOCK_METHOD(litebus::Future<Status>, EvictInstanceOnAgent,
                (const std::shared_ptr<messages::EvictAgentRequest> &req), (override));
    MOCK_METHOD(litebus::Future<Status>, EvictInstances,
                (const std::unordered_set<std::string> &instanceSet,
                 const std::shared_ptr<messages::EvictAgentRequest> &req, bool isEvictForReuse),
                (override));
    MOCK_METHOD(void, PutFailedInstanceStatusByAgentId, (const std::string &funcAgentID), (override));

    MOCK_METHOD(litebus::Future<Status>, ToScheduling, (const std::shared_ptr<messages::ScheduleRequest> &req),
                (override));
    MOCK_METHOD(litebus::Future<Status>, ToCreating,
                (const std::shared_ptr<messages::ScheduleRequest> &req,
                 const schedule_decision::ScheduleResult &result),
                (override));
    MOCK_METHOD(void, RegisterReadyCallback,
                (const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                 local_scheduler::InstanceReadyCallBack callback),
                (override));
    MOCK_METHOD(litebus::Future<Status>, ForceDeleteInstance, (const std::string &instanceID), (override));
    MOCK_METHOD(litebus::Future<Status>, DeleteSchedulingInstance, (const std::string &instanceID, const std::string &requestID), (override));
    MOCK_METHOD(void, RegisterClearGroupInstanceCallBack, (local_scheduler::ClearGroupInstanceCallBack callback), (override));
    MOCK_METHOD(litebus::Future<Status>, GracefulShutdown, (), (override));
    MOCK_METHOD(litebus::Future<bool>, IsInstanceRunning, (const std::string &instanceID), (override));
    MOCK_METHOD(litebus::Future<KillResponse>, ForwardSubscriptionEvent, (const std::shared_ptr<KillContext> &ctx),
                (override));
};
}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_INSTANCE_CTRL_H
