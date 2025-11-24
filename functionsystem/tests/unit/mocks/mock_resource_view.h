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

#ifndef TEST_UNIT_MOCKS_MOCK_RESOURCE_VIEW_H
#define TEST_UNIT_MOCKS_MOCK_RESOURCE_VIEW_H

#include <gmock/gmock.h>

#include <memory>

#include "common/resource_view/resource_view.h"
#include "common/resource_view/resource_view_actor.h"
#include "common/resource_view/resource_type.h"

namespace functionsystem::test {

const resource_view::ResourceViewActor::Param VIEW_ACTOR_PARAM {
    .isLocal = true,
    .enableTenantAffinity = true,
    .tenantPodReuseTimeWindow = 10
};

class MockResourceView : public resource_view::ResourceView {
public:
    static std::shared_ptr<MockResourceView> CreateMockResourceView()
    {
        std::string aid = "resource_view_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        auto stub = std::make_shared<resource_view::ResourceViewActor>(aid, "resourceUnitID", VIEW_ACTOR_PARAM);
        return std::make_shared<MockResourceView>(stub);
    }
    MockResourceView(std::shared_ptr<resource_view::ResourceViewActor> stub) : ResourceView(stub)
    {
    }
    MOCK_METHOD(litebus::Future<Status>, AddResourceUnit, (const resource_view::ResourceUnit &value), (override));
    MOCK_METHOD(litebus::Future<Status>, AddResourceUnitWithUrl,
                (const resource_view::ResourceUnit &value, const std::string &url), (override));


    MOCK_METHOD(litebus::Future<Status>, DeleteResourceUnit, (const std::string &unitID), (override));

    MOCK_METHOD(litebus::Future<Status>, DeleteLocalResourceView, (const std::string &localID), (override));

    MOCK_METHOD(litebus::Future<Status>, UpdateResourceUnit,
                (const std::shared_ptr<resource_view::ResourceUnit> &value, const resource_view::UpdateType &type),
                (override));

    MOCK_METHOD(litebus::Future<Status>, UpdateResourceUnitDelta,
                (const std::shared_ptr<resource_view::ResourceUnitChanges> &changes),
                (override));

    MOCK_METHOD(litebus::Future<Status>, AddInstances,
                ((const std::map<std::string, resource_view::InstanceAllocatedInfo> &insts)), (override));

    MOCK_METHOD(litebus::Future<Status>, DeleteInstances,
                (const std::vector<std::string> &instIDs, bool isVirtualInstance), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<resource_view::ResourceUnit>>, GetResourceView, (), (override));
    MOCK_METHOD(litebus::Future<std::shared_ptr<resource_view::ResourceUnit>>, GetResourceViewCopy, (), (override));
    MOCK_METHOD(litebus::Future<std::shared_ptr<resource_view::ResourceUnit>>, GetFullResourceView, (), (override));
    MOCK_METHOD(litebus::Future<std::shared_ptr<resource_view::ResourceUnitChanges>>, GetResourceViewChanges, (),
                (override));
    MOCK_METHOD(litebus::Future<std::string>, GetSerializedResourceView, (), (override));

    MOCK_METHOD(litebus::Future<litebus::Option<resource_view::ResourceUnit>>, GetResourceUnit,
                (const std::string &unitID), (override));

    MOCK_METHOD(void, ClearResourceView, (), (override));

    MOCK_METHOD(void, AddResourceUpdateHandler, (const resource_view::ResourceUpdateHandler &handler), (override));

    MOCK_METHOD(litebus::Future<litebus::Option<std::string>>, GetUnitByInstReqID, (const std::string &instReqID),
                (override));

    MOCK_METHOD(litebus::Future<resource_view::ResourceViewInfo>, GetResourceInfo, (), (override));
    MOCK_METHOD(litebus::Future<Status>, UpdateUnitStatus,
                (const std::string &unitID, resource_view::UnitStatus status), (override));

    MOCK_METHOD(litebus::Future<resource_view::PullResourceRequest>, GetUnitSnapshotInfo, (const std::string &id), (override));
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_RESOURCE_VIEW_H
