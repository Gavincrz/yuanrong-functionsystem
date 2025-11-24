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

#include "common/status/status.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace functionsystem::test {
class StatusTest : public ::testing::Test {};

Status ReturnFailed()
{
    return Status(StatusCode::FAILED);
}

Status ReturnIfNotOk()
{
    RETURN_IF_NOT_OK(ReturnFailed());
    return Status::OK();
}

TEST_F(StatusTest, StatusOK)
{
    auto status = Status::OK();
    EXPECT_TRUE(status.IsOk());
}

TEST_F(StatusTest, StatusFailed)
{
    auto status = Status(StatusCode::FAILED);
    EXPECT_FALSE(status.IsOk());
}

TEST_F(StatusTest, MarcoTest)
{
    auto status = ReturnIfNotOk();
    EXPECT_TRUE(status.IsError());
}

TEST_F(StatusTest, GetStatusDefaultDescription)
{
    auto status = Status::OK();
    EXPECT_THAT(status.ToString(), ::testing::ContainsRegex(R"([code: 0, status: No error occurs])"));
}

TEST_F(StatusTest, GetStatusDetailDescription)
{
    auto status = Status(StatusCode::FAILED, "detail error message");
    EXPECT_THAT(status.ToString(), ::testing::ContainsRegex(R"([code: 1, status: Common error code
detail: [detail error message]])"));
}

TEST_F(StatusTest, GetStatusAppendDescription)
{
    auto status = ReturnIfNotOk();
    status.AppendMessage("detail error message");
    EXPECT_THAT(status.ToString(), ::testing::ContainsRegex(R"([code: 1, status: Common error code
detail: [detail error message]])"));
}

TEST_F(StatusTest, GetStatusMultiDetailDescription)
{
    auto status = Status(StatusCode::FAILED, "detail error message");
    status.AppendMessage("append error message");
    EXPECT_THAT(status.ToString(), ::testing::ContainsRegex(R"([code: 1, status: Common error code
detail: [detail error message][append error message]])"));
}

TEST_F(StatusTest, GetStatusLineAndFileDescription)
{
    auto status = Status(StatusCode::FAILED, __LINE__, __FILE__, "detail error message");
    EXPECT_THAT(status.ToString(), ::testing::ContainsRegex(R"([code: 1, status: Common error code
Line of code :
File         :
detail: [detail error message]])"));
}
}  // namespace functionsystem::test
