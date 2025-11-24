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

#ifndef TEST_INTEGRATION_STUBS_LOCAL_SCHEDULER_SERVICE_STUB_H
#define TEST_INTEGRATION_STUBS_LOCAL_SCHEDULER_SERVICE_STUB_H

#include "common/proto/pb/posix_pb.h"
#include "common/rpc/stream/posix/control_client.h"

namespace functionsystem::test {

class LocalSchedulerServiceStub {
public:
    LocalSchedulerServiceStub() = default;
    ~LocalSchedulerServiceStub() = default;

    void InitControlClient(const std::string &instanceID, const std::string &runtimeID, const std::string &target,
                           const std::shared_ptr< ::grpc::ChannelCredentials> &creds);

    void StopControlClient() const;

    void RegisterHandler(runtime_rpc::StreamingMessage::BodyCase type,
                         const grpc::PosixFunctionSysControlHandler &func);

    litebus::Future<runtime_rpc::StreamingMessage> SendMessage(
        const std::shared_ptr<runtime_rpc::StreamingMessage> &request);

private:
    std::shared_ptr<grpc::ControlClient> controlClient_;
};
}  // namespace functionsystem::test

#endif  // TEST_INTEGRATION_STUBS_LOCAL_SCHEDULER_SERVICE_STUB_H
