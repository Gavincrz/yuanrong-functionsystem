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

#ifndef IAM_SERVER_INTERNAL_IAM_AKSK_MANAGER_ACTOR_H
#define IAM_SERVER_INTERNAL_IAM_AKSK_MANAGER_ACTOR_H

#include "common/constants/actor_name.h"
#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "common/meta_storage_accessor/meta_storage_accessor.h"
#include "common/utils/request_sync_helper.h"
#include "common/utils/aksk_content.h"

#include "iam_server/constants.h"

namespace functionsystem::iamserver {

using AKSKMap = std::unordered_map<std::string, std::shared_ptr<AKSKContent>>;
using FetchCredentialFunc = std::function<litebus::Future<std::shared_ptr<AKSKContent>>(
    const std::string &, const std::string &, const std::vector<std::string> &)>;
using SaveCredentialFunc = std::function<litebus::Future<Status>(
    const std::string &, const std::string &, const std::vector<std::string> &, const std::shared_ptr<AKSKContent> &)>;

class AKSKManagerActor : public litebus::ActorBase, public std::enable_shared_from_this<AKSKManagerActor> {
public:
    struct Config {
        std::string clusterID;
        uint32_t expiredTimeSpan;
        std::shared_ptr<MetaStoreClient> metaStoreClient;
        std::vector<std::shared_ptr<PermanentCredential>> permanentCreds;
        std::string hostAddress;
    };

    AKSKManagerActor() = delete;
    AKSKManagerActor(const std::string &name, const AKSKManagerActor::Config &config);

    ~AKSKManagerActor() override = default;

    Status Register();

    litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKByTenantID(const std::string &tenantID,
                                                                        const bool isPermanentValid = false);
    litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKByAK(const std::string &accessKey);
    litebus::Future<Status> AbandonAKSKByTenantID(const std::string &tenantID);

    litebus::Future<std::shared_ptr<AKSKContent>> GenerateNewAKSK(const std::string &tenantID,
                                                                  const bool isPermanentValid = false);

    litebus::Future<Status> OnGenerateCredential(const std::string &tenantID, const Status &status,
                                                 const std::shared_ptr<AKSKContent> &credential);

    void OnGenerateCredentialError(const std::string &tenantID, const litebus::Future<Status> &status,
                                   const std::shared_ptr<AKSKContent> &credential);

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    litebus::Future<Status> SyncToReplaceAKSK(bool isNew);

    void ForwardGetAKSKByTenantID(const litebus::AID &from, std::string &&name, std::string &&msg);
    void ForwardGetAKSKByAK(const litebus::AID &from, std::string &&name, std::string &&msg);
    void ForwardGetAKSKResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> SendForwardGetAKSKByTenantID(
        const std::string &tenantID, const bool needCreate, const bool isPermanentValid = false);
    void OnForwardGetNewAKSKByTenantID(
        const litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> &future,
        const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise, const std::string &tenantID);
    void OnForwardGetCacheAKSKByTenantID(
        const litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> &future,
        const std::shared_ptr<litebus::Promise<Status>> &promise);

    litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> SendForwardGetAKSKByAK(const std::string &accessKey);
    void OnForwardGetCacheAKSKByAK(
        const litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> &future,
        const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise, const std::string &accessKey);

    void RegisterFetchCredFromRemoteCb(const FetchCredentialFunc &cb)
    {
        fetchCredFromRemoteCb_ = cb;
    }

    void RegisterSaveCredToRemoteCb(const SaveCredentialFunc &cb)
    {
        saveCredToRemoteCb_ = cb;
    }

protected:
    void Init() override;
    void InitSystemAKSK();
    void Finalize() override;

    virtual litebus::Future<litebus::http::Response> SendRequest(const litebus::http::Request &request);

private:
    static void GetAKSKContentFromGetAKSKResponse(const std::shared_ptr<messages::GetAKSKResponse> &response,
                                                  std::shared_ptr<AKSKContent> &akskContent, const bool isNew);
    static void SetGetAKSKResponseFromAKSKContent(std::shared_ptr<messages::GetAKSKResponse> &response,
                                                  const std::shared_ptr<AKSKContent> &akskContent, const bool isNew);

    litebus::Future<Status> GenerateAKSK(const std::string &tenantID, const std::shared_ptr<AKSKContent> &akskContent,
                                         const bool isPermanentValid = false);

    litebus::Future<Status> GetCredentialFromURL(const std::string &tenantID,
                                                 const std::shared_ptr<AKSKContent> &credential);

    void AsyncInitialize(const std::shared_ptr<GetResponse> &newResponse,
                         const std::shared_ptr<GetResponse> &oldResponse);

