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

#ifndef IAM_SERVER_INTERNAL_IAM_TOKEN_MANAGER_ACTOR_H
#define IAM_SERVER_INTERNAL_IAM_TOKEN_MANAGER_ACTOR_H

#include "common/constants/actor_name.h"
#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "common/meta_storage_accessor/meta_storage_accessor.h"
#include "common/utils/request_sync_helper.h"
#include "common/proto/pb/message_pb.h"
#include "common/utils/token_transfer.h"
#include "common/utils/actor_driver.h"

#include "iam_server/constants.h"
#include "token_content.h"

namespace functionsystem::iamserver {

using TokenMap = std::unordered_map<std::string, std::shared_ptr<TokenContent>>;

const uint32_t CHECK_TOKEN_EXPIRED_INTERVAL = 2 * 60 * 1000;  // unit: ms, check token every 2 min

class TokenManagerActor : public litebus::ActorBase, public std::enable_shared_from_this<TokenManagerActor> {
public:
    TokenManagerActor() = delete;
    TokenManagerActor(const std::string &name, const std::string &clusterID, const uint32_t expiredTimeSpan,
                      const std::shared_ptr<MetaStoreClient> &metaStoreClient);

    ~TokenManagerActor() override = default;

    void ForwardGetToken(const litebus::AID &from, std::string &&name, std::string &&msg);
    void ForwardGetTokenResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    litebus::Future<std::shared_ptr<messages::GetTokenResponse>> SendForwardGetToken(const std::string &tenantID,
                                                                                 const bool needCreate);

    void OnForwardGetNewToken(
        const litebus::Future<std::shared_ptr<messages::GetTokenResponse>> future,
        const std::shared_ptr<litebus::Promise<std::shared_ptr<TokenSalt>>> &promise, const std::string &tenantID);

    void OnForwardGetCacheToken(const litebus::Future<std::shared_ptr<messages::GetTokenResponse>> future,
                                  const std::shared_ptr<litebus::Promise<Status>> &promise,
                                  const std::shared_ptr<TokenContent> &tokenContent);

    litebus::Future<std::shared_ptr<TokenSalt>> RequireEncryptToken(const std::string &tenantID);
    litebus::Future<Status> VerifyToken(const std::shared_ptr<TokenContent> &tokenContent);
    litebus::Future<Status> AbandonTokenByTenantID(const std::string &tenantID);

    litebus::Future<std::shared_ptr<TokenSalt>> GenerateNewToken(const std::string &tenantID);

    std::shared_ptr<TokenSalt> GetLocalNewToken(const std::string &tenantID);

    Status CheckTokenSameWithCache(const std::shared_ptr<TokenContent> token);

    litebus::Future<Status> SyncToReplaceToken(bool isNew);

    Status Register();

protected:
    void Init() override;
    void Finalize() override;
private:

    static Status DecryptToken(const std::string &tokenStr, const std::shared_ptr<TokenContent> &tokenContent);

    static Status EncryptToken(const std::shared_ptr<TokenContent> &tokenContent);

    static litebus::Option<std::string> EncryptTokenForStorage(const std::shared_ptr<TokenContent> &tokenContent);

    static Status DecryptTokenFromStorage(const std::string &encryptTokenFromStorage,
                                          const std::shared_ptr<TokenContent> &tokenContent);

    static std::shared_ptr<TokenContent> GetTokenContentFromEventValue(const std::string &value);
    /**
     * find tokenContent from local token cache
     * @param tenantID: key for find tokenContent in tokenMap
     * @param isNewTokenMap: find tokenContent from newTokenMap or oldTokenMap, true for newTokenMap
     */
    const std::shared_ptr<TokenContent> FindTokenFromCache(const std::string &tenantID, const bool &isNewTokenMap);

    Status GenerateToken(const std::string &tenantID, const std::shared_ptr<TokenContent> &tokenContent);

    Status OnRequireEncryptToken(const std::shared_ptr<TokenSalt> &tokenSalt, const litebus::AID &from,
                                 const std::shared_ptr<messages::GetTokenRequest> &tokenRequest);

    void PutGetTokenResponseToCache(const std::shared_ptr<messages::GetTokenResponse> &tokenResponse);

    void SyncTokenFromMetaStore(const std::shared_ptr<GetResponse> &response, bool isNew);

    void AsyncInitialize(const std::shared_ptr<GetResponse> &newResponse,
                         const std::shared_ptr<GetResponse> &oldResponse);

    litebus::Future<TokenMap> AsyncGetTokenFromMetaStore(bool isNew);

    Status ReplaceTokenMap(const std::unordered_map<std::string, std::shared_ptr<TokenContent>> tokenMap,
                           const bool isNew);

    litebus::Future<std::shared_ptr<PutResponse>> PutTokenToMetaStore(const std::shared_ptr<TokenContent> &tokenContent,
                                                                      const bool isNew);

    Status OnPutTokenFromMetaStore(const std::shared_ptr<PutResponse> &response,
                                   const std::shared_ptr<TokenContent> &tokenContent, const bool isNew,
                                   const bool isSetPromise = false);

    litebus::Future<std::shared_ptr<DeleteResponse>> DelTokenFromMetaStore(const std::string &tenantID,
                                                                           const bool isNew);

    Status OnDelTokenFromMetaStore(const std::shared_ptr<DeleteResponse> &response, const std::string &tenantID,
                                   const bool isNew);

    litebus::Future<Status> UpdateTokenInAdvancePutNew(const Status &status,
                                                       const std::shared_ptr<TokenContent> &tokenContent);

    void CheckTokenExpiredInAdvance();
    void CheckTokenExpiredInTime();

