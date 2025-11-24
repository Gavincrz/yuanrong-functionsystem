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

#include "common/aksk/aksk_util.h"
#include "gtest/gtest.h"

namespace functionsystem::test {
class GrpcAuthTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(GrpcAuthTest, SignAndVerifyStreamingMessageTest)
{
    std::string accessKey = "access_key";
    SensitiveValue secretKey("secret_key");
    auto message = std::make_shared<runtime_rpc::StreamingMessage>();
    message->set_messageid("message_id");

    // fail to sign empty StreamingMessage
    EXPECT_FALSE(SignStreamingMessage(accessKey, secretKey, message));
    EXPECT_FALSE(message->metadata().contains("access_key"));
    EXPECT_FALSE(message->metadata().contains("signature"));
    EXPECT_FALSE(message->metadata().contains("timestamp"));

    // success sign
    message->mutable_callreq()->set_requestid("123");
    EXPECT_TRUE(SignStreamingMessage(accessKey, secretKey, message));
    EXPECT_TRUE(message->metadata().contains("access_key"));
    EXPECT_EQ(message->metadata().at("access_key"), accessKey);
    EXPECT_TRUE(message->metadata().contains("signature"));
    EXPECT_TRUE(message->metadata().contains("timestamp"));

    // success verify
    EXPECT_TRUE(VerifyStreamingMessage(accessKey, secretKey, message));

    EXPECT_FALSE(VerifyStreamingMessage("fake_access_key", secretKey, message));

    SensitiveValue fakeSecretKey("fake_secret_key");
    EXPECT_FALSE(VerifyStreamingMessage(accessKey, fakeSecretKey, message));

    // tamper message
    message->mutable_callreq()->set_requestid("1234");
    EXPECT_FALSE(VerifyStreamingMessage(accessKey, secretKey, message));

    // tamper message type
    message->clear_callreq();
    message->mutable_callresultack()->set_message("123");
    EXPECT_FALSE(VerifyStreamingMessage(accessKey, secretKey, message));

    message->Clear();
    EXPECT_FALSE(VerifyStreamingMessage(accessKey, secretKey, message));
}

TEST_F(GrpcAuthTest, SignAndVerifyTimestampTest)
{
    std::string accessKey = "access_key";
    SensitiveValue secretKey("secret_key");
    auto timestamp = litebus::time::GetCurrentUTCTime();
    auto signature = SignTimestamp(accessKey, secretKey, timestamp);
    EXPECT_FALSE(signature.empty());

    EXPECT_TRUE(VerifyTimestamp(accessKey, secretKey, timestamp, signature));

    EXPECT_FALSE(VerifyTimestamp("fake_access_key", secretKey, timestamp, signature));

    SensitiveValue fakeSecretKey("fake_secret_key");
    EXPECT_FALSE(VerifyTimestamp(accessKey, fakeSecretKey, timestamp, signature));

    auto fakeTimestamp = litebus::time::GetCurrentUTCTime();
    EXPECT_FALSE(VerifyTimestamp(accessKey, fakeSecretKey, fakeTimestamp, signature));

    EXPECT_FALSE(VerifyTimestamp(accessKey, fakeSecretKey, timestamp, "fake_signature"));
}
}  // namespace functionsystem::test