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

#include "aksk_manager_actor.h"

#include <async/defer.hpp>

#include "common/http/http_util.h"
#include "common/utils/hex.h"
#include "common/utils/meta_store_kv_operation.h"

namespace functionsystem::iamserver {

using namespace functionsystem::explorer;

const uint32_t HMAC256_KEYLEN = 32;

AKSKManagerActor::AKSKManagerActor(const std::string &name, const AKSKManagerActor::Config &config)
    : litebus::ActorBase(name)
{
    member_ = std::make_shared<Member>();
    member_->clusterID = config.clusterID;
    member_->akskExpiredTimeSpan = config.expiredTimeSpan;
    // min aksk expiredTimeSpan is 720 s
    if (member_->akskExpiredTimeSpan / MIN_AHEAD_TIME_FACTOR < TIME_AHEAD_OF_EXPIRED) {
        member_->aheadUpdateExpireAKSKTime = member_->akskExpiredTimeSpan / MIN_AHEAD_TIME_FACTOR;
    }
    if (member_->aheadUpdateExpireAKSKTime * MS_SECOND / MIN_EXPIRED_FACTOR < CHECK_EXPIRED_INTERVAL) {
        member_->checkAKSKExpireInterval = member_->aheadUpdateExpireAKSKTime * MS_SECOND / MIN_EXPIRED_FACTOR;
    }
    member_->permanentCreds = config.permanentCreds;
    ASSERT_IF_NULL(config.metaStoreClient);
    member_->metaStoreClient = config.metaStoreClient;
    member_->metaStorageAccessor = std::make_shared<MetaStorageAccessor>(config.metaStoreClient);
    member_->credentialHostAddress_ = config.hostAddress;
}

void AKSKManagerActor::Init()
{
    ASSERT_IF_NULL(member_);
    ASSERT_IF_NULL(member_->metaStoreClient);
    auto masterBusiness = std::make_shared<MasterBusiness>(shared_from_this(), member_);
    auto slaveBusiness = std::make_shared<SlaveBusiness>(shared_from_this(), member_);
    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);
    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);
    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        "AKSKManagerActor", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &AKSKManagerActor::UpdateLeaderInfo, leaderInfo);
        });
    InitSystemAKSK();
    curStatus_ = SLAVE_BUSINESS;
    business_ = slaveBusiness;
    auto newAKSKResponse =
        member_->metaStoreClient->Get(GenAKSKKeyWatchPrefix(member_->clusterID, true), { .prefix = true }).Get();
    auto oldAKSKResponse =
        member_->metaStoreClient->Get(GenAKSKKeyWatchPrefix(member_->clusterID, false), { .prefix = true }).Get();
    Register();
    litebus::Async(GetAID(), &AKSKManagerActor::AsyncInitialize, newAKSKResponse, oldAKSKResponse);
    Receive("ForwardGetAKSKByTenantID", &AKSKManagerActor::ForwardGetAKSKByTenantID);
    Receive("ForwardGetAKSKByAK", &AKSKManagerActor::ForwardGetAKSKByAK);
    Receive("ForwardGetAKSKResponse", &AKSKManagerActor::ForwardGetAKSKResponse);
}

void AKSKManagerActor::InitSystemAKSK()
{
    std::shared_ptr<AKSKContent> akskContent = std::make_shared<AKSKContent>();
    auto enableAKSK = litebus::os::GetEnv(litebus::os::LITEBUS_AKSK_ENABLED);
    if (enableAKSK.IsNone() || enableAKSK.Get() != "1") {
        return;
    }
    auto tmpAk = litebus::os::GetEnv(litebus::os::LITEBUS_ACCESS_KEY);
    auto tmpSK = litebus::os::GetEnv(litebus::os::LITEBUS_SECRET_KEY);
    auto tmpDK = litebus::os::GetEnv(LITEBUS_DATA_KEY);
    if (tmpAk.IsSome() && tmpSK.IsSome() && tmpDK.IsSome()) {
        akskContent->tenantID = "0";
        akskContent->accessKey = tmpAk.Get();
        akskContent->secretKey = SensitiveValue(tmpSK.Get());
        akskContent->dataKey = SensitiveValue(tmpDK.Get());
        akskContent->role = SYSTEM_ROLE;
    } else {
        YRLOG_WARN("Failed to obtain the secret key when 2fa enabled");
        return;
    }
    member_->authKey_ = KeyForAKSK(tmpAk.Get(), SensitiveValue(tmpSK.Get()), SensitiveValue(tmpDK.Get()));
    member_->newAKSKMap["0"] = akskContent;
    member_->akMap[akskContent->accessKey] = akskContent;
}

void AKSKManagerActor::ForwardGetAKSKByTenantID(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto akskRequest = std::make_shared<messages::GetAKSKByTenantIDRequest>();
    if (!akskRequest->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse get aksk by tenantID request");
        return;
    }
    YRLOG_INFO("{}|{}|receive get aksk by tenantID request, isCreate({})", akskRequest->requestid(),
               akskRequest->tenantid(), akskRequest->iscreate());
    ASSERT_IF_NULL(business_);
    (void)business_->HandleForwardGetAKSKByTenantID(from, akskRequest);
}

void AKSKManagerActor::ForwardGetAKSKResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto akskResponse = std::make_shared<messages::GetAKSKResponse>();
    if (!akskResponse->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse aksk response");
        return;
    }
    YRLOG_INFO("{}|{}|receive get aksk response", akskResponse->requestid(), akskResponse->tenantid());
    forwardGetAKSKSync_.Synchronized(akskResponse->requestid(), akskResponse);
}

Status AKSKManagerActor::Register()
{
    if (member_->metaStorageAccessor == nullptr) {
        YRLOG_ERROR("meta store accessor is null");
        return Status(FAILED);
    }
    std::function<litebus::Future<std::shared_ptr<Watcher>>(const litebus::Future<std::shared_ptr<Watcher>> &)> after =
        [](const litebus::Future<std::shared_ptr<Watcher>> &watcher) -> litebus::Future<std::shared_ptr<Watcher>> {
            KillProcess("timeout to watch key, kill oneself.");
            return watcher;
        };
    // keep retrying to watch in 30 seconds. kill process if timeout to watch.
    auto watchOpt = WatchOption{ .prefix = true, .prevKv = false, .revision = 0, .keepRetry = true };
    auto newSyncer = [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &AKSKManagerActor::IAMAKSKSyncer, getResponse, true);
    };
    auto oldSyncer = [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &AKSKManagerActor::IAMAKSKSyncer, getResponse, false);
    };
    auto handler = [aid(GetAID())](const std::vector<WatchEvent> &events, bool) -> bool {
        auto respCopy = events;
        litebus::Async(aid, &AKSKManagerActor::UpdateAKSKEvent, respCopy);
        return true;
    };
    (void)member_->metaStorageAccessor
        ->RegisterObserver(GenAKSKKeyWatchPrefix(member_->clusterID, true), watchOpt, handler, newSyncer)
        .After(WATCH_TIMEOUT_MS, after);
    (void)member_->metaStorageAccessor
        ->RegisterObserver(GenAKSKKeyWatchPrefix(member_->clusterID, false), watchOpt, handler, oldSyncer)
        .After(WATCH_TIMEOUT_MS, after);
    return Status::OK();
}

void AKSKManagerActor::Finalize()
{
    litebus::TimerTools::Cancel(member_->checkAKSKExpiredInAdvanceTimer);
    litebus::TimerTools::Cancel(member_->checkAKSKExpiredInTimeTimer);
}