    litebus::Future<SyncResult> IAMAKSKSyncer(const std::shared_ptr<GetResponse> &getResponse, bool isNew);

    void SyncAKSKFromMetaStore(const std::shared_ptr<GetResponse> &response, bool isNew);

    litebus::Future<AKSKMap> AsyncGetAKSKFromMetaStore(bool isNew);

    Status ReplaceAKSKMap(const AKSKMap &akskMap, const bool isNew);

    litebus::Future<std::shared_ptr<PutResponse>> PutAKSKToMetaStore(const std::shared_ptr<AKSKContent> &akskContent,
                                                                     const bool isNew);

    Status OnPutAKSKToMetaStore(const std::shared_ptr<PutResponse> &response,
                                const std::shared_ptr<AKSKContent> &akskContent, const bool isNew,
                                const bool isSetPromise = false);

    std::shared_ptr<AKSKContent> FindAKSKByTenantIDFromCache(const std::string &tenantID, const bool isNew);

    std::shared_ptr<AKSKContent> FindAKSKByAKFromCache(const std::string &accessKey);

    void CheckAKSKExpiredInAdvance();
    void CheckAKSKExpiredInTime();

    void UpdateAKSKEvent(const std::vector<WatchEvent> &events);

    static std::shared_ptr<AKSKContent> GetAKSKContentFromEventValue(const std::string &value);

    void PutAKSKFromEventValueToCache(const std::string &eventValue, const bool isNew);
    void DelAKSKFromCache(const std::string &tenantID, const bool isNew);

    void UpdateNewAKSKCompareTime(const std::shared_ptr<AKSKContent> &akskContent);
    void UpdateOldAKSKCompareTime(const std::shared_ptr<AKSKContent> &akskContent);
    void UpdateAKMapCompareTime(const std::shared_ptr<AKSKContent> &akskContent);

    Status OnRequireAKSKByTenantID(const std::shared_ptr<AKSKContent> &akskContent, const litebus::AID &from,
                                   const std::shared_ptr<messages::GetAKSKByTenantIDRequest> &akskRequest);

    void PutGetAKSKResponseToCache(const std::shared_ptr<messages::GetAKSKResponse> &akskResponse);

    litebus::Future<Status> HandleAbandonAKSKByTenantID(const std::string &tenantID);

    litebus::Future<std::shared_ptr<DeleteResponse>> DelAKSKFromMetaStore(const std::string &tenantID,
                                                                          const bool isNew);
    Status OnDelAKSKFromMetaStore(const std::shared_ptr<DeleteResponse> &response, const std::string &tenantID,
                                  const bool isNew);

    litebus::Future<Status> DelNewAKSK(const Status &status, const std::string &tenantID);

    litebus::Future<Status> HandleUpdateAKSKInAdvance(const std::string &tenantID);
    litebus::Future<Status> HandleUpdateAKSKInTime(const std::string &tenantID);

    litebus::Future<Status> OnUpdateAKSKInAdvance(const Status &status, const std::string &tenantID);
    litebus::Future<Status> OnUpdateAKSKInTime(const Status &status, const std::string &tenantID);

    litebus::Future<Status> UpdateAKSKInAdvancePutNew(const Status &status,
                                                      const std::shared_ptr<AKSKContent> &akskContent);

    litebus::Future<Status> HandlePermanentCredential(bool isReady);
    litebus::Future<Status> DoHandlePermanentCredential();

    litebus::Future<Status> OnFetchPermanentCredentialFromRemote(
        const litebus::Future<std::shared_ptr<AKSKContent>> &future,
        const std::shared_ptr<PermanentCredential> &credential);

    litebus::Future<Status> OnRequirePermanentCredential(const litebus::Future<std::shared_ptr<AKSKContent>> &future,
                                                         const std::shared_ptr<PermanentCredential> &credential);

    struct Member {
        /**
         * aksk that will be refreshed in advance, with new expired time
         * map key is tenantID
         */
        std::unordered_map<std::string, std::shared_ptr<AKSKContent>> newAKSKMap;
        /**
         * aksk that will be deleted in time, still valid, with old expired time
         * in case for refreshed aksk has not been transmitted to runtime which leads to verifying failed
         * map key is tenantID
         */
        std::unordered_map<std::string, std::shared_ptr<AKSKContent>> oldAKSKMap;
        /**
         * all aksk
         * map key is accessKey
         */
        std::unordered_map<std::string, std::shared_ptr<AKSKContent>> akMap;
        std::unordered_map<std::string, std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>>>
            genCredPendingQueue_;
        std::unordered_set<std::string> inProgressAdvanceTenants;
        std::unordered_set<std::string> inProgressExpireTenants;
        std::vector<std::shared_ptr<PermanentCredential>> permanentCreds{};
        KeyForAKSK authKey_;

