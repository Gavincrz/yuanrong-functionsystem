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

#ifndef TEST_UNIT_MOCKS_MOCK_OBS_WRAPPER_H
#define TEST_UNIT_MOCKS_MOCK_OBS_WRAPPER_H

#include <gmock/gmock.h>

#include "eSDKOBS.h"
#include "code_deployer/obs_wrapper.h"

namespace functionsystem::test {
class MockObsWrapper : public functionsystem::function_agent::ObsWrapper {
public:
    MockObsWrapper() : functionsystem::function_agent::ObsWrapper()
    {
    }
    ~MockObsWrapper() = default;

    MOCK_METHOD0(DeinitializeObs, void());

    MOCK_METHOD0(InitializeObs, obs_status());
};
}

#endif //TEST_UNIT_MOCKS_OBS_WRAPPER_CALLER_H