litebus::Future<std::shared_ptr<AKSKContent>> AKSKManagerActor::RequireAKSKByTenantID(const std::string &tenantID,
                                                                                      const bool isPermanentValid)
{
    if (!member_->initialized) {
        auto akskContent = std::make_shared<AKSKContent>();
        akskContent->status = Status(StatusCode::IAM_WAIT_INITIALIZE_COMPLETE, "iam-server is initializing");
        return akskContent;
    }

    // 1. find local new AKSK in cache
    auto localAKSK = FindAKSKByTenantIDFromCache(tenantID, true);
    if (localAKSK->IsValid(NEW_CREDENTIAL_EXPIRED_OFFSET).IsOk()) {
        return localAKSK;
    }
    // 2. try to generate new AKSK
    ASSERT_IF_NULL(business_);
    return business_->RequireAKSKByTenantID(tenantID, isPermanentValid);
}

litebus::Future<std::shared_ptr<AKSKContent>> AKSKManagerActor::RequireAKSKByAK(const std::string &accessKey)
{
    if (!member_->initialized) {
        auto akskContent = std::make_shared<AKSKContent>();
        akskContent->status = Status(StatusCode::IAM_WAIT_INITIALIZE_COMPLETE, "iam-server is initializing");
        return akskContent;
    }
    ASSERT_IF_NULL(business_);
    return business_->RequireAKSKByAK(accessKey);
}

litebus::Future<Status> AKSKManagerActor::AbandonAKSKByTenantID(const std::string &tenantID)
{
    if (!member_->initialized) {
        return Status(StatusCode::IAM_WAIT_INITIALIZE_COMPLETE, "iam-server is initializing");
    }
    ASSERT_IF_NULL(business_);
    return business_->AbandonAKSKByTenantID(tenantID);
}

litebus::Future<SyncResult> AKSKManagerActor::IAMAKSKSyncer(const std::shared_ptr<GetResponse> &getResponse, bool isNew)
{
    if (getResponse == nullptr) {
        YRLOG_WARN("getResponse is null");
        return SyncResult{ Status(StatusCode::FAILED, "getResponse is null") };
    }

    auto prefix = GenAKSKKeyWatchPrefix(member_->clusterID, isNew);
    if (getResponse->status.IsError()) {
        YRLOG_INFO("failed to get key({}) from meta storage", prefix);
        return SyncResult{ getResponse->status };
    }
    if (getResponse->kvs.empty()) {
        YRLOG_INFO("get no result with key({}) from meta storage, revision is {}", prefix,
                   getResponse->header.revision);
        return SyncResult{ Status::OK() };
    }
    std::vector<WatchEvent> watchEvents;
    std::set<std::string> etcdSet;
    for (auto &kv : getResponse->kvs) {
        auto eventKey = TrimKeyPrefix(kv.key(), member_->metaStoreClient->GetTablePrefix());
        auto tenantID = GetAKSKTenantID(eventKey);
        if (tenantID.empty()) {
            YRLOG_ERROR("invalid aksk key: {}", eventKey);
            continue;
        }
        watchEvents.emplace_back(WatchEvent{ .eventType = EVENT_TYPE_PUT, .kv = kv, .prevKv = {} });
        etcdSet.emplace(tenantID);
    }
    UpdateAKSKEvent(watchEvents);
    if (isNew) {
        // delete new aksk from cache which not in metastore
        for (auto it = member_->newAKSKMap.begin(); it != member_->newAKSKMap.end();) {
            if (etcdSet.find(it->first) == etcdSet.end()) {
                YRLOG_INFO("{} is not in etcd, but in newAKSKMap cache, need to delete", it->first);
                auto needDelAKSKContent = member_->newAKSKMap[it->first];
                (void)member_->akMap.erase(needDelAKSKContent->accessKey);
                it = member_->newAKSKMap.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        // delete old aksk from cache which not in metastore
        for (auto it = member_->oldAKSKMap.begin(); it != member_->oldAKSKMap.end();) {
            if (etcdSet.find(it->first) == etcdSet.end()) {
                YRLOG_INFO("{} is not in etcd, but in oldAKSKMap cache, need to delete", it->first);
                auto needDelAKSKContent = member_->oldAKSKMap[it->first];
                (void)member_->akMap.erase(needDelAKSKContent->accessKey);
                it = member_->oldAKSKMap.erase(it);
            } else {
                ++it;
            }
        }
    }
    return SyncResult{ Status::OK() };
}

void AKSKManagerActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    litebus::AID masterAID(masterName_, leaderInfo.address);
    member_->masterAID = masterAID;
    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("new status({}) business don't exist", newStatus);
        return;
    }
    business_ = businesses_[newStatus];
    business_->OnChange();
    curStatus_ = newStatus;
}

void AKSKManagerActor::AsyncInitialize(const std::shared_ptr<GetResponse> &newResponse,
                                       const std::shared_ptr<GetResponse> &oldResponse)
{
    SyncAKSKFromMetaStore(newResponse, true);
    SyncAKSKFromMetaStore(oldResponse, false);
    CheckAKSKExpiredInAdvance();
    CheckAKSKExpiredInTime();
    member_->initialized = true;
    member_->isReady.SetValue(true);
}

void AKSKManagerActor::SyncAKSKFromMetaStore(const std::shared_ptr<GetResponse> &response, bool isNew)
{
    if (response == nullptr) {
        YRLOG_WARN("get response is null");
        return;
    }
    if (response->status.IsError()) {
        YRLOG_ERROR("failed to sync aksk, err: {}", response->status.ToString());
        return;
    }
    if (response->kvs.empty()) {
        // normal case, not log.
        return;
    }
    for (const auto &kv : response->kvs) {
        PutAKSKFromEventValueToCache(kv.value(), isNew);
    }
}

std::shared_ptr<AKSKContent> AKSKManagerActor::GetAKSKContentFromEventValue(const std::string &eventValue)
{
    auto encAKSKContent = TransToEncAKSKContentFromJson(eventValue);
    auto akskContent = std::make_shared<AKSKContent>();
    if (encAKSKContent->status.IsError()) {
        YRLOG_ERROR("get encrypt akskContent from json failed, err: {}", encAKSKContent->status.ToString());
        akskContent->status = encAKSKContent->status;
        return akskContent;
    }
    akskContent = DecryptAKSKContentFromStorage(encAKSKContent);
    if (akskContent->status.IsError()) {
        YRLOG_ERROR("decrypt akskContent failed.");
        return akskContent;
    }
    return akskContent;
}

void AKSKManagerActor::PutAKSKFromEventValueToCache(const std::string &eventValue, const bool isNew)
{
    auto akskContent = GetAKSKContentFromEventValue(eventValue);
    // no need to check expire time when sync from etcd
    if (akskContent->status.IsError()) {
        return;
    }
    if (isNew) {
        UpdateNewAKSKCompareTime(akskContent);
    } else {
        UpdateOldAKSKCompareTime(akskContent);
    }
    UpdateAKMapCompareTime(akskContent);
}

void AKSKManagerActor::DelAKSKFromCache(const std::string &tenantID, const bool isNew)
{
    if (isNew) {
        if (member_->newAKSKMap.find(tenantID) != member_->newAKSKMap.end()) {
            auto needDelNewAKSKContent = member_->newAKSKMap[tenantID];
            member_->akMap.erase(needDelNewAKSKContent->accessKey);
        }
        member_->newAKSKMap.erase(tenantID);
    } else {
        if (member_->oldAKSKMap.find(tenantID) != member_->oldAKSKMap.end()) {
            auto needDelOldAKSKContent = member_->oldAKSKMap[tenantID];
            member_->akMap.erase(needDelOldAKSKContent->accessKey);
        }
        member_->oldAKSKMap.erase(tenantID);
    }
}

void AKSKManagerActor::UpdateAKSKEvent(const std::vector<WatchEvent> &events)
{
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), member_->metaStoreClient->GetTablePrefix());
        auto tenantID = GetAKSKTenantID(eventKey);
        bool isNew = litebus::strings::StartsWithPrefix(eventKey, INTERNAL_IAM_AKSK_PREFIX + NEW_PREFIX);
        if (tenantID.empty()) {
            YRLOG_ERROR("invalid aksk key: {}", eventKey);
            continue;
        }
        YRLOG_INFO("receive aksk event, tenantID({}), isNew:{}, type:{}", tenantID, isNew,
                   fmt::underlying(event.eventType));
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                PutAKSKFromEventValueToCache(event.kv.value(), isNew);
                break;
            }
            case EVENT_TYPE_DELETE: {
                DelAKSKFromCache(tenantID, isNew);
                break;
            }
            default: {
                YRLOG_WARN("unknown event type {}", fmt::underlying(event.eventType));
            }
        }
    }
}

