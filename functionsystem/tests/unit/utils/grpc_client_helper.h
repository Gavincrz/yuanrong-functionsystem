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

#ifndef TEST_UNIT_UTILS_GRPC_CLIENT_HELPER_H
#define TEST_UNIT_UTILS_GRPC_CLIENT_HELPER_H

#include "common/rpc/client/grpc_client.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "etcd/server/etcdserver/api/v3election/v3electionpb/v3election.grpc.pb.h"

namespace functionsystem::test {
class GrpcClientHelper {
public:
    GrpcClientHelper(const int connectTimeoutMs)
    {
        const int MILL = 1000 * 1000;
        GrpcClient<etcdserverpb::KV>::SetConnectTimeout({ 0, connectTimeoutMs * MILL, GPR_TIMESPAN });
        GrpcClient<etcdserverpb::Lease>::SetConnectTimeout({ 0, connectTimeoutMs * MILL, GPR_TIMESPAN });
        GrpcClient<etcdserverpb::Maintenance>::SetConnectTimeout({ 0, connectTimeoutMs * MILL, GPR_TIMESPAN });
        GrpcClient<v3electionpb::Election>::SetConnectTimeout({ 0, connectTimeoutMs * MILL, GPR_TIMESPAN });
    }
    ~GrpcClientHelper()
    {
        GrpcClient<etcdserverpb::KV>::SetConnectTimeout({ 1, 0, GPR_TIMESPAN });
        GrpcClient<etcdserverpb::Lease>::SetConnectTimeout({ 1, 0, GPR_TIMESPAN });
        GrpcClient<etcdserverpb::Maintenance>::SetConnectTimeout({ 1, 0, GPR_TIMESPAN });
        GrpcClient<v3electionpb::Election>::SetConnectTimeout({ 1, 0, GPR_TIMESPAN });
    }
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_UTILS_GRPC_CLIENT_HELPER_H
