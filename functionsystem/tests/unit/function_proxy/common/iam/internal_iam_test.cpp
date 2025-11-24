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

#include <gtest/gtest.h>

#include "httpd/http_connect.hpp"

#include "common/etcd_service/etcd_service_driver.h"
#include "common/utils/files.h"
#include "function_proxy/common/iam/internal_iam.h"
#include "mocks/mock_meta_store_client.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace functionsystem::function_proxy;

namespace {
const std::string DEFAULT_POLICY_PATH = "/home/sn/stupid_internal_iam_secret";
const std::string POLICY_FILE_NAME = "policy";
const std::string RESOURCE_DIR = "/home/sn/resource";
const std::string A_TXT_CONTENT = "f48f9d5a9706088947ac438ebe005aa26c9370579f2231c538b28894a315562182da0eb18002c86728c4cdc0df5efb19e1c2060e93370fd891d4f3d9e5b2b61376643f86d0210ce996446a985759b15112037a5a2f6463cf5fd6afc7ff30fe814bf960eb0c16c5059407c74d6a93a8b3110405cbc935dff672da3b648d62e0d5cecd91bc7063211e6b33210afb6899e8322eabffe167318a5ac5d591aa7579efd37e9e4c7fcf390e97c1151b7c1bf00b4a18764a1a0cac1fda1ea6389b39d755127f0e5bc072e6d5936738be1585535dc63b71ad58686f71c821325009de36bdbac31c1c044845bd1bb41230ec9815695ef3f9e7143a16410113ff3286147a76";
const std::string B_TXT_CONTENT = "5d3da9f432be72b34951c737053eb2c816aaccae2b390d092046288aa5ce2cc5b16529f8197de316303735fbc0c041ccc3885b9be5fef4933b6806febb940b6bb609b3bf1d1501110e3ba62c6d8b2cf4388a08a8e123a3cea96daec619fbca177bdf092461f5701b02e5af83ddf0f6ce40deb279cda3ec7d6805237d229e26e30555f3dd890b7306b42bdef0ca1f963dbe25cd00d75018ab3216fcd3b7002b8a493d015306bf264cca12718890ef11c8d9e54721ebd6bdecab6c7084442f45611f249d9b5d703414770a46380d0b97c0187185241e9b6187c8168414370649fe6e7afef83a0df645424c4b6c0631dc3ef50c30af37eda905a1886ca12474c68a";
const std::string D_TXT_CONTENT = "37a1b37efbb9bb6beadb4446f40aa2c4bcaeb298192fa390ed03ee65bfcd54e55da39bae9961b9fa0d4b89591e41eed835ed01cca315eab75ebaf8a9e7b02287a468ec6d0c61f9f8e4d58dad90fb8a6a13bee7fe4685dbb535bfdb7e76b328d66b4d4bc7aa48791b205d1d2f2ef176f2b5b80a8ddc34ed9514372130eb896bc18745facf059a7fa37ef5e2ef413d0030f5bca581055eb3b3565dca642651cb802530e2e4964ab3c8a37370adfd65c80483398a1a8668caed455deabae0dbae7fb2bcdeeee4c2a2d9431ed93c6527985ef684127691904c799e13f37daeb1cb7ebfb0904d61796362514e521ac0fed682fd952ca3e9ce9a7a4407aaaa44f8aab6";
const std::string E_TXT_CONTENT = "43b0d158d9dcf4ffd416eb4e6a89d1b7a66d595c43329bb5c1c66d5befe33c37f31da53aaf539e43238457c46e1f28339cb9dda461c71c0ea2dba3dc8006684ff0d8d59ee2192582983c155e400d5b7cadcb65bbe682e61d175af54549796e447f3174b95f1f50998ae7785b5c0c359746e1ee6eeb989284fbe9e0f801ce5a7267285afbab7694c0e8434d6b86991298a46039de4d1fbfd824b8337b11c2d0b2f30ed4d46312e315cd9042abddc09ea73169f9e1f5baa496d44ed5cac9659cab076212499ef09a56db69e7444d665195a0562a7c82d176d027b0ecc7f4a26215e003fd463bf3911633baf85ee98f9187357a65ee2869b3d93a3871d830b4034e";

const std::string TEST_TENANT_ID = "TEST_TENANT_ID";
const std::string TEST_INSTANCE_ID_1 = "TEST_INSTANCE_ID_1";
const std::string TEST_INSTANCE_ID_2 = "TEST_INSTANCE_ID_2";

// encrypt once
std::string EncryptToken(std::string &token)
{
    return token;
}
// encrypt twice
std::string EncryptTokenForStorage(std::string &token)
{
    auto now = static_cast<uint64_t>(std::time(nullptr));
    std::string tokenForStorage = token + "+" + std::to_string(now + 720);
    return tokenForStorage;
}

std::shared_ptr<AKSKContent> GenAkSkContent(const std::string &tenantID, const std::string &ak, const std::string &sk,
                                            const std::string &dk)
{
    auto credential = std::make_shared<AKSKContent>();
    auto now = static_cast<uint64_t>(std::time(nullptr));
    credential->tenantID = tenantID;
    credential->expiredTimeStamp = now + 720;
    credential->accessKey = ak;
    credential->secretKey = sk;
    credential->dataKey = dk;
    return credential;
}
}

class MockIAMClient : public IAMClient {
public:
    static std::shared_ptr<MockIAMClient> CreateMockIAMClient(const std::string &iamAddress)
    {
        auto client = std::make_shared<MockIAMClient>();
        client->SetIAMAddress(iamAddress);
        return client;
    }

    MOCK_METHOD(litebus::Future<litebus::http::Response>, LaunchRequest, (const litebus::http::Request &request),
                (override));

private:
    std::string iamAddress_;
};

