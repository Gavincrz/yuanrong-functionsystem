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

#include "common/aksk/aksk_util.h"
#include "common/constants/constants.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "common/utils/aksk_content.h"
#include "common/utils/sensitive_value.h"
#include "function_proxy/common/iam/internal_credential_manager_actor.h"
#include "utils/future_test_helper.h"
#include "utils/os_utils.hpp"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace functionsystem::function_proxy;

class BuildInCredentialTest : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        metaStorageAccessor_ = std::make_shared<MetaStorageAccessor>(metaStoreClient_);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        metaStoreClient_ = nullptr;
        metaStorageAccessor_ = nullptr;
        etcdSrvDriver_->StopServer();
        etcdSrvDriver_ = nullptr;
    }

protected:
    void SetUp() override
    {
        litebus::os::SetEnv("LITEBUS_AKSK_ENABLED", "1");
        litebus::os::SetEnv("LITEBUS_ACCESS_KEY", "c2b4887bb63e71b88773503708d625d8f0662ca18e513972bfcc8ffcfc7e3ed2");
        litebus::os::SetEnv(
            "LITEBUS_SECRET_KEY",
            SensitiveValue("16bf66680df37fd9d5ff6f781b50c53c463f4af0bb87c72c102aa7dde71da7cf").GetData());
        litebus::os::SetEnv(
            LITEBUS_DATA_KEY,
            SensitiveValue("0D2F1C6AEA8AFF3204128DFFD7CEC95471D1149DB628DF5267FDA5233C1D2F05").GetData());
    }

    void TearDown() override
    {
        litebus::os::UnSetEnv("LITEBUS_AKSK_ENABLED");
        litebus::os::UnSetEnv("LITEBUS_ACCESS_KEY");
        litebus::os::UnSetEnv("LITEBUS_SECRET_KEY");
        litebus::os::UnSetEnv(LITEBUS_DATA_KEY);
    }

    inline static std::shared_ptr<MetaStoreClient> metaStoreClient_;
    inline static std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_;
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::string metaStoreServerHost_;
};

inline void ToUpper(std::string *source)
{
    (void)std::transform(source->begin(), source->end(), source->begin(),
                         [](unsigned char c) { return std::toupper(c); });
}

TEST_F(BuildInCredentialTest, InitCredentialIAM_ValidEnv_Test)  // NOLINT
{
    const SensitiveValue dataKey = GetComponentDataKey();
    ASSERT_FALSE(dataKey.Empty());

    std::string cipher;
    SensitiveValue plain(R"({"tenantID":"t","accessKey":"a","secretKey":"s","dataKey":"d"})");
    litebus::os::SetEnv(YR_BUILD_IN_CREDENTIAL, plain.GetData());

    auto actorName = "InternalCredentialManagerActor_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    const auto actor = std::make_shared<InternalCredentialManagerActor>(actorName, "");
    // actor->BindIAMClient(IAMClient::CreateIAMClient(TEST_IAM_BASE_PATH));
    actor->BindMetaStorageAccessor(metaStorageAccessor_);
    const auto aid = litebus::Spawn(actor);

    const auto count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
    EXPECT_AWAIT_READY(count);  // wait async action
    EXPECT_EQ(count.Get(), 2);

    EXPECT_NE(actor->newCredMap_.find("t"), actor->newCredMap_.end());
    EXPECT_EQ(actor->newCredMap_.find("t")->second->accessKey, "a");

    litebus::os::UnSetEnv(YR_BUILD_IN_CREDENTIAL);

    auto result = litebus::Async(aid, &InternalCredentialManagerActor::AbandonCredential, "t");
    EXPECT_AWAIT_READY(result);  // wait async action
    EXPECT_TRUE(result.IsOK());

    const auto count_2 = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
    EXPECT_AWAIT_READY(count_2);  // wait async action
    EXPECT_EQ(count_2.Get(), 2);  // 永久凭据不支持删除

    litebus::Terminate(aid);
    litebus::Await(aid);
}