void AKSKManagerActor::UpdateNewAKSKCompareTime(const std::shared_ptr<AKSKContent> &akskContent)
{
    if (member_->newAKSKMap.find(akskContent->tenantID) == member_->newAKSKMap.end()) {
        YRLOG_INFO("{}|aksk expire timestamp({})", akskContent->tenantID, akskContent->expiredTimeStamp);
        member_->newAKSKMap[akskContent->tenantID] = akskContent;
        return;
    }
    auto cacheAKSKContent = member_->newAKSKMap[akskContent->tenantID];
    if (akskContent->expiredTimeStamp != 0 && akskContent->expiredTimeStamp < cacheAKSKContent->expiredTimeStamp) {
        YRLOG_ERROR("{}|cache aksk expire timestamp({}) is newer than ({}), skip update new aksk",
                    akskContent->tenantID, cacheAKSKContent->expiredTimeStamp, akskContent->expiredTimeStamp);
        return;
    }
    member_->newAKSKMap[akskContent->tenantID] = akskContent;
    YRLOG_INFO("{}|update new cache aksk, expire timestamp({})", akskContent->tenantID, akskContent->expiredTimeStamp);
}

void AKSKManagerActor::UpdateOldAKSKCompareTime(const std::shared_ptr<AKSKContent> &akskContent)
{
    if (member_->oldAKSKMap.find(akskContent->tenantID) == member_->oldAKSKMap.end()) {
        YRLOG_INFO("{}|aksk expire timestamp({})", akskContent->tenantID, akskContent->expiredTimeStamp);
        member_->oldAKSKMap[akskContent->tenantID] = akskContent;
        return;
    }
    auto cacheAKSKContent = member_->oldAKSKMap[akskContent->tenantID];
    if (akskContent->expiredTimeStamp < cacheAKSKContent->expiredTimeStamp) {
        YRLOG_ERROR("{}|cache aksk expire timestamp({}) is newer than ({}), skip update old aksk",
                    akskContent->tenantID, cacheAKSKContent->expiredTimeStamp, akskContent->expiredTimeStamp);
        return;
    }
    member_->oldAKSKMap[akskContent->tenantID] = akskContent;
    YRLOG_INFO("{}|update old cache aksk, expire timestamp({})", akskContent->tenantID, akskContent->expiredTimeStamp);
}

void AKSKManagerActor::UpdateAKMapCompareTime(const std::shared_ptr<AKSKContent> &akskContent)
{
    if (member_->akMap.find(akskContent->accessKey) == member_->akMap.end()) {
        YRLOG_INFO("{}|(akMap) aksk expire timestamp({})", akskContent->accessKey, akskContent->expiredTimeStamp);
        member_->akMap[akskContent->accessKey] = akskContent;
        return;
    }
    auto cacheAKSKContent = member_->akMap[akskContent->accessKey];
    if (akskContent->expiredTimeStamp < cacheAKSKContent->expiredTimeStamp) {
        YRLOG_ERROR("{}|(akMap) cache aksk expire timestamp({}) is newer than ({}), skip update aksk",
                    akskContent->accessKey, cacheAKSKContent->expiredTimeStamp, akskContent->expiredTimeStamp);
        return;
    }
    member_->akMap[akskContent->accessKey] = akskContent;
    YRLOG_INFO("{}|(akMap) update cache aksk expire timestamp({})", akskContent->accessKey,
               akskContent->expiredTimeStamp);
}

std::shared_ptr<AKSKContent> AKSKManagerActor::FindAKSKByTenantIDFromCache(const std::string &tenantID,
                                                                                 const bool isNew)
{
    if (isNew) {
        if (member_->newAKSKMap.find(tenantID) != member_->newAKSKMap.end()) {
            return member_->newAKSKMap[tenantID];
        }
    } else {
        if (member_->oldAKSKMap.find(tenantID) != member_->oldAKSKMap.end()) {
            return member_->oldAKSKMap[tenantID];
        }
    }
    auto akskContent = std::make_shared<AKSKContent>();
    akskContent->status = Status(StatusCode::PARAMETER_ERROR, "local cache not found");
    return akskContent;
}

std::shared_ptr<AKSKContent> AKSKManagerActor::FindAKSKByAKFromCache(const std::string &accessKey)
{
    if (member_->akMap.find(accessKey) != member_->akMap.end()) {
        return member_->akMap[accessKey];
    } else {
        auto akskContent = std::make_shared<AKSKContent>();
        akskContent->status = Status(StatusCode::FAILED, "akskContent not found in cache");
        return akskContent;
    }
}

void AKSKManagerActor::CheckAKSKExpiredInAdvance()
{
    std::unordered_set<std::string> needUpdateSet;
    for (const auto &akskIter : member_->newAKSKMap) {
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (akskIter.second->expiredTimeStamp != 0 &&
            akskIter.second->expiredTimeStamp < (now + member_->aheadUpdateExpireAKSKTime)) {
            needUpdateSet.insert(akskIter.first);
        }
    }
    for (const auto &val : needUpdateSet) {
        ASSERT_IF_NULL(business_);
        (void)business_->UpdateAKSKInAdvance(val);
    }
    member_->checkAKSKExpiredInAdvanceTimer = litebus::AsyncAfter(member_->checkAKSKExpireInterval, GetAID(),
                                                                  &AKSKManagerActor::CheckAKSKExpiredInAdvance);
}

void AKSKManagerActor::CheckAKSKExpiredInTime()
{
    std::unordered_set<std::string> needDeleteSet;
    for (const auto &akskIter : member_->oldAKSKMap) {
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (akskIter.second->expiredTimeStamp != 0 && akskIter.second->expiredTimeStamp < now) {
            needDeleteSet.insert(akskIter.first);
        }
    }
    for (const auto &val : needDeleteSet) {
        ASSERT_IF_NULL(business_);
        (void)business_->UpdateAKSKInTime(val);
    }
    member_->checkAKSKExpiredInTimeTimer =
        litebus::AsyncAfter(member_->checkAKSKExpireInterval, GetAID(), &AKSKManagerActor::CheckAKSKExpiredInTime);
}

litebus::Future<Status> AKSKManagerActor::SyncToReplaceAKSK(bool isNew)
{
    if (!member_->initialized) {
        return Status::OK();
    }
    return AsyncGetAKSKFromMetaStore(isNew)
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::ReplaceAKSKMap, std::placeholders::_1, isNew));
}

litebus::Future<AKSKMap> AKSKManagerActor::AsyncGetAKSKFromMetaStore(bool isNew)
{
    ASSERT_IF_NULL(member_->metaStoreClient);
    auto prefix = GenAKSKKeyWatchPrefix(member_->clusterID, isNew);
    std::function<AKSKMap(const std::shared_ptr<GetResponse> &)> then = [](const std::shared_ptr<GetResponse> &response)
        ->AKSKMap {
        AKSKMap output;
        if (response->status.IsError()) {
            YRLOG_ERROR("fail to sync aksk, err: {}", response->status.ToString());
            return output;
        }
        if (response->kvs.empty()) {
            // normal case, not log.
            return output;
        }
        for (const auto &kv : response->kvs) {
            auto akskContent = GetAKSKContentFromEventValue(kv.value());
            if (akskContent->status.IsError()) {
                continue;
            }
            (void)output.emplace(akskContent->tenantID, akskContent);
        }
        return output;
    };
    return member_->metaStoreClient->Get(prefix, { .prefix = true }).Then(then);
}

