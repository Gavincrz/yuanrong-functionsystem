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

#ifndef IAM_SERVER_IAM_ACTOR_IAM_ACTOR_H
#define IAM_SERVER_IAM_ACTOR_IAM_ACTOR_H

#include <utility>

#include "common/http/api_router_register.h"
#include "common/aksk/aksk_util.h"
#include "common/meta_store_adapter/meta_store_healthy_observer.h"
#include "iam/internal_iam/internal_iam.h"

namespace functionsystem::iamserver {

class IAMActor : public ApiRouterRegister, public litebus::ActorBase {
public:
    explicit IAMActor(const std::string &name);

    ~IAMActor() override = default;

    litebus::Future<HttpResponse> VerifyToken(const HttpRequest &request);
    litebus::Future<HttpResponse> RequireEncryptToken(const HttpRequest &request);
    litebus::Future<HttpResponse> AbandonToken(const HttpRequest &request);

    litebus::Future<HttpResponse> RequireAKSKByTenantID(const HttpRequest &request);
    litebus::Future<HttpResponse> RequireAKSKByAK(const HttpRequest &request);
    litebus::Future<HttpResponse> AbandonAKSK(const HttpRequest &request);

    std::pair<litebus::http::ResponseCode, std::string> RequestFilter(const std::string &methodName,
                                                                      const InternalIAM::IAMCredType &type,
                                                                      const HttpRequest &request);

    void OnHealthyStatus(const Status &status);
    bool VerifyRequest(const HttpRequest &request);
    void SetAuthKey();
    void BindInternalIAM(const std::shared_ptr<InternalIAM> &internalIAM);

private:
    std::shared_ptr<InternalIAM> internalIAM_;

    bool useAKSK_{false};

    KeyForAKSK authKey_;
};

class IAMMetaStoreObserver : public MetaStoreHealthyObserver {
public:
    explicit IAMMetaStoreObserver(litebus::ActorReference actor) : actor_(std::move(actor))
    {
    }
    ~IAMMetaStoreObserver() override = default;
    void OnHealthyStatus(const Status &status) override;

private:
    litebus::ActorReference actor_;
};
}  // namespace functionsystem::iamserver
#endif // IAM_SERVER_IAM_ACTOR_IAM_ACTOR_H