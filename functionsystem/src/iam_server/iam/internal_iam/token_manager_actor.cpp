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

#include "token_manager_actor.h"

#include <async/defer.hpp>

#include "common/utils/meta_store_kv_operation.h"

namespace functionsystem::iamserver {

using namespace functionsystem::explorer;

TokenManagerActor::TokenManagerActor(const std::string &name, const std::string &clusterID,
                                     const uint32_t expiredTimeSpan,
                                     const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    : litebus::ActorBase(name)
{
    member_ = std::make_shared<Member>();
    member_->clusterID = clusterID;
    member_->tokenExpiredTimeSpan = expiredTimeSpan;
    // min token expiredTimeSpan is 720 s
    if (member_->tokenExpiredTimeSpan / MIN_AHEAD_TIME_FACTOR < TIME_AHEAD_OF_EXPIRED) {
        member_->aheadUpdateExpireTokenTime = member_->tokenExpiredTimeSpan / MIN_AHEAD_TIME_FACTOR;
    }
    if (member_->aheadUpdateExpireTokenTime * MS_SECOND / MIN_EXPIRED_FACTOR < CHECK_TOKEN_EXPIRED_INTERVAL) {
        member_->checkTokenExpireInterval = member_->aheadUpdateExpireTokenTime * MS_SECOND / MIN_EXPIRED_FACTOR;
    }
    ASSERT_IF_NULL(metaStoreClient);
    member_->metaStoreClient = metaStoreClient;
    member_->metaStorageAccessor = std::make_shared<MetaStorageAccessor>(metaStoreClient);
}

void TokenManagerActor::Init()
{
    ASSERT_IF_NULL(member_);
    ASSERT_IF_NULL(member_->metaStoreClient);
    auto masterBusiness = std::make_shared<MasterBusiness>(shared_from_this(), member_);
    auto slaveBusiness = std::make_shared<SlaveBusiness>(shared_from_this(), member_);
    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);
    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);
    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        "TokenManagerActor", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &TokenManagerActor::UpdateLeaderInfo, leaderInfo);
        });
    curStatus_ = SLAVE_BUSINESS;
    business_ = slaveBusiness;
    auto newTokenResponse =
        member_->metaStoreClient->Get(GenTokenKeyWatchPrefix(member_->clusterID, true), { .prefix = true }).Get();
    auto oldTokenResponse =
        member_->metaStoreClient->Get(GenTokenKeyWatchPrefix(member_->clusterID, false), { .prefix = true }).Get();
    Register();
    litebus::Async(GetAID(), &TokenManagerActor::AsyncInitialize, newTokenResponse, oldTokenResponse);
    Receive("ForwardGetToken", &TokenManagerActor::ForwardGetToken);
    Receive("ForwardGetTokenResponse", &TokenManagerActor::ForwardGetTokenResponse);
}

Status TokenManagerActor::Register()
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
        return litebus::Async(aid, &TokenManagerActor::IAMTokenSyncer, getResponse, true);
    };
    auto oldSyncer = [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &TokenManagerActor::IAMTokenSyncer, getResponse, false);
    };
    auto handler = [aid(GetAID())](const std::vector<WatchEvent> &events, bool) {
        auto respCopy = events;
        litebus::Async(aid, &TokenManagerActor::UpdateTokenEvent, respCopy);
        return true;
    };
    (void)member_->metaStorageAccessor
        ->RegisterObserver(GenTokenKeyWatchPrefix(member_->clusterID, true), watchOpt, handler, newSyncer)
        .After(WATCH_TIMEOUT_MS, after);
    (void)member_->metaStorageAccessor
        ->RegisterObserver(GenTokenKeyWatchPrefix(member_->clusterID, false), watchOpt, handler, oldSyncer)
        .After(WATCH_TIMEOUT_MS, after);
    return Status::OK();
}


void TokenManagerActor::Finalize()
{
    litebus::TimerTools::Cancel(member_->checkTokenExpiredInAdvanceTimer);
    litebus::TimerTools::Cancel(member_->checkTokenExpiredInTimeTimer);
}

void TokenManagerActor::CheckTokenExpiredInAdvance()
{
    std::unordered_set<std::string> needUpdateSet;
    for (const auto &tokenIter : member_->newTokenMap) {
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (tokenIter.second->expiredTimeStamp < (now + member_->aheadUpdateExpireTokenTime)) {
            needUpdateSet.insert(tokenIter.first);
        }
    }
    for (const auto &val : needUpdateSet) {
        YRLOG_INFO("start update token in advance, tenantID: {}", val);
        ASSERT_IF_NULL(business_);
        (void)business_->UpdateTokenInAdvance(val);
    }
    member_->checkTokenExpiredInAdvanceTimer = litebus::AsyncAfter(member_->checkTokenExpireInterval, GetAID(),
                                                                   &TokenManagerActor::CheckTokenExpiredInAdvance);
}

void TokenManagerActor::CheckTokenExpiredInTime()
{
    std::unordered_set<std::string> needDeleteSet;
    for (const auto &tokenIter : member_->oldTokenMap) {
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if ((tokenIter.second->expiredTimeStamp) < now) {
            needDeleteSet.insert(tokenIter.first);
        }
    }
    for (const auto &val : needDeleteSet) {
        YRLOG_INFO("start update token in time, tenantID: {}", val);
        ASSERT_IF_NULL(business_);
        (void)business_->UpdateTokenInTime(val);
    }
    member_->checkTokenExpiredInTimeTimer =
        litebus::AsyncAfter(member_->checkTokenExpireInterval, GetAID(), &TokenManagerActor::CheckTokenExpiredInTime);
}

void TokenManagerActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
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

void TokenManagerActor::AsyncInitialize(const std::shared_ptr<GetResponse> &newResponse,
                                        const std::shared_ptr<GetResponse> &oldResponse)
{
    SyncTokenFromMetaStore(newResponse, true);
    SyncTokenFromMetaStore(oldResponse, false);
    CheckTokenExpiredInAdvance();
    CheckTokenExpiredInTime();
    member_->initialized = true;
}

void TokenManagerActor::SyncTokenFromMetaStore(const std::shared_ptr<GetResponse> &response, bool isNew)
{
    if (response->status.IsError()) {
        YRLOG_ERROR("fail to sync token, err: {}", response->status.ToString());
        return;
    }
    if (response->kvs.empty()) {
        // normal case, not log.
        return;
    }
    for (const auto &kv : response->kvs) {
        auto eventKey = TrimKeyPrefix(kv.key(), member_->metaStoreClient->GetTablePrefix());
        auto tokenSalt = TransTokenSaltFromJson(kv.value());
        auto tokenContent = std::make_shared<TokenContent>();
        tokenContent->salt = tokenSalt->salt;
        if (auto status = DecryptTokenFromStorage(tokenSalt->token, tokenContent); status.IsError()) {
            YRLOG_ERROR("sync token failed while transforming token, key: {}, err: {}", eventKey, status.ToString());
            continue;
        }
        if (isNew) {
            UpdateNewTokenCompareTime(tokenContent);
        } else {
            UpdateOldTokenCompareTime(tokenContent);
        }
    }
}

litebus::Future<TokenMap> TokenManagerActor::AsyncGetTokenFromMetaStore(bool isNew)
{
    ASSERT_IF_NULL(member_->metaStoreClient);
    auto prefix = GenTokenKeyWatchPrefix(member_->clusterID, isNew);
    std::function<std::unordered_map<std::string, std::shared_ptr<TokenContent>>(const std::shared_ptr<GetResponse> &)>
        then = [](const std::shared_ptr<GetResponse> &response)
        -> std::unordered_map<std::string, std::shared_ptr<TokenContent>> {
        std::unordered_map<std::string, std::shared_ptr<TokenContent>> output;

        if (response->status.IsError()) {
            YRLOG_ERROR("fail to sync token, err: {}", response->status.ToString());
            return output;
        }
        if (response->kvs.empty()) {
            // normal case, not log.
            return output;
        }
        for (const auto &kv : response->kvs) {
            auto tokenContent = GetTokenContentFromEventValue(kv.value());
            if (tokenContent == nullptr) {
                continue;
            }
            (void)output.emplace(tokenContent->tenantID, tokenContent);
        }
        return output;
    };
    return member_->metaStoreClient->Get(prefix, { .prefix = true }).Then(then);
}


std::shared_ptr<TokenContent> TokenManagerActor::GetTokenContentFromEventValue(const std::string &value)
{
    auto tokenSalt = TransTokenSaltFromJson(value);
    if (tokenSalt->salt.empty() || tokenSalt->token.empty()) {
        YRLOG_ERROR("token or salt is empty");
        return nullptr;
    }
    auto tokenContent = std::make_shared<TokenContent>();
    tokenContent->salt = tokenSalt->salt;
    if (auto status = DecryptTokenFromStorage(tokenSalt->token, tokenContent); status.IsError()) {
        YRLOG_ERROR("sync token failed while transforming token", status.ToString());
        return nullptr;
    }
    return tokenContent;
}

Status TokenManagerActor::ReplaceTokenMap(const std::unordered_map<std::string, std::shared_ptr<TokenContent>> tokenMap,
                                          const bool isNew)
{
    if (tokenMap.empty()) {
        return Status::OK();
    }
    for (const auto &item : tokenMap) {
        if (isNew) {
            UpdateNewTokenCompareTime(item.second);
        } else {
            UpdateOldTokenCompareTime(item.second);
        }
    }
    return Status::OK();
}

litebus::Future<Status> TokenManagerActor::SyncToReplaceToken(bool isNew)
{
    if (!member_->initialized) {
        return Status::OK();
    }
    return AsyncGetTokenFromMetaStore(isNew).Then(
        litebus::Defer(GetAID(), &TokenManagerActor::ReplaceTokenMap, std::placeholders::_1, isNew));
}

void TokenManagerActor::UpdateTokenEvent(const std::vector<WatchEvent> &events)
{
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), member_->metaStoreClient->GetTablePrefix());
        auto tenantID = GetTokenTenantID(eventKey);
        bool isNew = litebus::strings::StartsWithPrefix(eventKey, INTERNAL_IAM_TOKEN_PREFIX + NEW_PREFIX);
        if (tenantID.empty()) {
            YRLOG_ERROR("invalid token key: {}", eventKey);
            continue;
        }
        YRLOG_INFO("receive token event, tenantID({}), isNew:{}, type:{}", tenantID, isNew,
                   fmt::underlying(event.eventType));
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                auto tokenContent = GetTokenContentFromEventValue(event.kv.value());
                if (tokenContent == nullptr) {
                    continue;
                }
                if (isNew) {
                    UpdateNewTokenCompareTime(tokenContent);
                } else {
                    UpdateOldTokenCompareTime(tokenContent);
                }
                break;
            }
            case EVENT_TYPE_DELETE: {
                if (isNew) {
                    (void)member_->newTokenMap.erase(tenantID);
                } else {
                    (void)member_->oldTokenMap.erase(tenantID);
                }
                break;
            }
            default: {
                YRLOG_WARN("unknown event type {}", fmt::underlying(event.eventType));
            }
        }
    }
}

