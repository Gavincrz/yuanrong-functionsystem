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

#ifndef IAM_SERVER_FLAGS_FLAGS_H
#define IAM_SERVER_FLAGS_FLAGS_H

#include "common/common_flags/common_flags.h"

namespace functionsystem::iamserver {

class Flags : public CommonFlags {
public:
    Flags();
    ~Flags() override;

    const std::string &GetLogConfig() const
    {
        return logConfig;
    }

    const std::string &GetNodeID() const
    {
        return nodeID;
    }

    const std::string &GetIP() const
    {
        return ip;
    }

    const std::string &GetHTTPListenPort() const
    {
        return httpListenPort;
    }

    const std::string &GetMetaStoreAddress() const
    {
        return metaStoreAddress;
    }

    bool GetEnableTrace() const
    {
        return enableTrace;
    }

    bool GetIsEnableIAM() const
    {
        return enableIAM_;
    }

    uint32_t GetTokenExpiredTimeSpan() const
    {
        return tokenExpiredTimeSpan_;
    }

    const std::string &GetDecryptAlgorithm() const
    {
        return decryptAlgorithm_;
    }

    const std::string &GetResourcePath() const
    {
        return resourcePath_;
    }

    const std::string &GetK8sBasePath() const
    {
        return k8sBasePath_;
    }

    const std::string &GetK8sNamespace() const
    {
        return k8sNamespace_;
    }

    const std::string &GetElectionMode() const
    {
        return electionMode_;
    }

    uint32_t GetElectLeaseTTL() const
    {
        return electLeaseTTL_;
    }

    uint32_t GetElectKeepAliveInterval() const
    {
        return electKeepAliveInterval_;
    }

    const std::string GetIamCredentialType() const
    {
        return iamCredentialType_;
    }

    const std::string GetPermanentCredentialConfigPath() const
    {
        return permanentCredentialConfigPath_;
    }

    [[nodiscard]] std::string GetCredentialHostAddress() const
    {
        return credentialHostAddress_;
    }

private:
    std::string logConfig;
    std::string nodeID;
    std::string ip;
    std::string httpListenPort;
    std::string metaStoreAddress;
    bool enableTrace = false;
    std::string servicesPath_;
    std::string libPath_;
    bool enableIAM_ = false;
    uint32_t tokenExpiredTimeSpan_;
    std::string resourcePath_;
    std::string decryptAlgorithm_;
    std::string k8sBasePath_;
    std::string k8sNamespace_;
    uint32_t electLeaseTTL_;
    uint32_t electKeepAliveInterval_;
    std::string electionMode_;
    std::string iamCredentialType_;
    std::string permanentCredentialConfigPath_;
    std::string credentialHostAddress_;
};
} // functionsystem::iamserver
#endif // IAM_SERVER_FLAGS_FLAGS_H