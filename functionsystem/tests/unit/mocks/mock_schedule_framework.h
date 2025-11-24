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

#ifndef TEST_UNIT_MOCKS_MOCK_SCHEDULER_FRAMWORK_H
#define TEST_UNIT_MOCKS_MOCK_SCHEDULER_FRAMWORK_H

#include <gmock/gmock.h>

#include "common/resource_view/resource_type.h"
#include "common/scheduler_framework/framework/framework.h"

namespace functionsystem::test {
using namespace schedule_framework;
class MockSchedulerFramework : public Framework {
public:
    MOCK_METHOD(bool, RegisterPolicy, (const std::shared_ptr<SchedulePolicyPlugin> &plugin), (override));
    MOCK_METHOD(bool, UnRegisterPolicy, (const std::string &name), (override));

    MOCK_METHOD(ScheduleResults, SelectFeasible,
                (const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                 const resource_view::ResourceUnit &resourceUnit, uint32_t expectedFeasible),
                (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_SCHEDULER_FRAMWORK_H
