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

#include "function_agent/code_deployer/shared_dir_deployer.h"

#include <sys/stat.h>
#include <gtest/gtest.h>

#include "common/metadata/metadata.h"
#include "common/utils/path.h"
#include "function_agent/common/constants.h"

namespace functionsystem::test {

class SharedDirDeployerTest : public testing::Test {
    void TearDown() override
    {
        auto defaultFilePath = litebus::os::Join(functionsystem::GetDeployDir(), "shared");
        litebus::os::Rmdir(defaultFilePath);
        std::string filePath = litebus::os::Join("/tmp/DeploySharedDir", "shared");
        litebus::os::Rmdir(filePath);
    }
};

TEST_F(SharedDirDeployerTest, DeploySharedDir)
{
    auto deployer = std::make_shared<functionsystem::function_agent::SharedDirDeployer>();

    auto defaultFilePath = "/tmp/shared";
    litebus::os::Rmdir(defaultFilePath);
    auto des = deployer->GetDestination("", "", "shared1");
    ASSERT_EQ(des, litebus::os::Join(defaultFilePath, "shared1"));
    ASSERT_FALSE(deployer->IsDeployed(des, false));
    auto request = std::make_shared<messages::DeployRequest>();
    request->mutable_deploymentconfig()->set_objectid("shared1");
    auto res = deployer->Deploy(request);
    EXPECT_FALSE(res.status.IsError()) << res.status.ToString();
    ASSERT_TRUE(deployer->IsDeployed(des, false));
    struct stat fileStat;
    ASSERT_TRUE(stat(des.c_str(), &fileStat) == 0);
    mode_t actualMode = fileStat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
    mode_t expectedMode = S_IRWXU | S_IRWXG;
    ASSERT_EQ(actualMode, expectedMode) << actualMode;
    deployer->Clear(des, "");
    ASSERT_FALSE(deployer->IsDeployed(des, false));

    std::string filePath = "/tmp/DeploySharedDir";
    litebus::os::Rmdir(filePath);
    des = deployer->GetDestination(filePath, "", "shared1");
    ASSERT_EQ(des, litebus::os::Join(filePath, "shared/shared1"));
    ASSERT_FALSE(deployer->IsDeployed(des, false));
    request = std::make_shared<messages::DeployRequest>();
    request->mutable_deploymentconfig()->set_deploydir(filePath);
    request->mutable_deploymentconfig()->set_objectid("shared1");
    res = deployer->Deploy(request);
    EXPECT_FALSE(res.status.IsError()) << res.status.ToString();
    ASSERT_TRUE(deployer->IsDeployed(des, false)) << des;
    deployer->Clear(des, "");
    ASSERT_FALSE(deployer->IsDeployed(des, false));
}

TEST_F(SharedDirDeployerTest, DeploySharedDirFailed)
{
    auto deployer = std::make_shared<functionsystem::function_agent::SharedDirDeployer>();

    auto defaultFilePath = litebus::os::Join(functionsystem::GetDeployDir(), "shared");
    auto request = std::make_shared<messages::DeployRequest>();
    request->mutable_deploymentconfig()->set_objectid("shared1;");
    auto res = deployer->Deploy(request);
    EXPECT_TRUE(res.status.IsError()) << res.status.ToString();

    request->mutable_deploymentconfig()->set_objectid("");
    res = deployer->Deploy(request);
    EXPECT_TRUE(res.status.IsError()) << res.status.ToString();

    request->mutable_deploymentconfig()->set_objectid(std::string(65, 'A'));
    res = deployer->Deploy(request);
    EXPECT_TRUE(res.status.IsError()) << res.status.ToString();
}

TEST_F(SharedDirDeployerTest, SetTTL)
{
    auto deployer = std::make_shared<functionsystem::function_agent::SharedDirDeployer>();
    auto dest = "/tmp/shared/abc";
    EXPECT_EQ(deployer->GetTTL(dest), -1);
    int ttl = 5;
    deployer->SetTTL(dest, ttl);
    EXPECT_EQ(deployer->GetTTL(dest), ttl);
    deployer->Clear(dest, "");
    EXPECT_EQ(deployer->GetTTL(dest), -1);
}

}  // namespace functionsystem::test