        std::shared_ptr<MetaStoreClient> metaStoreClient{ nullptr };
        std::shared_ptr<MetaStorageAccessor> metaStorageAccessor{ nullptr };
        uint32_t akskExpiredTimeSpan;
        std::string clusterID;
        litebus::AID masterAID;
        uint32_t checkAKSKExpireInterval{ CHECK_EXPIRED_INTERVAL };
        uint32_t aheadUpdateExpireAKSKTime{ TIME_AHEAD_OF_EXPIRED };
        litebus::Timer checkAKSKExpiredInAdvanceTimer;
        litebus::Timer checkAKSKExpiredInTimeTimer;
        litebus::Promise<bool> isReady;
        bool initialized{ false };
        // The third-party credential hosting platform
        std::string credentialHostAddress_;
    };

    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<AKSKManagerActor> &actor, const std::shared_ptr<Member> &member)
            : actor_(actor), member_(member) {}
        ~Business() override = default;

        virtual litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKByTenantID(
            const std::string &tenantID, const bool isPermanentValid = false) = 0;
        virtual litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKByAK(const std::string &accessKey) = 0;
        virtual litebus::Future<Status> AbandonAKSKByTenantID(const std::string &tenantID) = 0;
        virtual litebus::Future<Status> UpdateAKSKInAdvance(const std::string &ak) = 0;
        virtual litebus::Future<Status> UpdateAKSKInTime(const std::string &ak) = 0;
        virtual litebus::Future<Status> HandleForwardGetAKSKByTenantID(
            const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByTenantIDRequest> &akskRequest) = 0;
        virtual litebus::Future<Status> HandleForwardGetAKSKByAK(
            const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByAKRequest> &akskRequest) = 0;
        virtual litebus::Future<Status> HandlePermanentCredential() = 0;

    protected:
        std::weak_ptr<AKSKManagerActor> actor_;
        std::shared_ptr<Member> member_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<AKSKManagerActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member) {}
        ~MasterBusiness() override = default;

        void OnChange() override;
        litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKByTenantID(
            const std::string &tenantID, const bool isPermanentValid = false) override;
        litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKByAK(const std::string &accessKey) override;
        litebus::Future<Status> AbandonAKSKByTenantID(const std::string &tenantID) override;
        litebus::Future<Status> UpdateAKSKInAdvance(const std::string &tenantID) override;
        litebus::Future<Status> UpdateAKSKInTime(const std::string &tenantID) override;
        litebus::Future<Status> HandleForwardGetAKSKByTenantID(
            const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByTenantIDRequest> &akskRequest) override;
        litebus::Future<Status> HandleForwardGetAKSKByAK(
            const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByAKRequest> &akskRequest) override;
        litebus::Future<Status> HandlePermanentCredential() override;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<AKSKManagerActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member) {}
        ~SlaveBusiness() override = default;

        void OnChange() override;
        litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKByTenantID(
            const std::string &tenantID, const bool isPermanentValid = false) override;
        litebus::Future<std::shared_ptr<AKSKContent>> RequireAKSKByAK(const std::string &accessKey) override;
        litebus::Future<Status> AbandonAKSKByTenantID(const std::string &tenantID) override;
        litebus::Future<Status> UpdateAKSKInAdvance(const std::string &tenantID) override;
        litebus::Future<Status> UpdateAKSKInTime(const std::string &tenantID) override;
        litebus::Future<Status> HandleForwardGetAKSKByTenantID(
            const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByTenantIDRequest> &akskRequest) override;
        litebus::Future<Status> HandleForwardGetAKSKByAK(
            const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByAKRequest> &akskRequest) override;
        litebus::Future<Status> HandlePermanentCredential() override;
    };

    std::shared_ptr<Member> member_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    std::string curStatus_;
    std::string masterName_{ AKSK_MANAGER_ACTOR_NAME };
    std::shared_ptr<Business> business_{ nullptr };
    FetchCredentialFunc fetchCredFromRemoteCb_{ nullptr };
    SaveCredentialFunc saveCredToRemoteCb_{ nullptr };
    const uint32_t forwardGetAKSKTimeout_ = 5000;
    REQUEST_SYNC_HELPER(AKSKManagerActor, std::shared_ptr<messages::GetAKSKResponse>, forwardGetAKSKTimeout_,
        forwardGetAKSKSync_);
};
}

#endif // IAM_SERVER_INTERNAL_IAM_AKSK_MANAGER_ACTOR_H
