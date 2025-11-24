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
#include "function_master/scaler/utils/parse_helper.h"

namespace functionsystem::test {

class ParseHelperTest : public ::testing::Test {};

TEST_F(ParseHelperTest, TruncateIllegalLabel)
{
    std::string input(63, 'a');
    std::string expected = input;
    std::string actual = TruncateIllegalLabel(input);
    EXPECT_EQ(actual, expected);

    std::string input2(66, 'a');
    std::string expected2(63, 'a');
    std::string actual2 = TruncateIllegalLabel(input2);
    EXPECT_EQ(actual, expected);
}

TEST_F(ParseHelperTest, ParseCPUEnvTest)
{
    std::string input = "100m";
    auto actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "100");

    input = "100.1m";
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "100.1");

    input = "0";
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "");

    input = "0.5";
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "500");

    input = "1.51";
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "1510");

    input = "-1";
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "");

    input = "10";
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "10000");

    input = "10000000000000000";
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "");

    input = "xxx";
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "");

    input = std::to_string(INT_MAX/1000 + 1);
    actual = ParseCPUEnv(input);
    EXPECT_EQ(actual, "");
}

TEST_F(ParseHelperTest, ParseMemoryEnv)
{
    std::string input = "100Mi";
    auto actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "100");

    input = "100.1Mi";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "100.1");

    input = "0Gi";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "");

    input = "-1Gi";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "");

    input = "1Gi";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "1024");

    input = "0.5Gi";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "512");

    input = "1.5Gi";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "1536");

    input = "10000000000000000Gi";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "");

    input = "xxx";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "");

    input = std::to_string(INT_MAX/1000 + 1) + "Gi";
    actual = ParseMemoryEnv(input);
    EXPECT_EQ(actual, "");
}
}