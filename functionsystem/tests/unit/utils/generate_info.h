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

#ifndef TEST_UNIT_UTILS_GENERATE_INFO_H
#define TEST_UNIT_UTILS_GENERATE_INFO_H

#include <random>
#include <sstream>

#include "common/proto/pb/posix_pb.h"
#include "common/resource_view/resource_type.h"
#include "common/types/instance_state.h"
#include "common/explorer/explorer.h"

namespace functionsystem::test {
using namespace functionsystem::resource_view;
inline InstanceInfo GenInstanceInfo(const std::string &instanceID, const std::string &funcAgentID,
                                    const std::string &function, InstanceState instanceStatus)
{
    InstanceInfo instanceInfo;
    instanceInfo.set_instanceid(instanceID);
    instanceInfo.set_functionagentid(funcAgentID);
    instanceInfo.set_function(function);
    instanceInfo.mutable_instancestatus()->set_code(int32_t(instanceStatus));

    return instanceInfo;
}

inline explorer::LeaderInfo GetLeaderInfo(const litebus::AID &aid)
{
    explorer::LeaderInfo leaderInfo{.name = aid.Name(), .address = aid.Url()};
    return leaderInfo;
}

inline std::string GenerateRandomName(std::string prefix)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 99999);

    std::ostringstream oss;
    oss << prefix << "_" << std::setw(2) << std::setfill('0') << (dis(gen) % 100) << "-" << dis(gen);

    return oss.str();
}

}  // namespace functionsystem::test
#endif  // TEST_UNIT_UTILS_GENERATE_INFO_H