Status AKSKManagerActor::ReplaceAKSKMap(const AKSKMap &akskMap, const bool isNew)
{
    if (akskMap.empty()) {
        return Status::OK();
    }
    for (const auto &item : akskMap) {
        if (item.second == nullptr) {
            continue;
        }
        if (isNew) {
            UpdateNewAKSKCompareTime(item.second);
        } else {
            UpdateOldAKSKCompareTime(item.second);
        }
        UpdateAKMapCompareTime(item.second);
    }
    return Status::OK();
}

void AKSKManagerActor::OnGenerateCredentialError(const std::string &tenantID, const litebus::Future<Status> &status,
                                                 const std::shared_ptr<AKSKContent> &credential)
{
    if (status.IsOK()) {
        return;  // not handle in error branch
    }

    if (member_->genCredPendingQueue_.find(tenantID) == member_->genCredPendingQueue_.end()) {
        YRLOG_ERROR("{}|can not find pending promise.", tenantID);
        return;
    }

    credential->status = Status(StatusCode::FAILED, &"Generate credential error: "[status.GetErrorCode()]);
    member_->genCredPendingQueue_[tenantID]->SetValue(credential);
    member_->genCredPendingQueue_.erase(tenantID);
}

litebus::Future<Status> AKSKManagerActor::OnGenerateCredential(const std::string &tenantID, const Status &status,
                                                               const std::shared_ptr<AKSKContent> &credential)
{
    if (member_->genCredPendingQueue_.find(tenantID) == member_->genCredPendingQueue_.end()) {
        YRLOG_ERROR("{}|can not find pending promise.", tenantID);
        return status;
    }

    auto promise = member_->genCredPendingQueue_[tenantID];
    if (status.IsError()) {
        credential->status = status;

        member_->genCredPendingQueue_[tenantID]->SetValue(credential);
        member_->genCredPendingQueue_.erase(tenantID);
        return status;
    }

    auto valid = credential->IsValid(NEW_CREDENTIAL_EXPIRED_OFFSET);
    if (valid.IsError()) {
        credential->status = valid;

        member_->genCredPendingQueue_[tenantID]->SetValue(credential);
        member_->genCredPendingQueue_.erase(tenantID);
        return valid;
    }

    if (credential->expiredTimeStamp == 0) {
        member_->akMap[credential->accessKey] = credential;
        member_->newAKSKMap[credential->tenantID] = credential;
        YRLOG_INFO("{}|put credential: {} into credential map", credential->tenantID,
                   credential->secretKey.GetMaskData());

        member_->genCredPendingQueue_[tenantID]->SetValue(credential);
        member_->genCredPendingQueue_.erase(tenantID);

        return Status::OK();
    }

    YRLOG_DEBUG("{}|success to generate new aksk, start to put it to metastore", tenantID);
    return PutAKSKToMetaStore(credential, true)
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnPutAKSKToMetaStore, std::placeholders::_1, credential, true,
                             true));
}

litebus::Future<std::shared_ptr<AKSKContent>> AKSKManagerActor::GenerateNewAKSK(const std::string &tenantID,
                                                                                const bool isPermanentValid)
{
    if (member_->genCredPendingQueue_.find(tenantID) != member_->genCredPendingQueue_.end()) {
        return member_->genCredPendingQueue_[tenantID]->GetFuture();
    }

    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<AKSKContent>>>();
    auto credential = std::make_shared<AKSKContent>();
    YRLOG_INFO("{}|start to generate new aksk", tenantID);
    member_->genCredPendingQueue_[tenantID] = promise;
    (void)GenerateAKSK(tenantID, credential, isPermanentValid)
        .Then(std::function<litebus::Future<Status>(const Status &)>(
            [=](const Status &status) -> litebus::Future<Status> {
                return litebus::Async(GetAID(), &AKSKManagerActor::OnGenerateCredential, tenantID, status, credential);
            }))
        .OnComplete([=](const litebus::Future<Status> &status) {
            (void)litebus::Async(GetAID(), &AKSKManagerActor::OnGenerateCredentialError, tenantID, status, credential);
        });
    // use promise future, avoid contagion
    return promise->GetFuture();
}

litebus::Future<litebus::http::Response> AKSKManagerActor::SendRequest(const litebus::http::Request &request)
{
    return litebus::http::LaunchRequest(request);
}

litebus::Future<Status> AKSKManagerActor::GetCredentialFromURL(const std::string &tenantID,
                                                               const std::shared_ptr<AKSKContent> &credential)
{
    std::string path = "/iam/v1/tenants/" + tenantID;
    auto tURL = litebus::http::URL::Decode(member_->credentialHostAddress_ + path);
    if (!tURL.IsOK()) {
        YRLOG_ERROR("{}|Get credential fail, illegal URL: {}", tenantID, tURL.GetErrorCode());
        return Status(FAILED, &"Get credential fail, illegal URL: "[tURL.GetErrorCode()]);
    }

    std::map<std::string, std::string> headers;
    headers["x-aes-alg"] = "AES-GCM2";

    litebus::http::Request request(METHOD_GET, false, tURL.Get());
    for (const auto &kv : headers) {
        request.headers[kv.first] = kv.second;
    }
    auto signHeaders =
        SignHttpRequestX(SignRequest(request.method, request.url.path, nullptr, headers, ""), member_->authKey_);
    for (const auto &kv : signHeaders) {
        request.headers[kv.first] = kv.second;
    }

    YRLOG_INFO("{}|Get credential from {}", tenantID, member_->credentialHostAddress_);
    return SendRequest(request)
        .Then(std::function<Status(const litebus::http::Response &)>(
            [=](const litebus::http::Response &response) -> Status {
                if (response.retCode != litebus::http::ResponseCode::OK) {
                    YRLOG_ERROR("{}|Get credential error: {}", tenantID,
                                litebus::http::Response::GetStatusDescribe(response.retCode));
                    return Status(FAILED, "Get credential error: "
                                              + litebus::http::Response::GetStatusDescribe(response.retCode));
                }

                nlohmann::json bodyJson;
                try {
                    bodyJson = nlohmann::json::parse(response.body);
                } catch (nlohmann::json::exception &) {
                    YRLOG_ERROR("{}|Get credential error: illegal JSON", tenantID);
                    return Status(FAILED, "Get credential error: illegal JSON");
                }

                auto resultJson = bodyJson.find("result");
                if (resultJson == bodyJson.end()) {
                    YRLOG_ERROR("{}|Get credential error: result not exist", tenantID);
                    return Status(FAILED, "Get credential error: result not exist");
                }

                auto credentialJson = resultJson.value().find("credential");
                if (credentialJson == resultJson.value().end()) {
                    YRLOG_ERROR("{}|Get credential error: credential not exist", tenantID);
                    return Status(FAILED, "Get credential error: credential not exist");
                }

                SensitiveValue dataKey = GetComponentDataKey();
                if (dataKey.Empty()) {
                    YRLOG_ERROR("{}|Get credential error: not found component dataKey.", tenantID);
                    return Status(FAILED, "Get credential error: not found component dataKey.");
                }

                SensitiveValue plain;
                std::string cipher = credentialJson.value();
                if (auto status(DecryptByDataKey(dataKey, plain, cipher)); status.IsError()) {
                    YRLOG_ERROR("{}|Get credential error: illegal cipher, {}", status.ToString(), tenantID);
                    return Status(FAILED, "Get credential error: illegal cipher, " + status.ToString());
                }

                auto content = TransToAKSKContentFromJsonNew(plain.GetData());
                content->expiredTimeStamp = 0;  // must, used by IsValid
                content->tenantID = tenantID;
                if (!content->IsValid()) {
                    YRLOG_ERROR("{}|Get credential error: illegal credential, {}", tenantID,
                                content->status.GetMessage());
                    return Status(FAILED, "Get credential error: illegal credential, " + content->status.GetMessage());
                }
                credential->accessKey = content->accessKey;
                credential->secretKey = content->secretKey;
                credential->dataKey = content->dataKey;
                credential->expiredTimeStamp = 0;
                return Status::OK();
            }))
        .OnComplete([&](const litebus::Future<Status> &status) {
            if (status.IsError()) {
                YRLOG_ERROR("{}|Get credential error: illegal future: {}", tenantID, status.GetErrorCode());
            }
        });
}

