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

#ifndef TEST_UNIT_MOCKS_MOCK_POSIX_CONTROL_STREAM_CLIENT_H
#define TEST_UNIT_MOCKS_MOCK_POSIX_CONTROL_STREAM_CLIENT_H

#include <gmock/gmock.h>

#include <cstdint>

#include "common/rpc/stream/posix/control_client.h"

namespace functionsystem::test {
class MockPosixControlWrapper : public grpc::PosixControlWrapper {
public:
    MockPosixControlWrapper() = default;
    MOCK_METHOD(std::shared_ptr<grpc::ControlClient>, InitPosixStream,
                (const std::string &instanceID, const std::string &runtimeID, const grpc::ControlClientConfig &config),
                (override));
};

class MockControlClient : public grpc::ControlClient {
public:
    MockControlClient() : grpc::ControlClient()
    {
    }
    MOCK_METHOD(void, Start, (), (override));
    MOCK_METHOD(void, Stop, (), (override));
    MOCK_METHOD(bool, IsDone, (), (override));
    MOCK_METHOD(litebus::Future<runtime_rpc::StreamingMessage>, Send,
                (const std::shared_ptr<runtime_rpc::StreamingMessage> &request), (override));
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_POSIX_CONTROL_STREAM_CLIENT_H