litebus::Future<SyncResult> TokenManagerActor::IAMTokenSyncer(const std::shared_ptr<GetResponse> &getResponse,
                                                              bool isNew)
{
    auto prefix = GenTokenKeyWatchPrefix(member_->clusterID, isNew);
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
        auto tenantID = GetTokenTenantID(eventKey);
        if (tenantID.empty()) {
            YRLOG_ERROR("invalid token key: {}", eventKey);
            continue;
        }
        watchEvents.emplace_back(WatchEvent{ .eventType = EVENT_TYPE_PUT, .kv = kv, .prevKv = {} });
        etcdSet.emplace(tenantID);
    }
    UpdateTokenEvent(watchEvents);
    if (isNew) {
        // delete new token from cache
        for (auto it = member_->newTokenMap.begin(); it != member_->newTokenMap.end();) {
            if (etcdSet.find(it->first) == etcdSet.end()) {
                YRLOG_INFO("{} is not in etcd, but in newTokenMap cache, need to delete", it->first);
                it = member_->newTokenMap.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        // delete old token from cache
        for (auto it = member_->oldTokenMap.begin(); it != member_->oldTokenMap.end();) {
            if (etcdSet.find(it->first) == etcdSet.end()) {
                YRLOG_INFO("{} is not in etcd, but in oldTokenMap cache, need to delete", it->first);
                it = member_->oldTokenMap.erase(it);
            } else {
                ++it;
            }
        }
    }
    return SyncResult{ Status::OK() };
}

Status TokenManagerActor::DecryptToken(const std::string &tokenStr, const std::shared_ptr<TokenContent> &tokenContent)
{
    auto pos = tokenStr.find(SPLIT_SYMBOL);
    if (pos == std::string::npos) {
        YRLOG_ERROR("decrypt token string to TokenContent failed, split symbol not found");
        return Status(StatusCode::PARAMETER_ERROR,
                      "decrypt token string to TokenContent failed, split symbol not found");
    }
    std::string key = tokenStr.substr(0, pos);
    std::string info = tokenStr.substr(pos + 1);
    auto tokenDecrypt =  SensitiveValue(info);
    char decryptedToken[INTERNAL_IAM_TOKEN_MAX_SIZE + 1] = { 0 };
    auto ret = memcpy_s(decryptedToken, INTERNAL_IAM_TOKEN_MAX_SIZE, tokenDecrypt.GetData(),
                        tokenDecrypt.GetSize());
    if (ret != 0) {
        YRLOG_ERROR("decrypted token copy failed! err:{}", std::to_string(ret));
        return Status(StatusCode::PARAMETER_ERROR, "decrypted token copy failed! err: " + std::to_string(ret));
    }
    tokenContent->encryptToken = tokenStr;
    return tokenContent->Parse(decryptedToken);
}

Status TokenManagerActor::EncryptToken(const std::shared_ptr<TokenContent> &tokenContent)
{
    char serializeToken[INTERNAL_IAM_TOKEN_MAX_SIZE] = { 0 };
    size_t tokenSize{ 0 };
    Status status = tokenContent->Serialize(serializeToken, tokenSize);
    if (status.IsError()) {
        return Status(StatusCode::PARAMETER_ERROR, "serialize token failed, err: " + status.ToString());
    }
    SensitiveValue tokenSensitiveValue(serializeToken, tokenSize);
    tokenContent->encryptToken = serializeToken;
    return Status::OK();
}

litebus::Option<std::string> TokenManagerActor::EncryptTokenForStorage(
    const std::shared_ptr<TokenContent> &tokenContent)
{
    // 1. encrypt token string for storage, token string is sensitive value
    // token in storage format is: Encrypt(encryptToken+timestamp)
    return tokenContent->encryptToken + SPLIT_SYMBOL_TIMESTAMP + std::to_string(tokenContent->expiredTimeStamp);
}

Status TokenManagerActor::DecryptTokenFromStorage(const std::string &encryptTokenFromStorage,
                                                  const std::shared_ptr<TokenContent> &tokenContent)
{
    // token in storage format is: Encrypt(encryptToken+timestamp)
    std::string tokenFromStorageStr(encryptTokenFromStorage);
    auto pos = tokenFromStorageStr.rfind(SPLIT_SYMBOL_TIMESTAMP);
    if (pos == std::string::npos) {
        return Status(StatusCode::PARAMETER_ERROR, "get token from decrypt storage token failed!");
    }
    // sensitiveTokenStr is sensitive value, need to clean memory
    std::string sensitiveTokenStr = tokenFromStorageStr.substr(0, pos);
    auto status = DecryptToken(sensitiveTokenStr, tokenContent);
    if (status.IsError()) {
        return Status(StatusCode::PARAMETER_ERROR,
                      "transform encrypt token to token content failed! err: " + status.ToString());
    }
    CleanSensitiveStrMemory(sensitiveTokenStr, "decrypt token from storage");
    CleanSensitiveStrMemory(tokenFromStorageStr, "decrypt token from storage");
    return Status::OK();
}

Status TokenManagerActor::CheckTokenSameWithCache(const std::shared_ptr<TokenContent> token)
{
    auto newToken = FindTokenFromCache(token->tenantID, true);
    // compare with local new tokenContent
    if (newToken->IsValid().IsOk()) {
        if (*token == *newToken) {
            YRLOG_DEBUG("{}|verify tokenContent success, token is new", token->tenantID);
            return Status::OK();
        }
        YRLOG_WARN("{}|tokenContent is not equal to local new tokenContent", token->tenantID);
    }
    auto oldToken = FindTokenFromCache(token->tenantID, false);
    // compare with local old tokenContent
    if (oldToken->IsValid().IsOk()) {
        if (*token == *oldToken) {
            YRLOG_DEBUG("{}|verify tokenContent success, token is old", token->tenantID);
            return Status::OK();
        }
        YRLOG_ERROR("{}|tokenContent is not equal to local old tokenContent", token->tenantID);
        return Status(StatusCode::PARAMETER_ERROR, "tokenContent is not equal to local new and old tokenContent");
    }
    YRLOG_ERROR("tokenContent not found in local cache or not equal to local new tokenContent", token->tenantID);
    return Status(StatusCode::PARAMETER_ERROR,
                  "tokenContent not found in local cache or not equal to local new tokenContent");
}

void TokenManagerActor::ForwardGetToken(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto tokenRequest = std::make_shared<messages::GetTokenRequest>();
    if (!tokenRequest->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse token request");
        return;
    }
    YRLOG_INFO("{}|{}|receive get token request, isCreate({})", tokenRequest->requestid(), tokenRequest->tenantid(),
               tokenRequest->iscreate());
    ASSERT_IF_NULL(business_);
    (void)business_->HandleForwardGetToken(from, tokenRequest);
}

void TokenManagerActor::ForwardGetTokenResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto tokenResponse = std::make_shared<messages::GetTokenResponse>();
    if (!tokenResponse->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse token response");
        return;
    }
    YRLOG_INFO("{}|{}|receive get token response", tokenResponse->requestid(), tokenResponse->tenantid());
    forwardGetTokenSync_.Synchronized(tokenResponse->requestid(), tokenResponse);
}

