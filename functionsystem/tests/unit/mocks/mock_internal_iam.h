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


#ifndef UT_MOCKS_MOCK_INTERNAL_IAM_H
#define UT_MOCKS_MOCK_INTERNAL_IAM_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <async/async.hpp>
#include <nlohmann/json.hpp>

#include "async/future.hpp"
#include "async/option.hpp"
#include "common/logs/logging.h"
#include "common/utils/token_transfer.h"
#include "function_proxy/common/iam/internal_iam.h"

namespace functionsystem::test {
using namespace functionsystem::function_proxy;
class MockInternalIAM : public InternalIAM {
public:
    explicit MockInternalIAM(const Param &param) : InternalIAM(param)
    {
    }
    ~MockInternalIAM() override = default;

    MOCK_METHOD(bool, IsIAMEnabled, (), (override));
    MOCK_METHOD(IAMCredType, GetCredType, (), (override));
    // mock token
    MOCK_METHOD(litebus::Future<std::shared_ptr<TokenSalt>>, RequireEncryptToken, (const std::string &tenantID),
                (override));
    MOCK_METHOD(litebus::Future<Status>, VerifyToken, (const std::string &token), (override));
    MOCK_METHOD(Status, Authorize, (AuthorizeParam & authorizeParam), (override));
    MOCK_METHOD(litebus::Future<Status>, AbandonTokenByTenantID, (const std::string &tenantID), (override));
    // mock ak sk
    MOCK_METHOD(litebus::Future<std::shared_ptr<AKSKContent>>, RequireCredentialByTenantID,
                (const std::string &tenantID), (override));
    MOCK_METHOD(litebus::Future<std::shared_ptr<AKSKContent>>, RequireCredentialByAK, (const std::string &accessKey),
                (override));
    MOCK_METHOD(litebus::Future<Status>, AbandonCredentialByTenantID, (const std::string &tenantID), (override));
    MOCK_METHOD(litebus::Future<bool>, IsSystemTenant, (const std::string &tenantID), (override));
};
}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_KUBE_CLIENT_H