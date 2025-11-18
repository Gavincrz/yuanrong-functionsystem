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

#ifndef IAM_SERVER_INTERNAL_IAM_INTERNAL_IAM_H
#define IAM_SERVER_INTERNAL_IAM_INTERNAL_IAM_H

#include "common/status/status.h"
#include "common/meta_storage_accessor/meta_storage_accessor.h"
#include "common/utils/token_transfer.h"
#include "token_manager_actor.h"
#include "aksk_manager_actor.h"

namespace functionsystem::iamserver {
class InternalIAM {
public:
    enum class IAMCredType : uint32_t { TOKEN = 0, AK_SK = 1 };
    struct Param {
        uint32_t tokenExpiredTimeSpan;
        bool isEnableIAM;
        std::string clusterID;
        IAMCredType credType{ IAMCredType::TOKEN };
        std::string permanentCredentialConfigPath;
        std::string credentialHostAddress;
    };
    explicit InternalIAM(const Param &param) : startParam_(std::move(param)) {};
    virtual ~InternalIAM();

    inline bool IsIAMEnabled() const
    {
        return startParam_.isEnableIAM;
    }

    bool Init();
    // set virtual for mocking
    virtual litebus::Future<Status> VerifyToken(const std::shared_ptr<TokenContent> &tokenContent);
    // set virtual for mocking
    virtual litebus::Future<std::shared_ptr<TokenSalt>> RequireEncryptToken(const std::string &tenantID);
    // set virtual for mocking
    virtual litebus::Future<Status> AbandonTokenByTenantID(const std::string &tenantID);

    virtual litebus::Future<Status> SyncToReplaceToken(bool isNew);
    /**
    * generate new aksk or return local new aksk in cache if exists.
    * for creating aksk when creating instance
    */
    virtual litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKContentByTenantID(
        const std::string &tenantID, const bool isPermanentValid = false);
    /**
     * get aksk by ak.
     * for verifying request from instance
     */
    virtual litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKContentByAK(const std::string &accessKey);

    virtual litebus::Future<Status> AbandonAKSKByTenantID(const std::string &tenantID);

    virtual litebus::Future<Status> SyncToReplaceAKSK(bool isNew);

    void BindMetaStoreClient(const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    {
        metaStoreClient_ = metaStoreClient;
    }

    const IAMCredType &GetIAMCredType() const
    {
        return startParam_.credType;
    }
private:
    std::vector<std::shared_ptr<PermanentCredential>> LoadPermanentCredentialConfig(const std::string &configPath);

    Param startParam_;
    std::shared_ptr<MetaStoreClient> metaStoreClient_{ nullptr };
    std::shared_ptr<TokenManagerActor> tokenManagerActor_{ nullptr };
    std::shared_ptr<AKSKManagerActor> akskManagerActor_{ nullptr };
    std::shared_ptr<StsManager> stsManager_ {nullptr};
};
}  // namespace functionsystem::iamserver
#endif // IAM_SERVER_INTERNAL_IAM_INTERNAL_IAM_H