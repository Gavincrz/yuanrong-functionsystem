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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/logs/logging.h"
#include "domain_scheduler/flags/flags.h"

namespace functionsystem::test {

using namespace std;

class DomainSchedulerFlagsTest : public ::testing::Test {};

TEST_F(DomainSchedulerFlagsTest, DomainSchedulerFlagsOK)
{
    const char *argv[] = { "./domain_scheduler",
                           "--log_config={\"filepath\": \"/home/yr/log\",\"level\": \"DEBUG\",\"rolling\": "
                           "{\"maxsize\": 100, \"maxfiles\": 1}}",
                           "--node_id=10",
                           "--ip=127.0.0.1",
                           "--domain_listen_port=8080",
                           "--global_dddress=127.0.0.1:58580",
                           "--meta_store_address=127.0.0.1:60000" };
    domain_scheduler::Flags flags;

    litebus::Option<std::string> parse = flags.ParseFlags(7, argv);
    ASSERT_TRUE(parse.IsNone());

    EXPECT_EQ(flags.GetLogConfig(),
              "{\"filepath\": \"/home/yr/log\",\"level\": \"DEBUG\",\"rolling\": {\"maxsize\": 100, \"maxfiles\": 1}}");
    EXPECT_EQ(flags.GetNodeID(), "10");
    EXPECT_EQ(flags.GetIP(), "127.0.0.1");
    EXPECT_EQ(flags.GetDomainListenPort(), "8080");
    EXPECT_EQ(flags.GetGlobalAddress(), "127.0.0.1:58580");
    EXPECT_EQ(flags.GetMetaStoreAddress(), "127.0.0.1:60000");
}

TEST_F(DomainSchedulerFlagsTest, DomainSchedulerFlagsFail)
{
    const char *argv[] = { "./domain_scheduler" };
    domain_scheduler::Flags flags;
    litebus::Option<std::string> parse = flags.ParseFlags(1, argv);
    ASSERT_TRUE(parse.IsSome());
}

}  // namespace functionsystem::test