litebus::Future<Status> TokenManagerActor::AbandonTokenByTenantID(const std::string &tenantID)
{
    if (!member_->initialized) {
        return Status(StatusCode::IAM_WAIT_INITIALIZE_COMPLETE, "iam-server is initializing");
    }
    ASSERT_IF_NULL(business_);
    return business_->AbandonTokenByTenantID(tenantID);
}

litebus::Future<std::shared_ptr<TokenSalt>> TokenManagerActor::RequireEncryptToken(const std::string &tenantID)
{
    if (!member_->initialized) {
        auto tokenSalt = std::make_shared<TokenSalt>();
        tokenSalt->status = Status(StatusCode::IAM_WAIT_INITIALIZE_COMPLETE, "iam-server is initializing");
        return tokenSalt;
    }
    if (member_->newTokenRequestMap.find(tenantID) != member_->newTokenRequestMap.end()) {
        return member_->newTokenRequestMap[tenantID]->GetFuture();
    }
    // 1. find local token in cache
    auto localToken = GetLocalNewToken(tenantID);
    if (localToken->status.IsOk()) {
        return localToken;
    }
    // 2. try to generate new
    ASSERT_IF_NULL(business_);
    return business_->RequireEncryptToken(tenantID);
}

litebus::Future<Status> TokenManagerActor::VerifyToken(const std::shared_ptr<TokenContent> &tokenContent)
{
    if (!member_->initialized) {
        return Status(StatusCode::IAM_WAIT_INITIALIZE_COMPLETE, "iam-server is initializing");
    }
    auto status = DecryptToken(tokenContent->encryptToken, tokenContent);
    if (status.IsError()) {
        return status;
    }
    YRLOG_DEBUG("{}|verify token, expiredTime:{}", tokenContent->tenantID, tokenContent->expiredTimeStamp);
    //  check expired time, if token expired then verify failed
    auto now = static_cast<uint64_t>(std::time(nullptr));
    if (tokenContent->expiredTimeStamp < now) {
        YRLOG_ERROR("token expired, now = {}, expiredTimeStamp = {}", now, tokenContent->expiredTimeStamp);
        return Status(StatusCode::PARAMETER_ERROR,
                      "token has expired, expiredTimeStamp = " + std::to_string(tokenContent->expiredTimeStamp));
    }
    ASSERT_IF_NULL(business_);
    return business_->VerifyToken(tokenContent);
}

litebus::Future<std::shared_ptr<TokenSalt>> TokenManagerActor::GenerateNewToken(const std::string &tenantID)
{
    YRLOG_INFO("{}|start to generate new token", tenantID);
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<TokenSalt>>>();
    member_->newTokenRequestMap[tenantID] = promise;
    auto tokenContent = std::make_shared<TokenContent>();
    auto tokenSalt = std::make_shared<TokenSalt>();
    Status status = GenerateToken(tenantID, tokenContent);
    if (status.IsError()) {
        tokenSalt->status = status;
        YRLOG_ERROR("{}|failed to generate new token, err:", tenantID, status.ToString());
        (void)member_->newTokenRequestMap.erase(tenantID);
        return tokenSalt;
    }
    YRLOG_DEBUG("{}|success to generate new token, start to put it to metastore", tenantID);
    (void)PutTokenToMetaStore(tokenContent, true)
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::OnPutTokenFromMetaStore, std::placeholders::_1, tokenContent,
                             true, true));
    return promise->GetFuture();
}

