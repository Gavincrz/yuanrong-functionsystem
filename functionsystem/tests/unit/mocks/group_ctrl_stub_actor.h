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

#ifndef UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_GROUP_CTRL_STUB_ACTOR_H
#define UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_GROUP_CTRL_STUB_ACTOR_H

#include <actor/actor.hpp>
#include <async/future.hpp>

#include <gmock/gmock.h>

#include "common/proto/pb/message_pb.h"

namespace functionsystem::test {
class DomainGroupCtrlActorStub : public litebus::ActorBase {
public:
    explicit DomainGroupCtrlActorStub(const std::string &name) : ActorBase(name) {
        YRLOG_INFO("start domain stub: {}", name);
    }
    ~DomainGroupCtrlActorStub() = default;

    void ForwardGroupSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("receive {} from: {}", name, std::string(from));
        auto registeredMsg = MockForwardGroupSchedule();
        Send(from, "OnForwardGroupSchedule", std::move(registeredMsg));
    }
    MOCK_METHOD(std::string, MockForwardGroupSchedule, ());

protected:
    void Init() override
    {
        Receive("ForwardGroupSchedule", &DomainGroupCtrlActorStub::ForwardGroupSchedule);
    }
    void Finalize() override
    {
    }
};
}
#endif  // UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_GROUP_CTRL_STUB_ACTOR_H
