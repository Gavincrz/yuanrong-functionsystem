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

#ifndef UT_MOCKS_MOCK_FUNCTION_AGENT_MGR_H
#define UT_MOCKS_MOCK_FUNCTION_AGENT_MGR_H

#include <gmock/gmock.h>

#include <async/async.hpp>

#include "common/heartbeat/heartbeat_client.h"
#include "common/logs/logging.h"
#include "local_scheduler/function_agent_manager/function_agent_mgr.h"
namespace functionsystem::test {

class MockFunctionAgentMgr : public local_scheduler::FunctionAgentMgr {
public:
    explicit MockFunctionAgentMgr(const std::string &name, std::shared_ptr<MetaStoreClient> metaStoreClient)
        : local_scheduler::FunctionAgentMgr(std::make_shared<local_scheduler::FunctionAgentMgrActor>(
              name, local_scheduler::FunctionAgentMgrActor::Param(), "nodeID", std::move(metaStoreClient)))
    {
    }
    ~MockFunctionAgentMgr() = default;

    MOCK_METHOD(litebus::Future<messages::DeployInstanceResponse>, DeployInstance,
                (const std::shared_ptr<messages::DeployInstanceRequest> &request, const std::string &funcAgentID),
                (override));

    MOCK_METHOD(litebus::Future<messages::KillInstanceResponse>, KillInstance,
                (const std::shared_ptr<messages::KillInstanceRequest> &request, const std::string &funcAgentID,
                 bool isRecovering),
                (override));

    MOCK_METHOD(litebus::Future<messages::InstanceStatusInfo>, QueryInstanceStatusInfo,
                (const std::string &funcAgentID, const std::string &instanceID, const std::string &runtimeID),
                (override));

    MOCK_METHOD(litebus::Future<messages::UpdateCredResponse>, UpdateCred,
                (const std::string &funcAgentID, const std::shared_ptr<messages::UpdateCredRequest> &request),
                (override));

    MOCK_METHOD(litebus::Future<Status>, EvictAgent, (const std::shared_ptr<messages::EvictAgentRequest> &req),
                (override));

    MOCK_METHOD(litebus::Future<bool>, IsFuncAgentRecovering, (const std::string &funcAgentID), (override));

    MOCK_METHOD(litebus::Future<Status>, GracefulShutdown, (), (override));
    MOCK_METHOD(void, SetAbnormal, (), (override));
};

}  // namespace functionsystem::test

#endif