Status TokenManagerActor::GenerateToken(const std::string &tenantID, const std::shared_ptr<TokenContent> &tokenContent)
{
    // 1. generate token
    tokenContent->tenantID = tenantID;
    auto now = static_cast<uint64_t>(std::time(nullptr));
    tokenContent->expiredTimeStamp =
        (UINT64_MAX - now < member_->tokenExpiredTimeSpan) ? now : now + member_->tokenExpiredTimeSpan;

    Status status = EncryptToken(tokenContent);
    if (status.IsError()) {
        YRLOG_ERROR("{}|failed to encrypt token, err: {}", tenantID, status.ToString());
        return Status(StatusCode::FAILED, "encrypt token failed, err: " + status.ToString());
    }
    status = tokenContent->IsValid(NEW_TOKEN_EXPIRED_OFFSET);
    if (status.IsError()) {
        YRLOG_ERROR("{}|tokenContent is not valid, err: {}", tenantID, status.ToString());
        return Status(StatusCode::FAILED, "tokenContent is not valid, err: " + status.ToString());
    }
    return Status::OK();
}

litebus::Future<std::shared_ptr<PutResponse>> TokenManagerActor::PutTokenToMetaStore(
    const std::shared_ptr<TokenContent> &tokenContent, const bool isNew)
{
    auto encryptForStorage = EncryptTokenForStorage(tokenContent);
    if (encryptForStorage.IsNone()) {
        std::shared_ptr<PutResponse> putResponse = std::make_shared<PutResponse>();
        putResponse->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "encrypt tokenContent for storage failed");
        return putResponse;
    }
    ASSERT_IF_NULL(member_->metaStoreClient);
    auto tokenJson =
        TransToJsonFromTokenSalt(TokenSalt{ .token = encryptForStorage.Get(), .salt = tokenContent->salt });
    return member_->metaStoreClient->Put(GenTokenKey(member_->clusterID, tokenContent->tenantID, isNew), tokenJson, {});
}

Status TokenManagerActor::OnPutTokenFromMetaStore(const std::shared_ptr<PutResponse> &response,
                                                  const std::shared_ptr<TokenContent> &tokenContent, const bool isNew,
                                                  const bool isSetPromise)
{
    if (response->status.IsError()) {
        YRLOG_WARN("{}|failed to put token, isNew:{} err: {}", tokenContent->tenantID, isNew,
                   response->status.ToString());
    } else {
        YRLOG_INFO("{}|success to put token to meta-store, isNew:{}", tokenContent->tenantID, isNew);
        if (isNew) {
            UpdateNewTokenCompareTime(tokenContent);
        } else {
            UpdateOldTokenCompareTime(tokenContent);
        }
    }
    // for require new token
    if (isSetPromise && member_->newTokenRequestMap.find(tokenContent->tenantID) != member_->newTokenRequestMap.end()) {
        auto promise = member_->newTokenRequestMap[tokenContent->tenantID];
        auto tokenSalt = std::make_shared<TokenSalt>();
        tokenSalt->status = response->status;
        tokenSalt->token = tokenContent->encryptToken;
        tokenSalt->salt = tokenContent->salt;
        tokenSalt->expiredTimeStamp = tokenContent->expiredTimeStamp;
        promise->SetValue(tokenSalt);
        member_->newTokenRequestMap.erase(tokenContent->tenantID);
    }
    return response->status;
}

litebus::Future<std::shared_ptr<DeleteResponse>> TokenManagerActor::DelTokenFromMetaStore(const std::string &tenantID,
                                                                                          const bool isNew)
{
    ASSERT_IF_NULL(member_->metaStoreClient);
    return member_->metaStoreClient->Delete(GenTokenKey(member_->clusterID, tenantID, isNew), {});
}

Status TokenManagerActor::OnDelTokenFromMetaStore(const std::shared_ptr<DeleteResponse> &response,
                                                  const std::string &tenantID, const bool isNew)
{
    if (response->status.IsError()) {
        YRLOG_WARN("{}|failed to put token, isNew:{} err: {}", tenantID, isNew, response->status.ToString());
    } else {
        YRLOG_INFO("{}|success to put token to meta-store, isNew:{}", tenantID, isNew);
        if (isNew) {
            member_->newTokenMap.erase(tenantID);
        } else {
            member_->oldTokenMap.erase(tenantID);
        }
    }
    return response->status;
}

void TokenManagerActor::MasterBusiness::OnChange()
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->SyncToReplaceToken(true);
    actor->SyncToReplaceToken(false);
}

litebus::Future<std::shared_ptr<messages::GetTokenResponse>> TokenManagerActor::SendForwardGetToken(
    const std::string &tenantID, const bool needCreate)
{
    if (member_->masterAID.Name().empty()) {
        YRLOG_ERROR("{}|master is none, failed to forward", tenantID);
        auto tokenResp = std::make_shared<messages::GetTokenResponse>();
        // inner error, need to retry
        tokenResp->set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
        tokenResp->set_message("master is none, failed to forward");
        return tokenResp;
    }
    auto tokenRequest = std::make_shared<messages::GetTokenRequest>();
    tokenRequest->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    tokenRequest->set_tenantid(tenantID);
    tokenRequest->set_iscreate(needCreate);
    YRLOG_INFO("{}|{} forward get token to({})", tokenRequest->requestid(), tokenRequest->tenantid(),
               member_->masterAID.HashString());
    auto future = forwardGetTokenSync_.AddSynchronizer(tokenRequest->requestid());
    Send(member_->masterAID, "ForwardGetToken", tokenRequest->SerializeAsString());
    return future;
}

void TokenManagerActor::OnForwardGetNewToken(
    const litebus::Future<std::shared_ptr<messages::GetTokenResponse>> future,
    const std::shared_ptr<litebus::Promise<std::shared_ptr<TokenSalt>>> &promise, const std::string &tenantID)
{
    auto tokenSalt = std::make_shared<TokenSalt>();
    if (future.IsError()) {
        YRLOG_ERROR("{}|failed to forward get token, err:{}", tenantID, future.GetErrorCode());
        tokenSalt->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to forward get token");
        promise->SetValue(tokenSalt);
        return;
    }
    auto tokenResp = future.Get();
    if (tokenResp->code() != 0) {
        tokenSalt->status =
            Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to forward get token, err is " + tokenResp->message());
        promise->SetValue(tokenSalt);
        return;
    }
    PutGetTokenResponseToCache(tokenResp);
    promise->SetValue(GetLocalNewToken(tenantID));
}