class InternalIAMTest : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
        testIamBasePath_ = "http://127.0.0.1:" + std::to_string(port);
    }

    void SetUp() override
    {
        auto applePath = RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION + "/" + APPLE;
        auto boyPath = RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION + "/" + BOY;
        auto dogPath = RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION + "/" + DOG;
        auto eggPath = RESOURCE_DIR + "/" + RDO + "/" + ROOT_KEY_VERSION + "/" + EGG;
        if (FileExists(applePath) && FileExists(boyPath) && FileExists(dogPath) && FileExists(eggPath)) {
            isResourceExisted_ = true;
        }
        if (!isResourceExisted_) {
            EXPECT_TRUE(litebus::os::Mkdir(applePath).IsNone());
            TouchFile(litebus::os::Join(applePath, A_TXT));
            EXPECT_TRUE(Write(litebus::os::Join(applePath, A_TXT), A_TXT_CONTENT));

            EXPECT_TRUE(litebus::os::Mkdir(boyPath).IsNone());
            TouchFile(litebus::os::Join(boyPath, B_TXT));
            EXPECT_TRUE(Write(litebus::os::Join(boyPath, B_TXT), B_TXT_CONTENT));

            EXPECT_TRUE(litebus::os::Mkdir(dogPath).IsNone());
            TouchFile(litebus::os::Join(dogPath, D_TXT));
            EXPECT_TRUE(Write(litebus::os::Join(dogPath, D_TXT), D_TXT_CONTENT));

            EXPECT_TRUE(litebus::os::Mkdir(eggPath).IsNone());
            TouchFile(litebus::os::Join(eggPath, E_TXT));
            EXPECT_TRUE(Write(litebus::os::Join(eggPath, E_TXT), E_TXT_CONTENT));
        }

        if (!FileExists(policyPath_)) {
            EXPECT_TRUE(litebus::os::Mkdir(DEFAULT_POLICY_PATH).IsNone());
            TouchFile(policyPath_);
        }
        litebus::os::SetEnv("LITEBUS_AKSK_ENABLED", "1");
        litebus::os::SetEnv("LITEBUS_ACCESS_KEY", "c2b4887bb63e71b88773503708d625d8f0662ca18e513972bfcc8ffcfc7e3ed2");
        litebus::os::SetEnv("LITEBUS_SECRET_KEY", SensitiveValue("16bf66680df37fd9d5ff6f781b50c53c463f4af0bb87c72c102aa7dde71da7cf").GetData());
        litebus::os::SetEnv(LITEBUS_DATA_KEY, SensitiveValue("0D2F1C6AEA8AFF3204128DFFD7CEC95471D1149DB628DF5267FDA5233C1D2F05").GetData());
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        metaStorageAccessor_ = std::make_shared<MetaStorageAccessor>(metaStoreClient_);
        auto iamClient = IAMClient::CreateIAMClient(testIamBasePath_);
        internalIAM_ = std::make_shared<InternalIAM>(param_);
        internalIAM_->BindMetaStorageAccessor(metaStorageAccessor_);
        internalIAM_->BindIAMClient(iamClient);
        EXPECT_TRUE(internalIAM_->Init());
        internalIAMCred_ = std::make_shared<InternalIAM>(credParam_);
        internalIAMCred_->BindMetaStorageAccessor(metaStorageAccessor_);
        internalIAMCred_->BindIAMClient(iamClient);
        EXPECT_TRUE(internalIAMCred_->Init());
    }

    void TearDown() override
    {
        YRLOG_DEBUG("In TearDown");
        if (FileExists(policyPath_)) {
            EXPECT_TRUE(litebus::os::Rmdir(DEFAULT_POLICY_PATH).IsNone());
        }
        metaStorageAccessor_->metaClient_ = metaStoreClient_;
        internalIAM_ = nullptr;
        internalIAMCred_ = nullptr;
        metaStoreClient_ = nullptr;
        metaStorageAccessor_ = nullptr;
        etcdSrvDriver_->StopServer();
        etcdSrvDriver_ = nullptr;
        litebus::os::UnSetEnv("LITEBUS_AKSK_ENABLED");
        litebus::os::UnSetEnv("LITEBUS_ACCESS_KEY");
        litebus::os::UnSetEnv("LITEBUS_SECRET_KEY");
        litebus::os::UnSetEnv(LITEBUS_DATA_KEY);
        YRLOG_DEBUG("TearDown finish");
    }

    bool isResourceExisted_{ false };
    std::string policyPath_ = litebus::os::Join(DEFAULT_POLICY_PATH, POLICY_FILE_NAME);
    InternalIAM::Param param_{ true, policyPath_ };
    InternalIAM::Param credParam_{ true, policyPath_, "", IAMCredType::AK_SK};
    std::shared_ptr<InternalIAM> internalIAM_;
    std::shared_ptr<InternalIAM> internalIAMCred_;
    std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_;
    std::string metaStoreServerHost_;
    inline static std::string testIamBasePath_;
};

using ::testing::Return;

/**
 * Feature: InternalIAMTest--InitTest
 * Description: test InternalIAM Init interface
 * Steps:
 * 1. test Init interface without enable iam, failed
 * 2. test Init interface with sync token from metastore, check token counts in token map
 */
TEST_F(InternalIAMTest, InitTest)  // NOLINT
{
    std::string initToken = "init_token";
    std::string encryptToken = EncryptToken(initToken);
    std::string encryptTokenForStorage = EncryptTokenForStorage(encryptToken);
    TokenSalt tokenSalt{.token = encryptTokenForStorage, .salt = ""};
    EXPECT_TRUE(metaStoreClient_->Put("/yr/iam/token/new//" + TEST_TENANT_ID, TransToJsonFromTokenSalt(tokenSalt), {})
                    .Get()
                    ->status.IsOk());

    auto testIAM = std::make_shared<InternalIAM>(param_);
    auto iamClient = IAMClient::CreateIAMClient(testIamBasePath_);
    testIAM->BindMetaStorageAccessor(metaStorageAccessor_);
    testIAM->BindIAMClient(iamClient);
    EXPECT_TRUE(testIAM->Init());
    sleep(1);
    EXPECT_TRUE(testIAM->IsIAMEnabled());
    EXPECT_EQ(testIAM->syncTokenActor_->newTokenMap_.size(), static_cast<uint32_t>(1));
    YRLOG_DEBUG("InitTest finish");
}

/**
 * Feature: InternalIAMTest--RequireVerifyAbandonTokenTest
 * Description: test InternalIAM VerifyToken RequireEncryptToken AbandonTokenByTenantID interface
 * Steps:
 * 1. RequireEncryptToken, no token cache, ret code error, failed
 * 2. RequireEncryptToken, no token cache, no token key, failed
 * 3. RequireEncryptToken, no token cache, success
 * 4. RequireEncryptToken, in token cache, success
 * 5. VerifyToken, in token cache, success
 * 5. VerifyToken, no token cache, failed
 * 6. AbandonToken, in token cache, success
 */
