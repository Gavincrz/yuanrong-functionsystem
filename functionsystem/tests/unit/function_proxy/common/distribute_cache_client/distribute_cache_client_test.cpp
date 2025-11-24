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
#include <gtest/gtest.h>

#include <fstream>

#include "common/hex/hex.h"
#include "function_proxy/common/distribute_cache_client/ds_cache_client_impl.h"
#include "utils/os_utils.hpp"

namespace functionsystem::test {

class DistributeCacheClientTest : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
    }
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

/**
* Feature:
* Description: Handle DSCacheClientImpl
* Steps:
* 1. Create DSStateCache client
* 2. Set, Get, Del interface

* Expectation:
* 1. Status is Error
*/
TEST_F(DistributeCacheClientTest, DSCacheClientImpl)
{
    datasystem::ConnectOptions connectOptions{ .host = "0.0.0.0", .port = 31002, .connectTimeoutMs = 10 };
    DSCacheClientImpl client(connectOptions);
    std::string key = "key";
    std::string value = "value";
    std::string val = "";
    std::unordered_map<std::string, std::string> mymp;
    auto status = client.Set(key, value);
    EXPECT_TRUE(status.IsError());
    status = client.Get(key, val);
    EXPECT_TRUE(status.IsError());
    std::vector<std::string> keys = { "key1", "key2" };
    std::vector<std::string> values = { "val1", "val2" };
    status = client.Get(keys, values);
    EXPECT_TRUE(status.IsError());
    status = client.Del(key);
    EXPECT_TRUE(status.IsError());
    status = client.Del(keys, values);
    EXPECT_TRUE(status.IsError());

    client.EnableDSClient(false);
    client.SetDSAuthEnable(false);
    status = client.Init();
    EXPECT_TRUE(status.IsOk());
}

}  // namespace functionsystem::test