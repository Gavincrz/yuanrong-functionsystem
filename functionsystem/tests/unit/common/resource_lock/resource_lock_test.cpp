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
#include <gmock/gmock.h>

#include "common/resource_lock/lease_lock.h"
#include "mocks/mock_kube_client.h"
#include "utils/future_test_helper.h"

using namespace functionsystem::resource_lock;
using V1Lease = functionsystem::kube_client::model::V1Lease;

namespace functionsystem::test {

class ResourceLockTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite() {
    }

    [[maybe_unused]] static void TearDownTestSuite() {
    }

    void SetUp()
    {
    }

    void TearDown()
    {
    }
};

TEST_F(ResourceLockTest, LeaseLockTest)
{
    auto mockClient = std::make_shared<MockKubeClient>();
    std::shared_ptr<LeaseLock> lock = std::make_shared<LeaseLock>("a", mockClient, "default", "function-master");
    EXPECT_EQ("a", lock->Identity());
    // get lease
    litebus::Promise<std::shared_ptr<V1Lease>> promise;
    promise.SetFailed(404);
    EXPECT_CALL(*mockClient, ReadNamespacedLease).WillOnce(testing::Return(promise.GetFuture()));
    auto res = lock->Get();
    res.Get();
    EXPECT_TRUE(res.IsError());
    litebus::Promise<std::shared_ptr<V1Lease>> promise1;
    nlohmann::json podJson = nlohmann::json::parse(DEFAULT_LEASE_JSON_STR);
    std::shared_ptr<V1Lease> lease = std::make_shared<V1Lease>();
    lease->FromJson(podJson);
    promise1.SetValue(lease);
    EXPECT_CALL(*mockClient, ReadNamespacedLease).WillOnce(testing::Return(promise1.GetFuture()));
    res = lock->Get();
    res.Get();
    EXPECT_EQ("10.10.10.10:22770", res.Get()->GetSpec()->GetHolderIdentity());
    // create lease
    std::shared_ptr<LeaderElectionRecord> record = std::make_shared<LeaderElectionRecord>();
    record->holderIdentity = "test";
    record->leaseTransitions = 1;
    record->acquireTime = std::chrono::system_clock::now();
    record->renewTime = std::chrono::system_clock::now();
    record->leaseDurationSeconds = 2;
    litebus::Promise<std::shared_ptr<V1Lease>> createPromise;
    createPromise.SetFailed(404);
    EXPECT_CALL(*mockClient, CreateNamespacedLease).WillOnce(testing::Return(createPromise.GetFuture()));
    auto resStatus = lock->Create(record);
    resStatus.Get();
    EXPECT_TRUE(resStatus.IsError());
    litebus::Future<std::shared_ptr<V1Lease>> body;
    EXPECT_CALL(*mockClient, CreateNamespacedLease)
        .WillOnce(testing::DoAll(test::FutureArg<1>(&body), testing::Return(lease)));
    resStatus = lock->Create(record);
    resStatus.Get();
    ASSERT_AWAIT_READY(body);
    EXPECT_EQ("test", body.Get()->GetSpec()->GetHolderIdentity());
    // update
    litebus::Promise<std::shared_ptr<V1Lease>> updatePromise;
    updatePromise.SetFailed(404);
    EXPECT_CALL(*mockClient, ReplaceNamespacedLease).WillOnce(testing::Return(createPromise.GetFuture()));
    lock->SetLease(lease);
    resStatus = lock->Update(record);
    resStatus.Get();
    EXPECT_TRUE(resStatus.IsError());
    litebus::Future<std::shared_ptr<V1Lease>> body1;
    EXPECT_CALL(*mockClient, ReplaceNamespacedLease)
        .WillOnce(testing::DoAll(test::FutureArg<2>(&body1), testing::Return(lease)));
    resStatus = lock->Update(record);
    resStatus.Get();
    ASSERT_AWAIT_READY(body1);
    EXPECT_EQ("test", body.Get()->GetSpec()->GetHolderIdentity());
}

}