TEST_F(BuildInCredentialTest, InitCredentialIAM_InvalidEnv_Test)  // NOLINT
{
    SensitiveValue dataKey = GetComponentDataKey();
    ASSERT_FALSE(dataKey.Empty());

    auto actorName = "InternalCredential_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto actor = std::make_shared<InternalCredentialManagerActor>(actorName, "");
    // actor->BindIAMClient(IAMClient::CreateIAMClient(TEST_IAM_BASE_PATH));
    actor->BindMetaStorageAccessor(metaStorageAccessor_);
    auto aid = litebus::Spawn(actor);
    // Do set env action after load credential from env within Init
    auto ready = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
    ASSERT_AWAIT_READY(ready);  // for ready
    EXPECT_EQ(ready.Get(), 1);

    {  // invalid json
        SensitiveValue plain(R"({])");
        litebus::os::SetEnv(YR_BUILD_IN_CREDENTIAL, plain.GetData());

        litebus::Async(aid, &InternalCredentialManagerActor::LoadBuiltInCredential);
        auto count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
        ASSERT_AWAIT_READY(count);  // wait async action
        EXPECT_EQ(count.Get(), 1);

        litebus::os::UnSetEnv(YR_BUILD_IN_CREDENTIAL);
    }

    {  // invalid access key
        SensitiveValue plain(R"({"tenantID":"t"})");
        litebus::os::SetEnv(YR_BUILD_IN_CREDENTIAL, plain.GetData());

        litebus::Async(aid, &InternalCredentialManagerActor::LoadBuiltInCredential);
        auto count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
        ASSERT_AWAIT_READY(count);  // wait async action
        EXPECT_EQ(count.Get(), 1);

        litebus::os::UnSetEnv(YR_BUILD_IN_CREDENTIAL);
    }

    {  // invalid secret key
        SensitiveValue plain(R"({"tenantID":"t","accessKey":"a","secretKey":""})");
        litebus::os::SetEnv(YR_BUILD_IN_CREDENTIAL, plain.GetData());

        litebus::Async(aid, &InternalCredentialManagerActor::LoadBuiltInCredential);
        auto count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
        ASSERT_AWAIT_READY(count);  // wait async action
        EXPECT_EQ(count.Get(), 1);

        litebus::os::UnSetEnv(YR_BUILD_IN_CREDENTIAL);
    }

    {  // invalid data key
        SensitiveValue plain(R"({"tenantID":"t","accessKey":"a","secretKey":"s","dataKey":""})");
        litebus::os::SetEnv(YR_BUILD_IN_CREDENTIAL, plain.GetData());

        litebus::Async(aid, &InternalCredentialManagerActor::LoadBuiltInCredential);
        auto count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
        ASSERT_AWAIT_READY(count);  // wait async action
        EXPECT_EQ(count.Get(), 1);

        litebus::os::UnSetEnv(YR_BUILD_IN_CREDENTIAL);
    }

    {  // no component data key
        litebus::os::UnSetEnv(LITEBUS_DATA_KEY);

        SensitiveValue plain(R"({"tenantID":"t","accessKey":"a","secretKey":"s","dataKey":""})");
        litebus::os::SetEnv(YR_BUILD_IN_CREDENTIAL, plain.GetData());

        litebus::Async(aid, &InternalCredentialManagerActor::LoadBuiltInCredential);
        auto count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
        ASSERT_AWAIT_READY(count);  // wait async action
        EXPECT_EQ(count.Get(), 1);

        litebus::os::UnSetEnv(YR_BUILD_IN_CREDENTIAL);
    }

    litebus::Terminate(aid);
    litebus::Await(aid);
}

class AES_GCM_Test : public ::testing::Test {};

