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

#ifndef UNIT_MOCKS_MOCK_LOCAL_SCHED_SRV_ACTOR_H
#define UNIT_MOCKS_MOCK_LOCAL_SCHED_SRV_ACTOR_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "function_proxy/local_scheduler/local_scheduler_service/local_sched_srv_actor.h"

namespace functionsystem::test {
using namespace functionsystem::local_scheduler;
class MockLocalSchedSrvActor : public LocalSchedSrvActor {
public:
    explicit MockLocalSchedSrvActor(const std::string &name)
        : LocalSchedSrvActor(LocalSchedSrvActor::Param{
              name,
              "",
              false,
              0,
              0,
              0,
              "",
              0,
          }){};

    ~MockLocalSchedSrvActor(){};

    void Init() override
    {
        Receive("Registered", &MockLocalSchedSrvActor::Registered);
        Receive("UnRegistered", &MockLocalSchedSrvActor::UnRegistered);
        Receive("UpdateSchedTopoView", &MockLocalSchedSrvActor::UpdateSchedTopoView);
        Receive("ResponseNotifyWorkerStatus", &MockLocalSchedSrvActor::ResponseNotifyWorkerStatus);
        Receive("EvictAgent", &MockLocalSchedSrvActor::EvictAgent);
    }

    void Registered(const litebus::AID from, std::string &&name, std::string &&msg)
    {
        MockRegistered(from, name, msg);
    }

    MOCK_METHOD3(MockRegistered, void(const litebus::AID from, std::string name, std::string msg));

    void UnRegistered(const litebus::AID from, std::string &&name, std::string &&msg)
    {
        MockUnRegistered(from, name, msg);
    }

    MOCK_METHOD3(MockUnRegistered, void(const litebus::AID from, std::string name, std::string msg));

    void UpdateSchedTopoView(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockUpdateSchedTopoView(from, name, msg);
    }

    MOCK_METHOD3(MockUpdateSchedTopoView, void(const litebus::AID from, std::string name, std::string msg));

    void ResponseNotifyWorkerStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseNotifyWorkerStatus(from, name, msg);
    }

    MOCK_METHOD3(MockResponseNotifyWorkerStatus, void(const litebus::AID from, std::string name, std::string msg));

    void RegisterToGlobalScheduler(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "Register", std::string(msg));
    }

    void UnRegisterToGlobalScheduler(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "UnRegister", std::string(msg));
    }

    void EvictAgent(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockEvictAgent(from, name, msg);
    }

    MOCK_METHOD3(MockEvictAgent, void(const litebus::AID from, std::string name, std::string msg));

    void EvictAgentAck(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "EvictAck", std::string(msg));
    }

    void NotifyEvictResult(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "NotifyEvictResult", std::string(msg));
    }
};

}  // namespace functionsystem::test

#endif  // UNIT_MOCKS_MOCK_LOCAL_SCHED_SRV_ACTOR_H
