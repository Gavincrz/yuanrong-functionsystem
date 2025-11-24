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

#ifndef FUNCTION_AGENT_MOCK_AGENT_DEPLOYER_H
#define FUNCTION_AGENT_MOCK_AGENT_DEPLOYER_H

#include <gmock/gmock.h>

#include "function_agent/code_deployer/deployer.h"
#include "function_agent/code_deployer/s3_deployer.h"

namespace functionsystem::test {
class MockTestAgentS3Deployer : public function_agent::S3Deployer {
public:
    explicit MockTestAgentS3Deployer(std::shared_ptr<S3Config> config, messages::CodePackageThresholds codePackageThresholds,
               bool enableSignatureValidation = false): function_agent::S3Deployer(config, nullptr, codePackageThresholds, enableSignatureValidation)
    {}
    MOCK_METHOD(bool, IsDeployed, (const std::string &destination, const bool isMonopoly),
                (override));
    MOCK_METHOD(function_agent::DeployResult, Deploy, (const std::shared_ptr<messages::DeployRequest> &req),
                (override));
    MOCK_METHOD(bool, Clear, (const std::string &filePath, const std::string &objectKey), (override));
};
}  // namespace functionsystem::test

#endif  // FUNCTION_AGENT_MOCK_AGENT_DEPLOYER_H