TEST_F(InternalIAMTest, RequireVerifyAbandonTokenTest)  // NOLINT
{
    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAM_->BindIAMClient(testIamClient);
    internalIAM_->syncTokenActor_->BindIAMClient(testIamClient);

    auto now = static_cast<uint64_t>(std::time(nullptr));
    auto response1 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };
    auto response2 = litebus::http::Response{ litebus::http::ResponseCode::OK };
    response2.headers["X-Expired-Time-Span"] = "aaaa";
    auto response3 = litebus::http::Response{ litebus::http::ResponseCode::OK };
    response3.headers["X-Auth"] = "encrypt_token";
    response3.headers["X-Expired-Time-Span"] = std::to_string(now - 400);
    auto response4 = litebus::http::Response{ litebus::http::ResponseCode::OK };
    response4.headers["X-Auth"] = "encrypt_token";
    response4.headers["X-Expired-Time-Span"] = std::to_string(now + 400);
    auto response5 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };
    auto response6 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };

    EXPECT_CALL(*testIamClient, LaunchRequest).Times(6).WillOnce(Return(response1)).WillOnce(Return(response2))
        .WillOnce(Return(response3)).WillOnce(Return(response4)).WillOnce(Return(response5)).WillOnce(Return(response6));

    EXPECT_TRUE(internalIAM_->RequireEncryptToken(TEST_TENANT_ID).Get()->token.empty());
    EXPECT_TRUE(internalIAM_->RequireEncryptToken(TEST_TENANT_ID).Get()->token.empty());
    auto tokenSalt = internalIAM_->RequireEncryptToken(TEST_TENANT_ID).Get();
    EXPECT_STREQ(tokenSalt->token.c_str(), "encrypt_token");
    EXPECT_TRUE(tokenSalt->expiredTimeStamp < now);

    auto tokenSalt1 = internalIAM_->RequireEncryptToken(TEST_TENANT_ID).Get();
    EXPECT_STREQ(tokenSalt1->token.c_str(), "encrypt_token");
    EXPECT_TRUE(tokenSalt1->expiredTimeStamp > now);
    EXPECT_EQ(internalIAM_->syncTokenActor_->newTokenMap_.size(), static_cast<uint32_t>(1));

    EXPECT_TRUE(internalIAM_->VerifyToken("encrypt_token").Get().IsOk());
    EXPECT_FALSE(internalIAM_->VerifyToken("err_token").Get().IsOk());

    EXPECT_TRUE(internalIAM_->AbandonTokenByTenantID(TEST_TENANT_ID).Get().IsOk());
    EXPECT_FALSE(internalIAM_->VerifyToken("encrypt_token").Get().IsOk());
}

TEST_F(InternalIAMTest, RequireTokenWhenGoingToExpiredTest)
{
    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAM_->BindIAMClient(testIamClient);
    internalIAM_->syncTokenActor_->BindIAMClient(testIamClient);
    auto now = static_cast<uint64_t>(std::time(nullptr));
    auto response1 = litebus::http::Response{ litebus::http::ResponseCode::OK };
    response1.headers["X-Auth"] = "encrypt_token1";
    response1.headers["X-Expired-Time-Span"] = std::to_string(now + 15);
    auto response2 = litebus::http::Response{ litebus::http::ResponseCode::OK };
    response2.headers["X-Auth"] = "encrypt_token2";
    response2.headers["X-Expired-Time-Span"] = std::to_string(now + 400);

    EXPECT_CALL(*testIamClient, LaunchRequest).Times(2).WillOnce(Return(response1)).WillOnce(Return(response2));
    auto tokenSalt1 = internalIAM_->RequireEncryptToken(TEST_TENANT_ID).Get();
    EXPECT_STREQ(tokenSalt1->token.c_str(), "encrypt_token1");
    auto tokenSalt2 = internalIAM_->RequireEncryptToken(TEST_TENANT_ID).Get();
    EXPECT_STREQ(tokenSalt2->token.c_str(), "encrypt_token2");
}

/**
 * Feature: InternalIAMTest--UpdatePolicyTest
 * Description: test AuthorizeProxy UpdatePolicy interface
 * Steps:
 * 1. call UpdatePolicy with no policy file exists, success
 * 2. call UpdatePolicy with invalid json of policy file, failed
 * 3. call UpdatePolicy with valid format of policy file, success
 **/
TEST_F(InternalIAMTest, UpdatePolicyTest)
{
    if (FileExists(policyPath_)) {
        EXPECT_TRUE(litebus::os::Rmdir(DEFAULT_POLICY_PATH).IsNone());
    }
    AuthorizeProxy authorizeProxy(policyPath_);
    EXPECT_FALSE(authorizeProxy.UpdatePolicy().IsOk());

    EXPECT_TRUE(litebus::os::Mkdir(DEFAULT_POLICY_PATH).IsNone());
    TouchFile(policyPath_);
    std::string emptyStr = R"()";
    EXPECT_TRUE(Write(policyPath_, emptyStr));
    EXPECT_FALSE(authorizeProxy.UpdatePolicy().IsOk());

    std::string invalidPolicyStr = R"({"invalidPolicyStr##"})";
    EXPECT_TRUE(Write(policyPath_, invalidPolicyStr));
    EXPECT_FALSE(authorizeProxy.UpdatePolicy().IsOk());

    if (FileExists(policyPath_)) {
        EXPECT_TRUE(litebus::os::Rm(policyPath_).IsNone());
    }
    TouchFile(policyPath_);
    std::string validPolicyStr = R"({"tenant_group":{"system":{"tenant0":["func1"]}}, "white_list": {}, "policy":{"allow":{"invoke":{"system":{"system": ["*"]}}, "create":{}, "kill":{}}, "deny":{}}})";
    EXPECT_TRUE(Write(policyPath_, validPolicyStr));
    EXPECT_TRUE(authorizeProxy.UpdatePolicy().IsOk());
    if (FileExists(policyPath_)) {
        EXPECT_TRUE(litebus::os::Rmdir(DEFAULT_POLICY_PATH).IsNone());
    }
}