void TokenManagerActor::PutGetTokenResponseToCache(const std::shared_ptr<messages::GetTokenResponse> &tokenResponse)
{
    if (tokenResponse->newtoken().empty()) {
        YRLOG_WARN("get token response, token is empty");
        return;
    }
    auto tokenContent = std::make_shared<TokenContent>();
    tokenContent->salt = tokenResponse->salt();
    auto status = DecryptToken(tokenResponse->newtoken(), tokenContent);
    if (status.IsError()) {
        YRLOG_ERROR("{}|failed decrypt forward get token, err:{}", tokenResponse->tenantid(), status.ToString());
        return;
    }
    UpdateNewTokenCompareTime(tokenContent);
    if (!tokenResponse->oldtoken().empty()) {
        auto oldTokenContent = std::make_shared<TokenContent>();
        oldTokenContent->salt = tokenResponse->salt();
        auto oldStatus = DecryptToken(tokenResponse->oldtoken(), oldTokenContent);
        if (oldStatus.IsError()) {
            YRLOG_WARN("{}|failed decrypt old token, err:{}", tokenResponse->tenantid(), status.ToString());
            return;
        }
        UpdateOldTokenCompareTime(oldTokenContent);
    }
}

void TokenManagerActor::OnForwardGetCacheToken(
    const litebus::Future<std::shared_ptr<messages::GetTokenResponse>> future,
    const std::shared_ptr<litebus::Promise<Status>> &promise, const std::shared_ptr<TokenContent> &tokenContent)
{
    if (future.IsError()) {
        promise->SetValue(Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to forward get token"));
        return;
    }
    auto tokenResp = future.Get();
    if (tokenResp->code() != 0) {
        promise->SetValue(
            Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to forward get token, err is " + tokenResp->message()));
        return;
    }
    PutGetTokenResponseToCache(tokenResp);
    if (tokenContent != nullptr) {
        // for verify token
        promise->SetValue(CheckTokenSameWithCache(tokenContent));
    } else {
        promise->SetValue(Status::OK());
    }
}

const std::shared_ptr<TokenContent> TokenManagerActor::FindTokenFromCache(const std::string &tenantID,
                                                                          const bool &isNewTokenMap)
{
    if (isNewTokenMap) {
        if (member_->newTokenMap.find(tenantID) != member_->newTokenMap.end()) {
            return member_->newTokenMap[tenantID];
        }
    } else {
        if (member_->oldTokenMap.find(tenantID) != member_->oldTokenMap.end()) {
            return member_->oldTokenMap[tenantID];
        }
    }
    return std::make_shared<TokenContent>();
}

std::shared_ptr<TokenSalt> TokenManagerActor::GetLocalNewToken(const std::string &tenantID)
{
    auto localNewTokenContent = FindTokenFromCache(tenantID, true);
    auto tokenSalt = std::make_shared<TokenSalt>();
    if (localNewTokenContent->IsValid(NEW_TOKEN_EXPIRED_OFFSET).IsOk()) {
        tokenSalt->token = localNewTokenContent->encryptToken;
        tokenSalt->salt = localNewTokenContent->salt;
        tokenSalt->expiredTimeStamp = localNewTokenContent->expiredTimeStamp;
        YRLOG_INFO("{}|require token return local", tenantID);
        return tokenSalt;
    }
    tokenSalt->status = Status(StatusCode::FAILED, "local not found");
    return tokenSalt;
}

Status TokenManagerActor::OnRequireEncryptToken(const std::shared_ptr<TokenSalt> &tokenSalt, const litebus::AID &from,
                                                const std::shared_ptr<messages::GetTokenRequest> &tokenRequest)
{
    auto tokenResponse = std::make_shared<messages::GetTokenResponse>();
    tokenResponse->set_tenantid(tokenRequest->tenantid());
    tokenResponse->set_requestid(tokenRequest->requestid());
    tokenResponse->set_newtoken(tokenSalt->token);
    tokenResponse->set_salt(tokenSalt->salt);
    tokenResponse->set_code(static_cast<int32_t>(tokenSalt->status.StatusCode()));
    tokenResponse->set_message(tokenSalt->status.ToString());
    (void)Send(from, "ForwardGetTokenResponse", tokenResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> TokenManagerActor::HandleUpdateTokenInTime(const std::string &tenantID)
{
    if (member_->oldTokenMap.find(tenantID) == member_->oldTokenMap.end()) {
        return Status::OK();
    }
    if (member_->inProgressExpireTenants.find(tenantID) != member_->inProgressExpireTenants.end()) {
        YRLOG_WARN("{}|token is updating, ignore it", tenantID);
        return Status::OK();
    }
    member_->inProgressExpireTenants.insert(tenantID);
    return DelTokenFromMetaStore(tenantID, false)
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::OnDelTokenFromMetaStore, std::placeholders::_1, tenantID,
                             false))
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::OnUpdateTokenInTime, std::placeholders::_1, tenantID));
}

litebus::Future<Status> TokenManagerActor::OnUpdateTokenInTime(const Status &status, const std::string &tenantID)
{
    if (status.IsError()) {
        YRLOG_WARN("{}|failed to update token in time, err is {}", tenantID, status.ToString());
    }
    (void)member_->inProgressExpireTenants.erase(tenantID);
    return status;
}