litebus::Future<Status> AKSKManagerActor::GenerateAKSK(const std::string &tenantID,
                                                       const std::shared_ptr<AKSKContent> &akskContent,
                                                       const bool isPermanentValid)
{
    akskContent->tenantID = tenantID;
    auto now = static_cast<uint64_t>(std::time(nullptr));
    if (isPermanentValid) {
        akskContent->expiredTimeStamp = 0;
        akskContent->role = SYSTEM_ROLE;
    } else {
        akskContent->expiredTimeStamp =
            (UINT64_MAX - now < member_->akskExpiredTimeSpan) ? now : now + member_->akskExpiredTimeSpan;
        akskContent->role = NORMAL_ROLE;
    }

    if (!member_->credentialHostAddress_.empty()) {
        return GetCredentialFromURL(tenantID, akskContent);
    } else {
        // generate secure random bytes for key
        auto ak = litebus::Option<SensitiveValue>();
        // ak is not SensitiveValue
        akskContent->accessKey = CharStringToHexString(std::string(ak.Get().GetData(), HMAC256_KEYLEN));
        auto sk = litebus::Option<SensitiveValue>();
        akskContent->secretKey = sk.Get();
        auto dk = litebus::Option<SensitiveValue>();
        akskContent->dataKey = dk.Get();
        return Status::OK();
    }
}

litebus::Future<std::shared_ptr<PutResponse>> AKSKManagerActor::PutAKSKToMetaStore(
    const std::shared_ptr<AKSKContent> &akskContent, const bool isNew)
{
    auto encryptAKSKContent = EncryptAKSKContentForStorage(akskContent);
    if (encryptAKSKContent->status.IsError()) {
        std::shared_ptr<PutResponse> putResponse = std::make_shared<PutResponse>();
        putResponse->status = Status(StatusCode::PARAMETER_ERROR, "encrypt akskContent for storage failed");
        return putResponse;
    }
    auto encryptAKSKJson = TransToJsonFromEncAKSKContent(encryptAKSKContent);
    ASSERT_IF_NULL(member_->metaStoreClient);
    return member_->metaStoreClient->Put(GenAKSKKey(member_->clusterID, akskContent->tenantID, isNew), encryptAKSKJson,
                                         {});
}

Status AKSKManagerActor::OnPutAKSKToMetaStore(const std::shared_ptr<PutResponse> &response,
                                              const std::shared_ptr<AKSKContent> &akskContent, const bool isNew,
                                              const bool isSetPromise)
{
    if (response == nullptr) {
        YRLOG_WARN("{}|put rsp is null", akskContent->tenantID);
    } else if (response->status.IsError()) {
        YRLOG_WARN("{}|failed to put aksk, isNew:{} err: {}", akskContent->tenantID, isNew,
                   response->status.ToString());
    } else {
        YRLOG_INFO("{}|success to put aksk to meta-store, isNew:{}", akskContent->tenantID, isNew);
        if (isNew) {
            UpdateNewAKSKCompareTime(akskContent);
        } else {
            UpdateOldAKSKCompareTime(akskContent);
        }
        UpdateAKMapCompareTime(akskContent);
    }
    // for generate new aksk
    if (isSetPromise && member_->genCredPendingQueue_.find(akskContent->tenantID)
                        != member_->genCredPendingQueue_.end()) {
        auto promise = member_->genCredPendingQueue_[akskContent->tenantID];
        if (response == nullptr || response->status.IsError()) {
            akskContent->status = Status(StatusCode::FAILED, "persist and cache aksk failed");
        }
        promise->SetValue(akskContent);
        member_->genCredPendingQueue_.erase(akskContent->tenantID);
    }
    return response->status;
}

litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> AKSKManagerActor::SendForwardGetAKSKByTenantID(
    const std::string &tenantID, const bool needCreate, const bool isPermanentValid)
{
    if (member_->masterAID.Name().empty()) {
        YRLOG_ERROR("{}|master is none, failed to forward", tenantID);
        auto akskResp = std::make_shared<messages::GetAKSKResponse>();
        // inner error, need to retry
        akskResp->set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
        akskResp->set_message("master is none, failed to forward");
        return akskResp;
    }
    auto akskRequest = std::make_shared<messages::GetAKSKByTenantIDRequest>();
    akskRequest->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    akskRequest->set_tenantid(tenantID);
    akskRequest->set_iscreate(needCreate);
    akskRequest->set_ispermanentvalid(isPermanentValid);
    YRLOG_INFO("{}|{} forward get aksk by tenantID to({})", akskRequest->requestid(), akskRequest->tenantid(),
               member_->masterAID.HashString());
    auto future = forwardGetAKSKSync_.AddSynchronizer(akskRequest->requestid());
    Send(member_->masterAID, "ForwardGetAKSKByTenantID", akskRequest->SerializeAsString());
    return future;
}

void AKSKManagerActor::OnForwardGetNewAKSKByTenantID(
    const litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> &future,
    const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise, const std::string &tenantID)
{
    auto akskContent = std::make_shared<AKSKContent>();
    if (future.IsError()) {
        YRLOG_ERROR("{}|failed to forward get aksk by tenantID, err:{}", tenantID, future.GetErrorCode());
        akskContent->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to forward get aksk by tenantID");
        promise->SetValue(akskContent);
        return;
    }
    const auto &akskResp = future.Get();
    if (akskResp == nullptr) {
        YRLOG_ERROR("{}|aksk rsp is null", tenantID);
        akskContent->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "on forward get new aksk is null");
        promise->SetValue(akskContent);
        return;
    }
    if (akskResp->code() != 0) {
        YRLOG_ERROR("{}|error respond on forward get new aksk, err:{}", tenantID, future.GetErrorCode());
        akskContent->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR,
                                     "error respond on forward get new aksk, err: " + akskResp->message());
        promise->SetValue(akskContent);
        return;
    }
    PutGetAKSKResponseToCache(akskResp);
    auto localNewAKSKContent = FindAKSKByTenantIDFromCache(tenantID, true);
    if (localNewAKSKContent->IsValid(NEW_CREDENTIAL_EXPIRED_OFFSET).IsError()) {
        YRLOG_ERROR("{}|forward get new aksk is invalid, err: {}", tenantID, localNewAKSKContent->status.ToString());
        akskContent->status = Status(StatusCode::PARAMETER_ERROR,
                                     "forward new aksk invalid, err: " + localNewAKSKContent->status.ToString());
        promise->SetValue(akskContent);
        return;
    }
    promise->SetValue(localNewAKSKContent);
}

void AKSKManagerActor::OnForwardGetCacheAKSKByTenantID(
    const litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> &future,
    const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    if (future.IsError()) {
        promise->SetValue(Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to forward get aksk by tenantID"));
        return;
    }
    const auto &akskResp = future.Get();
    if (akskResp == nullptr) {
        YRLOG_ERROR("aksk rsp is null");
        promise->SetValue(Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "on forward get new aksk is null"));
        return;
    }
    if (akskResp->code() != 0) {
        promise->SetValue(Status(StatusCode::ERR_INNER_SYSTEM_ERROR,
                                 "failed to forward get aksk by tenantID, err is " + akskResp->message()));
        return;
    }
    PutGetAKSKResponseToCache(akskResp);
    promise->SetValue(Status::OK());
}

litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> AKSKManagerActor::SendForwardGetAKSKByAK(
    const std::string &accessKey)
{
    if (member_->masterAID.Name().empty()) {
        YRLOG_ERROR("ak({})|master is none, failed to forward", accessKey);
        auto akskResp = std::make_shared<messages::GetAKSKResponse>();
        // inner error, need to retry
        akskResp->set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
        akskResp->set_message("master is none, failed to forward");
        return akskResp;
    }
    auto akskRequest = std::make_shared<messages::GetAKSKByAKRequest>();
    akskRequest->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    akskRequest->set_accesskey(accessKey);
    YRLOG_INFO("{}|{} forward get aksk by ak to({})", akskRequest->requestid(), akskRequest->accesskey(),
               member_->masterAID.HashString());
    auto future = forwardGetAKSKSync_.AddSynchronizer(akskRequest->requestid());
    Send(member_->masterAID, "ForwardGetAKSKByAK", akskRequest->SerializeAsString());
    return future;
}

void AKSKManagerActor::ForwardGetAKSKByAK(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto akskRequest = std::make_shared<messages::GetAKSKByAKRequest>();
    if (!akskRequest->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse get aksk by ak request");
        return;
    }
    YRLOG_INFO("{}|{}|receive get aksk by ak request", akskRequest->requestid(), akskRequest->accesskey());
    ASSERT_IF_NULL(business_);
    (void)business_->HandleForwardGetAKSKByAK(from, akskRequest);
}

void AKSKManagerActor::OnForwardGetCacheAKSKByAK(
    const litebus::Future<std::shared_ptr<messages::GetAKSKResponse>> &future,
    const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise, const std::string &accessKey)
{
    auto akskContent = std::make_shared<AKSKContent>();
    if (future.IsError()) {
        YRLOG_ERROR("{}|failed to forward get aksk by ak, err:{}", accessKey, future.GetErrorCode());
        akskContent->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to forward get aksk by ak");
        promise->SetValue(akskContent);
        return;
    }
    const auto &akskResp = future.Get();
    if (akskResp == nullptr) {
        YRLOG_ERROR("{}|aksk rsp is null", accessKey);
        akskContent->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "on forward get cache aksk is null");
        promise->SetValue(akskContent);
        return;
    }
    if (akskResp->code() != 0) {
        akskContent->status =
            Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to forward get aksk, err is " + akskResp->message());
        promise->SetValue(akskContent);
        return;
    }
    PutGetAKSKResponseToCache(akskResp);
    promise->SetValue(FindAKSKByAKFromCache(accessKey));
}

void AKSKManagerActor::PutGetAKSKResponseToCache(
    const std::shared_ptr<messages::GetAKSKResponse> &akskResponse)
{
    // get and cache new aksk
    auto newAKSKContent = std::make_shared<AKSKContent>();
    GetAKSKContentFromGetAKSKResponse(akskResponse, newAKSKContent, true);
    if (newAKSKContent->status.IsError()) {
        YRLOG_ERROR("failed to get new akskContent from response, err: {}", newAKSKContent->status.ToString());
        return;
    }
    UpdateNewAKSKCompareTime(newAKSKContent);
    UpdateAKMapCompareTime(newAKSKContent);
    // get and cache old aksk if exists, for update in advance that will have new and old aksk in response
    auto oldAKSKContent = std::make_shared<AKSKContent>();
    GetAKSKContentFromGetAKSKResponse(akskResponse, oldAKSKContent, false);
    if (oldAKSKContent->status.IsOk()) {
        UpdateOldAKSKCompareTime(oldAKSKContent);
        UpdateAKMapCompareTime(oldAKSKContent);
    }
}

void AKSKManagerActor::GetAKSKContentFromGetAKSKResponse(const std::shared_ptr<messages::GetAKSKResponse> &response,
                                                         std::shared_ptr<AKSKContent> &akskContent,
                                                         const bool isNew)
{
    if (response->code() != 0) {
        akskContent->status = Status(StatusCode::PARAMETER_ERROR,
                                     "error response from get aksk response, err: " + response->message());
        return;
    }
    auto encAKSKContent = std::make_shared<EncAKSKContent>();
    encAKSKContent->tenantID = response->tenantid();
    encAKSKContent->role = response->role();
    if (isNew) {
        encAKSKContent->accessKey = response->newaccesskey();
        encAKSKContent->secretKey = response->newsecretkey();
        encAKSKContent->dataKey = response->newdatakey();
        try {
            encAKSKContent->expiredTimeStamp = std::stoull(response->newexpiredtimestamp());
        } catch (std::exception &e) {
            YRLOG_ERROR("failed to parse new expiredTimeStamp from get aksk response, error: {}", e.what());
            akskContent->status = Status(StatusCode::PARAMETER_ERROR, "parse new expiredTimeStamp failed.");
            return;
        }
    } else {
        encAKSKContent->accessKey = response->oldaccesskey();
        encAKSKContent->secretKey = response->oldsecretkey();
        encAKSKContent->dataKey = response->olddatakey();
        try {
            encAKSKContent->expiredTimeStamp = std::stoull(response->oldexpiredtimestamp());
        } catch (std::exception &e) {
            YRLOG_ERROR("failed to parse old expiredTimeStamp from get aksk response, error: {}", e.what());
            akskContent->status = Status(StatusCode::PARAMETER_ERROR, "parse old expiredTimeStamp failed.");
            return;
        }
    }
    akskContent = DecryptAKSKContentFromStorage(encAKSKContent);
}

Status AKSKManagerActor::OnRequireAKSKByTenantID(const std::shared_ptr<AKSKContent> &akskContent,
                                                 const litebus::AID &from,
                                                 const std::shared_ptr<messages::GetAKSKByTenantIDRequest> &akskRequest)
{
    auto akskResponse = std::make_shared<messages::GetAKSKResponse>();
    akskResponse->set_requestid(akskRequest->requestid());
    SetGetAKSKResponseFromAKSKContent(akskResponse, akskContent, true);
    (void)Send(from, "ForwardGetAKSKResponse", akskResponse->SerializeAsString());
    return Status::OK();
}

void AKSKManagerActor::SetGetAKSKResponseFromAKSKContent(std::shared_ptr<messages::GetAKSKResponse> &response,
                                                         const std::shared_ptr<AKSKContent> &akskContent,
                                                         const bool isNew)
{
    if (akskContent->status.IsError()) {
        YRLOG_ERROR("{}|aksk is invalid when set get aksk response", akskContent->tenantID);
        response->set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
        response->set_message("aksk is invalid, err: " + akskContent->status.ToString());
        return;
    }
    auto encAKSKContent = EncryptAKSKContentForStorage(akskContent);
    if (encAKSKContent->status.IsError()) {
        YRLOG_ERROR("{}|failed to encrypt aksk when set get aksk response", akskContent->tenantID);
        response->set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
        response->set_message("failed to encrypt aksk");
        return;
    }

    response->set_tenantid(encAKSKContent->tenantID);
    response->set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response->set_role(encAKSKContent->role);
    if (isNew) {
        response->set_newaccesskey(encAKSKContent->accessKey);
        response->set_newsecretkey(encAKSKContent->secretKey);
        response->set_newdatakey(encAKSKContent->dataKey);
        response->set_newexpiredtimestamp(std::to_string(encAKSKContent->expiredTimeStamp));
        return;
    }
    response->set_oldaccesskey(encAKSKContent->accessKey);
    response->set_oldsecretkey(encAKSKContent->secretKey);
    response->set_olddatakey(encAKSKContent->dataKey);
    response->set_oldexpiredtimestamp(std::to_string(encAKSKContent->expiredTimeStamp));
}

litebus::Future<Status> AKSKManagerActor::HandleAbandonAKSKByTenantID(const std::string &tenantID)
{
    YRLOG_INFO("{}|start to delete old aksk", tenantID);
    return DelAKSKFromMetaStore(tenantID, false)
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnDelAKSKFromMetaStore, std::placeholders::_1, tenantID,
                             false))
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::DelNewAKSK, std::placeholders::_1, tenantID));
}

