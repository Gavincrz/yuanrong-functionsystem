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

#ifndef TEST_UNIT_MOCKS_MOCK_RUNTIME_EXECUTOR_H
#define TEST_UNIT_MOCKS_MOCK_RUNTIME_EXECUTOR_H

#include <gmock/gmock.h>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "runtime_manager/executor/runtime_executor.h"
#include "utils/volume_mount.h"

namespace functionsystem::test {


class MockVolumeMount : public runtime_manager::VolumeMount {
public:
    MOCK_METHOD(Status, ExecMount,
                (const std::string &host, const std::string &mountSharePath, const std::string &localMountPath),
                (override));
    MOCK_METHOD(int, Connect, (int32_t clientSocket, struct sockaddr_in addr, int addrLen), (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_RUNTIME_EXECUTOR_H