litebus::Future<Status> TokenManagerActor::OnUpdateTokenInAdvance(const Status &status, const std::string &tenantID)
{
    if (status.IsError()) {
        YRLOG_WARN("{}|failed to update token in advance, err is {}", tenantID, status.ToString());
    }
    (void)member_->inProgressAdvanceTenants.erase(tenantID);
    return status;
}

litebus::Future<Status> TokenManagerActor::HandleAbandonTokenByTenantID(const std::string &tenantID)
{
    return DelTokenFromMetaStore(tenantID, false)
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::OnDelTokenFromMetaStore, std::placeholders::_1, tenantID,
                             false))
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::DelNewToken, std::placeholders::_1, tenantID));
}

litebus::Future<Status> TokenManagerActor::DelNewToken(const Status &status, const std::string &tenantID)
{
    if (status.IsError()) {
        return status;
    }
    YRLOG_INFO("{}|start to delete new token", tenantID);
    return DelTokenFromMetaStore(tenantID, true)
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::OnDelTokenFromMetaStore, std::placeholders::_1, tenantID,
                             true));
}

litebus::Future<Status> TokenManagerActor::HandleUpdateTokenInAdvance(const std::string &tenantID)
{
    if (member_->newTokenMap.find(tenantID) == member_->newTokenMap.end()) {
        return Status::OK();
    }
    // generate new token from RequireToken
    if (member_->newTokenRequestMap.find(tenantID) != member_->newTokenRequestMap.end()) {
        YRLOG_WARN("{}|token is updating, ignore it", tenantID);
        return Status::OK();
    }
    if (member_->inProgressAdvanceTenants.find(tenantID) != member_->inProgressAdvanceTenants.end()) {
        YRLOG_WARN("{}|token is updating, ignore it", tenantID);
        return Status::OK();
    }
    member_->inProgressAdvanceTenants.insert(tenantID);
    auto tokenContent = member_->newTokenMap[tenantID];
    auto oldTokenContent = tokenContent->Copy();
    auto newTokenContent = tokenContent->Copy();
    return PutTokenToMetaStore(oldTokenContent, false)
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::OnPutTokenFromMetaStore, std::placeholders::_1,
                             oldTokenContent, false, false))
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::UpdateTokenInAdvancePutNew, std::placeholders::_1,
                             newTokenContent))
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::OnUpdateTokenInAdvance, std::placeholders::_1, tenantID));
}

litebus::Future<Status> TokenManagerActor::UpdateTokenInAdvancePutNew(
    const Status &status, const std::shared_ptr<TokenContent> &tokenContent)
{
    if (status.IsError()) {
        return status;
    }
    if (member_->newTokenMap.find(tokenContent->tenantID) == member_->newTokenMap.end()) {
        return Status::OK();
    }
    auto cacheTokenContent = member_->newTokenMap[tokenContent->tenantID];
    if (tokenContent->expiredTimeStamp < cacheTokenContent->expiredTimeStamp) {
        YRLOG_ERROR("{}|token changed, skip update new token", tokenContent->tenantID);
        return Status::OK();
    }
    auto now = static_cast<uint64_t>(std::time(nullptr));
    tokenContent->expiredTimeStamp = now + member_->tokenExpiredTimeSpan;
    if (auto stat = EncryptToken(tokenContent); stat.IsError()) {
        YRLOG_ERROR("{}|failed to update token in advance, err:{}", tokenContent->tenantID, stat.ToString());
        return stat;
    }
    return PutTokenToMetaStore(tokenContent, true)
        .Then(litebus::Defer(GetAID(), &TokenManagerActor::OnPutTokenFromMetaStore, std::placeholders::_1, tokenContent,
                             true, false));
}

void TokenManagerActor::UpdateNewTokenCompareTime(const std::shared_ptr<TokenContent> &tokenContent)
{
    if (member_->newTokenMap.find(tokenContent->tenantID) == member_->newTokenMap.end()) {
        YRLOG_INFO("{}|token expire timestamp({})", tokenContent->tenantID, tokenContent->expiredTimeStamp);
        member_->newTokenMap[tokenContent->tenantID] = tokenContent;
        return;
    }
    auto cacheTokenContent = member_->newTokenMap[tokenContent->tenantID];
    if (tokenContent->expiredTimeStamp < cacheTokenContent->expiredTimeStamp) {
        YRLOG_ERROR("{}|cache token expire timestamp({}) is newer than ({}), skip update new token",
                    tokenContent->tenantID, cacheTokenContent->expiredTimeStamp, tokenContent->expiredTimeStamp);
        return;
    }
    member_->newTokenMap[tokenContent->tenantID] = tokenContent;
    YRLOG_INFO("{}|token expire timestamp({})", tokenContent->tenantID, tokenContent->expiredTimeStamp);
}

void TokenManagerActor::UpdateOldTokenCompareTime(const std::shared_ptr<TokenContent> &tokenContent)
{
    if (member_->oldTokenMap.find(tokenContent->tenantID) == member_->newTokenMap.end()) {
        YRLOG_INFO("{}|token expire timestamp({})", tokenContent->tenantID, tokenContent->expiredTimeStamp);
        member_->oldTokenMap[tokenContent->tenantID] = tokenContent;
        return;
    }
    auto cacheTokenContent = member_->oldTokenMap[tokenContent->tenantID];
    if (tokenContent->expiredTimeStamp < cacheTokenContent->expiredTimeStamp) {
        YRLOG_ERROR("{}|cache token expire timestamp({}) is newer than ({}), skip update new token",
                    tokenContent->tenantID, cacheTokenContent->expiredTimeStamp, tokenContent->expiredTimeStamp);
        return;
    }
    member_->oldTokenMap[tokenContent->tenantID] = tokenContent;
    YRLOG_INFO("{}|token expire timestamp({})", tokenContent->tenantID, tokenContent->expiredTimeStamp);
}