litebus::Future<std::shared_ptr<DeleteResponse>> AKSKManagerActor::DelAKSKFromMetaStore(const std::string &tenantID,
                                                                                        const bool isNew)
{
    ASSERT_IF_NULL(member_->metaStoreClient);
    return member_->metaStoreClient->Delete(GenAKSKKey(member_->clusterID, tenantID, isNew), {});
}

Status AKSKManagerActor::OnDelAKSKFromMetaStore(const std::shared_ptr<DeleteResponse> &response,
                                                const std::string &tenantID,
                                                const bool isNew)
{
    if (response == nullptr) {
        YRLOG_WARN("{}|failed to delete aksk, isNew:{} err: delete rsp is null.", tenantID, isNew);
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "delete rsp is null");
    }
    if (response->status.IsError()) {
        YRLOG_WARN("{}|failed to delete aksk, isNew:{} err: {}", tenantID, isNew, response->status.ToString());
    } else {
        YRLOG_INFO("{}|success to delete aksk from meta-store, isNew:{}", tenantID, isNew);
        DelAKSKFromCache(tenantID, isNew);
    }
    return response->status;
}

litebus::Future<Status> AKSKManagerActor::DelNewAKSK(const Status &status, const std::string &tenantID)
{
    if (status.IsError()) {
        return status;
    }
    YRLOG_INFO("{}|start to delete new aksk", tenantID);
    return DelAKSKFromMetaStore(tenantID, true)
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnDelAKSKFromMetaStore, std::placeholders::_1, tenantID,
                             true));
}

litebus::Future<Status> AKSKManagerActor::HandleUpdateAKSKInAdvance(const std::string &tenantID)
{
    if (member_->newAKSKMap.find(tenantID) == member_->newAKSKMap.end()) {
        return Status::OK();
    }
    if (member_->genCredPendingQueue_.find(tenantID) != member_->genCredPendingQueue_.end()) {
        YRLOG_WARN("{}|cred is updating, ignore it", tenantID);
        return Status::OK();
    }
    if (member_->inProgressAdvanceTenants.find(tenantID) != member_->inProgressAdvanceTenants.end()) {
        YRLOG_WARN("{}|cred is updating, ignore it", tenantID);
        return Status::OK();
    }
    member_->inProgressAdvanceTenants.insert(tenantID);
    auto akskContent = member_->newAKSKMap[tenantID];
    auto oldAKSKContent = akskContent->Copy();
    auto newAKSKContent = akskContent->Copy();
    return PutAKSKToMetaStore(oldAKSKContent, false)
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnPutAKSKToMetaStore, std::placeholders::_1, oldAKSKContent,
                             false, false))
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::UpdateAKSKInAdvancePutNew, std::placeholders::_1,
                             newAKSKContent))
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnUpdateAKSKInAdvance, std::placeholders::_1, tenantID));
}

litebus::Future<Status> AKSKManagerActor::HandleUpdateAKSKInTime(const std::string &tenantID)
{
    if (member_->oldAKSKMap.find(tenantID) == member_->oldAKSKMap.end()) {
        return Status::OK();
    }
    if (member_->inProgressExpireTenants.find(tenantID) != member_->inProgressExpireTenants.end()) {
        YRLOG_WARN("{}|cred is updating, ignore it", tenantID);
        return Status::OK();
    }
    member_->inProgressExpireTenants.insert(tenantID);
    return DelAKSKFromMetaStore(tenantID, false)
        .Then(
            litebus::Defer(GetAID(), &AKSKManagerActor::OnDelAKSKFromMetaStore, std::placeholders::_1, tenantID, false))
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnUpdateAKSKInTime, std::placeholders::_1, tenantID));
}


litebus::Future<Status> AKSKManagerActor::OnUpdateAKSKInTime(const Status &status, const std::string &tenantID)
{
   if (status.IsError()) {
       YRLOG_WARN("{}|failed to update cred in time, err is {}", tenantID, status.ToString());
   }
   (void)member_->inProgressExpireTenants.erase(tenantID);
   return status;
}

litebus::Future<Status> AKSKManagerActor::OnUpdateAKSKInAdvance(const Status &status, const std::string &tenantID)
{
   if (status.IsError()) {
       YRLOG_WARN("{}|failed to update cred in advance, err is {}", tenantID, status.ToString());
   }
   (void)member_->inProgressAdvanceTenants.erase(tenantID);
   return status;
}

litebus::Future<Status> AKSKManagerActor::UpdateAKSKInAdvancePutNew(const Status &status,
                                                                    const std::shared_ptr<AKSKContent> &akskContent)
{
    if (status.IsError()) {
        return status;
    }
    if (member_->newAKSKMap.find(akskContent->tenantID) == member_->newAKSKMap.end()) {
        return Status::OK();
    }
    auto cacheAKSKContent = member_->newAKSKMap[akskContent->tenantID];
    if (akskContent->expiredTimeStamp < cacheAKSKContent->expiredTimeStamp) {
        YRLOG_ERROR("{}|aksk changed, skip update new aksk", akskContent->tenantID);
        return Status::OK();
    }
    auto newAKSKContent = std::make_shared<AKSKContent>();
    return GenerateAKSK(akskContent->tenantID, newAKSKContent, false)
        .Then(std::function<litebus::Future<Status>(const Status &)>([&](const Status &ret) -> litebus::Future<Status> {
            if (ret.IsError()) {
                YRLOG_ERROR("{}|failed to generate new aksk when update in advance, err: {}", akskContent->tenantID,
                            ret.ToString());
                return ret;
            }

            return litebus::Async(GetAID(), &AKSKManagerActor::PutAKSKToMetaStore, newAKSKContent, true)
                .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnPutAKSKToMetaStore, std::placeholders::_1,
                                     newAKSKContent, true, false));
        }))
        .OnComplete([](const litebus::Future<Status> &ret) {
            if (ret.IsError()) {
                YRLOG_ERROR("{}|failed to generate new aksk when update in advance, err: {}", ret.GetErrorCode());
            }
        });
}

litebus::Future<Status> AKSKManagerActor::HandlePermanentCredential(bool isReady)
{
    if (!isReady) {
        return Status::OK();
    }
    ASSERT_IF_NULL(business_);
    return business_->HandlePermanentCredential();
}

litebus::Future<Status> AKSKManagerActor::DoHandlePermanentCredential()
{
    if (fetchCredFromRemoteCb_ == nullptr) {
        return Status::OK();
    }
    for (const auto &credential : member_->permanentCreds) {
        if (credential->tenantID.empty()) {
            continue;
        }
        fetchCredFromRemoteCb_(credential->credentialName, credential->serviceName, credential->microserviceNames)
            .OnComplete(litebus::Defer(GetAID(), &AKSKManagerActor::OnFetchPermanentCredentialFromRemote,
                                       std::placeholders::_1, credential));
    }
    return Status::OK();
}

litebus::Future<Status> AKSKManagerActor::OnFetchPermanentCredentialFromRemote(
    const litebus::Future<std::shared_ptr<AKSKContent>> &future, const std::shared_ptr<PermanentCredential> &credential)
{
    if (future.IsError() || !future.Get()->IsValid()) {
        YRLOG_WARN("{}|cannot get permanent credential from remote, try to require new", credential->tenantID);
        return RequireAKSKByTenantID(credential->tenantID, true)
            .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnRequirePermanentCredential, std::placeholders::_1,
                                 credential));
    }
    auto akskContent = future.Get();
    return PutAKSKToMetaStore(akskContent, true)
        .Then(litebus::Defer(GetAID(), &AKSKManagerActor::OnPutAKSKToMetaStore, std::placeholders::_1, akskContent,
                             true, false));
}