    litebus::Future<Status> HandleUpdateTokenInTime(const std::string &tenantID);

    litebus::Future<Status> HandleUpdateTokenInAdvance(const std::string &tenantID);

    litebus::Future<Status> OnUpdateTokenInTime(const Status &status, const std::string &tenantID);

    litebus::Future<Status> OnUpdateTokenInAdvance(const Status &status, const std::string &tenantID);

    litebus::Future<Status> HandleAbandonTokenByTenantID(const std::string &tenantID);

    litebus::Future<Status> DelNewToken(const Status &status, const std::string &tenantID);

    void UpdateTokenEvent(const std::vector<WatchEvent> &events);

    litebus::Future<SyncResult> IAMTokenSyncer(const std::shared_ptr<GetResponse> &getResponse, bool isNew);

    void UpdateNewTokenCompareTime(const std::shared_ptr<TokenContent> &tokenContent);
    void UpdateOldTokenCompareTime(const std::shared_ptr<TokenContent> &tokenContent);

    struct Member {
        std::shared_ptr<MetaStoreClient> metaStoreClient{ nullptr };
        std::shared_ptr<MetaStorageAccessor> metaStorageAccessor{ nullptr };
        /**
         * token that will be refreshed in advance, with new expired time
         */
        std::unordered_map<std::string, std::shared_ptr<TokenContent>> newTokenMap;
        /**
         * token that will be deleted in time, still valid, with old expired time
         * in case for refreshed token has not been transmitted to runtime which leads to verifying failed
         */
        std::unordered_map<std::string, std::shared_ptr<TokenContent>> oldTokenMap;
        std::unordered_map<std::string, std::shared_ptr<litebus::Promise<std::shared_ptr<TokenSalt>>>>
            newTokenRequestMap;
        std::unordered_set<std::string> inProgressAdvanceTenants;
        std::unordered_set<std::string> inProgressExpireTenants;
        uint32_t tokenExpiredTimeSpan;
        std::string clusterID;
        litebus::AID masterAID;
        uint32_t checkTokenExpireInterval{ CHECK_TOKEN_EXPIRED_INTERVAL };
        uint32_t aheadUpdateExpireTokenTime{ TIME_AHEAD_OF_EXPIRED };
        litebus::Timer checkTokenExpiredInAdvanceTimer;
        litebus::Timer checkTokenExpiredInTimeTimer;
        bool initialized{ false };
    };

    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<TokenManagerActor> &actor, const std::shared_ptr<Member> &member)
            : actor_(actor), member_(member) {}
        ~Business() override = default;

        virtual litebus::Future<std::shared_ptr<TokenSalt>> RequireEncryptToken(const std::string &tenantID) = 0;
        virtual litebus::Future<Status> VerifyToken(const std::shared_ptr<TokenContent> &tokenContent) = 0;
        virtual litebus::Future<Status> AbandonTokenByTenantID(const std::string &tenantID) = 0;
        virtual litebus::Future<Status> UpdateTokenInAdvance(const std::string &tenantID) = 0;
        virtual litebus::Future<Status> UpdateTokenInTime(const std::string &tenantID) = 0;
        virtual litebus::Future<Status> HandleForwardGetToken(
            const litebus::AID &from, const std::shared_ptr<messages::GetTokenRequest> &tokenRequest) = 0;

    protected:
        std::weak_ptr<TokenManagerActor> actor_;
        std::shared_ptr<Member> member_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<TokenManagerActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member)
        {
        }
        ~MasterBusiness() override = default;

        void OnChange() override;
        litebus::Future<std::shared_ptr<TokenSalt>> RequireEncryptToken(const std::string &tenantID) override;
        litebus::Future<Status> VerifyToken(const std::shared_ptr<TokenContent> &tokenContent) override;
        litebus::Future<Status> AbandonTokenByTenantID(const std::string &tenantID) override;
        litebus::Future<Status> UpdateTokenInAdvance(const std::string &tenantID) override;
        litebus::Future<Status> UpdateTokenInTime(const std::string &tenantID) override;
        litebus::Future<Status> HandleForwardGetToken(
            const litebus::AID &from, const std::shared_ptr<messages::GetTokenRequest> &tokenRequest) override;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<TokenManagerActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member)
        {
        }
        ~SlaveBusiness() override = default;

        void OnChange() override;
        litebus::Future<std::shared_ptr<TokenSalt>> RequireEncryptToken(const std::string &tenantID) override;
        litebus::Future<Status> VerifyToken(const std::shared_ptr<TokenContent> &tokenContent) override;
        litebus::Future<Status> AbandonTokenByTenantID(const std::string &tenantID) override;
        litebus::Future<Status> UpdateTokenInAdvance(const std::string &tenantID) override;
        litebus::Future<Status> UpdateTokenInTime(const std::string &tenantID) override;
        litebus::Future<Status> HandleForwardGetToken(
            const litebus::AID &from, const std::shared_ptr<messages::GetTokenRequest> &tokenRequest) override;
    };

    std::shared_ptr<Member> member_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    std::string curStatus_;
    std::string masterName_{ TOKEN_MANAGER_ACTOR_NAME };
    std::shared_ptr<Business> business_{ nullptr };
    const uint32_t forwardGetTokenTimeout_ = 5000;
    REQUEST_SYNC_HELPER(TokenManagerActor, std::shared_ptr<messages::GetTokenResponse>, forwardGetTokenTimeout_,
                        forwardGetTokenSync_);
};

}
#endif  // IAM_SERVER_INTERNAL_IAM_TOKEN_MANAGER_ACTOR_H
