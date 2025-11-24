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

#include "common/aksk/aksk_util.h"
#include "common/aksk/sign_request.h"
#include "common/utils/aksk_content.h"

namespace functionsystem::test {

class AKSKManagerTest : public ::testing::Test {};

TEST_F(AKSKManagerTest, SignAndVerifyHttpRequest_Test)
{
    KeyForAKSK key{ "ak", SensitiveValue("xxxxx"), SensitiveValue("defdf") };

    std::string method = "GET";
    std::string path = "/getAAA";
    std::map<std::string, std::string> initMap = { { "q1", "value1" }, { "q2", "value2" }, { "q3", "value3" } };
    std::shared_ptr<std::map<std::string, std::string>> queries =
        std::make_shared<std::map<std::string, std::string>>(initMap);

    std::map<std::string, std::string> headers = { { "h1", "value1" }, { "h2", "value2" }, { "h3", "value3" } };
    std::string body = R"({"aaa":"bbb"})";
    std::string ak = "ak";
    std::map<std::string, std::string> out = SignHttpRequest(SignRequest(method, path, queries, headers, body), key);
    headers[HEADER_TOKEN_KEY] = out[HEADER_TOKEN_KEY];
    headers[HEADER_SIGNED_HEADER_KEY] = out[HEADER_SIGNED_HEADER_KEY];
    EXPECT_EQ(out[HEADER_SIGNED_HEADER_KEY], "h1;h2;h3");
    bool verify = VerifyHttpRequest(SignRequest(method, path, queries, headers, body), key);
    EXPECT_EQ(verify, true);
    headers["aaa"] = "bbb";
    verify = VerifyHttpRequest(SignRequest(method, path, queries, headers, body), key);
    EXPECT_EQ(verify, true);
    headers["h1"] = "v1";
    verify = VerifyHttpRequest(SignRequest(method, path, queries, headers, body), key);
    EXPECT_EQ(verify, false);
}

TEST_F(AKSKManagerTest, SignAndVerifyActorMsg_Test)
{
    KeyForAKSK key{ "ak", SensitiveValue("xxxxxx"), SensitiveValue("defdf") };

    std::string msgName = "func1";
    std::string msgBody = "{xxxx:xxxx}";
    std::string ak = "ak";

    std::string actorOut = SignActorMsg(msgName, msgBody, key);
    auto verify = VerifyActorMsg(msgName, msgBody, actorOut, key);
    EXPECT_EQ(verify, true);
    msgBody = "{xxxx:xxx}";
    verify = VerifyActorMsg(msgName, msgBody, actorOut, key);
    EXPECT_EQ(verify, false);
}

}  // namespace functionsystem::test