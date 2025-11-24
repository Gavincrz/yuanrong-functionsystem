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

#ifndef TEST_UNIT_MOCK_RESOURCE_GROUP_MGR_ACTOR_H
#define TEST_UNIT_MOCK_RESOURCE_GROUP_MGR_ACTOR_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "actor/actor.hpp"
#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::test {

class MockResourceGroupActor : public litebus::ActorBase {
public:
    MockResourceGroupActor() : litebus::ActorBase(RESOURCE_GROUP_MANAGER)
    {
    }

    void ForwardReportAgentAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::ReportAgentAbnormalRequest request;
        if (!request.ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse response for ReportAgentAbnormalResponse");
            return;
        }
        auto resp = MockForwardReportAgentAbnormal(request);
        resp.set_requestid(request.requestid());
        Send(from, "ForwardReportAgentAbnormalResponse", resp.SerializeAsString());
    }

    MOCK_METHOD(messages::ReportAgentAbnormalResponse, MockForwardReportAgentAbnormal,
                (const messages::ReportAgentAbnormalRequest &req));

protected:
    void Init() override
    {
        Receive("ForwardReportAgentAbnormal", &MockResourceGroupActor::ForwardReportAgentAbnormal);
    }
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCK_RESOURCE_GROUP_MGR_ACTOR_H