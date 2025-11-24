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

#include "common/utils/files.h"

#include <gtest/gtest.h>

namespace functionsystem::test {
const std::string TEST_DIR = "/tmp/testdir";

class FilesTest : public ::testing::Test {
public:
    static void ExecCommand(const std::string &cmd)
    {
        int code = std::system(cmd.c_str());
        if (code) {
            YRLOG_ERROR("failed to execute cmd({}). code: {}", cmd, code);
        }
        ASSERT_EQ(code, 0);
    }

    void TearDown() override
    {
        litebus::os::Rmdir(TEST_DIR);
    }
};

TEST_F(FilesTest, FileExistsTest)
{
    // create test dir
    litebus::os::Mkdir(TEST_DIR);
    EXPECT_TRUE(FileExists(TEST_DIR));
    EXPECT_FALSE(FileExists(TEST_DIR + "/file.txt"));

    ExecCommand("touch " + TEST_DIR + "/file.txt");
    EXPECT_TRUE(FileExists(TEST_DIR + "/file.txt"));

    // delete test dir
    litebus::os::Rmdir(TEST_DIR);
    EXPECT_FALSE(FileExists(TEST_DIR));
    EXPECT_FALSE(FileExists(TEST_DIR + "/file.txt"));
}

TEST_F(FilesTest, GetPermissionTest)
{
    litebus::os::Rmdir(TEST_DIR);
    EXPECT_TRUE(GetPermission(TEST_DIR).IsNone());

    litebus::os::Mkdir(TEST_DIR);
    ExecCommand("chmod 777 " + TEST_DIR);
    auto permission = GetPermission(TEST_DIR);
    EXPECT_TRUE(permission.IsSome());
    EXPECT_EQ(permission.Get().owner, static_cast<uint32_t>(7));
    EXPECT_EQ(permission.Get().group, static_cast<uint32_t>(7));
    EXPECT_EQ(permission.Get().others, static_cast<uint32_t>(7));

    ExecCommand("chmod 510 " + TEST_DIR);
    permission = GetPermission(TEST_DIR);
    EXPECT_TRUE(permission.IsSome());
    EXPECT_EQ(permission.Get().owner, static_cast<uint32_t>(5));
    EXPECT_EQ(permission.Get().group, static_cast<uint32_t>(1));
    EXPECT_EQ(permission.Get().others, static_cast<uint32_t>(0));

    litebus::os::Rmdir(TEST_DIR);
}

TEST_F(FilesTest, GetOwnerTest)
{
    litebus::os::Rmdir(TEST_DIR);
    EXPECT_TRUE(GetOwner(TEST_DIR).IsNone());

    litebus::os::Mkdir(TEST_DIR);
    ExecCommand("chown 1000:2000 " + TEST_DIR);
    auto owner = GetOwner(TEST_DIR);
    EXPECT_TRUE(owner.IsSome());
    EXPECT_EQ(owner.Get().first, static_cast<uint32_t>(1000));
    EXPECT_EQ(owner.Get().second, static_cast<uint32_t>(2000));

    ExecCommand("chown 2000:1000 " + TEST_DIR);
    owner = GetOwner(TEST_DIR);
    EXPECT_TRUE(owner.IsSome());
    EXPECT_EQ(owner.Get().first, static_cast<uint32_t>(2000));
    EXPECT_EQ(owner.Get().second, static_cast<uint32_t>(1000));

    litebus::os::Rmdir(TEST_DIR);
}

TEST_F(FilesTest, IsWriteableTest)
{
    Permissions permissions{ .owner = 7, .group = 7, .others = 7 };
    EXPECT_TRUE(IsWriteable(permissions, { 0, 0 }, 1000, 1000));  // others
    EXPECT_TRUE(IsWriteable(permissions, { 0, 0 }, 0, 1000));     // in group
    EXPECT_TRUE(IsWriteable(permissions, { 0, 0 }, 1000, 0));     // owner

    permissions.owner = 6;
    permissions.group = 3;
    permissions.others = 1;
    EXPECT_FALSE(IsWriteable(permissions, { 0, 0 }, 1000, 1000));  // others
    EXPECT_TRUE(IsWriteable(permissions, { 0, 0 }, 1000, 0));      // in group
    EXPECT_TRUE(IsWriteable(permissions, { 0, 0 }, 0, 1000));      // owner

    permissions.owner = 2;
    permissions.group = 4;
    permissions.others = 0;
    EXPECT_FALSE(IsWriteable(permissions, { 0, 0 }, 1000, 1000));  // others
    EXPECT_FALSE(IsWriteable(permissions, { 0, 0 }, 1000, 0));     // in group
    EXPECT_TRUE(IsWriteable(permissions, { 0, 0 }, 0, 1000));      // owner
}

TEST_F(FilesTest, IsPathWriteableTest)
{
    litebus::os::Rmdir(TEST_DIR);
    EXPECT_FALSE(IsPathWriteable(TEST_DIR, 0, 0));

    litebus::os::Mkdir(TEST_DIR);
    ExecCommand("chmod 751 " + TEST_DIR);
    ExecCommand("chown 1000:2000 " + TEST_DIR);
    EXPECT_TRUE(IsPathWriteable(TEST_DIR, 1000, 0));   // owner
    EXPECT_FALSE(IsPathWriteable(TEST_DIR, 0, 2000));  // in group
    EXPECT_FALSE(IsPathWriteable(TEST_DIR, 0, 0));     // others

    ExecCommand("chmod 722 " + TEST_DIR);
    EXPECT_TRUE(IsPathWriteable(TEST_DIR, 1000, 0));  // owner
    EXPECT_TRUE(IsPathWriteable(TEST_DIR, 0, 2000));  // in group
    EXPECT_TRUE(IsPathWriteable(TEST_DIR, 0, 0));     // others

    ExecCommand("chmod 521 " + TEST_DIR);
    EXPECT_FALSE(IsPathWriteable(TEST_DIR, 1000, 0));  // owner
    EXPECT_TRUE(IsPathWriteable(TEST_DIR, 0, 2000));   // in group
    EXPECT_FALSE(IsPathWriteable(TEST_DIR, 0, 0));     // others

    litebus::os::Rmdir(TEST_DIR);
}
}  // namespace functionsystem::test
