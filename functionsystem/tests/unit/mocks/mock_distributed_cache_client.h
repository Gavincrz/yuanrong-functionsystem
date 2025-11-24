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

#ifndef UT_MOCKS_MOCK_DISTRIBUTED_CACHE_CLIENT_H
#define UT_MOCKS_MOCK_DISTRIBUTED_CACHE_CLIENT_H

#include <gmock/gmock.h>

#include "function_proxy/common/distribute_cache_client/distributed_cache_client.h"

namespace functionsystem::test {

class MockDistributedCacheClient : public DistributedCacheClient {
public:
    MOCK_METHOD(Status, Init, (), (override));
    MOCK_METHOD(Status, Set, (const std::string &key, const std::string &val), (override));
    MOCK_METHOD(Status, Get, (const std::string &key, std::string &val), (override));
    MOCK_METHOD(Status, Get, (const std::vector<std::string> &keys, std::vector<std::string> &vals), (override));
    MOCK_METHOD(Status, Del, (const std::string &key), (override));
    MOCK_METHOD(Status, Del, (const std::vector<std::string> &keys, std::vector<std::string> &failedKeys), (override));
    MOCK_METHOD(Status, GetHealthStatus, (), (override));
    MOCK_METHOD(Status, ShutDown, (), (override));
};

}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_DISTRIBUTED_CACHE_CLIENT_H
