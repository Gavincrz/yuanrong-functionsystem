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

#include "common/resource_view/resource_view_mgr.h"

#include <gtest/gtest.h>
#include "mocks/mock_resource_view.h"
#include "utils/port_helper.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
const std::string LITEBUS_URL("127.0.0.1:8080");  // NOLINT
using namespace functionsystem::resource_view;
using namespace ::testing;
class ResourceViewMgrTest : public ::testing::Test {
protected:
    void SetUp()
    {
    }

    void TearDown()
    {
    }

private:
    std::shared_ptr<ResourceViewMgr> mgr_;
};

TEST_F(ResourceViewMgrTest, Init)
{
    auto mgr_ = std::make_shared<ResourceViewMgr>();
    mgr_->Init("host");
    auto aid = litebus::AID("host-ResourceViewActor", LITEBUS_URL);
    auto primary = litebus::GetActor(aid);
    ASSERT_EQ(primary != nullptr, true);
    aid = litebus::AID("host-virtualResourceViewActor", LITEBUS_URL);
    auto vir = litebus::GetActor(aid);
    ASSERT_EQ(vir != nullptr, true);
}

TEST_F(ResourceViewMgrTest, GetInf)
{
    auto mgr_ = std::make_shared<ResourceViewMgr>();
    mgr_->Init("host");
    auto primary = mgr_->GetInf(ResourceType::PRIMARY);
    ASSERT_EQ(primary != nullptr, true);
    auto vir = mgr_->GetInf(ResourceType::VIRTUAL);
    ASSERT_EQ(vir != nullptr, true);
}

TEST_F(ResourceViewMgrTest, GetResources)
{
    auto mgr_ = std::make_shared<ResourceViewMgr>();
    auto mockPrimary = MockResourceView::CreateMockResourceView();
    auto mockVirtual = MockResourceView::CreateMockResourceView();

    mgr_->primary_ = mockPrimary;
    mgr_->virtual_ = mockVirtual;
    EXPECT_CALL(*mockPrimary, GetFullResourceView).WillOnce(Return(AsyncReturn(std::make_shared<ResourceUnit>())));
    EXPECT_CALL(*mockVirtual, GetFullResourceView).WillOnce(Return(AsyncReturn(std::make_shared<ResourceUnit>())));
    auto future = mgr_->GetResources();
    auto resources = future.Get();
    EXPECT_EQ(resources.size(), (size_t)2);
    EXPECT_EQ(resources.find(ResourceType::PRIMARY) != resources.end(), true);
    EXPECT_EQ(resources.find(ResourceType::VIRTUAL) != resources.end(), true);

    litebus::Future<std::shared_ptr<ResourceUnit>> failure;
    failure.SetFailed(StatusCode::FAILED);
    EXPECT_CALL(*mockPrimary, GetFullResourceView).WillOnce(Return(AsyncReturn(failure)));
    EXPECT_CALL(*mockVirtual, GetFullResourceView).WillOnce(Return(AsyncReturn(std::make_shared<ResourceUnit>())));
    future = mgr_->GetResources();
    resources = future.Get();
    EXPECT_EQ(resources.size(), (size_t)0);
}

TEST_F(ResourceViewMgrTest, GetChanges)
{
    auto mgr_ = std::make_shared<ResourceViewMgr>();
    auto mockPrimary = MockResourceView::CreateMockResourceView();
    auto mockVirtual = MockResourceView::CreateMockResourceView();

    mgr_->primary_ = mockPrimary;
    mgr_->virtual_ = mockVirtual;
    EXPECT_CALL(*mockPrimary, GetResourceViewChanges).WillOnce(Return(AsyncReturn(std::make_shared<ResourceUnitChanges>())));
    EXPECT_CALL(*mockVirtual, GetResourceViewChanges).WillOnce(Return(AsyncReturn(std::make_shared<ResourceUnitChanges>())));
    auto future = mgr_->GetChanges();
    auto resources = future.Get();
    EXPECT_EQ(resources.size(), (size_t)2);
    EXPECT_EQ(resources.find(ResourceType::PRIMARY) != resources.end(), true);
    EXPECT_EQ(resources.find(ResourceType::VIRTUAL) != resources.end(), true);

    litebus::Future<std::shared_ptr<ResourceUnitChanges>> failure;
    failure.SetFailed(StatusCode::FAILED);
    EXPECT_CALL(*mockPrimary, GetResourceViewChanges).WillOnce(Return(AsyncReturn(failure)));
    EXPECT_CALL(*mockVirtual, GetResourceViewChanges).WillOnce(Return(AsyncReturn(std::make_shared<ResourceUnitChanges>())));
    future = mgr_->GetChanges();
    resources = future.Get();
    EXPECT_EQ(resources.size(), (size_t)0);
}

}