/**
* Feature InternalIAMTest--UpdatePolicyContentTest
* Description: test policyContent UpdatePolicyContent interface
* Steps:
* 1. call UpdatePolicy with empty policy string, success
* 2. call UpdatePolicy with invalid json of policy string, failed
* 3. call UpdatePolicy with valid json but invalid format of policy string, failed
* 4. call UpdatePolicy with valid json and valid format of policy string, success
**/
TEST_F(InternalIAMTest, UpdatePolicyContentTest)
{
    PolicyContent policyContent;
    // empty policy string
    std::string policyStr1 = R"()";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr1).IsOk());
    // invalid json string
    std::string policyStr2 = R"({"invalidPolicyStr##"})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr2).IsOk());
    // valid json string, invalid key
    std::string policyStr3 = R"({"group##":{}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr3).IsOk());

    std::string policyStr4 = R"({"tenant_group":{}, "white_list##":{}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr4).IsOk());

    std::string policyStr5 = R"({"tenant_group":{}, "white_list":{}, "policy##":{}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr5).IsOk());

    std::string policyStr6 = R"({"tenant_group":{}, "white_list":{}, "policy":{"allow##":{}}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr6).IsOk());

    std::string policyStr7 = R"({"tenant_group":{}, "white_list":{}, "policy":{"allow":{}, "deny##":{}}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr7).IsOk());

    std::string policyStr8 = R"({"tenant_group":{}, "white_list":{"": []}, "policy":{"allow":{}, "deny":{}}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr8).IsOk());

    std::string policyStr9 = R"({"tenant_group":{}, "white_list":{}, "policy":{"allow":{"":{}}, "deny":{}}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr9).IsOk());

    std::string policyStr10 = R"({"tenant_group":{}, "white_list":{"func1": []}, "policy":{"allow":{"invalid_method":{}}, "deny":{}}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr10).IsOk());

    std::string policyStr11 = R"({"tenant_group":{}, "white_list":{"func1": []}, "policy":{"allow":{"invoke":{"invalid_group":{"system": ["*"]}}}, "deny":{}}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr11).IsOk());

    std::string policyStr12 = R"({"tenant_group":{"system":{"tenant0":["func1"]}}, "white_list":{"func1": []}, "policy":{"allow":{"invoke":{"system":{"invalid_group": ["*"]}}}, "deny":{}}})";
    EXPECT_FALSE(policyContent.UpdatePolicyContent(policyStr12).IsOk());

    // valid json string
    std::string policyStr13 = R"({"tenant_group":{"system":{"tenant0":["func1"]}}, "white_list":{"func1":["tenant0"], "func2": ["tenant0"]}, "policy":{"allow":{"invoke":{"system":{"system": ["*"]}}, "create":{}, "kill":{}}, "deny":{}}})";
    EXPECT_TRUE(policyContent.UpdatePolicyContent(policyStr13).IsOk());
    std::string policyStr14 = R"({"tenant_group":{"system":{"tenant0":["func1"]}}, "white_list": {"func1":[], "func2":[""]}, "policy":{"allow":{"invoke":{"system":{"system": ["*"]}}, "create":{"system": {"system": ["func1"]}}, "kill":{"system": {"system": [""]}}}, "deny":{"tenant_list":["TEST_TENANT_ID"]}}})";
    EXPECT_TRUE(policyContent.UpdatePolicyContent(policyStr14).IsOk());
    std::string policyStr15 = R"({"tenant_group":{"system":{}, "external": {}}, "white_list": {"func1":[], "func2":[""]}, "policy":{"allow":{"invoke":{"system":{"system": ["*"]}, "external":{"external":["="]}}, "create":{"system": {"system": []}}, "kill":{"system": {"system": [""]}}}, "deny":{"tenant_list":["TEST_TENANT_ID"]}}})";
    Status status = policyContent.UpdatePolicyContent(policyStr15);
    EXPECT_TRUE(status.IsOk());
}

/**
 * Feature InternalIAMTest--AuthorizeTest
 * Description: test AuthorizeProxy Authorize interface
 * Steps:
 * 1. satisfy allow rule, success
 * 2. satisfy deny rule, failed
 */
TEST_F(InternalIAMTest, AuthorizeTest)
{
    if (FileExists(policyPath_)) {
        EXPECT_TRUE(litebus::os::Rmdir(DEFAULT_POLICY_PATH).IsNone());
    }
    AuthorizeProxy authorizeProxy(policyPath_);
    EXPECT_TRUE(litebus::os::Mkdir(DEFAULT_POLICY_PATH).IsNone());
    TouchFile(policyPath_);
    std::string policyStr = R"({"tenant_group":{"system":{"tenant0":["func1"]}}, "white_list":{"func1":["tenant0"], "func2": ["tenant0"]}, "policy":{"allow":{"invoke":{"system":{"system": ["*"]}}, "create":{}, "kill":{}}, "deny":{}}})";
    EXPECT_TRUE(Write(policyPath_, policyStr));
    EXPECT_TRUE(authorizeProxy.UpdatePolicy().IsOk());

    AuthorizeParam authorizeParam1{
        .callerTenantID = "1",
        .calleeTenantID = "1",
        .callMethod = "invoke",
        .funcName = "func1"
    };
    EXPECT_FALSE(authorizeProxy.Authorize(authorizeParam1).IsOk());
    authorizeParam1 = {
        .callerTenantID = "tenant0",
        .calleeTenantID = "tenant0",
        .callMethod = "invoke",
        .funcName = "func1"
    };
    EXPECT_TRUE(authorizeProxy.Authorize(authorizeParam1).IsOk());
}

/**
* Feature InternalIAMTest--HandleAuthorizeRequestTest
* Description: test PolicyHandler HandleAuthorizeRequest interface
* Steps:
* 1. empty policy, success
* 2. deny rules satisfied, failed
* 3. deny rules not satisfied, allow rules not satisfied, failed
* 4. deny rules not satisfied, allow rules satisfied, success
**/
TEST_F(InternalIAMTest, HandleAuthorizeRequestTest)
{
    PolicyHandler policyHandler;
    auto policyContent = std::make_shared<PolicyContent>();
    AuthorizeParam authorizeParam1{
        .callerTenantID = TEST_TENANT_ID,
        .calleeTenantID = TEST_TENANT_ID,
        .callMethod = "invoke",
        .funcName = "func1"
    };
    // empty policy
    std::string policyStr1 = R"()";
    EXPECT_FALSE(policyContent->UpdatePolicyContent(policyStr1).IsOk());
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam1, policyContent).IsOk());

    std::string policyStr2 = R"({
        "tenant_group": {
            "system": {
                "0": ["func0"]
            },
            "external": {}
        },
        "white_list": {
            "func0": ["1"]
        },
        "policy": {
            "allow": {
                "invoke": {
                    "system": {
                        "system": ["*"],
                        "external": ["*"]
                    },
                    "external": {
                        "external": ["white_list","=","func1"]
                    }
                },
                "create": {
                    "system": {
                        "system": ["*"],
                        "external": ["*"]
                    }
                },
                "kill": {
                    "system": {
                        "system": ["*"],
                        "external": ["*"]
                    },
                    "external": {
                        "external": ["white_list","func1"]
                    }
                }
            },
            "deny": {
                "tenant_list": ["4"]
            }
        }
    })";
    EXPECT_TRUE(policyContent->UpdatePolicyContent(policyStr2).IsOk());

    AuthorizeParam authorizeParam5;

    authorizeParam5 = {
        .callerTenantID = "",
        .calleeTenantID = "0",
        .callMethod = "invoke",
        .funcName = "func0"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());

    authorizeParam5 = {
        .callerTenantID = "0",
        .calleeTenantID = "",
        .callMethod = "invoke",
        .funcName = "func0"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());

    authorizeParam5 = {
        .callerTenantID = "0",
        .calleeTenantID = "0",
        .callMethod = "",
        .funcName = "func0"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());

    authorizeParam5 = {
        .callerTenantID = "0",
        .calleeTenantID = "0",
        .callMethod = "invoke",
        .funcName = ""
    };
    EXPECT_TRUE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());

    authorizeParam5 = {
        .callerTenantID = "0",
        .calleeTenantID = "0",
        .callMethod = "invalid_method",
        .funcName = "func0"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());

    authorizeParam5 = {
        .callerTenantID = "4",
        .calleeTenantID = "0",
        .callMethod = "invoke",
        .funcName = "func0"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());

    authorizeParam5 = {
        .callerTenantID = "0",
        .calleeTenantID = "0",
        .callMethod = "create",
        .funcName = "func0"
    };
    EXPECT_TRUE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());

    authorizeParam5 = {
        .callerTenantID = "1",
        .calleeTenantID = "0",
        .callMethod = "create",
        .funcName = "func0"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());
    authorizeParam5 = {
        .callerTenantID = "1",
        .calleeTenantID = "0",
        .callMethod = "invoke",
        .funcName = "func0"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());
    authorizeParam5 = {
        .callerTenantID = "2",
        .calleeTenantID = "1",
        .callMethod = "invoke",
        .funcName = "func0"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());
    authorizeParam5 = {
        .callerTenantID = "1",
        .calleeTenantID = "1",
        .callMethod = "invoke",
        .funcName = "func0"
    };
    EXPECT_TRUE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());
    authorizeParam5 = {
        .callerTenantID = "2",
        .calleeTenantID = "2",
        .callMethod = "invoke",
        .funcName = "func2"
    };
    EXPECT_TRUE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());
    authorizeParam5 = {
        .callerTenantID = "2",
        .calleeTenantID = "3",
        .callMethod = "invoke",
        .funcName = "func1"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());
    authorizeParam5 = {
        .callerTenantID = "2",
        .calleeTenantID = "3",
        .callMethod = "kill",
        .funcName = "func1"
    };
    EXPECT_TRUE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());
    authorizeParam5 = {
        .callerTenantID = "2",
        .calleeTenantID = "3",
        .callMethod = "kill",
        .funcName = "func2"
    };
    EXPECT_FALSE(policyHandler.HandleAuthorizeRequest(authorizeParam5, policyContent).IsOk());
}

