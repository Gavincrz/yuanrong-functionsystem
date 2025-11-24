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

#include "runtime_manager/utils/volume_mount.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netdb.h>
#include <sys/mount.h>
#include <unistd.h>

#include <iostream>

#include "common/logs/logging.h"
#include "mocks/mock_volume_mount.h"
#include "utils/os_utils.hpp"

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

class VolumeMountTest : public ::testing::Test {
public:
    void SetUp() override
    {
        mockMounter_ = std::make_shared<MockVolumeMount>();
        mounter_ = std::make_shared<VolumeMount>();
    }

    void TearDown() override
    {
    }

protected:
    std::shared_ptr<MockVolumeMount> mockMounter_;
    std::shared_ptr<VolumeMount> mounter_;
};

TEST_F(VolumeMountTest, VolumeMountTestEmptyFuncMounts)
{
    messages::FuncMountConfig mountConfig;
    messages::FuncMountUser mountUser;
    mountUser.set_userid(1004);
    mountUser.set_groupid(1004);
    mountConfig.mutable_funcmountuser()->CopyFrom(mountUser);

    auto mountResultStatus = mounter_->MountVolumesForFunction(mountConfig);
    YRLOG_INFO("mountResultStatus: {}", mountResultStatus.ToString());
    EXPECT_EQ(mountResultStatus.IsOk(), true);
}

TEST_F(VolumeMountTest, VolumeMountTestLocalPathExists)
{
    messages::FuncMountConfig mountConfig;
    messages::FuncMountUser mountUser;
    mountUser.set_userid(1004);
    mountUser.set_groupid(1004);
    mountConfig.mutable_funcmountuser()->CopyFrom(mountUser);

    auto funcMountSFS = mountConfig.add_funcmounts();
    funcMountSFS->set_mounttype("sfs");
    funcMountSFS->set_mountsharepath("10.244.134.153:/home/disk/lzt/nfs-share");
    funcMountSFS->set_localmountpath("/testMount/testSFS");

    (void)litebus::os::Mkdir("/testMount/testSFS");
    auto mountResultStatus = mockMounter_->MountVolumesForFunction(mountConfig);
    YRLOG_INFO("mountResultStatus: {}", mountResultStatus.ToString());
    EXPECT_EQ(mountResultStatus.StatusCode(), StatusCode::SUCCESS);

    funcMountSFS->set_status("disabled");
    mountResultStatus = mockMounter_->MountVolumesForFunction(mountConfig);
    EXPECT_EQ(mountResultStatus.StatusCode(), StatusCode::SUCCESS);
    (void)litebus::os::Rmdir("/testMount/testSFS");
}

TEST_F(VolumeMountTest, VolumeMountTestExecMountFailed)
{
    messages::FuncMountConfig mountConfig;
    messages::FuncMountUser mountUser;
    mountUser.set_userid(1004);
    mountUser.set_groupid(1004);
    mountConfig.mutable_funcmountuser()->CopyFrom(mountUser);

    messages::FuncMount funcMountECS;
    funcMountECS.set_mounttype("ecs");
    funcMountECS.set_mountsharepath("10.244.134.153:/home/disk/lzt/ecs-share");
    funcMountECS.set_localmountpath("/testMount/testECS");
    mountConfig.add_funcmounts()->CopyFrom(funcMountECS);

    (void)litebus::os::Rmdir("/testMount/testECS");
    EXPECT_CALL(*mockMounter_, Connect).WillOnce(testing::Return(0));
    EXPECT_CALL(*mockMounter_, ExecMount(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, "mount failed")));
    auto mountResultStatus = mockMounter_->MountVolumesForFunction(mountConfig);
    YRLOG_INFO("mountResultStatus: {}", mountResultStatus.ToString());
    EXPECT_EQ(mountResultStatus.StatusCode(), StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED);
    (void)litebus::os::Rmdir("/testMount/testECS");
}

TEST_F(VolumeMountTest, InvalidSharedPath)
{
    messages::FuncMount funcMount;
    funcMount.set_mounttype("ecs");
    funcMount.set_mountsharepath("1000.244.134.153:/home/disk/lzt/ecs-share");
    funcMount.set_localmountpath("/testMount/testECS");
    EXPECT_EQ(mounter_->MountVolume(funcMount).StatusCode(), StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED);

    funcMount.set_mountsharepath("invalidIP:/home/disk/lzt/ecs-share");
    EXPECT_EQ(mounter_->MountVolume(funcMount).StatusCode(), StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED);
}

/**
 * Feature:
 * Description: Test ExecMount
 * Steps:
 * 1. Call ExecMount
 * Expectation:
 * 1. Receive RUNTIME_MANAGER_MOUNT_VOLUME_FAILED status.
 */
TEST_F(VolumeMountTest, ExecMountTest)
{
    std::string host = "localhost";
    std::string mountSharePath = "/testMount";
    std::string localPath = "/testMount";

    auto mounter = std::make_shared<VolumeMount>();
    auto status = mounter->ExecMount(host, mountSharePath, localPath);
    EXPECT_EQ(status, StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED);
}
}  // namespace functionsystem::test
