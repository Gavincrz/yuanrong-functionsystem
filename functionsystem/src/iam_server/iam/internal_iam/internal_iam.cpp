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

#include "internal_iam.h"

#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "common/sts/sts_manager.h"

namespace functionsystem::iamserver {

InternalIAM::~InternalIAM()
{
    if (tokenManagerActor_ != nullptr) {
        litebus::Terminate(tokenManagerActor_->GetAID());
        litebus::Await(tokenManagerActor_->GetAID());
    }
    if (akskManagerActor_ != nullptr) {
        litebus::Terminate(akskManagerActor_->GetAID());
        litebus::Await(akskManagerActor_->GetAID());
    }
}

bool InternalIAM::Init()
{
    if (!startParam_.isEnableIAM) {
        YRLOG_WARN("iam is not enable, Init failed.");
        return false;
    }
    if (startParam_.credType == IAMCredType::TOKEN) {
        tokenManagerActor_ = std::make_shared<TokenManagerActor>(TOKEN_MANAGER_ACTOR_NAME, startParam_.clusterID,
                                                                 startParam_.tokenExpiredTimeSpan, metaStoreClient_);
        litebus::Spawn(tokenManagerActor_);
    } else if (startParam_.credType == IAMCredType::AK_SK) {
        auto permanentCredConfig = LoadPermanentCredentialConfig(startParam_.permanentCredentialConfigPath);
        if (stsManager_ == nullptr) {
            stsManager_ = std::make_shared<StsManager>(std::make_shared<StsClient>());
        }
        auto status = stsManager_->Init(true);
        AKSKManagerActor::Config config{ .clusterID = startParam_.clusterID,
                                         .expiredTimeSpan = startParam_.tokenExpiredTimeSpan,
                                         .metaStoreClient = metaStoreClient_,
                                         .permanentCreds = permanentCredConfig,
                                         .hostAddress = startParam_.credentialHostAddress };
        akskManagerActor_ = std::make_shared<AKSKManagerActor>(AKSK_MANAGER_ACTOR_NAME, config);
        if (status.IsOk()) {
            akskManagerActor_->RegisterFetchCredFromRemoteCb(
                [stsManager(stsManager_)](const std::string &credentialName, const std::string &service,
                                          const std::vector<std::string> &mircoServices) {
                    return stsManager->CheckCredentialExist(credentialName, service, mircoServices);
                });
            akskManagerActor_->RegisterSaveCredToRemoteCb(
                [stsManager(stsManager_)](const std::string &credentialName, const std::string &service,
                                          const std::vector<std::string> &mircoServices,
                                          const std::shared_ptr<AKSKContent> &akskContent) {
                    return stsManager->CreateCredential(credentialName, service, mircoServices, akskContent);
                });
            YRLOG_INFO("success to register fetch and save credential callback");
        }
        litebus::Spawn(akskManagerActor_);
    } else {
        YRLOG_ERROR("InternalIAM cred type is invalid.");
        return false;
    }
    YRLOG_DEBUG("InternalIAM Init finished.");
    return true;
}

litebus::Future<Status> InternalIAM::VerifyToken(const std::shared_ptr<TokenContent> &tokenContent)
{
    ASSERT_IF_NULL(tokenManagerActor_);
    return litebus::Async(tokenManagerActor_->GetAID(), &TokenManagerActor::VerifyToken, tokenContent);
}

litebus::Future<std::shared_ptr<TokenSalt>> InternalIAM::RequireEncryptToken(const std::string &tenantID)
{
    ASSERT_IF_NULL(tokenManagerActor_);
    return litebus::Async(tokenManagerActor_->GetAID(), &TokenManagerActor::RequireEncryptToken, tenantID);
}

litebus::Future<Status> InternalIAM::AbandonTokenByTenantID(const std::string &tenantID)
{
    ASSERT_IF_NULL(tokenManagerActor_);
    return litebus::Async(tokenManagerActor_->GetAID(), &TokenManagerActor::AbandonTokenByTenantID, tenantID);
}

litebus::Future<Status> InternalIAM::SyncToReplaceToken(bool isNew)
{
    ASSERT_IF_NULL(tokenManagerActor_);
    return litebus::Async(tokenManagerActor_->GetAID(), &TokenManagerActor::SyncToReplaceToken, isNew);
}

litebus::Future<std::shared_ptr<AKSKContent>> InternalIAM::RequireAKSKContentByTenantID(const std::string &tenantID,
                                                                                        const bool isPermanentValid)
{
    ASSERT_IF_NULL(akskManagerActor_);
    return litebus::Async(akskManagerActor_->GetAID(), &AKSKManagerActor::RequireAKSKByTenantID, tenantID,
                          isPermanentValid);
}

litebus::Future<std::shared_ptr<AKSKContent>> InternalIAM::RequireAKSKContentByAK(const std::string &accessKey)
{
    ASSERT_IF_NULL(akskManagerActor_);
    return litebus::Async(akskManagerActor_->GetAID(), &AKSKManagerActor::RequireAKSKByAK, accessKey);
}

litebus::Future<Status> InternalIAM::AbandonAKSKByTenantID(const std::string &tenantID)
{
    ASSERT_IF_NULL(akskManagerActor_);
    return litebus::Async(akskManagerActor_->GetAID(), &AKSKManagerActor::AbandonAKSKByTenantID, tenantID);
}

litebus::Future<Status> InternalIAM::SyncToReplaceAKSK(bool isNew)
{
    ASSERT_IF_NULL(akskManagerActor_);
    return litebus::Async(akskManagerActor_->GetAID(), &AKSKManagerActor::SyncToReplaceAKSK, isNew);
}

std::vector<std::shared_ptr<PermanentCredential>> InternalIAM::LoadPermanentCredentialConfig(
    const std::string &configPath)
{
    if (!FileExists(configPath)) {
        return {};
    }
    std::string jsonStr = Read(configPath);
    return TransToPermanentCredFromJson(jsonStr);
}
}  // namespace functionsystem::iamserver