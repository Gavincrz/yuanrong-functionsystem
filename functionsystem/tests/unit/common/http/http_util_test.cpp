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

#include "common/http/http_util.h"

#include "gtest/gtest.h"

namespace functionsystem::test {
class HttpUtilTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(HttpUtilTest, QueryEscapeTest)
{
    std::string input = "Hello World-1_2.3~!+@#$%^&*()";
    std::string expected = "Hello+World-1_2.3~%21%2B%40%23%24%25%5E%26%2A%28%29";
    EXPECT_EQ(EscapeQuery(input), expected);

    EXPECT_EQ(EscapeQuery(""), "");
    EXPECT_EQ(EscapeQuery("123"), "123");
    EXPECT_EQ(EscapeQuery("Hello"), "Hello");
}

TEST_F(HttpUtilTest, UrlEscapeTest)
{
    EXPECT_EQ(EscapeURL("", false), "");

    std::string url = "https://www.example.com/path/to/resource?param=value 1+2*3~4";
    std::string expectedNotReplacePath =
        "https%3A%2F%2Fwww.example.com%2Fpath%2Fto%2Fresource%3Fparam%3Dvalue%201%2B2%2A3~4";
    EXPECT_EQ(EscapeURL(url, false), expectedNotReplacePath);
    EXPECT_EQ(EscapeURL(url, true), "https%3A//www.example.com/path/to/resource%3Fparam%3Dvalue%201%2B2%2A3~4");
}

TEST_F(HttpUtilTest, GetCanonicalRequestTest)
{
    std::string method = METHOD_GET;
    std::string path = "/path/to/resource";
    std::shared_ptr<std::map<std::string, std::string>> queries =
        std::make_shared<std::map<std::string, std::string>>();
    queries->insert({ "p2", "value2" });  // need sort
    queries->insert({ "p3", "value3" });  // need sort
    queries->insert({ "p1", "value1" });  // need sort
    std::map<std::string, std::string> headers = { { "h2", "**" }, { "Host", "example.com" } };

    std::string expected =
        "GET\n"
        "/path/to/resource\n"
        "p1=value1&p2=value2&p3=value3\n"
        "host:example.com\nh2:**\n\n"
        "host;h2\n"
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    EXPECT_EQ(GetCanonicalRequest(method, path, queries, headers, EMPTY_CONTENT_SHA256), expected);
}

TEST_F(HttpUtilTest, GetCanonicalRequestWhenEmptyArgsTest)
{
    std::string expected = "GET\n/\n\n\n\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    std::map<std::string, std::string> headers = { { HEADER_AUTHORIZATION, "**" }, { HEADER_CONNECTION, "**" } };
    EXPECT_EQ(GetCanonicalRequest(METHOD_GET, "", nullptr, headers, ""), expected);
}
}  // namespace functionsystem::test