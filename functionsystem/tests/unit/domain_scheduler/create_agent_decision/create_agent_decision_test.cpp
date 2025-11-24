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

#include "common/create_agent_decision/create_agent_decision.h"

#include <gtest/gtest.h>

#include "common/resource_view/resource_type.h"
#include "common/resource_view/view_utils.h"

namespace functionsystem::test {
class CreateAgentDecisionTest : public ::testing::Test {};

TEST_F(CreateAgentDecisionTest, NeedCreateAgent)
{
    resource_view::InstanceInfo inst = view_utils::Get1DInstance();
    inst.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    ASSERT_FALSE(NeedCreateAgent(inst));
    (*inst.mutable_createoptions())[RESOURCE_OWNER_KEY] = SYSTEM_OWNER_VALUE;
    ASSERT_TRUE(NeedCreateAgent(inst));
    (*inst.mutable_createoptions())[DELEGATE_CONTAINER] = R"({"123":"123"})";
    ASSERT_TRUE(NeedCreateAgent(inst));
    resource_view::InstanceInfo inst1 = view_utils::Get1DInstance();
    inst1.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    (*inst1.mutable_scheduleoption()
          ->mutable_affinity()
          ->mutable_instanceaffinity()
          ->mutable_affinity())["antiAffinityInstance"] = resource_view::AffinityType::PreferredAntiAffinity;
    ASSERT_FALSE(NeedCreateAgent(inst1));
    resource_view::InstanceInfo inst2 = view_utils::Get1DInstance();
    (*inst2.mutable_createoptions())["AFFINITY_POOL_ID"] = "";
    ASSERT_FALSE(NeedCreateAgentInDomain(inst2, static_cast<int32_t>(StatusCode::AFFINITY_SCHEDULE_FAILED)));
    (*inst2.mutable_createoptions())["AFFINITY_POOL_ID"] = "pool1";
    ASSERT_TRUE(NeedCreateAgentInDomain(inst2, static_cast<int32_t>(StatusCode::AFFINITY_SCHEDULE_FAILED)));

}
}  // namespace functionsystem::test
