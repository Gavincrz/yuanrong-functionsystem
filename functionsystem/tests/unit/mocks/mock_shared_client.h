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

#ifndef UT_MOCKS_MOCK_SHARED_CLIENT_H
#define UT_MOCKS_MOCK_SHARED_CLIENT_H

#include "function_proxy/common/posix_client/shared_client/shared_client.h"
#include "gmock/gmock.h"

namespace functionsystem::test {

class MockSharedClient : public SharedClient {
public:
    MockSharedClient() : BaseClient(nullptr), SharedClient(nullptr)
    {
    }
    litebus::Future<runtime::CallResponse> InitCall(const std::shared_ptr<runtime::CallRequest> &request,
                                                    uint32_t timeOutMs = 5000) override
    {
        return InitCallWrapper(*request);
    }

    MOCK_METHOD(litebus::Future<SharedStreamMsg>, Call, (const SharedStreamMsg &request), (override));
    MOCK_METHOD(litebus::Future<runtime::CallResponse>, InitCallWrapper, (runtime::CallRequest &request));
    MOCK_METHOD(litebus::Future<runtime::NotifyResponse>, NotifyResult, (runtime::NotifyRequest && request),
                (override));
    MOCK_METHOD(litebus::Future<Status>, Heartbeat, (uint64_t timeMs), (override));
    MOCK_METHOD(litebus::Future<Status>, Readiness, (), (override));
    MOCK_METHOD(litebus::Future<runtime::ShutdownResponse>, Shutdown, (runtime::ShutdownRequest && request),
                (override));
    MOCK_METHOD(litebus::Future<runtime::SignalResponse>, Signal, (runtime::SignalRequest && request), (override));
    MOCK_METHOD(litebus::Future<runtime::CheckpointResponse>, Checkpoint, (runtime::CheckpointRequest && request),
                (override));
    MOCK_METHOD(litebus::Future<runtime::RecoverResponse>, Recover,
                (runtime::RecoverRequest && request, uint64_t timeoutMs), (override));
    MOCK_METHOD(bool, IsDone, (), (override));
};

}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_SHARED_CLIENT_H
