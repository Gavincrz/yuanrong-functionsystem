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

#ifndef TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHED_MGR_H
#define TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHED_MGR_H

#include <gmock/gmock.h>

#include "scheduler_manager/domain_sched_mgr.h"

namespace functionsystem::test {
const long long HEARTBEAT_TIMEOUT = 30000;

class MockDomainSchedMgr : public global_scheduler::DomainSchedMgr {
public:
    MockDomainSchedMgr(const std::string &name = "DomainSchedMgrActor")
        : global_scheduler::DomainSchedMgr(
              std::make_shared<global_scheduler::DomainSchedMgrActor>(name, HEARTBEAT_TIMEOUT))
    {
    }
    explicit MockDomainSchedMgr(std::shared_ptr<global_scheduler::DomainSchedMgrActor> domainSchedMgrActor)
        : global_scheduler::DomainSchedMgr(std::move(domainSchedMgrActor))
    {
    }
    MockDomainSchedMgr(const MockDomainSchedMgr &mgr) : global_scheduler::DomainSchedMgr(mgr)
    {
    }
    MOCK_METHOD(void, Start, (), (override));
    MOCK_METHOD(void, Stop, (), (override));
    MOCK_METHOD(void, UpdateSchedTopoView,
                (const std::string &name, const std::string &address, const messages::ScheduleTopology &topology),
                (const, override));
    MOCK_METHOD(litebus::Future<Status>, Connect, (const std::string &name, const std::string &address), (const, override));
    MOCK_METHOD(void, Disconnect, (), (const, override));
    MOCK_METHOD(void, Registered,
                (const litebus::AID &dst, const litebus::Option<messages::ScheduleTopology> &topology),
                (const, override));
    MOCK_METHOD(litebus::Future<Status>, Schedule,
                (const std::string &name, const std::string &address,
                 const std::shared_ptr<messages::ScheduleRequest> &req, const uint32_t retryCycle),
                (const, override));
    MOCK_METHOD(Status, AddDomainSchedCallback, (const global_scheduler::CallbackAddFunc &func), (const, override));
    MOCK_METHOD(Status, DelDomainSchedCallback, (const global_scheduler::CallbackDelFunc &func), (const, override));
    MOCK_METHOD(Status, DelLocalSchedCallback, (const global_scheduler::CallbackDelFunc &func), (const, override));
    MOCK_METHOD(Status, NotifyWorkerStatusCallback, (const global_scheduler::CallbackWorkerFunc &func), (const, override));
    MOCK_METHOD(litebus::Future<messages::QueryAgentInfoResponse>, QueryAgentInfo,
                (const std::string &name, const std::string &address,
                 const std::shared_ptr<messages::QueryAgentInfoRequest> &req),
                (override));
    MOCK_METHOD(litebus::Future<messages::QueryResourcesInfoResponse>, QueryResourcesInfo,
                (const std::string &name, const std::string &address,
                const std::shared_ptr<messages::QueryResourcesInfoRequest> &req),
                (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHED_MGR_H
