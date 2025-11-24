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

#include "common/utils/aksk_content.h"
#include <gtest/gtest.h>
#include <litebus.hpp>

namespace functionsystem::test {
class AKSKContentTest : public ::testing::Test {};

AKSKContent NewContent()
{
    AKSKContent content;
    content.tenantID = "tenant";
    content.secretKey = "secret";
    content.accessKey = "access";
    content.dataKey = "data";
    content.expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) + 3000;
    content.status = Status::OK();
    return content;
}

TEST_F(AKSKContentTest, AKSKContentValid)
{
    auto content = NewContent();
    EXPECT_EQ(content.IsValid(3).IsOk(), true);
}

TEST_F(AKSKContentTest, AKSKContentInValid_status_err)
{
    auto content = NewContent();
    content.status = Status(StatusCode::FAILED);
    EXPECT_FALSE(content.IsValid().IsOk());
}

TEST_F(AKSKContentTest, AKSKContentInValid_TenantID)
{
    auto content = NewContent();
    content.tenantID = std::string();
    EXPECT_FALSE(content.IsValid().IsOk());
}

TEST_F(AKSKContentTest, AKSKContentInValid_AccessKey)
{
    auto content = NewContent();
    content.accessKey = std::string();
    EXPECT_FALSE(content.IsValid().IsOk());
}

TEST_F(AKSKContentTest, AKSKContentInValid_SecretKey)
{
    auto content = NewContent();
    content.secretKey = std::string();
    EXPECT_FALSE(content.IsValid().IsOk());
}

TEST_F(AKSKContentTest, AKSKContentInValid_DataKey)
{
    auto content = NewContent();
    content.dataKey = std::string();
    EXPECT_FALSE(content.IsValid().IsOk());
}

TEST_F(AKSKContentTest, AKSKContentInValid_Expired)
{
    auto content = NewContent();
    content.expiredTimeStamp = 1;
    EXPECT_FALSE(content.IsValid(1).IsOk());
}

TEST_F(AKSKContentTest, AKSKContentInValid_Copy)
{
    auto content = NewContent();
    auto copy = content.Copy();
    EXPECT_EQ(content.tenantID, copy->tenantID);
    EXPECT_EQ(content.accessKey, copy->accessKey);
    EXPECT_EQ(content.secretKey, copy->secretKey);
    EXPECT_EQ(content.dataKey, copy->dataKey);
    EXPECT_EQ(content.expiredTimeStamp, copy->expiredTimeStamp);
}
}