/**
 * Feature: InternalIAMTest--VerifiedInstanceTest
 * Description: test InternalIAM verifiedInstance member
 * Steps:
 * 1. Insert Delete instance, check verifiedInstance size
 * 2. Insert Delete instance, check instance verified or not
 */
TEST_F(InternalIAMTest, VerifiedInstanceTest)
{
    EXPECT_EQ(internalIAM_->GetVerifiedInstanceSize(), static_cast<uint32_t>(0));
    internalIAM_->Insert(TEST_INSTANCE_ID_1);
    internalIAM_->Insert(TEST_INSTANCE_ID_2);
    EXPECT_EQ(internalIAM_->GetVerifiedInstanceSize(), static_cast<uint32_t>(2));
    EXPECT_TRUE(internalIAM_->VerifyInstance(TEST_INSTANCE_ID_1));
    EXPECT_TRUE(internalIAM_->VerifyInstance(TEST_INSTANCE_ID_2));
    internalIAM_->Delete(TEST_INSTANCE_ID_1);
    internalIAM_->Delete(TEST_INSTANCE_ID_2);
    EXPECT_EQ(internalIAM_->GetVerifiedInstanceSize(), static_cast<uint32_t>(0));
    EXPECT_FALSE(internalIAM_->VerifyInstance(TEST_INSTANCE_ID_1));
    EXPECT_FALSE(internalIAM_->VerifyInstance(TEST_INSTANCE_ID_2));
}

/**
 * Feature: InternalIAMTest--SyncTokenTest
 * Description: test SyncPutToken interface
 * 1. no token in cache, verify token failed
 * 2. put token into metastore, verify token success
 * 3. delete token from metastore, verify token failed
 */
TEST_F(InternalIAMTest, SyncTokenTest)
{
    metaStoreClient_->Delete("/yr/iam/token/", { false, true }).Get();
    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAM_->BindIAMClient(testIamClient);
    internalIAM_->syncTokenActor_->BindIAMClient(testIamClient);
    auto response1 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };

    EXPECT_CALL(*testIamClient, LaunchRequest).Times(1).WillOnce(Return(response1));

    EXPECT_FALSE(internalIAM_->VerifyToken("encrypt_token").Get().IsOk());

    std::string initToken = "test_token";
    std::string encryptToken = EncryptToken(initToken);
    YRLOG_DEBUG("encryptToken is: {}", encryptToken);
    std::string encryptTokenForStorage = EncryptTokenForStorage(encryptToken);

    TokenSalt tokenSalt{.token = encryptTokenForStorage, .salt = ""};
    EXPECT_TRUE(metaStoreClient_->Put("/yr/iam/token/new//" + TEST_TENANT_ID, TransToJsonFromTokenSalt(tokenSalt), {})
                    .Get()
                    ->status.IsOk());
    EXPECT_AWAIT_TRUE([&]() -> bool { return internalIAM_->syncTokenActor_->newTokenMap_.size() == 1;});
    EXPECT_EQ(internalIAM_->syncTokenActor_->newTokenMap_.size(), static_cast<uint32_t>(1));
    auto tokenSaltPtr = internalIAM_->RequireEncryptToken(TEST_TENANT_ID).Get();
    EXPECT_TRUE(internalIAM_->VerifyToken(tokenSaltPtr->token).Get().IsOk());
    metaStoreClient_->Delete("/yr/iam/token/", { false, true }).Get();
    ASSERT_AWAIT_TRUE([&]() -> bool { return internalIAM_->syncTokenActor_->newTokenMap_.size() == 0;});
}

TEST_F(InternalIAMTest, IAMTokenSyncerTest)
{
    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAM_->BindIAMClient(testIamClient);
    internalIAM_->syncTokenActor_->BindIAMClient(testIamClient);
    auto response1 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };

    EXPECT_CALL(*testIamClient, LaunchRequest).Times(1).WillOnce(Return(response1));

    EXPECT_FALSE(internalIAM_->VerifyToken("encrypt_token").Get().IsOk());

    std::string initToken = "test_token";
    std::string encryptToken = EncryptToken(initToken);
    YRLOG_DEBUG("encryptToken is: {}", encryptToken);
    std::string encryptTokenForStorage = EncryptTokenForStorage(encryptToken);

    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;

    {
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status(StatusCode::FAILED, "");
        auto future = internalIAM_->syncTokenActor_->IAMTokenSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());
    }

    {
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        auto future = internalIAM_->syncTokenActor_->IAMTokenSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
    }

    {
        TokenSalt tokenSalt{.token = encryptTokenForStorage, .salt = ""};
        auto iamKey = "/yr/iam/token/new//" + TEST_TENANT_ID;
        auto iamValue = TransToJsonFromTokenSalt(tokenSalt);

        KeyValue getKeyValue;
        getKeyValue.set_key(iamKey) ;
        getKeyValue.set_value(iamValue);

        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        rep->kvs.emplace_back(getKeyValue);

        EXPECT_EQ(internalIAM_->syncTokenActor_->newTokenMap_.size(), static_cast<uint32_t>(0));

        auto future = internalIAM_->syncTokenActor_->IAMTokenSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());

        EXPECT_EQ(internalIAM_->syncTokenActor_->newTokenMap_.size(), static_cast<uint32_t>(1));
        auto tokenSaltPtr = internalIAM_->RequireEncryptToken(TEST_TENANT_ID).Get();
        EXPECT_TRUE(internalIAM_->VerifyToken(tokenSaltPtr->token).Get().IsOk());
    }
}

