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

#include "iam_actor.h"

#include <ctime>
#include "httpd/http.hpp"
#include "common/utils/token_transfer.h"

namespace functionsystem::iamserver {
namespace {
// url for token
const std::string AUTH_TOKEN_URL = "/v1/token/auth"; // url for verify token when connect instance
const std::string REQUIRE_TOKEN_URL = "/v1/token/require"; // url for create token when create instance
const std::string ABANDON_TOKEN_URL = "/v1/token/abandon"; // url for abandon token
// url for aksk
const std::string REQUIRE_AKSK_BY_TENANT_ID_URL = "/v1/credential/require"; // url for create aksk when create instance
const std::string REQUIRE_AKSK_BY_AK_URL = "/v1/credential/auth"; // url for get aksk when verify requests signature
const std::string ABANDON_AKSK_URL = "/v1/credential/abandon"; // url for abandon aksk
// key for request header & token response header
const std::string HEADER_AUTH_KEY = "X-Auth"; // key for token
const std::string HEADER_TENANT_ID_KEY = "X-Tenant-ID"; // key for tenantID
const std::string HEADER_TENANT_SALT_KEY = "X-Salt"; // salt for tenant
const std::string HEADER_EXPIRED_TIME_SPAN = "X-Expired-Time-Span"; // key for expired time

Status GetValueFromHeaderMap(const litebus::http::HeaderMap &headerMap, const std::string &key, std::string &value)
{
    auto it = headerMap.find(key);
    if (it == headerMap.end()) {
        return Status(StatusCode::FAILED, "headerMap not have key: " + key);
    }
    if (it->second.empty()) {
        return Status(StatusCode::FAILED, "the value of " + it->first + " is empty");
    }
    value = it->second;
    return Status::OK();
}
} // namespace

IAMActor::IAMActor(const std::string &name) : ApiRouterRegister(), ActorBase(name)
{
    // token handler
    ApiRouterRegister::RegisterHandler(AUTH_TOKEN_URL, [aid(GetAID())](const HttpRequest &request) {
        return litebus::Async(aid, &IAMActor::VerifyToken, request);
    });
    ApiRouterRegister::RegisterHandler(REQUIRE_TOKEN_URL, [aid(GetAID())](const HttpRequest &request) {
        return litebus::Async(aid, &IAMActor::RequireEncryptToken, request);
    });
    ApiRouterRegister::RegisterHandler(ABANDON_TOKEN_URL, [aid(GetAID())](const HttpRequest &request) {
        return litebus::Async(aid, &IAMActor::AbandonToken, request);
    });
    // aksk handler
    ApiRouterRegister::RegisterHandler(REQUIRE_AKSK_BY_TENANT_ID_URL, [aid(GetAID())](const HttpRequest &request) {
        return litebus::Async(aid, &IAMActor::RequireAKSKByTenantID, request);
    });
    ApiRouterRegister::RegisterHandler(REQUIRE_AKSK_BY_AK_URL, [aid(GetAID())](const HttpRequest &request) {
        return litebus::Async(aid, &IAMActor::RequireAKSKByAK, request);
    });
    ApiRouterRegister::RegisterHandler(ABANDON_AKSK_URL, [aid(GetAID())](const HttpRequest &request) {
        return litebus::Async(aid, &IAMActor::AbandonAKSK, request);
    });
}

litebus::Future<HttpResponse> IAMActor::VerifyToken(const functionsystem::HttpRequest &request)
{
    auto filterResponse = RequestFilter("VerifyToken", InternalIAM::IAMCredType::TOKEN, request);
    if (filterResponse.first != litebus::http::ResponseCode::OK) {
        return GenerateHttpResponse(filterResponse.first, filterResponse.second);
    }
    std::string token;
    if (auto status(GetValueFromHeaderMap(request.headers, HEADER_AUTH_KEY, token)); status.IsError()) {
        YRLOG_ERROR("parse token from headerMap failed, err: {}", status.ToString());
        return GenerateHttpResponse(
            litebus::http::ResponseCode::BAD_REQUEST,
            "verify token failed, " +
            litebus::http::Response::GetStatusDescribe(litebus::http::ResponseCode::BAD_REQUEST) +
            ", parse token from headerMap failed.");
    }
    auto tokenContent = std::make_shared<TokenContent>();
    tokenContent->encryptToken = token;
    return internalIAM_->VerifyToken(tokenContent).Then([tokenContent](const Status &status) {
        if (status.IsError()) {
            YRLOG_ERROR("{}|verify token failed, err: {}", tokenContent->tenantID, status.ToString());
            auto httCode = litebus::http::ResponseCode::FORBIDDEN;
            if (status.StatusCode() == StatusCode::ERR_INNER_SYSTEM_ERROR ||
                status.StatusCode() == StatusCode::IAM_WAIT_INITIALIZE_COMPLETE) {
                httCode = litebus::http::ResponseCode::INTERNAL_SERVER_ERROR;
            }
            return GenerateHttpResponse(httCode, "verify token failed, " + status.ToString());
        }
        auto now = static_cast<uint64_t>(std::time(nullptr));
        HttpResponse response = GenerateHttpResponse(
            litebus::http::ResponseCode::OK,
            litebus::http::Response::GetStatusDescribe(litebus::http::ResponseCode::OK));
        response.headers.insert({{ HEADER_TENANT_ID_KEY, tokenContent->tenantID },
                                  { HEADER_EXPIRED_TIME_SPAN, std::to_string(tokenContent->expiredTimeStamp - now) }});
        return response;
    });
}

litebus::Future<HttpResponse> IAMActor::RequireEncryptToken(const functionsystem::HttpRequest &request)
{
    auto filterResponse = RequestFilter("RequireEncryptToken", InternalIAM::IAMCredType::TOKEN, request);
    if (filterResponse.first != litebus::http::ResponseCode::OK) {
        return GenerateHttpResponse(filterResponse.first, filterResponse.second);
    }
    std::string tenantID;
    if (auto status(GetValueFromHeaderMap(request.headers, HEADER_TENANT_ID_KEY, tenantID)); status.IsError()) {
        YRLOG_ERROR("get key({}) from header failed, err: {}", HEADER_TENANT_ID_KEY, status.ToString());
        return GenerateHttpResponse(litebus::http::ResponseCode::BAD_REQUEST,
                                    "require token failed, parse TenantID from headerMap failed.");
    }
    return internalIAM_->RequireEncryptToken(tenantID).Then([tenantID](const std::shared_ptr<TokenSalt> &tokenSalt) {
        if (tokenSalt->status.IsError() || tokenSalt->token.empty()) {
            YRLOG_ERROR("{}|RequireEncryptToken failed, err is {}", tenantID, tokenSalt->status.ToString());
            return GenerateHttpResponse(litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                                        "require token failed, err is " + tokenSalt->status.ToString());
        }
        HttpResponse response =
            GenerateHttpResponse(litebus::http::ResponseCode::OK,
                                 litebus::http::Response::GetStatusDescribe(litebus::http::ResponseCode::OK));
        (void)response.headers.insert({ HEADER_AUTH_KEY, tokenSalt->token });
        (void)response.headers.insert({ HEADER_TENANT_SALT_KEY, tokenSalt->salt });
        (void)response.headers.insert({ HEADER_EXPIRED_TIME_SPAN, std::to_string(tokenSalt->expiredTimeStamp) });
        CleanSensitiveStrMemory(tokenSalt->token, "before return encrypt token");
        return response;
    });
}

litebus::Future<HttpResponse> IAMActor::AbandonToken(const functionsystem::HttpRequest &request)
{
    auto filterResponse = RequestFilter("AbandonToken", InternalIAM::IAMCredType::TOKEN, request);
    if (filterResponse.first != litebus::http::ResponseCode::OK) {
        return GenerateHttpResponse(filterResponse.first, filterResponse.second);
    }
    std::string tenantID;
    if (auto status(GetValueFromHeaderMap(request.headers, HEADER_TENANT_ID_KEY, tenantID)); status.IsError()) {
        YRLOG_ERROR("get key({}) from header failed, err: {}", HEADER_TENANT_ID_KEY, status.ToString());
        return GenerateHttpResponse(litebus::http::ResponseCode::BAD_REQUEST,
                                    "abandon token failed, parse tenantID from headerMap failed.");
    }
    return internalIAM_->AbandonTokenByTenantID(tenantID).Then([tenantID](const Status &status) {
        if (status.IsError()) {
            YRLOG_ERROR("{}|AbandonToken failed, err is {}", tenantID, status.ToString());
            return GenerateHttpResponse(litebus::http::ResponseCode::NOT_FOUND,
                                        "abandon token failed, " + status.ToString());
        }
        YRLOG_DEBUG("{}|AbandonToken success", tenantID);
        return GenerateHttpResponse(litebus::http::ResponseCode::OK,
                                    litebus::http::Response::GetStatusDescribe(litebus::http::ResponseCode::OK));
    });
}

litebus::Future<HttpResponse> IAMActor::RequireAKSKByTenantID(const functionsystem::HttpRequest &request)
{
    if (auto [errorCode, message] = RequestFilter("RequireAKSKByTenantID", InternalIAM::IAMCredType::AK_SK, request);
        errorCode != litebus::http::ResponseCode::OK) {
        return GenerateHttpResponse(errorCode, message);
    }
    std::string tenantID;
    if (const auto status(GetValueFromHeaderMap(request.headers, HEADER_TENANT_ID_KEY, tenantID)); status.IsError()) {
        YRLOG_ERROR("no {} in header from {}, err: {}", HEADER_TENANT_ID_KEY,
                    request.client.IsSome() ? request.client.Get() : "unknown ip", status.ToString());
        return GenerateHttpResponse(litebus::http::ResponseCode::BAD_REQUEST,
                                    "require aksk by tenantID failed, parse TenantID from headerMap failed.");
    }
    return internalIAM_->RequireAKSKContentByTenantID(tenantID).Then(
        [tenantID, request](const std::shared_ptr<AKSKContent> &akskContent) {
            if (akskContent->status.IsError()) {
                YRLOG_ERROR("{}|RequireAKSKContentByTenantID failed by {}, err is {}", tenantID,
                            request.client.IsSome() ? request.client.Get() : "unknown ip",
                            akskContent->status.ToString());
                return GenerateHttpResponse(litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                                            "require aksk failed, err is " + akskContent->status.ToString());
            }
            // encrypt akskContent with component data key
            const auto encAKSKContent = EncryptAKSKContentForResponse(akskContent);
            if (encAKSKContent->status.IsError()) {
                YRLOG_ERROR("{}|RequireAKSKContentByTenantID failed by {}, encrypt aksk for response failed, err: {}",
                            tenantID, request.client.IsSome() ? request.client.Get() : "unknown ip",
                            encAKSKContent->status.ToString());
                return GenerateHttpResponse(
                    litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                    "require aksk by tenant id failed, err is " + encAKSKContent->status.ToString());
            }
            HttpResponse response =
                GenerateHttpResponse(litebus::http::ResponseCode::OK,
                                     litebus::http::Response::GetStatusDescribe(litebus::http::ResponseCode::OK));
            response.body = TransToJsonFromEncAKSKContent(encAKSKContent);
            return response;
        });
}

litebus::Future<HttpResponse> IAMActor::RequireAKSKByAK(const functionsystem::HttpRequest &request)
{
    auto filterResponse = RequestFilter("RequireAKSKByAK", InternalIAM::IAMCredType::AK_SK, request);
    if (filterResponse.first != litebus::http::ResponseCode::OK) {
        return GenerateHttpResponse(filterResponse.first, filterResponse.second);
    }
    std::string accessKey;
    if (const auto status(GetValueFromHeaderMap(request.headers, HEADER_AUTH_KEY, accessKey)); status.IsError()) {
        YRLOG_ERROR("no {} in header from {}, err: {}", HEADER_AUTH_KEY,
                    request.client.IsSome() ? request.client.Get() : "unknown ip", status.ToString());
        return GenerateHttpResponse(litebus::http::ResponseCode::BAD_REQUEST,
                                    "require aksk by access key failed, parse access key from headerMap failed.");
    }
    return internalIAM_->RequireAKSKContentByAK(accessKey).Then([accessKey, request](
                                                                    const std::shared_ptr<AKSKContent> &akskContent) {
        if (akskContent->status.IsError()) {
            YRLOG_ERROR("{}|RequireAKSKContentByAK failed by {}, err is {}", accessKey,
                        request.client.IsSome() ? request.client.Get() : "unknown ip", akskContent->status.ToString());
            return GenerateHttpResponse(litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                                        "require aksk by access key failed, err is " + akskContent->status.ToString());
        }
        // encrypt akskContent with component data key
        auto encAKSKContent = EncryptAKSKContentForResponse(akskContent);
        if (encAKSKContent->status.IsError()) {
            YRLOG_ERROR("{}|RequireAKSKContentByAK failed by {}, encrypt aksk for response failed, err: {}", accessKey,
                        request.client.IsSome() ? request.client.Get() : "unknown ip",
                        encAKSKContent->status.ToString());
            return GenerateHttpResponse(litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                                        "require aksk by ak failed, err is " + encAKSKContent->status.ToString());
        }
        HttpResponse response =
            GenerateHttpResponse(litebus::http::ResponseCode::OK,
                                 litebus::http::Response::GetStatusDescribe(litebus::http::ResponseCode::OK));
        response.body = TransToJsonFromEncAKSKContent(encAKSKContent);
        return response;
    });
}

litebus::Future<HttpResponse> IAMActor::AbandonAKSK(const functionsystem::HttpRequest &request)
{
    auto filterResponse = RequestFilter("AbandonAKSK", InternalIAM::IAMCredType::AK_SK, request);
    if (filterResponse.first != litebus::http::ResponseCode::OK) {
        return GenerateHttpResponse(filterResponse.first, filterResponse.second);
    }
    std::string tenantID;
    if (const auto status(GetValueFromHeaderMap(request.headers, HEADER_TENANT_ID_KEY, tenantID)); status.IsError()) {
        YRLOG_ERROR("no {} in header from {}, err: {}", HEADER_TENANT_ID_KEY,
                    request.client.IsSome() ? request.client.Get() : "unknown ip", status.ToString());
        return GenerateHttpResponse(litebus::http::ResponseCode::BAD_REQUEST,
                                    "abandon aksk failed, parse TenantID from headerMap failed.");
    }
    return internalIAM_->AbandonAKSKByTenantID(tenantID).Then([tenantID, request](const Status &status) {
        if (status.IsError()) {
            YRLOG_ERROR("{}|AbandonAKSK failed by {}, err is {}", tenantID,
                        request.client.IsSome() ? request.client.Get() : "unknown ip", status.ToString());
            return GenerateHttpResponse(litebus::http::ResponseCode::NOT_FOUND,
                                        "abandon aksk failed, " + status.ToString());
        }
        YRLOG_DEBUG("{}|AbandonAKSK success by {}", tenantID,
                    request.client.IsSome() ? request.client.Get() : "unknown ip");
        return GenerateHttpResponse(litebus::http::ResponseCode::OK,
                                    litebus::http::Response::GetStatusDescribe(litebus::http::ResponseCode::OK));
    });
}

std::pair<litebus::http::ResponseCode, std::string> IAMActor::RequestFilter(const std::string &methodName,
                                                                            const InternalIAM::IAMCredType &type,
                                                                            const HttpRequest &request)
{
    if (!VerifyRequest(request)) {
        YRLOG_DEBUG("{}: verifyRequest Failed from {}", methodName,
                    request.client.IsSome() ? request.client.Get() : "unknown ip");
        return std::make_pair(litebus::http::ResponseCode::FORBIDDEN, "verify failed");
    }
    if (request.method != "GET") {
        YRLOG_ERROR("{}: expecting GET but receive invalid method '{}' from {}", methodName, request.method,
                    request.client.IsSome() ? request.client.Get() : "unknown ip");
        return std::make_pair(litebus::http::ResponseCode::METHOD_NOT_ALLOWED,
                              "method is not allowed: " + request.method);
    }
    ASSERT_IF_NULL(internalIAM_);
    if (!internalIAM_->IsIAMEnabled()) {
        YRLOG_DEBUG("{}: internal iam is not enabled from {}", methodName,
                    request.client.IsSome() ? request.client.Get() : "unknown ip");
        return std::make_pair(litebus::http::ResponseCode::BAD_REQUEST, "iam is not enabled");
    }
    if (internalIAM_->GetIAMCredType() != type) {
        YRLOG_ERROR("internal iam is not launched with auth mode: {} from {}", static_cast<int>(type),
                    request.client.IsSome() ? request.client.Get() : "unknown ip");
        return std::make_pair(litebus::http::ResponseCode::BAD_REQUEST, "iam cred type is error");
    }
    return std::make_pair(litebus::http::ResponseCode::OK, "");
}

void IAMActor::BindInternalIAM(const std::shared_ptr<InternalIAM> &internalIAM)
{
    ASSERT_IF_NULL(internalIAM);
    internalIAM_ = internalIAM;
}

bool IAMActor::VerifyRequest(const HttpRequest &request)
{
    if (!useAKSK_) {
        return true;
    }
    std::shared_ptr<std::map<std::string, std::string>> queries =
            std::make_shared<std::map<std::string, std::string>>();
    for (const auto &pair: request.url.query) {
        queries->insert(pair);
    }
    std::map<std::string, std::string> headers = std::map<std::string, std::string>();
    for (const auto &pair: request.headers) {
        headers.insert(pair);
    }
    return VerifyHttpRequest(SignRequest(request.method, request.url.path, queries, headers, request.body),
                             authKey_);
}

void IAMActor::SetAuthKey()
{
    auto enableAKSK = litebus::os::GetEnv(litebus::os::LITEBUS_AKSK_ENABLED);
    if (enableAKSK.IsNone() || enableAKSK.Get() != "1") {
        return;
    }
    auto tmpAk = litebus::os::GetEnv(litebus::os::LITEBUS_ACCESS_KEY);
    auto tmpSK = litebus::os::GetEnv(litebus::os::LITEBUS_SECRET_KEY);
    auto tmpDK = litebus::os::GetEnv(LITEBUS_DATA_KEY);
    if (tmpAk.IsSome() && tmpSK.IsSome() && tmpDK.IsSome()) {
        useAKSK_ = true;
        authKey_ = KeyForAKSK(tmpAk.Get(), SensitiveValue(tmpSK.Get()), SensitiveValue(tmpDK.Get()));
    } else {
        YRLOG_WARN("Failed to obtain the secret key when 2fa enabled");
    }
}

void IAMActor::OnHealthyStatus(const Status &status)
{
    ASSERT_IF_NULL(internalIAM_);
    if (status.IsError()) {
        return;
    }
    YRLOG_DEBUG("metastore is recovered. sync iam token info from metastore");
    if (internalIAM_->GetIAMCredType() == InternalIAM::IAMCredType::TOKEN) {
        // for token sync
        (void)internalIAM_->SyncToReplaceToken(true);
        // for token that is about to expire
        (void)internalIAM_->SyncToReplaceToken(false);
    } else if (internalIAM_->GetIAMCredType() == InternalIAM::IAMCredType::AK_SK) {
        (void)internalIAM_->SyncToReplaceAKSK(true);
        // for aksk that is about to expire
        (void)internalIAM_->SyncToReplaceAKSK(false);
    }
}

void IAMMetaStoreObserver::OnHealthyStatus(const Status &status)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &IAMActor::OnHealthyStatus, status);
}
}  // namespace functionsystem::iamserver