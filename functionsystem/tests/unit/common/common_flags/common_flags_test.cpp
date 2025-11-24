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

#include "common/common_flags/common_flags.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace functionsystem::test {

using namespace std;

class CommonFlagsTest : public ::testing::Test {};

TEST_F(CommonFlagsTest, DomainSchedulerFlagsOK)
{
    const char *argv[] = { "./domain_scheduler", "--litebus_thread_num=50" };
    CommonFlags flags;

    litebus::Option<std::string> parse = flags.ParseFlags(2, argv);  // 2: args num
    ASSERT_TRUE(parse.IsNone());

    EXPECT_EQ(flags.GetLitebusThreadNum(), 50);  // 50: thread num
}

TEST_F(CommonFlagsTest, ETCDAuthTypeFlags)
{
    const char *argv[] = { "./function_master", "--etcd_auth_type=TLS" };
    CommonFlags flags;

    litebus::Option<std::string> parse = flags.ParseFlags(2, argv);  // 2: args num
    ASSERT_TRUE(parse.IsNone());

    EXPECT_EQ(flags.GetETCDAuthType(), "TLS");
}

TEST_F(CommonFlagsTest, EtcdSecretName)
{
    const char *argv[] = { "./function_master", "--etcd_secret_name=etcd-secert" };
    CommonFlags flags;

    litebus::Option<std::string> parse = flags.ParseFlags(2, argv);  // 2: args num
    ASSERT_TRUE(parse.IsNone());

    EXPECT_EQ(flags.GetEtcdSecretName(), "etcd-secert");
}

TEST_F(CommonFlagsTest, EtcdRootCAFile)
{
    const char *argv[] = { "./function_master", "--etcd_root_ca_file=ca.crt" };
    CommonFlags flags;

    litebus::Option<std::string> parse = flags.ParseFlags(2, argv);  // 2: args num
    ASSERT_TRUE(parse.IsNone());

    EXPECT_EQ(flags.GetETCDRootCAFile(), "ca.crt");
}

TEST_F(CommonFlagsTest, EtcdCertFile)
{
    const char *argv[] = { "./function_master", "--etcd_cert_file=client.crt" };
    CommonFlags flags;

    litebus::Option<std::string> parse = flags.ParseFlags(2, argv);  // 2: args num
    ASSERT_TRUE(parse.IsNone());

    EXPECT_EQ(flags.GetETCDCertFile(), "client.crt");
}

TEST_F(CommonFlagsTest, EtcdKeyFile)
{
    const char *argv[] = { "./function_master", "--etcd_key_file=client.key" };
    CommonFlags flags;

    litebus::Option<std::string> parse = flags.ParseFlags(2, argv);  // 2: args num
    ASSERT_TRUE(parse.IsNone());

    EXPECT_EQ(flags.GetETCDKeyFile(), "client.key");
}

}  // namespace functionsystem::test