TEST_F(InternalIAMTest, CheckTokenExpiredInAdvanceTest)
{
    std::string initToken = "test_token";
    std::string encryptToken = EncryptToken(initToken);
    auto tokenSalt = std::make_shared<TokenSalt>();
    tokenSalt->token = encryptToken;
    tokenSalt->salt = "salt";
    tokenSalt->expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) + 120;

    auto tokenSalt1 = std::make_shared<TokenSalt>();
    tokenSalt1->token = encryptToken;
    tokenSalt1->salt = "salt";
    auto expire1 = static_cast<uint64_t>(std::time(nullptr)) + 300;
    tokenSalt1->expiredTimeStamp = expire1;

    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAM_->BindIAMClient(testIamClient);
    internalIAM_->syncTokenActor_->BindIAMClient(testIamClient);
    auto response0 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };
    auto response1 = litebus::http::Response{ litebus::http::ResponseCode::OK };
    response1.headers["X-Auth"] = "encrypt_token";
    auto newExpired = static_cast<uint64_t>(std::time(nullptr)) + 600;
    response1.headers["X-Expired-Time-Span"] = std::to_string(newExpired);
    response1.headers["X-Salt"] = "salt1";
    EXPECT_CALL(*testIamClient, LaunchRequest).Times(2).WillOnce(Return(response0)).WillOnce(Return(response1));
    internalIAM_->syncTokenActor_->newTokenMap_["tenant001"] = tokenSalt;
    internalIAM_->syncTokenActor_->newTokenMap_["tenant002"] = tokenSalt1;
    internalIAM_->syncTokenActor_->CheckTokenExpiredInAdvance();
    internalIAM_->syncTokenActor_->CheckTokenExpiredInAdvance();
    ASSERT_AWAIT_TRUE(
        [&]() -> bool { return internalIAM_->syncTokenActor_->newTokenMap_["tenant001"]->expiredTimeStamp == newExpired;});
    EXPECT_TRUE( internalIAM_->syncTokenActor_->newTokenMap_["tenant002"]->expiredTimeStamp == expire1);
}

TEST_F(InternalIAMTest, InitWithCredentialTest)  // NOLINT
{
    auto credential = GenAkSkContent("tenant1", "access1", "secret1", "data1");
    auto encCredential = EncryptAKSKContentForStorage(credential);
    EXPECT_TRUE(metaStoreClient_->Put("/yr/iam/aksk/new//tenant1", TransToJsonFromEncAKSKContent(encCredential), {})
                    .Get()
                    ->status.IsOk());

    auto testIAM = std::make_shared<InternalIAM>(credParam_);
    auto iamClient = IAMClient::CreateIAMClient(testIamBasePath_);
    testIAM->BindMetaStorageAccessor(metaStorageAccessor_);
    testIAM->BindIAMClient(iamClient);
    EXPECT_TRUE(testIAM->Init());
    ASSERT_AWAIT_TRUE(
        [&]() -> bool { return testIAM->credentialActor_->newCredMap_.size() == 2;});
    EXPECT_TRUE(testIAM->IsIAMEnabled());
    YRLOG_DEBUG("InitTest finish");
}

TEST_F(InternalIAMTest, AbandonToken)  // NOLINT
{
    auto client = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    auto response = litebus::http::Response{ litebus::http::ResponseCode::OK };
    EXPECT_CALL(*client, LaunchRequest).WillOnce(Return(response));
    auto future = client->AbandonToken("x-tenant-id");
    EXPECT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get().IsOk());
}

TEST_F(InternalIAMTest, RequireCredential_Unreachable)  // NOLINT
{
    auto client = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    client->useAKSK_ = true;

    litebus::Status response{ litebus::http::HttpErrorCode::CONNECTION_REFUSED };
    EXPECT_CALL(*client, LaunchRequest).WillOnce(Return(response));

    auto credential = client->RequireEncryptCredential("x-tenant-id").Get();
    EXPECT_EQ(credential->status.StatusCode(), StatusCode::ERR_INNER_SYSTEM_ERROR);
}

TEST_F(InternalIAMTest, AbandonCredential)  // NOLINT
{
    auto client = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    {
        auto response = litebus::http::Response{ litebus::http::ResponseCode::OK };
        EXPECT_CALL(*client, LaunchRequest).WillOnce(Return(response));
        auto future = client->AbandonCredential("x-tenant-id");
        EXPECT_AWAIT_READY(future);
        EXPECT_TRUE(future.Get().IsOk());
    }

    {
        auto response = litebus::http::Response{ litebus::http::ResponseCode(500) };
        EXPECT_CALL(*client, LaunchRequest).WillOnce(Return(response));
        auto future = client->AbandonCredential("x-tenant-id");
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), ERR_INNER_SYSTEM_ERROR);
    }

    {
        auto response = litebus::http::Response{ litebus::http::ResponseCode(110) };
        EXPECT_CALL(*client, LaunchRequest).WillOnce(Return(response));
        auto future = client->AbandonCredential("x-tenant-id");
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), ERR_INNER_SYSTEM_ERROR);
    }

    {
        auto response = litebus::http::Response{ litebus::http::ResponseCode(400) };
        EXPECT_CALL(*client, LaunchRequest).WillOnce(Return(response));
        auto future = client->AbandonCredential("x-tenant-id");
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.Get().StatusCode(), FAILED);
    }
}