TEST_F(AES_GCM_Test, UpperHexKeyDataKey)  // NOLINT
{
    std::string upperHexKey = "E390F226ACF4F78E883F52147293BF25";
    std::string lowerHexKey = "e390f226acf4f78e883f52147293bf25";

    EXPECT_EQ(FromHexString(lowerHexKey), FromHexString(upperHexKey));

    litebus::os::SetEnv(LITEBUS_DATA_KEY, upperHexKey);
    SensitiveValue udk = GetComponentDataKey();
    litebus::os::UnSetEnv(LITEBUS_DATA_KEY);

    SensitiveValue ldk(FromHexString(lowerHexKey));

    EXPECT_EQ(udk, ldk);
}

class SystemCredentialTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);

        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        metaStorageAccessor_ = std::make_shared<MetaStorageAccessor>(metaStoreClient_);
    }

    void TearDown() override
    {
        litebus::os::UnSetEnv("LITEBUS_AKSK_ENABLED");
        litebus::os::UnSetEnv("LITEBUS_ACCESS_KEY");
        litebus::os::UnSetEnv("LITEBUS_SECRET_KEY");
        litebus::os::UnSetEnv(LITEBUS_DATA_KEY);

        metaStorageAccessor_ = nullptr;
        metaStoreClient_ = nullptr;

        etcdSrvDriver_->StopServer();
        etcdSrvDriver_ = nullptr;
    }

private:
    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_;
    std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    std::string metaStoreServerHost_;
};

TEST_F(SystemCredentialTest, LoadSystemCredential_Test)  // NOLINT
{
    litebus::os::SetEnv("LITEBUS_AKSK_ENABLED", "1");
    litebus::os::SetEnv("LITEBUS_ACCESS_KEY", "c2b4887bb63e71b88773503708d625d8f0662ca18e513972bfcc8ffcfc7e3ed2");
    litebus::os::SetEnv("LITEBUS_SECRET_KEY",
                        SensitiveValue("16bf66680df37fd9d5ff6f781b50c53c463f4af0bb87c72c102aa7dde71da7cf").GetData());
    litebus::os::SetEnv(LITEBUS_DATA_KEY,
                        SensitiveValue("0D2F1C6AEA8AFF3204128DFFD7CEC95471D1149DB628DF5267FDA5233C1D2F05").GetData());

    auto actorName = "InternalCredential_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto actor = std::make_shared<InternalCredentialManagerActor>(actorName, "default-cluster");
    // actor->BindIAMClient(IAMClient::CreateIAMClient(TEST_IAM_BASE_PATH));
    actor->BindMetaStorageAccessor(metaStorageAccessor_);
    auto aid = litebus::Spawn(actor);

    // Do set env action after load credential from env within Init
    auto count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
    ASSERT_AWAIT_READY(count);  // wait async action
    EXPECT_EQ(count.Get(), 1);

    litebus::Terminate(aid);
    litebus::Await(aid);
}

TEST_F(SystemCredentialTest, LoadSystemCredential_Error_Test)  // NOLINT
{
    auto actorName = "InternalCredential_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    auto actor = std::make_shared<InternalCredentialManagerActor>(actorName, "default-cluster");
    // actor->BindIAMClient(IAMClient::CreateIAMClient(TEST_IAM_BASE_PATH));
    actor->BindMetaStorageAccessor(metaStorageAccessor_);
    auto aid = litebus::Spawn(actor);

    // Do set env action after load credential from env within Init
    auto count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
    ASSERT_AWAIT_READY(count);  // wait async action
    EXPECT_EQ(count.Get(), 0);

    litebus::os::SetEnv("LITEBUS_AKSK_ENABLED", "1");
    litebus::Async(aid, &InternalCredentialManagerActor::LoadSystemCredential);
    count = litebus::Async(aid, &InternalCredentialManagerActor::GetCredentialCount);
    ASSERT_AWAIT_READY(count);  // wait async action
    EXPECT_EQ(count.Get(), 0);

    litebus::Terminate(aid);
    litebus::Await(aid);
}
}  // namespace functionsystem::test