litebus::Future<std::shared_ptr<TokenSalt>> TokenManagerActor::MasterBusiness::RequireEncryptToken(
    const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->GenerateNewToken(tenantID);
}

litebus::Future<Status> TokenManagerActor::MasterBusiness::VerifyToken(
    const std::shared_ptr<TokenContent> &tokenContent)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->CheckTokenSameWithCache(tokenContent);
}

litebus::Future<Status> TokenManagerActor::MasterBusiness::AbandonTokenByTenantID(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->HandleAbandonTokenByTenantID(tenantID);
}

litebus::Future<Status> TokenManagerActor::MasterBusiness::HandleForwardGetToken(
    const litebus::AID &from, const std::shared_ptr<messages::GetTokenRequest> &tokenRequest)
{
    std::string tenantID = tokenRequest->tenantid();
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto newTokenContent = actor->FindTokenFromCache(tenantID, true);
    auto oldTokenContent = actor->FindTokenFromCache(tenantID, false);
    if (newTokenContent->tenantID.empty() && tokenRequest->iscreate()) {
        // if new token cache is not exist and need to create
        (void)actor->RequireEncryptToken(tenantID).Then(litebus::Defer(
            actor->GetAID(), &TokenManagerActor::OnRequireEncryptToken, std::placeholders::_1, from, tokenRequest));
        return Status::OK();
    }
    auto tokenResponse = std::make_shared<messages::GetTokenResponse>();
    tokenResponse->set_tenantid(tokenRequest->tenantid());
    tokenResponse->set_requestid(tokenRequest->requestid());
    tokenResponse->set_salt(newTokenContent->salt);
    tokenResponse->set_newtoken(newTokenContent->encryptToken);
    tokenResponse->set_oldtoken(oldTokenContent->encryptToken);
    tokenResponse->set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    (void)actor->Send(from, "ForwardGetTokenResponse", tokenResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> TokenManagerActor::MasterBusiness::UpdateTokenInTime(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->HandleUpdateTokenInTime(tenantID);
}

litebus::Future<Status> TokenManagerActor::MasterBusiness::UpdateTokenInAdvance(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->HandleUpdateTokenInAdvance(tenantID);
}

void TokenManagerActor::SlaveBusiness::OnChange()
{
}

litebus::Future<Status> TokenManagerActor::SlaveBusiness::UpdateTokenInTime(const std::string &tenantID)
{
    member_->oldTokenMap.erase(tenantID);
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto promise = std::make_shared<litebus::Promise<Status>>();
    (void)actor->SendForwardGetToken(tenantID, false)
        .OnComplete(litebus::Defer(actor->GetAID(), &TokenManagerActor::OnForwardGetCacheToken, std::placeholders::_1,
                                   promise, nullptr));
    return promise->GetFuture();
}

litebus::Future<Status> TokenManagerActor::SlaveBusiness::UpdateTokenInAdvance(const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto promise = std::make_shared<litebus::Promise<Status>>();
    (void)actor->SendForwardGetToken(tenantID, false)
        .OnComplete(litebus::Defer(actor->GetAID(), &TokenManagerActor::OnForwardGetCacheToken, std::placeholders::_1,
                                   promise, nullptr));
    return promise->GetFuture();
}

litebus::Future<std::shared_ptr<TokenSalt>> TokenManagerActor::SlaveBusiness::RequireEncryptToken(
    const std::string &tenantID)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<TokenSalt>>>();
    (void)actor->SendForwardGetToken(tenantID, true)
        .OnComplete(litebus::Defer(actor->GetAID(), &TokenManagerActor::OnForwardGetNewToken, std::placeholders::_1,
                                   promise, tenantID));
    return promise->GetFuture();
}

litebus::Future<Status> TokenManagerActor::SlaveBusiness::VerifyToken(
    const std::shared_ptr<TokenContent> &tokenContent)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto newTokenContent = actor->FindTokenFromCache(tokenContent->tenantID, true);
    if (!newTokenContent->tenantID.empty()) {
        return actor->CheckTokenSameWithCache(tokenContent);
    }
    YRLOG_INFO("slave has not token in cache, need to get token from master");
    auto promise = std::make_shared<litebus::Promise<Status>>();
    (void)actor->SendForwardGetToken(tokenContent->tenantID, false)
        .OnComplete(litebus::Defer(actor->GetAID(), &TokenManagerActor::OnForwardGetCacheToken, std::placeholders::_1,
                                   promise, tokenContent));
    return promise->GetFuture();
}

litebus::Future<Status> TokenManagerActor::SlaveBusiness::HandleForwardGetToken(
    const litebus::AID &from, const std::shared_ptr<messages::GetTokenRequest> &tokenRequest)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto tokenResponse = std::make_shared<messages::GetTokenResponse>();
    tokenResponse->set_tenantid(tokenRequest->tenantid());
    tokenResponse->set_requestid(tokenRequest->requestid());
    tokenResponse->set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
    tokenResponse->set_message("slave cannot process get token request");
    (void)actor->Send(from, "ForwardGetTokenResponse", tokenResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> TokenManagerActor::SlaveBusiness::AbandonTokenByTenantID(const std::string &tenantID)
{
    // current we don't hava abandonToken operation
    return Status::OK();
}
}