litebus::Future<Status> AKSKManagerActor::OnRequirePermanentCredential(
    const litebus::Future<std::shared_ptr<AKSKContent>> &future, const std::shared_ptr<PermanentCredential> &credential)
{
    if (future.IsError() || !future.Get()->IsValid()) {
        YRLOG_ERROR("{}|failed to require permanent credential", credential->tenantID);
        return Status::OK();
    }
    if (saveCredToRemoteCb_) {
        return saveCredToRemoteCb_(credential->credentialName, credential->serviceName, credential->microserviceNames,
                                   future.Get());
    }
    return Status::OK();
}

void AKSKManagerActor::MasterBusiness::OnChange()
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->SyncToReplaceAKSK(true);
    actor->SyncToReplaceAKSK(false);
    (void)member_->isReady.GetFuture().Then(
        litebus::Defer(actor->GetAID(), &AKSKManagerActor::HandlePermanentCredential, std::placeholders::_1));
}

litebus::Future<std::shared_ptr<AKSKContent>> AKSKManagerActor::MasterBusiness::RequireAKSKByTenantID(
    const std::string &tenantID, const bool isPermanentValid)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->GenerateNewAKSK(tenantID, isPermanentValid);
}

litebus::Future<std::shared_ptr<AKSKContent>> AKSKManagerActor::MasterBusiness::RequireAKSKByAK(
    const std::string &accessKey)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->FindAKSKByAKFromCache(accessKey);
}

litebus::Future<Status> AKSKManagerActor::MasterBusiness::AbandonAKSKByTenantID(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->HandleAbandonAKSKByTenantID(tenantID);
}

litebus::Future<Status> AKSKManagerActor::MasterBusiness::UpdateAKSKInAdvance(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->HandleUpdateAKSKInAdvance(tenantID);
}

litebus::Future<Status> AKSKManagerActor::MasterBusiness::UpdateAKSKInTime(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->HandleUpdateAKSKInTime(tenantID);
}

litebus::Future<Status> AKSKManagerActor::MasterBusiness::HandleForwardGetAKSKByTenantID(
    const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByTenantIDRequest> &akskRequest)
{
    std::string tenantID = akskRequest->tenantid();
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto newAKSKContent = actor->FindAKSKByTenantIDFromCache(tenantID, true);
    auto oldAKSKContent = actor->FindAKSKByTenantIDFromCache(tenantID, false);
    if (newAKSKContent->status.IsError() && akskRequest->iscreate()) {
        // if new aksk cache is not exist and need to create
        (void)actor->RequireAKSKByTenantID(tenantID, akskRequest->ispermanentvalid())
            .Then(litebus::Defer(actor->GetAID(), &AKSKManagerActor::OnRequireAKSKByTenantID,
                                 std::placeholders::_1, from, akskRequest));
        return Status::OK();
    }
    auto akskResponse = std::make_shared<messages::GetAKSKResponse>();
    akskResponse->set_requestid(akskRequest->requestid());
    SetGetAKSKResponseFromAKSKContent(akskResponse, newAKSKContent, true);
    if (akskResponse->code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("failed to set get aksk response, err: {}", akskResponse->message());
        (void)actor->Send(from, "ForwardGetAKSKResponse", akskResponse->SerializeAsString());
    }
    if (oldAKSKContent->status.IsOk()) {
        SetGetAKSKResponseFromAKSKContent(akskResponse, oldAKSKContent, false);
    }
    (void)actor->Send(from, "ForwardGetAKSKResponse", akskResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> AKSKManagerActor::MasterBusiness::HandleForwardGetAKSKByAK(
    const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByAKRequest> &akskRequest)
{
    std::string accessKey = akskRequest->accesskey();
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto akskContent = actor->FindAKSKByAKFromCache(accessKey);
    // send aksk regardless of whether it is empty
    auto akskResponse = std::make_shared<messages::GetAKSKResponse>();
    akskResponse->set_requestid(akskRequest->requestid());
    SetGetAKSKResponseFromAKSKContent(akskResponse, akskContent, true);
    (void)actor->Send(from, "ForwardGetAKSKResponse", akskResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> AKSKManagerActor::MasterBusiness::HandlePermanentCredential()
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->DoHandlePermanentCredential();
}

void AKSKManagerActor::SlaveBusiness::OnChange()
{
}

litebus::Future<std::shared_ptr<AKSKContent>> AKSKManagerActor::SlaveBusiness::RequireAKSKByTenantID(
    const std::string &tenantID, const bool isPermanentValid)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);

    // 凭据托管场景，主和备都可以请求凭据
    if (!member_->credentialHostAddress_.empty()) {
        return actor->GenerateNewAKSK(tenantID, isPermanentValid);
    }

    // 凭据创建场景，强制由主创建租户凭据，备需要转发创建请求到主处理
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<AKSKContent>>>();
    (void)actor->SendForwardGetAKSKByTenantID(tenantID, true, isPermanentValid)
        .OnComplete(litebus::Defer(actor->GetAID(), &AKSKManagerActor::OnForwardGetNewAKSKByTenantID,
                                   std::placeholders::_1, promise, tenantID));
    return promise->GetFuture();
}

litebus::Future<std::shared_ptr<AKSKContent>> AKSKManagerActor::SlaveBusiness::RequireAKSKByAK(
    const std::string &accessKey)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto akskContent = actor->FindAKSKByAKFromCache(accessKey);
    if (akskContent->status.IsOk()) {
        return akskContent;
    }
    YRLOG_INFO("{}|slave has not aksk in cache, need to get aksk from master", accessKey);
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<AKSKContent>>>();
    (void)actor->SendForwardGetAKSKByAK(accessKey)
        .OnComplete(litebus::Defer(actor->GetAID(), &AKSKManagerActor::OnForwardGetCacheAKSKByAK, std::placeholders::_1,
                                   promise, accessKey));
    return promise->GetFuture();
}

litebus::Future<Status> AKSKManagerActor::SlaveBusiness::AbandonAKSKByTenantID(const std::string &tenantID)
{
    // currently we don't have AbandonAKSK operation
    return Status::OK();
}

litebus::Future<Status> AKSKManagerActor::SlaveBusiness::UpdateAKSKInAdvance(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto promise = std::make_shared<litebus::Promise<Status>>();
    (void)actor->SendForwardGetAKSKByTenantID(tenantID, false)
        .OnComplete(litebus::Defer(actor->GetAID(), &AKSKManagerActor::OnForwardGetCacheAKSKByTenantID,
                                   std::placeholders::_1, promise));
    return promise->GetFuture();
}

litebus::Future<Status> AKSKManagerActor::SlaveBusiness::UpdateAKSKInTime(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->DelAKSKFromCache(tenantID, false);
    auto promise = std::make_shared<litebus::Promise<Status>>();
    (void)actor->SendForwardGetAKSKByTenantID(tenantID, false)
        .OnComplete(litebus::Defer(actor->GetAID(), &AKSKManagerActor::OnForwardGetCacheAKSKByTenantID,
                                   std::placeholders::_1, promise));
    return promise->GetFuture();
}

litebus::Future<Status> AKSKManagerActor::SlaveBusiness::HandleForwardGetAKSKByTenantID(
    const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByTenantIDRequest> &akskRequest)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto akskResponse = std::make_shared<messages::GetAKSKResponse>();
    akskResponse->set_requestid(akskRequest->requestid());
    akskResponse->set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
    akskResponse->set_message("slave cannot process get aksk by tenantID request");
    (void)actor->Send(from, "ForwardGetAKSKResponse", akskResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> AKSKManagerActor::SlaveBusiness::HandleForwardGetAKSKByAK(
    const litebus::AID &from, const std::shared_ptr<messages::GetAKSKByAKRequest> &akskRequest)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto akskResponse = std::make_shared<messages::GetAKSKResponse>();
    akskResponse->set_requestid(akskRequest->requestid());
    akskResponse->set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
    akskResponse->set_message("slave cannot process get aksk by ak request");
    (void)actor->Send(from, "ForwardGetAKSKResponse", akskResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> AKSKManagerActor::SlaveBusiness::HandlePermanentCredential()
{
    return Status::OK();
}
}