TEST_F(InternalIAMTest, RequireVerifyAbandonCredentialWithCredentialTest)  // NOLINT
{
    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAMCred_->BindIAMClient(testIamClient);
    internalIAMCred_->credentialActor_->BindIAMClient(testIamClient);
    internalIAMCred_->credentialActor_->requestRetryInterval_ = 1;
    internalIAMCred_->credentialActor_->requestMaxRetryTimes_ = 1;
    auto  persistentCounter = std::make_shared<std::atomic<int>>(0);
    internalIAMCred_->RegisterUpdateCredentialCallback([cnt(persistentCounter)](const std::string &tenantID, const std::shared_ptr<AKSKContent> &content){
        (*cnt)++;
    });

    {
        // require credential err response
        litebus::Promise<litebus::http::Response> promise1;
        promise1.SetFailed(-1);
        litebus::Promise<litebus::http::Response> promise2;
        promise2.SetFailed(-1);
        auto response1 = litebus::http::Response{ litebus::http::ResponseCode::INTERNAL_SERVER_ERROR };
        auto response2 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };
        EXPECT_CALL(*testIamClient, LaunchRequest).Times(5).WillOnce(Return(promise1.GetFuture())).WillOnce(Return(promise2.GetFuture()))
            .WillOnce(Return(response1)).WillOnce(Return(response1))
            .WillOnce(Return(response2));
        EXPECT_EQ(internalIAMCred_->RequireCredentialByTenantID(TEST_TENANT_ID).Get()->status.StatusCode(), StatusCode::ERR_INNER_SYSTEM_ERROR);
        EXPECT_EQ(internalIAMCred_->RequireCredentialByTenantID(TEST_TENANT_ID).Get()->status.StatusCode(), StatusCode::ERR_INNER_SYSTEM_ERROR);
        EXPECT_EQ(internalIAMCred_->RequireCredentialByTenantID(TEST_TENANT_ID).Get()->status.StatusCode(), StatusCode::FAILED);
    }
    {
        // verify credential err response
        litebus::Promise<litebus::http::Response> promise1;
        promise1.SetFailed(-1);
        litebus::Promise<litebus::http::Response> promise2;
        promise2.SetFailed(-1);
        auto response1 = litebus::http::Response{ litebus::http::ResponseCode::INTERNAL_SERVER_ERROR };
        auto response2 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };
        EXPECT_CALL(*testIamClient, LaunchRequest).Times(5).WillOnce(Return(promise1.GetFuture())).WillOnce(Return(promise2.GetFuture()))
            .WillOnce(Return(response1)).WillOnce(Return(response1))
            .WillOnce(Return(response2));
        EXPECT_EQ(internalIAMCred_->RequireCredentialByAK("ak1").Get()->status.StatusCode(), StatusCode::ERR_INNER_SYSTEM_ERROR);
        EXPECT_EQ(internalIAMCred_->RequireCredentialByAK("ak1").Get()->status.StatusCode(), StatusCode::ERR_INNER_SYSTEM_ERROR);
        EXPECT_EQ(internalIAMCred_->RequireCredentialByAK("ak1").Get()->status.StatusCode(), StatusCode::FAILED);
    }
    {
        // request but body is error
        auto response1 = litebus::http::Response{ litebus::http::ResponseCode::OK };
        response1.body = R"({)";
        // expireStamp is error
        auto response2 = litebus::http::Response{ litebus::http::ResponseCode::OK };
        response2.body = R"({"tenantID":"tenant1", "accessKey":"ak", "secretKey":"sk", "dataKey":"", "expiredTimeStamp": "aaa" })";
        // expired
        auto credential = GenAkSkContent("tenant1", "access1", "secret1", "data1");
        auto encCredential = EncryptAKSKContentForStorage(credential);
        encCredential->expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) - 10;
        auto response5 = litebus::http::Response{ litebus::http::ResponseCode::OK };
        response5.body = TransToJsonFromEncAKSKContent(encCredential);

        EXPECT_CALL(*testIamClient, LaunchRequest).Times(3).WillOnce(Return(response1)).WillOnce(Return(response2)).WillOnce(Return(response5));
        EXPECT_EQ(internalIAMCred_->RequireCredentialByTenantID(TEST_TENANT_ID).Get()->status.StatusCode(), StatusCode::PARAMETER_ERROR);
        EXPECT_EQ(internalIAMCred_->RequireCredentialByTenantID(TEST_TENANT_ID).Get()->status.StatusCode(), StatusCode::PARAMETER_ERROR);

        EXPECT_TRUE(internalIAMCred_->RequireCredentialByTenantID(TEST_TENANT_ID).Get()->status.ToString().find("aksk expired time stamp is earlier") != std::string::npos);
    }
    {
        // require success
        auto credential = GenAkSkContent("tenant1", "access1", "secret1", "data1");
        auto encCredential = EncryptAKSKContentForStorage(credential);
        auto response = litebus::http::Response{ litebus::http::ResponseCode::OK };
        response.body = TransToJsonFromEncAKSKContent(encCredential);
        EXPECT_CALL(*testIamClient, LaunchRequest).Times(1).WillOnce(Return(response));
        auto akSk = internalIAMCred_->RequireCredentialByTenantID("tenant1").Get();
        EXPECT_TRUE(akSk->status.IsOk());
        EXPECT_EQ(akSk->tenantID, "tenant1");
        EXPECT_EQ(akSk->accessKey, "access1");
        EXPECT_EQ(std::string(akSk->secretKey.GetData()), "secret1");
        EXPECT_EQ(std::string(akSk->dataKey.GetData()), "data1");
        EXPECT_EQ(*persistentCounter, 0);
        // return from local
        auto akSk1 = internalIAMCred_->RequireCredentialByTenantID("tenant1").Get();
        EXPECT_EQ(std::string(akSk1->secretKey.GetData()), "secret1");
        // request by ak
        auto akSk2 = internalIAMCred_->RequireCredentialByAK("access1").Get();
        EXPECT_EQ(std::string(akSk2->secretKey.GetData()), "secret1");

        auto isSystemTenant = internalIAMCred_->IsSystemTenant("tenant1");
        ASSERT_AWAIT_READY(isSystemTenant);
        EXPECT_FALSE(isSystemTenant.Get());

        auto status = internalIAMCred_->AbandonCredentialByTenantID("tenant1").Get();
        EXPECT_TRUE(status.IsOk());
        EXPECT_TRUE(internalIAMCred_->credentialActor_->newCredMap_.find("tenant1")
                    == internalIAMCred_->credentialActor_->newCredMap_.end());
        EXPECT_TRUE(internalIAMCred_->credentialActor_->accessKey2TenantIDMap_.find("access1")
                    == internalIAMCred_->credentialActor_->accessKey2TenantIDMap_.end());
    }
    {
        // verify success
        auto credential = GenAkSkContent("tenant1", "access1", "secret1", "data1");
        auto encCredential = EncryptAKSKContentForStorage(credential);
        auto response = litebus::http::Response{ litebus::http::ResponseCode::OK };
        response.body = TransToJsonFromEncAKSKContent(encCredential);
        EXPECT_CALL(*testIamClient, LaunchRequest).Times(1).WillOnce(Return(response));
        auto akSk = internalIAMCred_->RequireCredentialByAK("access1").Get();
        EXPECT_TRUE(akSk->status.IsOk());
        EXPECT_EQ(akSk->tenantID, "tenant1");
        EXPECT_EQ(akSk->accessKey, "access1");
        EXPECT_EQ(std::string(akSk->secretKey.GetData()), "secret1");
        EXPECT_EQ(std::string(akSk->dataKey.GetData()), "data1");
    }
    {
        // require credential, and going to be expired, need to require new
        auto credential1 = GenAkSkContent("tenant11", "access1", "secret1", "data1");
        auto encCredential1 = EncryptAKSKContentForStorage(credential1);
        encCredential1->expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) + 20;
        auto response1 = litebus::http::Response{ litebus::http::ResponseCode::OK };
        response1.body = TransToJsonFromEncAKSKContent(encCredential1);

        auto credential2 = GenAkSkContent("tenant11", "access2", "secret1", "data1");
        auto encCredential2 = EncryptAKSKContentForStorage(credential2);
        encCredential2->expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) + 100;
        auto response2 = litebus::http::Response{ litebus::http::ResponseCode::OK };
        response2.body = TransToJsonFromEncAKSKContent(encCredential2);
        EXPECT_CALL(*testIamClient, LaunchRequest).Times(2).WillOnce(Return(response1)).WillOnce(Return(response2));
        auto akSk1 = internalIAMCred_->RequireCredentialByTenantID("tenant11").Get();
        auto akSk2 = internalIAMCred_->RequireCredentialByTenantID("tenant11").Get();
        EXPECT_TRUE(akSk1->accessKey !=  akSk2->accessKey);
        EXPECT_EQ(*persistentCounter, 1);
    }
}

