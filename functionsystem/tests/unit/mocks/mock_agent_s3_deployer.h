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

#ifndef FUNCTION_AGENT_MOCK_AGENT_S3_DEPLOYER_H
#define FUNCTION_AGENT_MOCK_AGENT_S3_DEPLOYER_H

#include <gmock/gmock.h>

#include "common/utils/files.h"
#include "function_agent/code_deployer/deployer.h"
#include "function_agent/code_deployer/s3_deployer.h"

using namespace functionsystem::function_agent;

namespace functionsystem::test {
class MockAgentS3Deployer : public S3Deployer {
public:
    explicit MockAgentS3Deployer(std::shared_ptr<S3Config> config, messages::CodePackageThresholds msg) : S3Deployer(config, msg)
    {
    }

    DeployResult Deploy(const std::shared_ptr<messages::DeployRequest> &request) override
    {
        YRLOG_WARN("MockAgentS3Deployer received Deploy request, deployDir({}), bucketID({}), objectID({})",
                   request->deploymentconfig().deploydir(), request->deploymentconfig().bucketid(),
                   request->deploymentconfig().objectid());
        DeployResult result;
        auto config = request->deploymentconfig();
        std::string codeDir = litebus::os::Join(config.deploydir(), "layer");
        if (!config.storagetype().empty()) {
            codeDir = litebus::os::Join(codeDir, "func");
        }
        auto splits = litebus::strings::Split(request->instanceid(), "-");
        // simulated download time
        if (splits.size() > 1) {
            auto downloadTime = std::stoi(splits[1]);
            usleep(downloadTime);
        }
        std::string bucketDir = litebus::os::Join(codeDir, config.bucketid());
        std::string destination = litebus::os::Join(bucketDir, config.objectid());

        (void)litebus::os::Mkdir(bucketDir);
        (void)TouchFile(destination);
        result.destination = destination;
        result.status = Status::OK();
        return result;
    }

    bool Clear(const std::string &filePath, const std::string &objectKey) override
    {
        YRLOG_WARN("MockAgentS3Deployer received Clear request of {}", filePath);
        (void)litebus::os::Rmdir(filePath);
        return true;
    }
};
}  // namespace functionsystem::test

#endif  // FUNCTION_AGENT_MOCK_AGENT_S3_DEPLOYER_H