TEST_F(InternalIAMTest, SyncCredentialTest)
{
    metaStoreClient_->Delete("/yr/iam/aksk/", { false, true }).Get();
    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAMCred_->BindIAMClient(testIamClient);
    internalIAMCred_->credentialActor_->BindIAMClient(testIamClient);
    auto credential1 = GenAkSkContent(TEST_TENANT_ID, "access1", "secret1", "data1");
    auto encCredential1 = EncryptAKSKContentForStorage(credential1);
    encCredential1->expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) + 100;
    EXPECT_TRUE(metaStoreClient_->Put("/yr/iam/aksk/new//" + TEST_TENANT_ID, TransToJsonFromEncAKSKContent(encCredential1), {})
                    .Get()
                    ->status.IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return internalIAMCred_->credentialActor_->newCredMap_.size() == 2;});
    auto akSk = internalIAMCred_->RequireCredentialByTenantID(TEST_TENANT_ID).Get();
    EXPECT_EQ(std::string(akSk->secretKey.GetData()), "secret1");
    auto akSk1 = internalIAMCred_->RequireCredentialByAK("access1").Get();
    EXPECT_EQ(std::string(akSk1->secretKey.GetData()), "secret1");
    metaStoreClient_->Delete("/yr/iam/aksk/", { false, true }).Get();
    ASSERT_AWAIT_TRUE([&]() -> bool { return internalIAMCred_->credentialActor_->newCredMap_.size() == 1;});
}

#if 0
TEST_F(InternalIAMTest, CheckCredentialExpiredInAdvanceTest)
{
    auto credential1 = GenAkSkContent("tenant001", "access1", "secret1", "data1");
    credential1->expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) + 120;
    auto credential2 = GenAkSkContent("tenant002", "access2", "secret2", "data2");
    credential2->expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) + 300;
    internalIAMCred_->credentialActor_->SaveCredentialAndTriggerUpdate("tenant001", credential1);
    internalIAMCred_->credentialActor_->SaveCredentialAndTriggerUpdate("tenant002", credential2);

    auto response1 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };
    auto response2 = litebus::http::Response{ litebus::http::ResponseCode::OK };
    auto credential11 = GenAkSkContent("tenant001", "access11", "secret11", "data11");
    credential11->expiredTimeStamp = static_cast<uint64_t>(std::time(nullptr)) + 720;
    auto encCredential11 = EncryptAKSKContentForStorage(credential11);
    response2.body = TransToJsonFromEncAKSKContent(encCredential11);

    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAMCred_->BindIAMClient(testIamClient);
    internalIAMCred_->credentialActor_->BindIAMClient(testIamClient);
    EXPECT_CALL(*testIamClient, CallApi).Times(2).WillOnce(Return(response1)).WillOnce(Return(response2));
    internalIAMCred_->credentialActor_->CheckCredentialExpiredInAdvance();
    internalIAMCred_->credentialActor_->CheckCredentialExpiredInAdvance();

    ASSERT_AWAIT_TRUE(
        [&]() -> bool { return internalIAMCred_->credentialActor_->newCredMap_["tenant001"]->expiredTimeStamp == credential11->expiredTimeStamp;});
    EXPECT_TRUE( internalIAMCred_->credentialActor_->newCredMap_["tenant002"]->expiredTimeStamp == credential2->expiredTimeStamp);
}
#endif

TEST_F(InternalIAMTest, IAMCredentialSyncerTest)
{
    auto response1 = litebus::http::Response{ litebus::http::ResponseCode::FORBIDDEN };
    auto testIamClient = MockIAMClient::CreateMockIAMClient(testIamBasePath_);
    internalIAMCred_->BindIAMClient(testIamClient);
    internalIAMCred_->credentialActor_->BindIAMClient(testIamClient);
    EXPECT_CALL(*testIamClient, LaunchRequest).Times(1).WillOnce(Return(response1));
    EXPECT_EQ(internalIAMCred_->RequireCredentialByTenantID(TEST_TENANT_ID).Get()->status.StatusCode(), StatusCode::FAILED);

    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;
    {
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status(StatusCode::FAILED, "");
        auto future = internalIAMCred_->credentialActor_->IAMCredentialSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());
    }

    {
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        auto future = internalIAMCred_->credentialActor_->IAMCredentialSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
    }

    {
        auto credential = GenAkSkContent("tenant1", "access1", "secret1", "data1");
        auto encCredential = EncryptAKSKContentForStorage(credential);
        auto iamKey = "/yr/iam/aksk/new//tenant1";
        auto iamValue = TransToJsonFromEncAKSKContent(encCredential);

        KeyValue getKeyValue;
        getKeyValue.set_key(iamKey) ;
        getKeyValue.set_value(iamValue);

        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        rep->kvs.emplace_back(getKeyValue);

        EXPECT_EQ(internalIAMCred_->credentialActor_->newCredMap_.size(), static_cast<uint32_t>(1));

        auto future = internalIAMCred_->credentialActor_->IAMCredentialSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());

        EXPECT_EQ(internalIAMCred_->credentialActor_->newCredMap_.size(), static_cast<uint32_t>(2));
        EXPECT_TRUE(internalIAMCred_->RequireCredentialByTenantID("tenant1").Get()->status.IsOk());
    }
}

} // namespace functionsystem::test