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

#include "common/resource_view/resource_view.h"

#include <gtest/gtest.h>

#include <map>

#include "common/constants/constants.h"
#include "common/metrics/metrics_adapter.h"
#include "common/resource_view/resource_poller.h"
#include "common/resource_view/resource_tool.h"
#include "common/types/instance_state.h"
#include "utils/future_test_helper.h"
#include "view_utils.h"
#include "utils/port_helper.h"

#define private public

namespace functionsystem::test {

using namespace functionsystem::resource_view;
using namespace functionsystem::test::view_utils;

const ResourceViewActor::Param CHILD_PARAM {
    .isLocal = true,
    .enableTenantAffinity = true,
    .tenantPodReuseTimeWindow = 1
};

ResourceViewActor::Param PARENT_PARAM {
    .isLocal = false,
    .enableTenantAffinity = true,
    .tenantPodReuseTimeWindow = 1
};

const std::string LOCAL_RESOUCE_VIEW_ID = "resoure_id";
const std::string DOMAIN_RESOUCE_VIEW_ID = "parent";
static const std::string NEED_RECOVER_VIEW = "needRecoverView";

class ResourceViewTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
    }

    void SetUp()
    {
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        domainUrl_ = "127.0.0.1:" + std::to_string(port);
    }

    void TearDown()
    {
    }

private:
    std::string domainUrl_;
};

TEST_F(ResourceViewTest, AddResourceUnitSuccess)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    auto view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 0);
    EXPECT_EQ(view.Get()->id(), LOCAL_RESOUCE_VIEW_ID);

    auto unit = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit.allocatable());
    resourceView.AddResourceUnit(unit);

    auto view2 = resourceView.GetResourceView();
    auto view2Ptr = view2.Get();
    ASSERT_EQ(view2Ptr->fragment_size(), 1);
    auto &bucket = view2Ptr->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit.id()).monopolynum(), 1);

    auto str = resourceView.GetSerializedResourceView();
    ASSERT_AWAIT_READY(str);
    ResourceUnit deserialiedUnit;
    EXPECT_EQ(deserialiedUnit.ParseFromString(str.Get()), true);
    EXPECT_EQ(deserialiedUnit.id(), LOCAL_RESOUCE_VIEW_ID);
    ASSERT_EQ(deserialiedUnit.fragment_size(), 1);

    auto rUnit = resourceView.GetResourceUnit(unit.id());
    ASSERT_TRUE(rUnit.Get().IsSome());
    EXPECT_EQ(rUnit.Get().Get().id(), unit.id());
    EXPECT_EQ(rUnit.Get().Get().allocatable().resources().size(), unit.allocatable().resources().size());

    resourceView.ClearResourceView();
    auto view3 = resourceView.GetResourceView();
    EXPECT_EQ(view3.Get()->fragment_size(), 0);
}

TEST_F(ResourceViewTest, AddResourceUnitSum)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    auto view = resourceView.GetResourceView();
    ASSERT_EQ(view.Get()->fragment_size(), 0);

    ResourceUnit unit = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit.allocatable());
    auto ret = resourceView.AddResourceUnit(unit);
    ASSERT_AWAIT_READY(ret);
    resourceView.PrintResourceView();
    view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 1);
    EXPECT_TRUE(view.Get()->fragment().at(unit.id()).id() == unit.id());
    EXPECT_TRUE(!view.Get()->id().empty());
    EXPECT_TRUE(view.Get()->allocatable() == unit.allocatable());
    EXPECT_TRUE(view.Get()->actualuse() == unit.actualuse());
    EXPECT_TRUE(view.Get()->capacity() == unit.capacity());
    auto &bucket = view.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit.id()).monopolynum(), 1);

    ResourceUnit unit2 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit2);
    ret = resourceView.AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);
    view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 2);
    EXPECT_TRUE(view.Get()->fragment().at(unit2.id()).id() == unit2.id());
    EXPECT_TRUE(!view.Get()->id().empty());
    EXPECT_TRUE(view.Get()->allocatable() == unit.allocatable() + unit2.allocatable());
    EXPECT_TRUE(view.Get()->actualuse() == unit.actualuse() + unit2.actualuse());
    EXPECT_TRUE(view.Get()->capacity() == unit.capacity() + unit2.capacity());
    bucket = view.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 2);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit2.id()).monopolynum(), 1);

    ResourceUnit unit3 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit3);
    resourceView.AddResourceUnit(unit3);
    view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 3);
    EXPECT_TRUE(view.Get()->fragment().at(unit3.id()).id() == unit3.id());
    EXPECT_TRUE(!view.Get()->id().empty());
    EXPECT_TRUE(view.Get()->allocatable() == unit.allocatable() + unit2.allocatable() + unit3.allocatable());
    bucket = view.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 3);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit3.id()).monopolynum(), 1);

    resourceView.ClearResourceView();
    auto view3 = resourceView.GetResourceView();
    EXPECT_EQ(view3.Get()->fragment_size(), 0);
}

TEST_F(ResourceViewTest, DeleteResourceUnit)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;
    auto init = resourceView.GetResourceView();
    auto initRevision = init.Get()->revision();
    EXPECT_EQ(int(initRevision), 0);

    auto unit1 = view_utils::Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit1);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit1.allocatable());
    auto unit2 = view_utils::Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit2);

    auto ret = resourceView.AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);
    ret = resourceView.AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);
    auto view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 2);
    EXPECT_TRUE(view.Get()->fragment().at(unit1.id()).capacity() == unit1.capacity());
    EXPECT_TRUE(view.Get()->fragment().at(unit2.id()).capacity() == unit2.capacity());

    ret = resourceView.DeleteResourceUnit(unit1.id());
    ASSERT_AWAIT_READY(ret);
    view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 1);
    EXPECT_TRUE(view.Get()->fragment().at(unit2.id()).capacity() == unit2.capacity());
    auto &bucket = view.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);

    ret = resourceView.DeleteResourceUnit(unit2.id());
    ASSERT_AWAIT_READY(ret);
    view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 0);
    EXPECT_EQ(view.Get()->revision(), initRevision + 4);
    bucket = view.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);

    view.Clear();
}

TEST_F(ResourceViewTest, InstanceAddAndDel)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);
    auto init = viewPtr->GetResourceView();
    auto initRevision = init.Get()->revision();
    EXPECT_EQ(int(initRevision), 0);

    auto unit1 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit1);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit1.allocatable());
    auto unit2 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit2);
    auto inst1 = Get1DInstance();
    auto inst2 = Get1DInstance();
    auto inst3 = Get1DInstance();
    auto inst4 = Get1DInstance();
    inst4.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    inst1.mutable_labels()->Add("label_inst1");
    inst1.mutable_labels()->Add(TENANT_ID + ":123456");
    inst2.mutable_labels()->Add("label_inst2:instance2");
    inst2.mutable_labels()->Add(TENANT_ID + ":789456");
    inst3.mutable_labels()->Add("label_inst2:instance2");
    inst4.mutable_labels()->Add("label-1");
    (*inst1.mutable_schedulerchain()->Add()) = unit1.id();
    inst1.set_unitid(unit1.id());
    (*inst2.mutable_schedulerchain()->Add()) = unit1.id();
    inst2.set_unitid(unit1.id());
    (*inst3.mutable_schedulerchain()->Add()) = unit2.id();
    inst3.set_unitid(unit2.id());
    (*inst4.mutable_schedulerchain()->Add()) = unit2.id();
    inst4.set_unitid(unit2.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
    instances.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    EXPECT_EQ(instances.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(instances.at(inst1.instanceid()).instanceInfo.schedulerchain().size(), 1);
    EXPECT_EQ(instances.at(inst1.instanceid()).instanceInfo.schedulerchain().at(0), unit1.id());

    // add unit 1, inst 1
    litebus::Future<Status> ret;
    ret = viewPtr->AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    ret = viewPtr->AddInstances(instances);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    EXPECT_EQ(rView.Get()->fragment_size(), 1);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(1));
    auto &rInstances = rView.Get()->instances();
    EXPECT_TRUE(rInstances.find(inst1.instanceid()) != rInstances.end());
    auto &rInst1 = rInstances.at(inst1.instanceid());
    EXPECT_EQ(rInst1.instanceid(), inst1.instanceid());
    EXPECT_TRUE(rInst1.resources() == inst1.resources());
    EXPECT_TRUE(rInst1.actualuse() == inst1.actualuse());
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() == (unit1.capacity() - inst1.resources()));
    auto fragInst = rView.Get()->fragment().at(unit1.id());
    EXPECT_TRUE(fragInst.capacity() == unit1.capacity());
    EXPECT_TRUE(fragInst.allocatable() == unit1.allocatable() - inst1.resources());
    EXPECT_EQ(fragInst.instances().size(), static_cast<uint32_t>(1));
    EXPECT_TRUE(fragInst.instances().find(inst1.instanceid()) != fragInst.instances().end());
    auto &rInstFragInst1 = fragInst.instances().at(inst1.instanceid());
    EXPECT_EQ(rInstFragInst1.instanceid(), inst1.instanceid());
    EXPECT_TRUE(rInstFragInst1.resources() == inst1.resources());
    EXPECT_TRUE(rInstFragInst1.actualuse() == inst1.actualuse());
    auto bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 0);
    EXPECT_EQ(bucket.total().sharednum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).sharednum(), 0);
    auto [remainProportion, remainMem] = GetMinimumUnitBucketIndex(unit1.allocatable() - inst1.resources());
    EXPECT_FALSE(rView.Get()->mutable_bucketindexs()->at(remainProportion).mutable_buckets()->contains(remainMem));

    auto affinityLabels = rView.Get()->nodelabels().find(AFFINITY_SCHEDULE_LABELS);
    EXPECT_NE(affinityLabels, rView.Get()->nodelabels().end());
    EXPECT_NE(rView.Get()->nodelabels().find("label_inst1"), rView.Get()->nodelabels().end());
    EXPECT_NE(fragInst.nodelabels().find("label_inst1"), fragInst.nodelabels().end());
    ASSERT_NE(fragInst.nodelabels().find(TENANT_ID), fragInst.nodelabels().end());
    ASSERT_NE(fragInst.nodelabels().at(TENANT_ID).items().count("123456"), static_cast<uint32_t>(0));
    EXPECT_EQ(fragInst.nodelabels().at(TENANT_ID).items().at("123456"), (size_t)2);

    // add inst 2
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    ret = viewPtr->AddInstances(instances2);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    EXPECT_EQ(rView.Get()->fragment_size(), 1);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(2));
    auto &rInstances2 = rView.Get()->instances();
    EXPECT_TRUE(rInstances2.find(inst2.instanceid()) != rInstances2.end());
    auto &rInst2 = rInstances.at(inst2.instanceid());
    EXPECT_EQ(rInst2.instanceid(), inst2.instanceid());
    EXPECT_TRUE(rInst2.resources() == inst2.resources());
    EXPECT_TRUE(rInst2.actualuse() == inst2.actualuse());
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() == ((unit1.capacity() - inst1.resources()) - inst2.resources()));
    fragInst = rView.Get()->fragment().at(unit1.id());
    EXPECT_TRUE(fragInst.capacity() == unit1.capacity());
    EXPECT_TRUE(fragInst.allocatable() == ((unit1.allocatable() - inst1.resources()) - inst2.resources()));
    EXPECT_EQ(fragInst.instances().size(), static_cast<uint32_t>(2));
    EXPECT_TRUE(fragInst.instances().find(inst1.instanceid()) != fragInst.instances().end());
    auto &rInstFragInst2 = fragInst.instances().at(inst2.instanceid());
    EXPECT_EQ(rInstFragInst2.instanceid(), inst2.instanceid());
    EXPECT_TRUE(rInstFragInst2.resources() == inst2.resources());
    EXPECT_TRUE(rInstFragInst2.actualuse() == inst2.actualuse());
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 0);
    auto [remain2Proportion, remain2Mem] =
        GetMinimumUnitBucketIndex(unit1.allocatable() - inst1.resources() - inst2.resources());
    EXPECT_FALSE(rView.Get()->mutable_bucketindexs()->at(remain2Proportion).mutable_buckets()->contains(remain2Mem));
    EXPECT_NE(rView.Get()->nodelabels().find("label_inst2"), rView.Get()->nodelabels().end());
    EXPECT_NE(rView.Get()->nodelabels().at("label_inst2").items().find("instance2"), rView.Get()->nodelabels().at("label_inst2").items().end());
    EXPECT_EQ(rView.Get()->nodelabels().at("label_inst2").items().at("instance2"), (size_t)1);
    ASSERT_NE(rView.Get()->nodelabels().find(TENANT_ID), fragInst.nodelabels().end());
    ASSERT_NE(rView.Get()->nodelabels().at(TENANT_ID).items().count("789456"), static_cast<long unsigned int>(0));
    EXPECT_EQ(rView.Get()->nodelabels().at(TENANT_ID).items().at("789456"), (size_t)2);
    EXPECT_NE(fragInst.nodelabels().find("label_inst2"), fragInst.nodelabels().end());
    EXPECT_NE(fragInst.nodelabels().at("label_inst2").items().find("instance2"), fragInst.nodelabels().at("label_inst2").items().end());
    EXPECT_EQ(fragInst.nodelabels().at("label_inst2").items().at("instance2"), (size_t)1);
    EXPECT_EQ(fragInst.nodelabels().at(TENANT_ID).items().at("789456"), (size_t)2);

    // add unit 2, inst 3
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances3;
    auto allocatePromise = std::make_shared<litebus::Promise<Status>>();
    instances3.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, allocatePromise });
    ret = viewPtr->AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    ret = viewPtr->AddInstances(instances3);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_AWAIT_READY(allocatePromise->GetFuture());
    EXPECT_EQ(allocatePromise->GetFuture().Get().IsOk(), true);

    EXPECT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(3));
    auto &rInstances3 = rView.Get()->instances();
    EXPECT_TRUE(rInstances3.find(inst3.instanceid()) != rInstances3.end());
    auto &rInst3 = rInstances.at(inst3.instanceid());
    EXPECT_EQ(rInst3.instanceid(), inst3.instanceid());
    EXPECT_TRUE(rInst3.resources() == inst3.resources());
    EXPECT_TRUE(rInst3.actualuse() == inst3.actualuse());
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity() + unit2.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() ==
                (unit1.capacity() + unit2.capacity() - inst1.resources() - inst2.resources() - inst3.resources()));
    fragInst = rView.Get()->fragment().at(unit2.id());
    EXPECT_TRUE(fragInst.capacity() == unit2.capacity());
    EXPECT_TRUE(fragInst.allocatable() == (unit2.allocatable() - inst3.resources()));
    EXPECT_EQ(fragInst.instances().size(), static_cast<uint32_t>(1));
    EXPECT_TRUE(fragInst.instances().find(inst3.instanceid()) != fragInst.instances().end());
    auto &rInstFragInst3 = fragInst.instances().at(inst3.instanceid());
    EXPECT_EQ(rInstFragInst3.instanceid(), inst3.instanceid());
    EXPECT_TRUE(rInstFragInst3.resources() == inst3.resources());
    EXPECT_TRUE(rInstFragInst3.actualuse() == inst3.actualuse());
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit2.id()).monopolynum(), 0);
    EXPECT_FALSE(rView.Get()->mutable_bucketindexs()->at(remainProportion).mutable_buckets()->contains(remainMem));
    EXPECT_NE(rView.Get()->nodelabels().find("label_inst2"), rView.Get()->nodelabels().end());
    EXPECT_NE(rView.Get()->nodelabels().at("label_inst2").items().find("instance2"), rView.Get()->nodelabels().at("label_inst2").items().end());
    EXPECT_EQ(rView.Get()->nodelabels().at("label_inst2").items().at("instance2"), static_cast<long unsigned int>(2));
    EXPECT_NE(fragInst.nodelabels().find("label_inst2"), fragInst.nodelabels().end());
    EXPECT_NE(fragInst.nodelabels().at("label_inst2").items().find("instance2"), fragInst.nodelabels().at("label_inst2").items().end());
    EXPECT_EQ(fragInst.nodelabels().at("label_inst2").items().at("instance2"), static_cast<long unsigned int>(1));

    // delete inst 3
    std::vector<std::string> ids1;
    ids1.push_back(inst3.instanceid());
    ret = viewPtr->DeleteInstances(ids1);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    EXPECT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(2));
    auto &rInstances4 = rView.Get()->instances();
    EXPECT_TRUE(rInstances4.find(inst3.instanceid()) == rInstances4.end());
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity() + unit2.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() ==
                (unit1.capacity() + unit2.capacity() - inst1.resources() - inst2.resources()));
    fragInst = rView.Get()->fragment().at(unit2.id());
    EXPECT_TRUE(fragInst.capacity() == unit2.capacity());
    EXPECT_TRUE(fragInst.allocatable() == unit2.allocatable());
    EXPECT_EQ(fragInst.instances().size(), static_cast<uint32_t>(0));
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit2.id()).monopolynum(), 1);
    EXPECT_FALSE(rView.Get()->mutable_bucketindexs()->at(remainProportion).mutable_buckets()->contains(remainMem));
    EXPECT_NE(rView.Get()->nodelabels().find("label_inst2"), rView.Get()->nodelabels().end());
    EXPECT_NE(rView.Get()->nodelabels().at("label_inst2").items().find("instance2"), rView.Get()->nodelabels().at("label_inst2").items().end());
    EXPECT_EQ(rView.Get()->nodelabels().at("label_inst2").items().at("instance2"), 1);
    EXPECT_EQ(fragInst.nodelabels().find("label_inst2"), fragInst.nodelabels().end());

    // delete inst 2
    std::vector<std::string> ids2;
    ids2.push_back(inst2.instanceid());
    ret = viewPtr->DeleteInstances(ids2);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    EXPECT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(1));
    auto &rInstances5 = rView.Get()->instances();
    EXPECT_TRUE(rInstances5.find(inst2.instanceid()) == rInstances5.end());
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity() + unit2.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() == (unit1.capacity() + unit2.capacity() - inst1.resources()));
    fragInst = rView.Get()->fragment().at(unit1.id());
    EXPECT_TRUE(fragInst.capacity() == unit1.capacity());
    EXPECT_TRUE(fragInst.allocatable() == unit1.allocatable() - inst1.resources());
    EXPECT_EQ(fragInst.instances().size(), static_cast<uint32_t>(1));
    EXPECT_TRUE(fragInst.instances().find(inst2.instanceid()) == fragInst.instances().end());
    EXPECT_TRUE(fragInst.instances().find(inst1.instanceid()) != fragInst.instances().end());
    auto &rInstFragInst5 = fragInst.instances().at(inst1.instanceid());
    EXPECT_EQ(rInstFragInst5.instanceid(), inst1.instanceid());
    EXPECT_TRUE(rInstFragInst5.resources() == inst1.resources());
    EXPECT_TRUE(rInstFragInst5.actualuse() == inst1.actualuse());
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 0);
    EXPECT_FALSE(rView.Get()->mutable_bucketindexs()->at(remainProportion).mutable_buckets()->contains(remainMem));
    EXPECT_EQ(rView.Get()->nodelabels().find("label_inst2"), rView.Get()->nodelabels().end());
    EXPECT_EQ(fragInst.nodelabels().find("label_inst2"), fragInst.nodelabels().end());

    // delete inst 1
    std::vector<std::string> ids3;
    ids3.push_back(inst1.instanceid());
    ret = viewPtr->DeleteInstances(ids3);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    EXPECT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(0));
    auto &rInstances6 = rView.Get()->instances();
    EXPECT_TRUE(rInstances6.find(inst1.instanceid()) == rInstances6.end());
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity() + unit2.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() == (unit1.capacity() + unit2.capacity()));
    fragInst = rView.Get()->fragment().at(unit1.id());
    EXPECT_TRUE(fragInst.capacity() == unit1.capacity());
    EXPECT_TRUE(fragInst.allocatable() == unit1.allocatable());
    EXPECT_EQ(fragInst.instances().size(), static_cast<uint32_t>(0));
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 2);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 1);
    EXPECT_FALSE(rView.Get()->mutable_bucketindexs()->at(remainProportion).mutable_buckets()->contains(remainMem));

    ASSERT_NE(rView.Get()->nodelabels().find(TENANT_ID), fragInst.nodelabels().end());
    ASSERT_NE(rView.Get()->nodelabels().at(TENANT_ID).items().count("789456"), (size_t)0);
    ASSERT_NE(rView.Get()->nodelabels().at(TENANT_ID).items().count("123456"), (size_t)0);
    EXPECT_EQ(rView.Get()->nodelabels().at(TENANT_ID).items().at("789456"), (size_t)1);
    EXPECT_EQ(rView.Get()->nodelabels().at(TENANT_ID).items().at("123456"), (size_t)1);

    // add inst 4 with monopoly
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances4;
    instances4.emplace(inst4.instanceid(), resource_view::InstanceAllocatedInfo{ inst4, nullptr });
    ret = viewPtr->AddInstances(instances4);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(1));
    auto &rInstances7 = rView.Get()->instances();
    EXPECT_TRUE(rInstances7.find(inst4.instanceid()) != rInstances7.end());
    auto &rInst4 = rInstances7.at(inst4.instanceid());
    EXPECT_EQ(rInst4.instanceid(), inst4.instanceid());
    EXPECT_TRUE(rInst4.resources() == inst4.resources());
    EXPECT_TRUE(rInst4.actualuse() == inst4.actualuse());
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity() + unit2.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() == unit1.capacity());
    fragInst = rView.Get()->fragment().at(unit2.id());
    EXPECT_TRUE(fragInst.capacity() == unit2.capacity());
    EXPECT_TRUE(fragInst.allocatable() == (unit2.allocatable() - unit2.allocatable()));
    EXPECT_EQ(fragInst.instances().size(), static_cast<uint32_t>(1));
    EXPECT_TRUE(fragInst.instances().find(inst4.instanceid()) != fragInst.instances().end());
    auto &rInstFragInst4 = fragInst.instances().at(inst4.instanceid());
    EXPECT_EQ(rInstFragInst4.instanceid(), inst4.instanceid());
    EXPECT_TRUE(rInstFragInst4.resources() == inst4.resources());
    EXPECT_TRUE(rInstFragInst4.actualuse() == inst4.actualuse());
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit2.id()).monopolynum(), 0);
    affinityLabels = rView.Get()->nodelabels().find(AFFINITY_SCHEDULE_LABELS);
    EXPECT_NE(affinityLabels, rView.Get()->nodelabels().end());
    EXPECT_NE(affinityLabels->second.items().find("label-1"), affinityLabels->second.items().end());
    affinityLabels = rView.Get()->nodelabels().find("label-1");
    EXPECT_NE(affinityLabels, rView.Get()->nodelabels().end());
    EXPECT_NE(affinityLabels->second.items().find(""), affinityLabels->second.items().end());

    // delete inst4
    ret = viewPtr->DeleteInstances({ inst4.instanceid() });
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    EXPECT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(0));
    auto &rInstances8 = rView.Get()->instances();
    EXPECT_TRUE(rInstances8.find(inst4.instanceid()) == rInstances8.end());
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity() + unit2.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() == (unit1.capacity() + unit2.capacity()));
    fragInst = rView.Get()->fragment().at(unit2.id());
    EXPECT_TRUE(fragInst.capacity() == unit2.capacity());
    EXPECT_TRUE(fragInst.allocatable() == unit2.allocatable());
    EXPECT_EQ(fragInst.instances().size(), static_cast<uint32_t>(0));
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit2.id()).monopolynum(), 0);
    affinityLabels = rView.Get()->nodelabels().find(AFFINITY_SCHEDULE_LABELS);
    EXPECT_NE(affinityLabels, rView.Get()->nodelabels().end());
    EXPECT_EQ(affinityLabels->second.items().find("label-1"), affinityLabels->second.items().end());
    affinityLabels = rView.Get()->nodelabels().find("label-1");
    EXPECT_EQ(affinityLabels, rView.Get()->nodelabels().end());

    // delete unit1
    ret = viewPtr->DeleteResourceUnit(unit1.id());
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->fragment_size(), 1);
    EXPECT_TRUE(rView.Get()->capacity() == unit2.capacity());
    EXPECT_TRUE(rView.Get()->allocatable() == unit2.capacity());
    EXPECT_TRUE(rView.Get()->fragment().find(unit1.id()) == rView.Get()->fragment().end());

    // delete unit2
    ret = viewPtr->DeleteResourceUnit(unit2.id());
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->revision(), initRevision + 12);
    EXPECT_EQ(rView.Get()->fragment_size(), 0);
    EXPECT_TRUE(rView.Get()->capacity() == Get0CpuMemResources());
    EXPECT_TRUE(rView.Get()->allocatable() == Get0CpuMemResources());
    EXPECT_TRUE(rView.Get()->fragment().find(unit2.id()) == rView.Get()->fragment().end());
    // add instance with invalid agent
    std::map<std::string, resource_view::InstanceAllocatedInfo> invalidInstances;
    inst4.set_unitid("xxxxxxx");
    invalidInstances.emplace(inst4.instanceid(), resource_view::InstanceAllocatedInfo{ inst4, std::make_shared<litebus::Promise<Status>>() });
    ret = viewPtr->AddInstances(invalidInstances);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    auto allocatedFuture = invalidInstances[inst4.instanceid()].allocatedPromise->GetFuture();
    ASSERT_AWAIT_READY(allocatedFuture);
    EXPECT_EQ(allocatedFuture.Get().IsOk(), false);
    EXPECT_EQ(allocatedFuture.Get().StatusCode(), StatusCode::ERR_INNER_SYSTEM_ERROR);
}

TEST_F(ResourceViewTest, AgentReuseTimer)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    std::set<std::string> disabledAgent;
    std::atomic<int> count = 0;
    viewPtr->RegisterUnitDisableFunc([&disabledAgent, &count](const std::string &agentID){
        disabledAgent.emplace(agentID);
        count++;
    });
    auto rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);
    auto init = viewPtr->GetResourceView();
    auto initRevision = init.Get()->revision();
    EXPECT_EQ(int(initRevision), 0);

    auto unit1 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit1);
    auto unit2 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit2);
    auto unit3 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit3);
    auto inst1 = Get1DInstance();
    auto inst2 = Get1DInstance();
    auto inst3 = Get1DInstance();
    auto inst4= Get1DInstance();

    inst1.mutable_labels()->Add("label_inst1");
    inst1.mutable_labels()->Add(TENANT_ID + ":123456");
    inst1.set_functionproxyid(LOCAL_RESOUCE_VIEW_ID);
    inst1.set_tenantid("123456");
    inst2.mutable_labels()->Add("label_inst2:instance2");
    inst2.mutable_labels()->Add(TENANT_ID + ":123456");
    inst2.set_tenantid("123456");
    inst2.set_functionproxyid(LOCAL_RESOUCE_VIEW_ID);
    inst3.mutable_labels()->Add("label_inst2:789456");
    inst3.set_functionproxyid(LOCAL_RESOUCE_VIEW_ID);
    inst3.set_tenantid("789456");
    inst4.mutable_labels()->Add("label_inst3:789456");
    inst4.set_functionproxyid(LOCAL_RESOUCE_VIEW_ID);
    inst4.set_tenantid("789456");

    (*inst1.mutable_schedulerchain()->Add()) = unit1.id();
    inst1.set_unitid(unit1.id());
    (*inst2.mutable_schedulerchain()->Add()) = unit1.id();
    inst2.set_unitid(unit1.id());
    (*inst3.mutable_schedulerchain()->Add()) = unit2.id();
    inst3.set_unitid(unit2.id());
    (*inst4.mutable_schedulerchain()->Add()) = unit3.id();
    inst4.set_unitid(unit3.id());

    // add unit 1, inst 1
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
    instances.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    litebus::Future<Status> ret;
    ret = viewPtr->AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    YRLOG_DEBUG("add inst1");
    ret = viewPtr->AddInstances(instances);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    // add inst 2
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    YRLOG_DEBUG("add inst2");
    ret = viewPtr->AddInstances(instances2);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    auto agentCache = viewPtr->GetAgentCache();
    EXPECT_EQ(agentCache[unit1.id()].size(), 2);

    // add unit 2, inst 3
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances3;
    instances3.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });
    ret = viewPtr->AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    YRLOG_DEBUG("add inst3");
    ret = viewPtr->AddInstances(instances3);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    // add unit 3, inst 4
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances4;
    instances4.emplace(inst4.instanceid(), resource_view::InstanceAllocatedInfo{ inst4, nullptr });
    ret = viewPtr->AddResourceUnit(unit3);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    YRLOG_DEBUG("add inst4");
    ret = viewPtr->AddInstances(instances4);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    // delete inst 3
    std::vector<std::string> ids1;
    ids1.push_back(inst3.instanceid());
    YRLOG_DEBUG("delete inst3");
    ret = viewPtr->DeleteInstances(ids1);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    auto reuseTimers = viewPtr->GetReuseTimers();
    EXPECT_TRUE(reuseTimers.count(inst3.unitid()) != 0);

    agentCache = viewPtr->GetAgentCache();
    EXPECT_TRUE(agentCache.count(inst3.unitid()) == 0);

    // delete inst 2
    std::vector<std::string> ids2;
    ids2.push_back(inst2.instanceid());
    YRLOG_DEBUG("delete inst2");
    ret = viewPtr->DeleteInstances(ids2);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    agentCache = viewPtr->GetAgentCache();
    EXPECT_TRUE(agentCache[inst2.unitid()].size() == 1);

    // delete inst 1
    std::vector<std::string> ids3;
    ids3.push_back(inst1.instanceid());
    YRLOG_DEBUG("delete inst1");
    ret = viewPtr->DeleteInstances(ids3);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    reuseTimers = viewPtr->GetReuseTimers();
    EXPECT_TRUE(reuseTimers.count(inst1.unitid()) != 0);

    agentCache = viewPtr->GetAgentCache();
    EXPECT_TRUE(agentCache.count(inst1.unitid()) == 0);

    // check unit status
    ASSERT_AWAIT_TRUE([&]() -> bool { return count == 2; });

    auto rUnit = viewPtr->GetResourceUnit(unit1.id());
    ASSERT_FALSE(rUnit.Get().IsSome());

    rUnit = viewPtr->GetResourceUnit(unit2.id());
    ASSERT_FALSE(rUnit.Get().IsSome());

    // delete inst 4 -- virtual instance
    std::vector<std::string> ids4;
    ids4.push_back(inst4.instanceid());
    YRLOG_DEBUG("delete inst4");
    auto begin = litebus::TimeWatch::Now();
    ret = viewPtr->DeleteInstances(ids4, true);
    ASSERT_AWAIT_READY(ret);
    ret.Get();
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);

    reuseTimers = viewPtr->GetReuseTimers();
    EXPECT_TRUE(reuseTimers.count(inst4.unitid()) == 0);

    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 2000; });

    EXPECT_TRUE(count == 2);
    rUnit = viewPtr->GetResourceUnit(unit3.id());
    ASSERT_TRUE(rUnit.Get().IsSome());
}

TEST_F(ResourceViewTest, Resource2DWithInstances)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    auto unit1 = Get2DResourceUnitWithInstances();
    auto inst1 = unit1.fragment().begin()->second.instances().begin()->second;
    auto inst2 = (++unit1.fragment().begin()->second.instances().begin())->second;
    auto inst3 = (++unit1.fragment().begin())->second.instances().begin()->second;
    auto inst4 = ((++unit1.fragment().begin())->second.instances().begin())->second;
    auto subUnit1 = unit1.fragment().begin()->second;
    auto subUnit2 = (++unit1.fragment().begin())->second;
    // add unit 1
    litebus::Future<Status> ret;
    ret = viewPtr->AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    YRLOG_DEBUG("Resource2DWithInstances position 1 : resource unit is {}.", ToString(*rView.Get()));

    // check top level
    EXPECT_EQ(rView.Get()->fragment_size(), 1);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(4));
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity());
    auto rInstances = rView.Get()->instances();
    ASSERT_TRUE(rInstances.find(inst1.instanceid()) != rInstances.end());
    ASSERT_TRUE(rInstances.find(inst2.instanceid()) != rInstances.end());
    ASSERT_TRUE(rInstances.find(inst3.instanceid()) != rInstances.end());
    ASSERT_TRUE(rInstances.find(inst4.instanceid()) != rInstances.end());
    auto rInst1 = rInstances.at(inst1.instanceid());
    EXPECT_EQ(rInst1.instanceid(), inst1.instanceid());
    EXPECT_TRUE(rInst1.resources() == inst1.resources());
    EXPECT_TRUE(rInst1.actualuse() == inst1.actualuse());
    EXPECT_EQ(rInst1.schedulerchain().size(), inst1.schedulerchain().size());
    auto rInst4 = rInstances.at(inst4.instanceid());
    EXPECT_EQ(rInst4.instanceid(), inst4.instanceid());
    EXPECT_TRUE(rInst4.resources() == inst4.resources());
    EXPECT_TRUE(rInst4.actualuse() == inst4.actualuse());
    EXPECT_EQ(rInst4.schedulerchain().size(), inst4.schedulerchain().size());

    ASSERT_TRUE(rView.Get()->fragment().find(unit1.id()) != rView.Get()->fragment().end());
    auto fragInst = rView.Get()->fragment().at(unit1.id());
    EXPECT_TRUE(fragInst.fragment().size() == 2);
    EXPECT_TRUE(fragInst.capacity() == unit1.capacity());
    EXPECT_TRUE(fragInst.instances_size() == 4);
    auto rSubUnit1 = fragInst.fragment().begin()->second;
    auto rSubUnit2 = (++fragInst.fragment().begin())->second;
    EXPECT_TRUE(rSubUnit1.instances().size() == 2);
    EXPECT_TRUE(rSubUnit1.capacity() == subUnit1.capacity());
    EXPECT_TRUE(rSubUnit2.instances().size() == 2);
    EXPECT_TRUE(rSubUnit2.capacity() == subUnit2.capacity());
    auto [unit1RemainProportion, unit1Remain] = GetMinimumUnitBucketIndex(subUnit1.allocatable());
    auto bucket = rView.Get()->mutable_bucketindexs()->at(unit1RemainProportion).mutable_buckets()->at(unit1Remain);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 0);
    EXPECT_EQ(bucket.total().sharednum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).sharednum(), 1);

    auto unit2 = Get2DResourceUnitWithInstances();
    inst1 = unit2.fragment().begin()->second.instances().begin()->second;
    inst2 = (++unit2.fragment().begin()->second.instances().begin())->second;
    inst3 = (++unit2.fragment().begin())->second.instances().begin()->second;
    inst4 = ((++unit2.fragment().begin())->second.instances().begin())->second;
    subUnit1 = unit2.fragment().begin()->second;
    subUnit2 = (++unit2.fragment().begin())->second;
    // add unit 2
    ret = viewPtr->AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    YRLOG_DEBUG("Resource2DWithInstances position 2 : resource unit is {}.", ToString(*rView.Get()));

    // check top level
    EXPECT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(8));
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity() + unit2.capacity());
    rInstances = rView.Get()->instances();
    ASSERT_TRUE(rInstances.find(inst1.instanceid()) != rInstances.end());
    ASSERT_TRUE(rInstances.find(inst2.instanceid()) != rInstances.end());
    ASSERT_TRUE(rInstances.find(inst3.instanceid()) != rInstances.end());
    ASSERT_TRUE(rInstances.find(inst4.instanceid()) != rInstances.end());
    rInst1 = rInstances.at(inst1.instanceid());
    EXPECT_EQ(rInst1.instanceid(), inst1.instanceid());
    EXPECT_TRUE(rInst1.resources() == inst1.resources());
    EXPECT_TRUE(rInst1.actualuse() == inst1.actualuse());
    EXPECT_EQ(rInst1.schedulerchain().size(), inst1.schedulerchain().size());
    rInst4 = rInstances.at(inst4.instanceid());
    EXPECT_EQ(rInst4.instanceid(), inst4.instanceid());
    EXPECT_TRUE(rInst4.resources() == inst4.resources());
    EXPECT_TRUE(rInst4.actualuse() == inst4.actualuse());
    EXPECT_EQ(rInst4.schedulerchain().size(), inst4.schedulerchain().size());

    ASSERT_TRUE(rView.Get()->fragment().find(unit2.id()) != rView.Get()->fragment().end());
    fragInst = rView.Get()->fragment().at(unit2.id());
    EXPECT_TRUE(fragInst.fragment().size() == 2);
    EXPECT_TRUE(fragInst.capacity() == unit1.capacity());
    EXPECT_TRUE(fragInst.instances_size() == 4);
    rSubUnit1 = fragInst.fragment().begin()->second;
    rSubUnit2 = (++fragInst.fragment().begin())->second;
    EXPECT_TRUE(rSubUnit1.instances().size() == 2);
    EXPECT_TRUE(rSubUnit1.capacity() == subUnit1.capacity());
    EXPECT_TRUE(rSubUnit2.instances().size() == 2);
    EXPECT_TRUE(rSubUnit2.capacity() == subUnit2.capacity());
    bucket = rView.Get()->mutable_bucketindexs()->at(unit1RemainProportion).mutable_buckets()->at(unit1Remain);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 0);
    EXPECT_EQ(bucket.total().sharednum(), 2);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).sharednum(), 1);

    // delete unit 2
    ret = viewPtr->DeleteResourceUnit(unit2.id());
    ASSERT_AWAIT_READY(ret);
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    YRLOG_DEBUG("Resource2DWithInstances position 3 : resource unit is {}.", ToString(*rView.Get()));

    // check top level
    EXPECT_EQ(rView.Get()->fragment_size(), 1);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(4));
    EXPECT_TRUE(rView.Get()->capacity() == unit1.capacity());
    rInstances = rView.Get()->instances();
    ASSERT_TRUE(rInstances.find(inst1.instanceid()) == rInstances.end());
    ASSERT_TRUE(rInstances.find(inst2.instanceid()) == rInstances.end());
    ASSERT_TRUE(rInstances.find(inst3.instanceid()) == rInstances.end());
    ASSERT_TRUE(rInstances.find(inst4.instanceid()) == rInstances.end());

    ASSERT_TRUE(rView.Get()->fragment().find(unit1.id()) != rView.Get()->fragment().end());
    fragInst = rView.Get()->fragment().at(unit1.id());
    EXPECT_TRUE(fragInst.fragment().size() == 2);
    EXPECT_TRUE(fragInst.capacity() == unit1.capacity());
    EXPECT_TRUE(fragInst.instances_size() == 4);
    rSubUnit1 = fragInst.fragment().begin()->second;
    rSubUnit2 = (++fragInst.fragment().begin())->second;
    EXPECT_TRUE(rSubUnit1.instances().size() == 2);
    EXPECT_TRUE(rSubUnit1.capacity() == subUnit1.capacity());
    EXPECT_TRUE(rSubUnit2.instances().size() == 2);
    EXPECT_TRUE(rSubUnit2.capacity() == subUnit2.capacity());

    // delete unit 1
    ret = viewPtr->DeleteResourceUnit(unit1.id());
    ASSERT_AWAIT_READY(ret);
    rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    YRLOG_DEBUG("Resource2DWithInstances position 4 : resource unit is {}.", ToString(*rView.Get()));

    // check top level
    EXPECT_EQ(rView.Get()->fragment_size(), 0);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(0));
    EXPECT_TRUE(IsEmpty(rView.Get()->capacity()));
    EXPECT_TRUE(IsEmpty(rView.Get()->allocatable()));
    EXPECT_TRUE(IsEmpty(rView.Get()->actualuse()));
}

TEST_F(ResourceViewTest, UpdateResourceUnitWithNpuResource)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto rView = viewPtr->GetResourceView();
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    auto unit0 = Get1DResourceUnitWithNpu();
    auto unit1 = Get1DResourceUnitWithNpu();
    auto ret = viewPtr->AddResourceUnit(unit0);
    ASSERT_AWAIT_READY(ret);
    ret = viewPtr->AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);

    ASSERT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_TRUE(rView.Get()->capacity() == (unit1.capacity() + unit0.capacity()));
    EXPECT_TRUE(rView.Get()->allocatable() == (unit1.allocatable() + unit0.allocatable()));
    EXPECT_TRUE(rView.Get()->actualuse() == (unit1.actualuse() + unit0.actualuse()));
    auto res = rView.Get()->capacity().resources()
                   .at(resource_view::NPU_RESOURCE_NAME+ "/310").vectors().values()
                   .at(resource_view::HETEROGENEOUS_MEM_KEY).vectors().begin()->second.values();
    EXPECT_EQ(res.at(0), 100);
    EXPECT_EQ(res.at(1), 100);
    EXPECT_EQ(res.at(7), 100);

    // update actual
    auto unit2 = unit1;
    // keep internal uuid equal
    auto tmp = unit2.mutable_actualuse()->mutable_resources()->at(resource_view::NPU_RESOURCE_NAME+"/310").mutable_vectors()
        ->mutable_values()
        ->at(resource_view::HETEROGENEOUS_MEM_KEY).mutable_vectors()->begin()->second.mutable_values();
    tmp->Clear();
    for (int i=0;i<8;i++){
        tmp->Add(21);
    }
    auto unptr = std::make_shared<ResourceUnit>(unit2);
    ret = viewPtr->UpdateResourceUnit(unptr, UpdateType::UPDATE_ACTUAL);
    ASSERT_AWAIT_READY(ret);
    rView = viewPtr->GetResourceView();
    EXPECT_TRUE(rView.Get()->capacity() == (unit0.capacity() + unit1.capacity()));
    EXPECT_TRUE(rView.Get()->allocatable() == (unit0.allocatable() + unit1.allocatable()));
    EXPECT_TRUE(rView.Get()->actualuse() == (unit0.actualuse() + unit2.actualuse()));
    auto fragInst = rView.Get()->fragment().at(unit2.id());
    EXPECT_TRUE(fragInst.capacity() == unit2.capacity());
    EXPECT_TRUE(fragInst.allocatable() == unit2.allocatable());
    EXPECT_TRUE(fragInst.actualuse() == unit2.actualuse());
}

TEST_F(ResourceViewTest, UpdateResourceUnit)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto rView = viewPtr->GetResourceView();
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    auto unit1 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit1);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit1.allocatable());
    auto unit2 = Get1DResourceUnit();
    unit2.set_alias("unit2");
    GenerateMinimumUnitBucketInfo(unit2);
    auto ret = viewPtr->AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);
    ret = viewPtr->AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);
    rView = viewPtr->GetResourceView();
    ASSERT_EQ(rView.Get()->fragment_size(), 2);
    EXPECT_TRUE(rView.Get()->capacity() == (unit1.capacity() + unit2.capacity()));
    EXPECT_TRUE(rView.Get()->allocatable() == (unit1.capacity() + unit2.capacity()));
    EXPECT_TRUE(rView.Get()->actualuse() == (unit1.actualuse() + unit2.actualuse()));
    auto bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 2);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 1);

    auto unit5 = std::make_shared<ResourceUnit>(unit2);
    auto resources2 = GetCpuResources();
    (*unit5->mutable_capacity()) = resources2;
    (*unit5->mutable_allocatable()) = resources2;
    (*unit5->mutable_actualuse()) = resources2;
    auto unit5Tmp = *unit5;
    ret = viewPtr->UpdateResourceUnit(unit5, UpdateType::UPDATE_ACTUAL);
    ASSERT_AWAIT_READY(ret);
    rView = viewPtr->GetResourceView();
    EXPECT_TRUE(rView.Get()->capacity() == (unit1.capacity() + unit2.capacity()));
    EXPECT_TRUE(rView.Get()->allocatable() == (unit1.allocatable() + unit2.allocatable()));
    EXPECT_TRUE(rView.Get()->actualuse() == (unit1.actualuse() + unit5Tmp.actualuse()));
    auto fragInst = rView.Get()->fragment().at(unit2.id());
    EXPECT_TRUE(fragInst.capacity() == unit2.capacity());
    EXPECT_TRUE(fragInst.allocatable() == unit2.allocatable());
    EXPECT_TRUE(fragInst.actualuse() == unit5Tmp.actualuse());
    EXPECT_TRUE(fragInst.alias() == "unit2");
}

TEST_F(ResourceViewTest, ResourceUnitWithInstances)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    auto view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 0);

    auto unit = Get1DResourceUnitWithInstances();
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit.allocatable());
    auto ret = resourceView.AddResourceUnit(unit);
    ASSERT_AWAIT_READY(ret);

    auto view2 = resourceView.GetResourceView();
    ASSERT_EQ(view2.Get()->fragment_size(), 1);
    EXPECT_EQ(view2.Get()->instances().size(), static_cast<uint32_t>(2));
    EXPECT_EQ(view2.Get()->instances().at(unit.instances().begin()->first).instanceid(),
              unit.instances().begin()->second.instanceid());
    EXPECT_EQ(view2.Get()->instances().at((++unit.instances().begin())->first).instanceid(),
              (++unit.instances().begin())->second.instanceid());
    auto bucket = view2.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().sharednum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit.id()).sharednum(), 1);

    auto rUnit = resourceView.GetResourceUnit(unit.id());
    ASSERT_TRUE(rUnit.Get().IsSome());
    EXPECT_EQ(rUnit.Get().Get().id(), unit.id());
    EXPECT_EQ(rUnit.Get().Get().instances().at(unit.instances().begin()->first).instanceid(),
              unit.instances().begin()->second.instanceid());
    EXPECT_EQ((rUnit.Get().Get().instances().at((++unit.instances().begin())->first)).instanceid(),
              (++unit.instances().begin())->second.instanceid());

    // delete unit
    ret = viewPtr->DeleteResourceUnit(unit.id());
    ASSERT_AWAIT_READY(ret);
    auto rView = viewPtr->GetResourceView();
    EXPECT_EQ(rView.Get()->fragment_size(), 0);
    EXPECT_TRUE(rView.Get()->capacity() == Get0CpuMemResources());
    EXPECT_TRUE(rView.Get()->allocatable() == Get0CpuMemResources());
    EXPECT_TRUE(rView.Get()->fragment().find(unit.id()) == rView.Get()->fragment().end());
}

TEST_F(ResourceViewTest, GetUnitByInstReqID)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    auto view = resourceView.GetResourceView();
    EXPECT_EQ(view.Get()->fragment_size(), 0);

    auto unit1 = Get1DResourceUnitWithInstances();
    auto unit2 = Get1DResourceUnitWithInstances();
    auto reqID11 = unit1.instances().begin()->second.requestid();
    auto reqID12 = (++unit1.instances().begin())->second.requestid();
    auto reqID21 = unit2.instances().begin()->second.requestid();
    auto reqID22 = (++unit2.instances().begin())->second.requestid();
    auto ret = resourceView.AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);
    ret = resourceView.AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);

    auto view2 = resourceView.GetResourceView();
    ASSERT_EQ(view2.Get()->fragment_size(), 2);
    EXPECT_EQ(view2.Get()->instances().size(), static_cast<uint32_t>(4));
    auto retUnitID = resourceView.GetUnitByInstReqID("");
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_TRUE(retUnitID.Get().IsNone());

    retUnitID = resourceView.GetUnitByInstReqID("X");
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_TRUE(retUnitID.Get().IsNone());

    retUnitID = resourceView.GetUnitByInstReqID(reqID11);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_TRUE(retUnitID.Get().IsSome());
    EXPECT_TRUE(retUnitID.Get().Get() == unit1.id());

    retUnitID = resourceView.GetUnitByInstReqID(reqID12);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_TRUE(retUnitID.Get().IsSome());
    EXPECT_TRUE(retUnitID.Get().Get() == unit1.id());

    retUnitID = resourceView.GetUnitByInstReqID(reqID21);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_TRUE(retUnitID.Get().IsSome());
    EXPECT_TRUE(retUnitID.Get().Get() == unit2.id());

    retUnitID = resourceView.GetUnitByInstReqID(reqID22);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_TRUE(retUnitID.Get().IsSome());
    EXPECT_TRUE(retUnitID.Get().Get() == unit2.id());

    ret = resourceView.DeleteResourceUnit(unit2.id());
    ASSERT_AWAIT_READY(ret);

    retUnitID = resourceView.GetUnitByInstReqID(reqID11);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_TRUE(retUnitID.Get().IsSome());
    EXPECT_TRUE(retUnitID.Get().Get() == unit1.id());

    retUnitID = resourceView.GetUnitByInstReqID(reqID12);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_TRUE(retUnitID.Get().IsSome());
    EXPECT_TRUE(retUnitID.Get().Get() == unit1.id());

    retUnitID = resourceView.GetUnitByInstReqID(reqID21);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_FALSE(retUnitID.Get().IsSome());

    retUnitID = resourceView.GetUnitByInstReqID(reqID22);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_FALSE(retUnitID.Get().IsSome());

    ret = resourceView.DeleteResourceUnit(unit1.id());
    ASSERT_AWAIT_READY(ret);

    retUnitID = resourceView.GetUnitByInstReqID(reqID11);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_FALSE(retUnitID.Get().IsSome());

    retUnitID = resourceView.GetUnitByInstReqID(reqID12);
    ASSERT_AWAIT_READY(retUnitID);
    EXPECT_FALSE(retUnitID.Get().IsSome());

    ret = resourceView.AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);
    ret = resourceView.AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);
}

TEST_F(ResourceViewTest, AddResourceUnitError)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto view = viewPtr->GetResourceView();
    ASSERT_EQ(view.Get()->fragment_size(), 0);

    auto unit = Get1DResourceUnit();
    unit.clear_id();
    EXPECT_FALSE(viewPtr->AddResourceUnit(unit).Get().IsOk());

    unit = Get1DResourceUnit();
    unit.clear_capacity();
    EXPECT_FALSE(viewPtr->AddResourceUnit(unit).Get().IsOk());

    unit = Get1DResourceUnit();
    unit.clear_allocatable();
    EXPECT_FALSE(viewPtr->AddResourceUnit(unit).Get().IsOk());

    unit = Get1DResourceUnit();
    unit.mutable_capacity()->mutable_resources()->clear();
    EXPECT_FALSE(viewPtr->AddResourceUnit(unit).Get().IsOk());

    unit = Get1DResourceUnit();
    unit.mutable_allocatable()->mutable_resources()->clear();
    EXPECT_FALSE(viewPtr->AddResourceUnit(unit).Get().IsOk());

    unit = Get1DResourceUnit();
    unit.mutable_capacity()->mutable_resources()->begin()->second.mutable_scalar()->set_value(-1.0);
    EXPECT_FALSE(viewPtr->AddResourceUnit(unit).Get().IsOk());

    unit = Get1DResourceUnit();
    unit.mutable_allocatable()->mutable_resources()->begin()->second.mutable_scalar()->set_value(-1.0);
    EXPECT_FALSE(viewPtr->AddResourceUnit(unit).Get().IsOk());

    unit = Get1DResourceUnit();
    ASSERT_TRUE(viewPtr->AddResourceUnit(unit).Get().IsOk());
    EXPECT_FALSE(viewPtr->AddResourceUnit(unit).Get().IsOk());
}

TEST_F(ResourceViewTest, DeleteResourceUnitError)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto view = viewPtr->GetResourceView();
    ASSERT_EQ(view.Get()->fragment_size(), 0);

    auto unit = Get1DResourceUnit();
    ASSERT_TRUE(viewPtr->AddResourceUnit(unit).Get().IsOk());

    EXPECT_FALSE(viewPtr->DeleteResourceUnit("").Get().IsOk());
    EXPECT_FALSE(viewPtr->DeleteResourceUnit("X").Get().IsOk());
    EXPECT_FALSE(viewPtr->DeleteResourceUnit(unit.id() + "X").Get().IsOk());
}

TEST_F(ResourceViewTest, UpdateResourceUnitError)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto view = viewPtr->GetResourceView();
    ASSERT_EQ(view.Get()->fragment_size(), 0);

    auto unit = std::make_shared<ResourceUnit>(Get1DResourceUnit());
    unit->clear_id();
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());

    unit = std::make_shared<ResourceUnit>(Get1DResourceUnit());
    unit->clear_capacity();
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());

    unit = std::make_shared<ResourceUnit>(Get1DResourceUnit());
    unit->clear_allocatable();
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());

    unit = std::make_shared<ResourceUnit>(Get1DResourceUnit());
    unit->mutable_capacity()->mutable_resources()->clear();
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());

    unit = std::make_shared<ResourceUnit>(Get1DResourceUnit());
    unit->mutable_allocatable()->mutable_resources()->clear();
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());

    unit = std::make_shared<ResourceUnit>(Get1DResourceUnit());
    unit->mutable_capacity()->mutable_resources()->begin()->second.mutable_scalar()->set_value(-1.0);
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());

    unit = std::make_shared<ResourceUnit>(Get1DResourceUnit());
    unit->mutable_allocatable()->mutable_resources()->begin()->second.mutable_scalar()->set_value(-1.0);
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());

    unit = std::make_shared<ResourceUnit>(Get1DResourceUnit());
    ASSERT_TRUE(viewPtr->AddResourceUnit(*unit).Get().IsOk());
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_STATIC).Get().IsOk());
    (*unit->mutable_id()) = unit->id() + "X";
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());
    unit->mutable_id()->clear();
    unit->set_revision(unit->revision() + 1);
    EXPECT_FALSE(viewPtr->UpdateResourceUnit(unit, UpdateType::UPDATE_ACTUAL).Get().IsOk());
}

TEST_F(ResourceViewTest, AddInstancesError)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    auto unit1 = Get1DResourceUnit();
    auto inst = Get1DInstance();
    (*inst.mutable_schedulerchain()->Add()) = unit1.id();
    inst.set_unitid(unit1.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instancesOrig;
    instancesOrig.emplace(inst.instanceid(), resource_view::InstanceAllocatedInfo{ inst, nullptr });
    ASSERT_TRUE(viewPtr->AddResourceUnit(unit1).Get().IsOk());
    ASSERT_TRUE(viewPtr->AddInstances(instancesOrig).Get().IsOk());
    EXPECT_FALSE(viewPtr->AddInstances(instancesOrig).Get().IsOk());
    ASSERT_TRUE(viewPtr->DeleteInstances({ inst.instanceid() }).Get().IsOk());

    std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
    instances = instancesOrig;
    instances.clear();
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());

    instances.clear();
    auto inst1 = inst;
    (*inst1.mutable_instanceid()) = inst1.instanceid() + "X";
    instances.insert({ inst.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr } });
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());

    instances.clear();
    inst1 = inst;
    (*inst1.mutable_instanceid()) = inst1.instanceid() + "X";
    instances.insert({ "X", resource_view::InstanceAllocatedInfo{ inst1, nullptr } });
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());

    instances.clear();
    inst1 = inst;
    instances.insert({ "", resource_view::InstanceAllocatedInfo{ inst1, nullptr } });
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());

    instances.clear();
    inst1 = inst;
    instances.insert({ "X", resource_view::InstanceAllocatedInfo{ inst1, nullptr } });
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());

    instances.clear();
    inst1 = inst;
    inst1.clear_resources();
    instances.insert({ inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr } });
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());

    instances.clear();
    inst1 = inst;
    inst1.mutable_resources()->mutable_resources()->begin()->second.mutable_scalar()->set_value(-1);
    instances.insert({ inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr } });
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());

    instances.clear();
    inst1 = inst;
    inst1.set_unitid("");
    instances.insert({ inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr } });
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());

    instances.clear();
    inst1 = inst;
    inst1.set_unitid("tmp");
    instances.insert({ inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr } });
    EXPECT_FALSE(viewPtr->AddInstances(instances).Get().IsOk());
}

TEST_F(ResourceViewTest, DeleteInstancesError)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    auto unit1 = Get1DResourceUnit();
    auto inst = Get1DInstance();
    std::vector<std::string> idsOri;
    idsOri.push_back(inst.instanceid());
    (*inst.mutable_schedulerchain()->Add()) = unit1.id();
    inst.set_unitid(unit1.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instancesOrig;
    instancesOrig.emplace(inst.instanceid(), resource_view::InstanceAllocatedInfo{ inst, nullptr });
    ASSERT_TRUE(viewPtr->AddResourceUnit(unit1).Get().IsOk());
    ASSERT_TRUE(viewPtr->AddInstances(instancesOrig).Get().IsOk());
    ASSERT_TRUE(viewPtr->DeleteInstances(idsOri).Get().IsOk());
    ASSERT_TRUE(viewPtr->AddInstances(instancesOrig).Get().IsOk());

    auto ids = idsOri;
    ids.clear();
    ASSERT_FALSE(viewPtr->DeleteInstances(ids).Get().IsOk());

    ids = idsOri;
    ids[0] = ids[0] + "X";
    ASSERT_FALSE(viewPtr->DeleteInstances(ids).Get().IsOk());

    ids = idsOri;
    ASSERT_TRUE(viewPtr->DeleteInstances(ids).Get().IsOk());
}

TEST_F(ResourceViewTest, GetResourceUnitError)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto rView = viewPtr->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    auto unit1 = Get1DResourceUnit();
    auto inst = Get1DInstance();
    (*inst.mutable_schedulerchain()->Add()) = unit1.id();
    inst.set_unitid(unit1.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instancesOrig;
    instancesOrig.emplace(inst.instanceid(), resource_view::InstanceAllocatedInfo{ inst, nullptr });
    ASSERT_TRUE(viewPtr->AddResourceUnit(unit1).Get().IsOk());
    ASSERT_TRUE(viewPtr->AddInstances(instancesOrig).Get().IsOk());
    ASSERT_TRUE(viewPtr->GetResourceUnit(unit1.id()).Get().IsSome());

    std::string unitID;
    ASSERT_FALSE(viewPtr->GetResourceUnit(unitID).Get().IsSome());

    unitID = unit1.id() + "X";
    ASSERT_FALSE(viewPtr->GetResourceUnit(unitID).Get().IsSome());
}

TEST_F(ResourceViewTest, ResourceUnitIncreamentChange)
{
    litebus::Future<Status> ret;
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    auto view = resourceView.GetResourceView();
    auto initRevision = view.Get()->revision();
    EXPECT_EQ(int(initRevision), 0);

    // 1.add resourceunit
    auto unit = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit);
    ret = resourceView.AddResourceUnit(unit);
    ASSERT_AWAIT_READY(ret);

    auto changes = resourceView.GetVersionChanges();
    auto current_revision = 1;
    auto changeTimes = 1;
    EXPECT_EQ(changes.size(), changeTimes);
    EXPECT_EQ(changes[current_revision].Changed_case(), ResourceUnitChange::kAddition);
    EXPECT_EQ(changes[current_revision].addition().resourceunit().id(), unit.id());

     // 2.add instance
     auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = unit.id();
    inst1.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
    instances.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    ret = resourceView.AddInstances(instances);
    ASSERT_AWAIT_READY(ret);

    changes = resourceView.GetVersionChanges();
    current_revision += 1;
    changeTimes += 1;
    EXPECT_EQ(changes.size(), changeTimes);
    EXPECT_EQ(changes[current_revision].Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(changes[current_revision].resourceunitid(), unit.id());
    EXPECT_EQ(changes[current_revision].modification().instancechanges(0).instance().unitid(), unit.id());
    EXPECT_EQ(changes[current_revision].modification().instancechanges(0).changetype(), InstanceChange::ADD);

    // 3.del instance
    std::vector<std::string> ids1;
    ids1.push_back(inst1.instanceid());
    ret = resourceView.DeleteInstances(ids1);
    ASSERT_AWAIT_READY(ret);

    changes = resourceView.GetVersionChanges();
    current_revision += 1;
    changeTimes += 1;
    EXPECT_EQ(changes.size(), changeTimes);
    EXPECT_EQ(changes[current_revision].Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(changes[current_revision].resourceunitid(), unit.id());
    EXPECT_EQ(changes[current_revision].modification().instancechanges(0).changetype(), InstanceChange::DELETE);

    // 4.update status
    ret = resourceView.UpdateUnitStatus(unit.id(), resource_view::UnitStatus::EVICTING);
    ASSERT_AWAIT_READY(ret);

    changes = resourceView.GetVersionChanges();
    current_revision += 1;
    changeTimes += 1;
    EXPECT_EQ(changes.size(), changeTimes);
    EXPECT_EQ(changes[current_revision].Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(changes[current_revision].resourceunitid(), unit.id());
    EXPECT_EQ(changes[current_revision].modification().statuschange().status(),
        static_cast<uint32_t>(resource_view::UnitStatus::EVICTING));

    // 5.del resourceunit
    ret = resourceView.DeleteResourceUnit(unit.id());
    ASSERT_AWAIT_READY(ret);

    changes = resourceView.GetVersionChanges();
    current_revision += 1;
    changeTimes += 1;
    EXPECT_EQ(changes.size(), changeTimes);
    EXPECT_EQ(changes[current_revision].Changed_case(), ResourceUnitChange::kDeletion);
    EXPECT_EQ(changes[current_revision].resourceunitid(), unit.id());
}

TEST_F(ResourceViewTest, MergeResourceViewChanges)
{
    litebus::Future<Status> ret;
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    auto initRevision = 0;
    auto masterRevisionInPullRequest = 0;
    auto currentLocalRevision = 0;

    // Modify the local resourceview
    // 1.add resourceunit
    auto currentRevision1 = 1;
    auto unit = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit);
    ret = resourceView.AddResourceUnit(unit);
    ASSERT_AWAIT_READY(ret);

    // 2.modify resourceunit -- add instance1
    auto currentRevision2 = 2;
    auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = unit.id();
    inst1.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    inst1.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances1;
    instances1.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    ret = resourceView.AddInstances(instances1);
    ASSERT_AWAIT_READY(ret);

    // 3.modify resourceunit -- add instance2
    auto currentRevision3 = 3;
    auto inst2 = Get1DInstance();
    (*inst2.mutable_schedulerchain()->Add()) = unit.id();
    inst2.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    inst2.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    ret = resourceView.AddInstances(instances2);
    ASSERT_AWAIT_READY(ret);

    // 4.modify resourceunit -- delete instance2
    auto currentRevision4 = 4;
    std::vector<std::string> ids;
    ids.push_back(inst2.instanceid());
    ret = resourceView.DeleteInstances(ids);
    ASSERT_AWAIT_READY(ret);

    // 5.modify resourceunit -- add instance2
    auto currentRevision5 = 5;
    ret = resourceView.AddInstances(instances2);
    ASSERT_AWAIT_READY(ret);

    // 6.modify resourceunit -- modify status
    auto currentRevision6 = 6;
    ret = resourceView.UpdateUnitStatus(unit.id(), resource_view::UnitStatus::EVICTING);
    ASSERT_AWAIT_READY(ret);

    // 7.delete resourceunit
    auto currentRevision7 = 7;
    ret = resourceView.DeleteResourceUnit(unit.id());
    ASSERT_AWAIT_READY(ret);

    // There are multiple situations to summarize the local resource view
    // Situation 1: add resourceunit
    ResourceUnitChanges result1;
    masterRevisionInPullRequest = initRevision;
    currentLocalRevision = currentRevision1;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result1);
    const ResourceUnitChange &change1 = result1.changes(0);
    EXPECT_EQ(result1.endrevision(), currentLocalRevision);
    EXPECT_EQ(result1.changes_size(), 1);
    EXPECT_EQ(change1.resourceunitid(), unit.id());
    EXPECT_EQ(change1.Changed_case(), ResourceUnitChange::kAddition);
    EXPECT_EQ(change1.addition().resourceunit().id(), unit.id());

    // Situation 2: modify resourceunit -- add instance
    ResourceUnitChanges result2;
    masterRevisionInPullRequest = currentRevision1;
    currentLocalRevision = currentRevision2;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result2);
    const ResourceUnitChange &change2 = result2.changes(0);
    EXPECT_EQ(result2.endrevision(), currentLocalRevision);
    EXPECT_EQ(result2.changes_size(), 1);
    EXPECT_EQ(change2.resourceunitid(), unit.id());
    EXPECT_EQ(change2.Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(change2.modification().instancechanges(0).changetype(), InstanceChange::ADD);
    EXPECT_EQ(change2.modification().instancechanges(0).instance().instanceid(), inst1.instanceid());

    // Situation 3: del resourceunit
    ResourceUnitChanges result3;
    masterRevisionInPullRequest = currentRevision6;
    currentLocalRevision = currentRevision7;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result3);
    const ResourceUnitChange &change3 = result3.changes(0);
    EXPECT_EQ(result3.endrevision(), currentLocalRevision);
    EXPECT_EQ(result3.changes_size(), 1);
    EXPECT_EQ(change3.resourceunitid(), unit.id());
    EXPECT_EQ(change3.Changed_case(), ResourceUnitChange::kDeletion);

    // Situation 4: add resourceunit + modify resourceunit(modify status)
    //          --> add resourceunit
    ResourceUnitChanges result4;
    masterRevisionInPullRequest = initRevision;
    currentLocalRevision = currentRevision6;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result4);
    const ResourceUnitChange &change4 = result4.changes(0);
    EXPECT_EQ(result4.endrevision(), currentLocalRevision);
    EXPECT_EQ(result4.changes_size(), 1);
    EXPECT_EQ(change4.resourceunitid(), unit.id());
    EXPECT_EQ(change4.Changed_case(), ResourceUnitChange::kAddition);
    EXPECT_EQ(change4.addition().resourceunit().status(),  static_cast<uint32_t>(resource_view::UnitStatus::EVICTING));

    // Situation 5: add resourceunit + any changes + delete resourceunit
    //          --> no changes
    ResourceUnitChanges result5;
    masterRevisionInPullRequest = initRevision;
    currentLocalRevision = currentRevision7;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result5);
    EXPECT_EQ(result5.endrevision(), currentLocalRevision);
    EXPECT_EQ(result5.changes_size(), 0);

    // Situation 6: modify resourceunit(add instance) + modify resourceunit(delete instance)
    //          --> no changes
    ResourceUnitChanges result6;
    masterRevisionInPullRequest = currentRevision2;
    currentLocalRevision = currentRevision4;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result6);
    EXPECT_EQ(result6.endrevision(), currentLocalRevision);
    EXPECT_EQ(result6.changes_size(), 0);

    // Situation 7: modify resourceunit(modify status) + delete resourceunit
    //          --> delete resourceunit
    ResourceUnitChanges result7;
    masterRevisionInPullRequest = currentRevision4;
    currentLocalRevision = currentRevision7;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result7);
    const ResourceUnitChange &change7 = result7.changes(0);
    EXPECT_EQ(result7.endrevision(), currentLocalRevision);
    EXPECT_EQ(result7.changes_size(), 1);
    EXPECT_EQ(change7.resourceunitid(), unit.id());
    EXPECT_EQ(change7.Changed_case(), ResourceUnitChange::kDeletion);

    // Situation 8: modify resourceunit(add instance1) + modify resourceunit(add instance2) +
    //              modify resourceunit(delete instance2)
    //          --> modify resourceunit(add instance1)
    ResourceUnitChanges result8;
    masterRevisionInPullRequest = currentRevision1;
    currentLocalRevision = currentRevision4;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result8);
    const ResourceUnitChange &change8 = result8.changes(0);
    EXPECT_EQ(result8.endrevision(), currentLocalRevision);
    EXPECT_EQ(change8.Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(change8.modification().instancechanges(0).changetype(), InstanceChange::ADD);
    EXPECT_EQ(change8.modification().instancechanges(0).instance().instanceid(), inst1.instanceid());

    // Situation 9: modify resourceunit(delete instance2) + modify resourceunit(add instance2)
    //          --> modify resourceunit(delete instance2 + add instance2)
    ResourceUnitChanges result9;
    masterRevisionInPullRequest = currentRevision3;
    currentLocalRevision = currentRevision5;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result9);
    EXPECT_EQ(result9.changes_size(), 1);
    const ResourceUnitChange &change9 = result9.changes(0);
    EXPECT_EQ(result9.endrevision(), currentLocalRevision);
    EXPECT_EQ(change9.Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(change9.modification().instancechanges(0).changetype(), InstanceChange::DELETE);
    EXPECT_EQ(change9.modification().instancechanges(0).instance().instanceid(), inst2.instanceid());
    EXPECT_EQ(change9.modification().instancechanges(1).changetype(), InstanceChange::ADD);
    EXPECT_EQ(change9.modification().instancechanges(1).instance().instanceid(), inst2.instanceid());
}

TEST_F(ResourceViewTest, MergeInstanceChange)
{
    litebus::Future<Status> ret;
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    auto masterRevisionInPullRequest = 0;
    auto currentLocalRevision = 0;

    // Modify the local resourceview
    // 1.add resourceunit
    auto unit = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit);
    ret = resourceView.AddResourceUnit(unit);
    ASSERT_AWAIT_READY(ret);

    // 2.modify resourceunit -- add instance1
    auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = unit.id();
    inst1.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    inst1.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances1;
    instances1.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    ret = resourceView.AddInstances(instances1);
    ASSERT_AWAIT_READY(ret);

    // 3.modify resourceunit -- add instance2
    auto currentRevision3 = 3;
    auto inst2 = Get1DInstance();
    (*inst2.mutable_schedulerchain()->Add()) = unit.id();
    inst2.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    inst2.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    ret = resourceView.AddInstances(instances2);
    ASSERT_AWAIT_READY(ret);

    // 4.modify resourceunit -- delete instance2
    auto currentRevision4 = 4;
    std::vector<std::string> ids;
    ids.push_back(inst2.instanceid());
    ret = resourceView.DeleteInstances(ids);
    ASSERT_AWAIT_READY(ret);

    // 5.modify resourceunit -- add instance2
    auto currentRevision5 = 5;
    ret = resourceView.AddInstances(instances2);
    ASSERT_AWAIT_READY(ret);

    // 6.modify resourceunit -- delete instance2
    auto currentRevision6 = 6;
    std::vector<std::string> ids1;
    ids1.push_back(inst2.instanceid());
    ret = resourceView.DeleteInstances(ids1);
    ASSERT_AWAIT_READY(ret);

    // 7.modify resourceunit -- add instance2
    auto currentRevision7 = 7;
    ret = resourceView.AddInstances(instances2);
    ASSERT_AWAIT_READY(ret);

    // 8.modify resourceunit -- delete instance2
    auto currentRevision8 = 8;
    std::vector<std::string> ids2;
    ids2.push_back(inst2.instanceid());
    ret = resourceView.DeleteInstances(ids2);
    ASSERT_AWAIT_READY(ret);

    // Situation 1: delete instance2 + add instance2
    //          --> delete instance2 + add instance2
    auto result = ResourceUnitChanges{};
    masterRevisionInPullRequest = currentRevision3;
    currentLocalRevision = currentRevision5;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result);
    EXPECT_EQ(result.endrevision(), currentLocalRevision);
    EXPECT_EQ(result.changes_size(), 1);
    auto change = result.changes(0);
    EXPECT_EQ(change.Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(change.modification().instancechanges(0).changetype(), InstanceChange::DELETE);
    EXPECT_EQ(change.modification().instancechanges(0).instance().instanceid(), inst2.instanceid());
    EXPECT_EQ(change.modification().instancechanges(1).changetype(), InstanceChange::ADD);
    EXPECT_EQ(change.modification().instancechanges(1).instance().instanceid(), inst2.instanceid());

    // Situation 2: add instance2 + delete instance2
    //          --> no changes
    result = ResourceUnitChanges{};
    masterRevisionInPullRequest = currentRevision4;
    currentLocalRevision = currentRevision6;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result);
    EXPECT_EQ(result.endrevision(), currentLocalRevision);
    EXPECT_EQ(result.changes_size(), 0);

    // Situation 3: delete instance2 + add instance2 + delete instance2
    //          --> delete instance2
    result = ResourceUnitChanges{};
    masterRevisionInPullRequest = currentRevision3;
    currentLocalRevision = currentRevision6;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result);
    EXPECT_EQ(result.changes_size(), 1);
    change = result.changes(0);
    EXPECT_EQ(result.endrevision(), currentLocalRevision);
    EXPECT_EQ(change.Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(change.modification().instancechanges(0).changetype(), InstanceChange::DELETE);
    EXPECT_EQ(change.modification().instancechanges(0).instance().instanceid(), inst2.instanceid());

    // Situation 4: add instance2 + delete instance2 + add instance2
    //          --> add instance2
    result = ResourceUnitChanges{};
    masterRevisionInPullRequest = currentRevision4;
    currentLocalRevision = currentRevision7;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result);
    EXPECT_EQ(result.changes_size(), 1);
    change = result.changes(0);
    EXPECT_EQ(result.endrevision(), currentLocalRevision);
    EXPECT_EQ(change.Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(change.modification().instancechanges(0).changetype(), InstanceChange::ADD);
    EXPECT_EQ(change.modification().instancechanges(0).instance().instanceid(), inst2.instanceid());

    // Situation 5: delete instance2 + add instance2 + delete instance2 + add instance2
    //          --> delete instance2 + add instance2
    result = ResourceUnitChanges{};
    masterRevisionInPullRequest = currentRevision3;
    currentLocalRevision = currentRevision7;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result);
    EXPECT_EQ(result.changes_size(), 1);
    change = result.changes(0);
    EXPECT_EQ(change.Changed_case(), ResourceUnitChange::kModification);
    EXPECT_EQ(change.modification().instancechanges(0).changetype(), InstanceChange::DELETE);
    EXPECT_EQ(change.modification().instancechanges(0).instance().instanceid(), inst2.instanceid());
    EXPECT_EQ(change.modification().instancechanges(1).changetype(), InstanceChange::ADD);
    EXPECT_EQ(change.modification().instancechanges(1).instance().instanceid(), inst2.instanceid());

    // Situation 6: add instance2 + delete instance2 + add instance2 + delete instance2
    //          --> no changes
    result = ResourceUnitChanges{};
    masterRevisionInPullRequest = currentRevision4;
    currentLocalRevision = currentRevision8;
    resourceView.MergeLocalResourceViewChanges(masterRevisionInPullRequest, currentLocalRevision, result);
    EXPECT_EQ(result.changes_size(), 0);
}

TEST_F(ResourceViewTest, MaintainsResourceUnitChangesOrderDuringMerge)
{
    litebus::Future<Status> ret;
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    // 1.add resourceunit 1
    auto currentRevision1 = 1;
    auto unit1 = Get1DResourceUnit();
    ret = resourceView.AddResourceUnit(unit1);
    ASSERT_AWAIT_READY(ret);

    // 2.add resourceunit 2
    auto unit2 = Get1DResourceUnit();
    ret = resourceView.AddResourceUnit(unit2);
    ASSERT_AWAIT_READY(ret);

    // 3.del resourceunit 1
    ret = resourceView.DeleteResourceUnit(unit1.id());
    ASSERT_AWAIT_READY(ret);

    // 4.add resourceunit 3
    auto currentRevision4 = 4;
    auto unit3 = Get1DResourceUnit();
    ret = resourceView.AddResourceUnit(unit3);
    ASSERT_AWAIT_READY(ret);

    // resourceunit included in the change: add resourceunit 2, del resourceunit 1, add resourceunit 3
    ResourceUnitChanges result;
    resourceView.MergeLocalResourceViewChanges(currentRevision1, currentRevision4, result);

    EXPECT_EQ(result.changes_size(), 3);
    EXPECT_EQ(result.changes(0).resourceunitid(), unit2.id());
    EXPECT_EQ(result.changes(0).Changed_case(), ResourceUnitChange::kAddition);
    EXPECT_EQ(result.changes(0).addition().resourceunit().id(), unit2.id());

    EXPECT_EQ(result.changes(1).resourceunitid(), unit1.id());
    EXPECT_EQ(result.changes(1).Changed_case(), ResourceUnitChange::kDeletion);

    EXPECT_EQ(result.changes(2).resourceunitid(), unit3.id());
    EXPECT_EQ(result.changes(2).Changed_case(), ResourceUnitChange::kAddition);
    EXPECT_EQ(result.changes(2).addition().resourceunit().id(), unit3.id());
}

TEST_F(ResourceViewTest, MaintainsInstanceChangesOrderDuringMerge)
{
    litebus::Future<Status> ret;
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;

    // 1.add resourceunit
    auto unit = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit);
    ret = resourceView.AddResourceUnit(unit);
    ASSERT_AWAIT_READY(ret);

    // 2.add inst1
    auto currentRevision2 = 2;
    auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = unit.id();
    inst1.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances1;
    instances1.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    ret = resourceView.AddInstances(instances1);
    ASSERT_AWAIT_READY(ret);

    // 3.add inst2
    auto inst2 = Get1DInstance();
    (*inst2.mutable_schedulerchain()->Add()) = unit.id();
    inst2.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    ret = resourceView.AddInstances(instances2);
    ASSERT_AWAIT_READY(ret);

    // 4.add inst3
    auto inst3 = Get1DInstance();
    (*inst3.mutable_schedulerchain()->Add()) = unit.id();
    inst3.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances3;
    instances3.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });
    ret = resourceView.AddInstances(instances3);
    ASSERT_AWAIT_READY(ret);

    // 5.del inst1
    std::vector<std::string> ids1;
    ids1.push_back(inst1.instanceid());
    ret = resourceView.DeleteInstances(ids1);
    ASSERT_AWAIT_READY(ret);

    // 6.del inst3
    std::vector<std::string> ids2;
    ids2.push_back(inst3.instanceid());
    ret = resourceView.DeleteInstances(ids2);
    ASSERT_AWAIT_READY(ret);

    // 7.del inst2
    std::vector<std::string> ids3;
    ids3.push_back(inst2.instanceid());
    ret = resourceView.DeleteInstances(ids3);
    ASSERT_AWAIT_READY(ret);

    // 8.add inst4
    auto inst4 = Get1DInstance();
    (*inst4.mutable_schedulerchain()->Add()) = unit.id();
    inst4.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances4;
    instances4.emplace(inst4.instanceid(), resource_view::InstanceAllocatedInfo{ inst4, nullptr });
    ret = resourceView.AddInstances(instances4);
    ASSERT_AWAIT_READY(ret);

    // 9.add inst5
    auto currentRevision9 = 9;
    auto inst5 = Get1DInstance();
    (*inst5.mutable_schedulerchain()->Add()) = unit.id();
    inst5.set_unitid(unit.id());
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances5;
    instances5.emplace(inst5.instanceid(), resource_view::InstanceAllocatedInfo{ inst5, nullptr });
    ret = resourceView.AddInstances(instances5);
    ASSERT_AWAIT_READY(ret);

    // Instances included in the change: delete instance1, add instance4, add instance5
    ResourceUnitChanges result;
    resourceView.MergeLocalResourceViewChanges(currentRevision2, currentRevision9, result);

    EXPECT_EQ(result.changes_size(), 1);
    const ResourceUnitChange &change = result.changes(0);
    EXPECT_EQ(change.modification().instancechanges_size(), 3);
    EXPECT_EQ(change.modification().instancechanges(0).changetype(), InstanceChange::DELETE);
    EXPECT_EQ(change.modification().instancechanges(0).instance().instanceid(), inst1.instanceid());

    EXPECT_EQ(change.modification().instancechanges(1).changetype(), InstanceChange::ADD);
    EXPECT_EQ(change.modification().instancechanges(1).instance().instanceid(), inst4.instanceid());

    EXPECT_EQ(change.modification().instancechanges(2).changetype(), InstanceChange::ADD);
    EXPECT_EQ(change.modification().instancechanges(2).instance().instanceid(), inst5.instanceid());
}

TEST_F(ResourceViewTest, PullResourceUnitTest)
{
    class MockUpdateHandler {
    public:
        MOCK_METHOD(void, Update, (), ());
    };
    auto handler = std::make_shared<MockUpdateHandler>();
    EXPECT_CALL(*handler, Update).WillRepeatedly(testing::Return());
    ResourcePoller::SetInterval(100);
    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    parent->AddResourceUpdateHandler([handler](){ handler->Update();});
    parent->TriggerTryPull();
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    child->ToReady();
    child->UpdateDomainUrlForLocal(domainUrl_);
    auto rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);
    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode +"-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());
    add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                         litebus::GetActor(childNode + "-ResourceViewActor")->GetAID().Url());
    ASSERT_FALSE(add.Get().IsOk());

    auto childView = child->GetFullResourceView().Get();
    auto curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());

    // 1.add resourceunit
    auto currentRevision1 = 1;
    auto unit = Get1DResourceUnit();
    auto agentId = unit.id();
    GenerateMinimumUnitBucketInfo(unit);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit.allocatable());
    auto begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->AddResourceUnit(unit).Get().IsOk());

    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 1);
    EXPECT_TRUE(rView.Get()->capacity() == unit.capacity());
    auto rUnitCapacity = rView.Get()->fragment().at(agentId).capacity();
    EXPECT_TRUE(rUnitCapacity == unit.capacity());
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localRevisionInDomain, currentRevision1);
    auto &bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId).monopolynum(), 1);
    bucket = rView.Get()->fragment().at(agentId).bucketindexs().at(unitProportion).buckets().at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.allocatable().at(agentId).monopolynum(), 1);

    auto emptySnap = parent->GetUnitSnapshotInfo("");
    EXPECT_EQ(emptySnap.Get().version(), 0);
    EXPECT_TRUE(emptySnap.Get().localviewinittime().empty());

    childView = child->GetFullResourceView().Get();
    curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());

    // 2.modify resourceunit -- add instance1(shared)
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = agentId;
    inst1.set_unitid(agentId);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances1;
    instances1.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->AddInstances(instances1).Get().IsOk());

    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(1));
    EXPECT_TRUE(rUnitCapacity - rView.Get()->allocatable() == inst1.resources());
    auto &rInstances1 = rView.Get()->instances();
    EXPECT_TRUE(rInstances1.find(inst1.instanceid()) != rInstances1.end());
    auto &rInst1 = rInstances1.at(inst1.instanceid());
    EXPECT_EQ(rInst1.instanceid(), inst1.instanceid());
    auto &rAgentInstances1 = rView.Get()->fragment().at(agentId).instances();
    EXPECT_TRUE(rAgentInstances1.find(inst1.instanceid()) != rAgentInstances1.end());

    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId).monopolynum(), 0);
    bucket = rView.Get()->fragment().at(agentId).bucketindexs().at(unitProportion).buckets().at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.allocatable().at(agentId).monopolynum(), 0);
    childView = child->GetFullResourceView().Get();
    curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());

    // 3.modify resourceunit -- add instance2(shared)
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    auto rOriginAllocatable = rView.Get()->allocatable();
    auto inst2 = Get1DInstance();
    (*inst2.mutable_schedulerchain()->Add()) = agentId;
    inst2.set_unitid(agentId);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->AddInstances(instances2).Get().IsOk());

    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(2));
    auto rNewAllocatable = rView.Get()->allocatable();
    EXPECT_TRUE(rOriginAllocatable - rNewAllocatable == inst2.resources());
    auto &rInstances2 = rView.Get()->instances();
    EXPECT_TRUE(rInstances2.find(inst2.instanceid()) != rInstances2.end());
    auto &rInst2 = rInstances2.at(inst2.instanceid());
    EXPECT_EQ(rInst2.instanceid(), inst2.instanceid());
    auto &rAgentInstances2 = rView.Get()->fragment().at(agentId).instances();
    EXPECT_TRUE(rAgentInstances2.find(inst2.instanceid()) != rAgentInstances2.end());

    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId).monopolynum(), 0);

    childView = child->GetFullResourceView().Get();
    curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());

    // 4.del instance1(shared)
    std::vector<std::string> ids1;
    ids1.push_back(inst1.instanceid());
    begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->DeleteInstances(ids1).Get().IsOk());

    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->instances().size(), 1);
    EXPECT_EQ(rView.Get()->fragment().at(agentId).instances().size(), 1);
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId).monopolynum(), 0);
    bucket = rView.Get()->fragment().at(agentId).bucketindexs().at(unitProportion).buckets().at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.allocatable().at(agentId).monopolynum(), 0);

    childView = child->GetFullResourceView().Get();
    curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());

    // 5.del instance2(shared)
    std::vector<std::string> ids2;
    ids2.push_back(inst2.instanceid());
    begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->DeleteInstances(ids2).Get().IsOk());

    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->instances().size(), 0);
    EXPECT_EQ(rView.Get()->fragment().at(agentId).instances().size(), 0);
    EXPECT_TRUE(rUnitCapacity == rView.Get()->allocatable());
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId).monopolynum(), 1);
    bucket = rView.Get()->fragment().at(agentId).bucketindexs().at(unitProportion).buckets().at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.allocatable().at(agentId).monopolynum(), 1);

    childView = child->GetFullResourceView().Get();
    curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());

    // 6.update status
    auto currentRevision6 = 6;
    begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->UpdateUnitStatus(agentId, resource_view::UnitStatus::EVICTING).Get().IsOk());

    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->fragment().at(agentId).status(),
        static_cast<uint32_t>(resource_view::UnitStatus::EVICTING));
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localRevisionInDomain, currentRevision6);

    childView = child->GetFullResourceView().Get();
    curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());

    // 7.modify resourceunit -- add instance3(monopoly)
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    auto inst3 = Get1DInstance();
    (*inst3.mutable_schedulerchain()->Add()) = agentId;
    inst3.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    inst3.set_unitid(agentId);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances3;
    instances3.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });
    begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->AddInstances(instances3).Get().IsOk());

    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    EXPECT_EQ(rView.Get()->instances().size(), static_cast<uint32_t>(1));
    EXPECT_TRUE(IsEmpty(rView.Get()->allocatable()));
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId).monopolynum(), 0);

    childView = child->GetFullResourceView().Get();
    curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());

    // 8.del resourceunit
    begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->DeleteResourceUnit(agentId).Get().IsOk());

    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);
    bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);

    childView = child->GetFullResourceView().Get();
    curSnap = parent->GetUnitSnapshotInfo(childNode).Get();
    EXPECT_EQ(curSnap.version(), childView->revision());
    EXPECT_EQ(curSnap.localviewinittime(), childView->viewinittime());
}

TEST_F(ResourceViewTest, PullResourceUnitWithSwitchDomainUrlTest)
{
    ResourcePoller::SetInterval(50);
    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    parent->TriggerTryPull();
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    child->ToReady();
    auto rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);
    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode +"-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());

    // local add resourceunit
    auto currentRevision1 = 1;
    auto unit = Get1DResourceUnit();
    auto agentId = unit.id();
    GenerateMinimumUnitBucketInfo(unit);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit.allocatable());
    auto begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->AddResourceUnit(unit).Get().IsOk());

    // 1.The domain URL in local is empty
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    // 2.The domain URL in local does not match the actual domain URL
    child->UpdateDomainUrlForLocal("fake_url");
    begin = litebus::TimeWatch::Now();
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    // 3.The domain URL in local is not empty
    // wait until the domain has completed the resourceview update
    child->UpdateDomainUrlForLocal(domainUrl_);
    begin = litebus::TimeWatch::Now();
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 1);
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localRevisionInDomain, currentRevision1);
}

TEST_F(ResourceViewTest, TestSwitchDomainUrl)
{
    std::string node = LOCAL_RESOUCE_VIEW_ID;
    auto view = resource_view::ResourceView::CreateResourceView(node, CHILD_PARAM);
    view->ToReady();

    auto rView = view->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    auto originChildViewInitTime = rView.Get()->viewinittime();

    // 1.initialize the domain URL for the first time
    view->UpdateDomainUrlForLocal(domainUrl_);
    rView = view->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->viewinittime(), originChildViewInitTime);

    // 2.Update with the different domain URL
    view->UpdateDomainUrlForLocal("new_url");
    rView = view->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_NE(rView.Get()->viewinittime(), originChildViewInitTime);
    originChildViewInitTime = rView.Get()->viewinittime();

    // 3.Update with the same domain URL
    view->UpdateDomainUrlForLocal("new_url");
    rView = view->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->viewinittime(), originChildViewInitTime);
}

TEST_F(ResourceViewTest, AddEmptyUnitWithUrl)
{
    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    // adding a empty local view
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    child->ToReady();

    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode +"-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());

    ASSERT_TRUE(parent->CheckLocalExistInDomainView(childNode));
    auto localViewInfo = parent->GetLocalInfoInDomain(childNode);
    EXPECT_EQ(localViewInfo.localViewInitTime, child->GetResourceView().Get()->viewinittime());
    EXPECT_EQ(localViewInfo.localRevisionInDomain, 0);
}

TEST_F(ResourceViewTest, AddNonEmptyUnitWithUrl)
{
    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    child->ToReady();

    // adding 3 agent units to local ivew, with the revision of local view being 3
    auto unit1 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit1);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit1.allocatable());
    ASSERT_TRUE(child->AddResourceUnit(unit1).Get().IsOk());
    auto unit2 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit2);
    ASSERT_TRUE(child->AddResourceUnit(unit2).Get().IsOk());
    auto unit3 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit3);
    ASSERT_TRUE(child->AddResourceUnit(unit3).Get().IsOk());

    // modify resourceunit1 -- add instance1(shared)
    // monopoly num : 1 --> 0
    // the revision of local view being 4
    auto unit1Id = unit1.id();
    auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = unit1Id;
    inst1.set_unitid(unit1Id);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances1;
    instances1.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    ASSERT_TRUE(child->AddInstances(instances1).Get().IsOk());

    // modify resourceunit2 -- add instance1(shared) and delete instance1(shared)
    // monopoly num : 1 --> 0 --> 1
    // the revision of local view being 6
    auto unit2Id = unit2.id();
    auto inst2 = Get1DInstance();
    (*inst2.mutable_schedulerchain()->Add()) = unit2Id;
    inst2.set_unitid(unit2Id);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    ASSERT_TRUE(child->AddInstances(instances2).Get().IsOk());

    std::vector<std::string> ids2;
    ids2.push_back(inst2.instanceid());
    ASSERT_TRUE(child->DeleteInstances(ids2).Get().IsOk());

    // regist local view to domain view
    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode +"-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());

    ASSERT_TRUE(parent->CheckLocalExistInDomainView(childNode));
    auto localViewInfo = parent->GetLocalInfoInDomain(childNode);
    EXPECT_EQ(localViewInfo.localRevisionInDomain, 6);
    EXPECT_EQ(localViewInfo.agentIDs.size(), 3);

    auto rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 3);
    EXPECT_TRUE(rView.Get()->capacity() == (unit1.capacity()+unit2.capacity()+unit3.capacity()));
    auto &bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 2);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit2.id()).monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit3.id()).monopolynum(), 1);

    auto runit1 = rView.Get()->fragment().at(unit1.id());
    bucket = runit1.mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit1.id()).monopolynum(), 0);

    auto runit2 = rView.Get()->fragment().at(unit2.id());
    bucket = runit2.mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit2.id()).monopolynum(), 1);

    auto runit3 = rView.Get()->fragment().at(unit3.id());
    bucket = runit3.mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(unit3.id()).monopolynum(), 1);
}

TEST_F(ResourceViewTest, CheckLatestReportUpdateInDomain)
{
    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    child->ToReady();

    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode +"-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());

    ASSERT_TRUE(parent->CheckLocalExistInDomainView(childNode));
    auto localViewInfo = parent->GetLocalInfoInDomain(childNode);
    auto localRevisionInDomain = localViewInfo.localRevisionInDomain;
    auto localViewInitTimeOrigin = child->GetResourceView().Get()->viewinittime();
    EXPECT_EQ(localViewInfo.localViewInitTime, child->GetResourceView().Get()->viewinittime());
    EXPECT_EQ(localRevisionInDomain, 0);

    // revision of local info in domain is 3
    std::shared_ptr<ResourceUnitChanges> resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_localviewinittime(localViewInitTimeOrigin);
    resourceUnitChanges->set_startrevision(0);
    resourceUnitChanges->set_endrevision(3);
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());

    // 1.
    // localInfoInDomain:     revision      : 3       ; localViewInitTime = localViewInitTimeOrigin
    // no pending update reqeust in domain
    // reportedUpdateReqInfo: RevisionRange : [0, 10] ; localViewInitTime = localViewInitTimeOrigin
    // --> discarding the new reported request
    resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_localviewinittime(localViewInitTimeOrigin);
    resourceUnitChanges->set_startrevision(0);
    resourceUnitChanges->set_endrevision(10);
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_FALSE(add.Get().IsOk());

    std::shared_ptr<ResourceUnitChanges> latestReportChanges = parent->GetLatestReportChanges(LOCAL_RESOUCE_VIEW_ID);
    EXPECT_EQ(latestReportChanges, nullptr);

    // add a pending update request, the revision range is: [3, 10]
    resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_localviewinittime(localViewInitTimeOrigin);
    resourceUnitChanges->set_startrevision(3);
    resourceUnitChanges->set_endrevision(10);
    parent->SetLatestReportChanges(childNode, *resourceUnitChanges);

    // 2.
    // localInfoInDomain:     revision      : 3       ; localViewInitTime = localViewInitTimeOrigin
    // pendingUpdateReqInfo:  RevisionRange : [3, 10] ; localViewInitTime = localViewInitTimeOrigin
    // reportedUpdateReqInfo: RevisionRange : [3, 15] ; localViewInitTime = localViewInitTimeOrigin
    // --> keeping the new reported request
    resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_localviewinittime(localViewInitTimeOrigin);
    resourceUnitChanges->set_startrevision(3);
    resourceUnitChanges->set_endrevision(15);
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());

    latestReportChanges = parent->GetLatestReportChanges(LOCAL_RESOUCE_VIEW_ID);
    EXPECT_EQ(latestReportChanges->startrevision(), 3);
    EXPECT_EQ(latestReportChanges->endrevision(), 15);

    // 3.
    // localInfoInDomain:     revision      : 3       ; localViewInitTime = localViewInitTimeOrigin
    // pendingUpdateReqInfo:  RevisionRange : [3, 15] ; localViewInitTime = localViewInitTimeOrigin
    // reportedUpdateReqInfo: RevisionRange : [0, 5]  ; localViewInitTime = localViewInitTimeNew1
    // --> keeping the new reported request
    resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_startrevision(0);
    resourceUnitChanges->set_endrevision(5);
    std::string localViewInitTimeNew1 = "localViewInitTimeNew1";
    resourceUnitChanges->set_localviewinittime(localViewInitTimeNew1);
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());

    latestReportChanges = parent->GetLatestReportChanges(LOCAL_RESOUCE_VIEW_ID);
    EXPECT_EQ(latestReportChanges->startrevision(), 0);
    EXPECT_EQ(latestReportChanges->endrevision(), 5);
    EXPECT_EQ(latestReportChanges->localviewinittime(), localViewInitTimeNew1);

    // 4.
    // localInfoInDomain:     revision      : 3       ; localViewInitTime = localViewInitTimeOrigin
    // pendingUpdateReqInfo:  RevisionRange : [0, 5]  ; localViewInitTime = localViewInitTimeNew1
    // reportedUpdateReqInfo: RevisionRange : [0, 7]  ; localViewInitTime = localViewInitTimeNew2
    // --> keeping the new reported request
    resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_startrevision(0);
    resourceUnitChanges->set_endrevision(7);
    std::string localViewInitTimeNew2 = "localViewInitTimeNew2";
    resourceUnitChanges->set_localviewinittime(localViewInitTimeNew2);
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());

    latestReportChanges = parent->GetLatestReportChanges(LOCAL_RESOUCE_VIEW_ID);
    EXPECT_EQ(latestReportChanges->startrevision(), 0);
    EXPECT_EQ(latestReportChanges->endrevision(), 7);
    EXPECT_EQ(latestReportChanges->localviewinittime(), localViewInitTimeNew2);

    // 5.
    // localInfoInDomain:     revision      : 3       ; localViewInitTime = localViewInitTimeOrigin
    // pendingUpdateReqInfo:  RevisionRange : [0, 7]  ; localViewInitTime = localViewInitTimeNew2
    // reportedUpdateReqInfo: RevisionRange : [3, 20] ; localViewInitTime = localViewInitTimeOrigin
    // --> discarding the new reported request
    resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_startrevision(3);
    resourceUnitChanges->set_endrevision(20);
    resourceUnitChanges->set_localviewinittime(localViewInitTimeOrigin);
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_FALSE(add.Get().IsOk());

    latestReportChanges = parent->GetLatestReportChanges(LOCAL_RESOUCE_VIEW_ID);
    EXPECT_EQ(latestReportChanges->startrevision(), 0);
    EXPECT_EQ(latestReportChanges->endrevision(), 7);
    EXPECT_EQ(latestReportChanges->localviewinittime(), localViewInitTimeNew2);

    // 6.
    // localInfoInDomain:     revision      : 3       ; localViewInitTime = localViewInitTimeOrigin
    // pendingUpdateReqInfo:  RevisionRange : [0, 7]  ; localViewInitTime = localViewInitTimeNew2
    // reportedUpdateReqInfo: RevisionRange : [0, 20] ; localViewInitTime = localViewInitTimeNew2
    // --> keeping the new reported request
    resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_startrevision(0);
    resourceUnitChanges->set_endrevision(20);
    resourceUnitChanges->set_localviewinittime(localViewInitTimeNew2);
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());

    latestReportChanges = parent->GetLatestReportChanges(LOCAL_RESOUCE_VIEW_ID);
    EXPECT_EQ(latestReportChanges->startrevision(), 0);
    EXPECT_EQ(latestReportChanges->endrevision(), 20);
    EXPECT_EQ(latestReportChanges->localviewinittime(), localViewInitTimeNew2);

    // 7.
    // localInfoInDomain:     revision      : 3       ; localViewInitTime = localViewInitTimeOrigin
    // pendingUpdateReqInfo:  RevisionRange : [0, 20] ; localViewInitTime = localViewInitTimeNew2
    // reportedUpdateReqInfo: RevisionRange : [5, 15] ; localViewInitTime = localViewInitTimeNew3
    // --> discarding the new reported request
    resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_startrevision(5);
    resourceUnitChanges->set_endrevision(15);
    std::string localViewInitTimeNew3 = "localViewInitTimeNew3";
    resourceUnitChanges->set_localviewinittime(localViewInitTimeNew3);
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_FALSE(add.Get().IsOk());

    latestReportChanges = parent->GetLatestReportChanges(LOCAL_RESOUCE_VIEW_ID);
    EXPECT_EQ(latestReportChanges->startrevision(), 0);
    EXPECT_EQ(latestReportChanges->endrevision(), 20);
    EXPECT_EQ(latestReportChanges->localviewinittime(), localViewInitTimeNew2);
}

TEST_F(ResourceViewTest, PullMultiResourceUnitChanges)
{
    ResourcePoller::SetInterval(1000);
    auto parent = resource_view::ResourceView::CreateResourceView("parent", PARENT_PARAM);
    parent->TriggerTryPull();
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    child->ToReady();
    child->UpdateDomainUrlForLocal(domainUrl_);
    auto rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode +"-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());

    // 1.add resourceunit1
    auto unit1 = Get1DResourceUnit();
    auto agentId1 = unit1.id();
    GenerateMinimumUnitBucketInfo(unit1);
    auto [unitProportion, unitMem] = GetMinimumUnitBucketIndex(unit1.allocatable());
    auto begin = litebus::TimeWatch::Now();
    ASSERT_TRUE(child->AddResourceUnit(unit1).Get().IsOk());

    // 2.add resourceunit2
    auto unit2 = Get1DResourceUnit();
    auto agentId2 = unit2.id();
    GenerateMinimumUnitBucketInfo(unit2);
    ASSERT_TRUE(child->AddResourceUnit(unit2).Get().IsOk());

    // 3.modify resourceunit1 -- add instance1(shared)
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = agentId1;
    inst1.set_unitid(agentId1);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances1;
    instances1.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    ASSERT_TRUE(child->AddInstances(instances1).Get().IsOk());

    // 4.modify resourceunit -- add instance2(shared)
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    auto inst2 = Get1DInstance();
    (*inst2.mutable_schedulerchain()->Add()) = agentId1;
    inst2.set_unitid(agentId1);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    ASSERT_TRUE(child->AddInstances(instances2).Get().IsOk());

    // 5.del instance1(shared)
    std::vector<std::string> ids1;
    ids1.push_back(inst1.instanceid());
    ASSERT_TRUE(child->DeleteInstances(ids1).Get().IsOk());

    // 6.modify resourceunit -- add instance3(shared)
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    auto inst3 = Get1DInstance();
    (*inst3.mutable_schedulerchain()->Add()) = agentId1;
    inst3.set_unitid(agentId1);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances3;
    instances3.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });
    ASSERT_TRUE(child->AddInstances(instances3).Get().IsOk());

    // 7.modify resourceunit2 -- add instance4(monopoly)
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    auto inst4 = Get1DInstance();
    (*inst4.mutable_schedulerchain()->Add()) = agentId2;
    inst4.set_unitid(agentId2);
    inst3.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances4;
    instances4.emplace(inst4.instanceid(), resource_view::InstanceAllocatedInfo{ inst4, nullptr });
    ASSERT_TRUE(child->AddInstances(instances4).Get().IsOk());

    // 8.add resourceunit3
    auto unit3 = Get1DResourceUnit();
    auto agentId3 = unit3.id();
    GenerateMinimumUnitBucketInfo(unit3);
    ASSERT_TRUE(child->AddResourceUnit(unit3).Get().IsOk());

    // result in domain
    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 2000; });
    rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 3);
    EXPECT_TRUE(rView.Get()->capacity() == (unit1.capacity()+unit2.capacity()+unit3.capacity()));
    auto rUnitCapacity = rView.Get()->fragment().at(agentId1).capacity();
    EXPECT_TRUE(rUnitCapacity == unit1.capacity());
    rUnitCapacity = rView.Get()->fragment().at(agentId2).capacity();
    EXPECT_TRUE(rUnitCapacity == unit2.capacity());
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localRevisionInDomain, 8);
    auto &bucket = rView.Get()->mutable_bucketindexs()->at(unitProportion).mutable_buckets()->at(unitMem);
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId1).monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId2).monopolynum(), 0);
    EXPECT_EQ(bucket.mutable_allocatable()->at(agentId3).monopolynum(), 1);
}

void CheckRevionChanges(std::unique_ptr<ResourceView> &parent, std::unique_ptr<ResourceView> &child,
                        uint64_t currentRevisionInLocal)
{
    auto begin = litebus::TimeWatch::Now();
    // wait until the domain has completed the resourceview update
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 200; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(LOCAL_RESOUCE_VIEW_ID).localRevisionInDomain, currentRevisionInLocal);

    auto changes = child->GetResourceViewChanges();
    ASSERT_EQ(changes.Get()->startrevision(), currentRevisionInLocal);
    ASSERT_EQ(changes.Get()->endrevision(), currentRevisionInLocal);
}

TEST_F(ResourceViewTest, UpdateLastReportedRevision)
{
    ResourcePoller::SetInterval(100);
    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    parent->TriggerTryPull();
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    child->ToReady();
    child->UpdateDomainUrlForLocal(domainUrl_);
    auto rView = parent->GetResourceView();
    ASSERT_AWAIT_READY(rView);
    ASSERT_EQ(rView.Get()->fragment_size(), 0);

    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode +"-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());

    // 1.add resourceunit1
    auto currentRevisionInLocal = 1;
    auto unit1 = Get1DResourceUnit();
    auto agentId1 = unit1.id();
    GenerateMinimumUnitBucketInfo(unit1);
    ASSERT_TRUE(child->AddResourceUnit(unit1).Get().IsOk());
    CheckRevionChanges(parent, child, currentRevisionInLocal);

    // 2.modify resourceunit1 -- add instance1(shared)
    auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = agentId1;
    inst1.set_unitid(agentId1);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances1;
    instances1.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    ASSERT_TRUE(child->AddInstances(instances1).Get().IsOk());
    currentRevisionInLocal += 1;
    CheckRevionChanges(parent, child, currentRevisionInLocal);

    // 3.modify resourceunit -- add instance2(shared)
    auto inst2 = Get1DInstance();
    (*inst2.mutable_schedulerchain()->Add()) = agentId1;
    inst2.set_unitid(agentId1);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    ASSERT_TRUE(child->AddInstances(instances2).Get().IsOk());
    currentRevisionInLocal += 1;
    CheckRevionChanges(parent, child, currentRevisionInLocal);

    // 4.modify resourceunit -- add instance3(shared) + update instance1 status
    // 4.1 add instance3(shared)
    auto inst3 = Get1DInstance();
    (*inst3.mutable_schedulerchain()->Add()) = agentId1;
    inst3.set_unitid(agentId1);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances3;
    instances3.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });
    ASSERT_TRUE(child->AddInstances(instances3).Get().IsOk());
    // check lastReportedRevision -- LastReportedRevision = currentRevisionInLocal
    auto changes = child->GetResourceViewChanges();
    ASSERT_EQ(changes.Get()->startrevision(), currentRevisionInLocal);
    ASSERT_EQ(changes.Get()->endrevision(), currentRevisionInLocal + 1);
    currentRevisionInLocal += 1;

    // 4.2 update instance1 status
    ASSERT_TRUE(child->UpdateUnitStatus(agentId1, resource_view::UnitStatus::EVICTING).Get().IsOk());
    // check lastReportedRevision -- LastReportedRevision = currentRevisionInLocal + 1
    changes = child->GetResourceViewChanges();
    ASSERT_EQ(changes.Get()->startrevision(), currentRevisionInLocal);
    ASSERT_EQ(changes.Get()->endrevision(), currentRevisionInLocal + 1);
    currentRevisionInLocal += 1;

    // 5.empty changes -- delete fake instance
    std::vector<std::string> ids;
    ids.push_back("fake_instance");
    ASSERT_FALSE(child->DeleteInstances(ids).Get().IsOk());
    currentRevisionInLocal += 1;
    CheckRevionChanges(parent, child, currentRevisionInLocal);
}

void RecoverResourceView(std::unique_ptr<ResourceView> &parent, std::string localViewInitTimeOrigin,
                         ResourceUnit &unit)
{
    auto begin = litebus::TimeWatch::Now();
    auto resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(LOCAL_RESOUCE_VIEW_ID);
    resourceUnitChanges->set_localviewinittime(localViewInitTimeOrigin);
    resourceUnitChanges->set_startrevision(0);
    resourceUnitChanges->set_endrevision(1);

    Addition addition;
    (*addition.mutable_resourceunit()) = unit;
    ResourceUnitChange resourceUnitChange;
    resourceUnitChange.set_resourceunitid(unit.id());
    *resourceUnitChange.mutable_addition() = addition;

    *resourceUnitChanges->add_changes() = resourceUnitChange;
    auto add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(LOCAL_RESOUCE_VIEW_ID).localViewInitTime, localViewInitTimeOrigin);
}

TEST_F(ResourceViewTest, PullResourceUnitChangesError)
{
    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode +"-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());

    // revision of local info in domain is 0, localViewInitTime is localViewInitTimeOrigin
    ASSERT_TRUE(parent->CheckLocalExistInDomainView(childNode));
    auto localViewInfo = parent->GetLocalInfoInDomain(childNode);
    auto localRevisionInDomain = localViewInfo.localRevisionInDomain;
    auto localViewInitTimeOrigin = child->GetResourceView().Get()->viewinittime();
    EXPECT_EQ(localViewInfo.localViewInitTime, child->GetResourceView().Get()->viewinittime());
    EXPECT_EQ(localRevisionInDomain, 0);

    auto unit = Get1DResourceUnit();
    unit.set_ownerid(childNode);

    // 1.add resourceunit ok
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    // 2.add resourceunit fail -- ownerid of resourceunit is emtpty
    auto begin = litebus::TimeWatch::Now();
    auto resourceUnitChanges = std::make_shared<ResourceUnitChanges>();
    resourceUnitChanges->set_localid(childNode);
    resourceUnitChanges->set_localviewinittime(localViewInitTimeOrigin);
    resourceUnitChanges->set_startrevision(1);
    resourceUnitChanges->set_endrevision(2);

    Addition addition;
    (*addition.mutable_resourceunit()) = unit;
    // ownerid of resourceunit is emtpty
    addition.mutable_resourceunit()->clear_ownerid();
    ResourceUnitChange resourceUnitChange;
    resourceUnitChange.set_resourceunitid(unit.id());
    *resourceUnitChange.mutable_addition() = addition;
    *resourceUnitChanges->add_changes() = resourceUnitChange;

    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localViewInitTime, NEED_RECOVER_VIEW);

    // recover resourceview
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    // 3.add resourceunit fail -- AddResourceUnit error(unitId is empty)
    begin = litebus::TimeWatch::Now();
    auto change = *(resourceUnitChanges->mutable_changes(0));
    change.mutable_addition()->mutable_resourceunit()->set_ownerid(childNode);
    // unitId is empty
    change.mutable_addition()->mutable_resourceunit()->clear_id();
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localViewInitTime, NEED_RECOVER_VIEW);

    // recover resourceview
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    // 4.add resourceunit fail -- AddResourceUnit error
    begin = litebus::TimeWatch::Now();
    change = *(resourceUnitChanges->mutable_changes(0));
    change.mutable_addition()->mutable_resourceunit()->set_id(unit.id());
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localViewInitTime, NEED_RECOVER_VIEW);

    // recover resourceview
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    // 4.delete resourceunit fail -- unitId is empty
    begin = litebus::TimeWatch::Now();
    Deletion deletion;
    // resourceUnitId of resourceUnitChange is empty
    resourceUnitChange = ResourceUnitChange{};
    *resourceUnitChange.mutable_deletion() = deletion;

    resourceUnitChanges->clear_changes();
    *resourceUnitChanges->add_changes() = resourceUnitChange;
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localViewInitTime, NEED_RECOVER_VIEW);

    // recover resourceview
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    // 5.delete resourceunit fail -- agent not found
    begin = litebus::TimeWatch::Now();
    resourceUnitChange.set_resourceunitid("fake_agent_id");
    resourceUnitChanges->clear_changes();
    *resourceUnitChanges->add_changes() = resourceUnitChange;
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localViewInitTime, NEED_RECOVER_VIEW);

    // recover resourceview
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    // 6.add instance fail  -- agent not found
    begin = litebus::TimeWatch::Now();
    auto inst1 = Get1DInstance();
    inst1.set_unitid("fake_agent_id");
    InstanceChange instanceChange;
    instanceChange.set_changetype(InstanceChange::ADD);
    instanceChange.set_instanceid(inst1.instanceid());
    instanceChange.mutable_instance()->CopyFrom(inst1);

    Modification modification;
    *modification.add_instancechanges() = instanceChange;

    resourceUnitChange = ResourceUnitChange{};
    resourceUnitChange.set_resourceunitid("fake_agent_id");
    *resourceUnitChange.mutable_modification() = modification;
    resourceUnitChanges->clear_changes();
    *resourceUnitChanges->add_changes() = resourceUnitChange;
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localViewInitTime, NEED_RECOVER_VIEW);

    // recover resourceview
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    // 7.delete instance fail  -- instance not found
    begin = litebus::TimeWatch::Now();
    inst1.set_unitid(unit.id());
    instanceChange.set_changetype(InstanceChange::DELETE);
    instanceChange.mutable_instance()->CopyFrom(inst1);

    modification.clear_instancechanges();
    *modification.add_instancechanges() = instanceChange;

    resourceUnitChange = ResourceUnitChange{};
    resourceUnitChange.set_resourceunitid(unit.id());
    *resourceUnitChange.mutable_modification() = modification;
    resourceUnitChanges->clear_changes();
    *resourceUnitChanges->add_changes() = resourceUnitChange;
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localViewInitTime, NEED_RECOVER_VIEW);

    // recover resourceview
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    // 7.add instance fail  -- unitid is empty
    begin = litebus::TimeWatch::Now();
    instanceChange.set_changetype(InstanceChange::ADD);
    inst1.clear_unitid();
    instanceChange.mutable_instance()->CopyFrom(inst1);

    modification.clear_instancechanges();
    *modification.add_instancechanges() = instanceChange;

    resourceUnitChange = ResourceUnitChange{};
    resourceUnitChange.set_resourceunitid(unit.id());
    *resourceUnitChange.mutable_modification() = modification;
    resourceUnitChanges->clear_changes();
    *resourceUnitChanges->add_changes() = resourceUnitChange;
    add = parent->UpdateResourceUnitDelta(resourceUnitChanges);
    ASSERT_TRUE(add.Get().IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return (litebus::TimeWatch::Now() - begin) > 100; });
    ASSERT_EQ(parent->GetLocalInfoInDomain(childNode).localViewInitTime, NEED_RECOVER_VIEW);
}

std::vector<ValueCounter> GenerateIdleToRecycleLabel()
{
    resources::Value::Counter unitCount1;
    (*unitCount1.mutable_items())["unlimited"] = 1;

    resources::Value::Counter unitCount2;
    (*unitCount2.mutable_items())["0"] = 1;

    resources::Value::Counter unitCount3;
    (*unitCount3.mutable_items())["1"] = 1;

    resources::Value::Counter unitCount4;
    (*unitCount4.mutable_items())["-2"] = 1;

    resources::Value::Counter unitCount5;
    (*unitCount5.mutable_items())["XXX"] = 1;

    resources::Value::Counter unitCount6;
    (*unitCount6.mutable_items())["XXX"] = 1;
    (*unitCount6.mutable_items())["000"] = 1;

    return std::vector<ValueCounter>{unitCount1, unitCount2, unitCount3, unitCount4, unitCount5, unitCount6};
}

TEST_F(ResourceViewTest, idleToRecyclePodLabelParse)
{
    std::string aid = LOCAL_RESOUCE_VIEW_ID + "-ResourceViewActor";
    auto implActor = std::make_shared<ResourceViewActor>(aid, LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto idleToRecycleLabels = GenerateIdleToRecycleLabel();
    static const std::string IDLE_TO_RECYCLE = "yr-idle-to-recycle";
    auto unit1 = Get1DResourceUnit();
    EXPECT_EQ(implActor->TestParseRecyclePodLabel(unit1), 0);
    (*unit1.mutable_nodelabels())[IDLE_TO_RECYCLE] = idleToRecycleLabels[0];
    EXPECT_EQ(implActor->TestParseRecyclePodLabel(unit1), -1);
    (*unit1.mutable_nodelabels())[IDLE_TO_RECYCLE] = idleToRecycleLabels[1];
    EXPECT_EQ(implActor->TestParseRecyclePodLabel(unit1), 0);
    (*unit1.mutable_nodelabels())[IDLE_TO_RECYCLE] = idleToRecycleLabels[2];
    EXPECT_EQ(implActor->TestParseRecyclePodLabel(unit1), 1);
    (*unit1.mutable_nodelabels())[IDLE_TO_RECYCLE] = idleToRecycleLabels[3];
    EXPECT_EQ(implActor->TestParseRecyclePodLabel(unit1), 0);
    (*unit1.mutable_nodelabels())[IDLE_TO_RECYCLE] = idleToRecycleLabels[4];
    EXPECT_EQ(implActor->TestParseRecyclePodLabel(unit1), 0);
    (*unit1.mutable_nodelabels())[IDLE_TO_RECYCLE] = idleToRecycleLabels[5];
    EXPECT_EQ(implActor->TestParseRecyclePodLabel(unit1), 0);
}

/*
 * test for idleToRecyclePodCreate while add agent
 * // pod status is normal
 * 1. pod has idleToRecyclePod, value is -1 -> timer not set
 * 2. pod has idleToRecyclePod, value is 0 -> timer not set
 * 3. pod has idleToRecyclePod, value is > 0 -> timer is set
 * 4. pod does not have idleToRecyclePod and not enable tenant affinity -> timer not set
 * 5. idleToRecyclePod value is invalid-> timer not set
 * 6. pod does not have idleToRecyclePod and enable tenant affinity but not instance in it-> timer not set
 */
TEST_F(ResourceViewTest, idleToRecyclePodCreate)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;
    auto view = resourceView.GetResourceView();
    static const std::string IDLE_TO_RECYCLE = "yr-idle-to-recycle";
    auto idleToRecycleLabels = GenerateIdleToRecycleLabel();
    std::set<std::string> disabledAgent;
    std::atomic<int> count = 0;
    viewPtr->RegisterUnitDisableFunc([&disabledAgent, &count](const std::string &agentID){
        disabledAgent.emplace(agentID);
        count++;
    });

    {
        // 1. pod has idleToRecyclePod, value is -1 -> timer not set
        auto unit1 = Get1DResourceUnit();
        unit1.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[0]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit1));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);
    }
    {
        // 2. pod has idleToRecyclePod, value is 0 -> timer not set
        auto unit2 = Get1DResourceUnit();
        unit2.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[1]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit2));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit2.id()), 0);
    }
    {
        // 3. pod has idleToRecyclePod, value is > 0 -> timer is set
        auto unit3 = Get1DResourceUnit();
        unit3.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[2]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 1);
        EXPECT_AWAIT_TRUE([&] () -> bool { return resourceView.GetReuseTimers().count(unit3.id()) == 0; });
        EXPECT_EQ(count,1);
        EXPECT_EQ(disabledAgent.count(unit3.id()), 1);
    }
    {
        // 4. pod does not have idleToRecyclePod and not enable tenant affinity -> timer not set
        auto unit3 = Get1DResourceUnit();
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 0);
    }
    {
        // 5. idleToRecyclePod value is invalid-> timer not set
        auto unit3 = Get1DResourceUnit();
        unit3.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[3]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 0);
        auto unit4 = Get1DResourceUnit();
        unit4.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[4]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit4));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit4.id()), 0);
    }
    {
        // 6. pod does not have idleToRecyclePod and enable tenant affinity but not instance in it-> timer not set
        auto unit3 = Get1DResourceUnit();
        viewPtr->SetEnableTenantAffinity(true);
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 0);
    }
}

/*
 * test for idleToRecyclePodCreate while add agent
 * 1. pod has idleToRecyclePod, value is > 0, but pod is Recovering -> timer not set
 * 1.1 pod is idleToRecyclePod, pod is Recovering and change to running -> timer is set according idleToRecyclePod
 * 2. pod not idleToRecyclePod,  enable tenant affinity, but pod is Recovering -> timer not set
 * 2.1 pod not idleToRecyclePod, enable tenant affinity, pod is Recovering and change to running: have instance in it -> timer is set else not set
 */
TEST_F(ResourceViewTest, idleToRecyclePodRecovering)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;
    auto view = resourceView.GetResourceView();
    static const std::string IDLE_TO_RECYCLE = "yr-idle-to-recycle";
    auto idleToRecycleLabels = GenerateIdleToRecycleLabel();

    std::set<std::string> disabledAgent;
    std::atomic<int> count = 0;
    viewPtr->RegisterUnitDisableFunc([&disabledAgent, &count](const std::string &agentID){
        disabledAgent.emplace(agentID);
        count++;
    });

    {
        // 1. pod has idleToRecyclePod, value is > 0, but pod is Recovering -> timer not set
        auto unit1 = Get1DResourceUnit();
        unit1.set_status(static_cast<uint32_t>(UnitStatus::RECOVERING));
        unit1.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[0]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit1));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);

        // pod is idleToRecyclePod, pod is Recovering and change to running -> timer is set according idleToRecyclePod
        ASSERT_AWAIT_READY(resourceView.UpdateUnitStatus(unit1.id(), UnitStatus::NORMAL));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);
    }
    {
        // 1. pod has idleToRecyclePod, value is > 0, but pod is Recovering -> timer not set
        auto unit1 = Get1DResourceUnit();
        unit1.set_status(static_cast<uint32_t>(UnitStatus::RECOVERING));
        unit1.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[2]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit1));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);

        // pod is idleToRecyclePod, pod is Recovering and change to running -> timer is set according idleToRecyclePod
        ASSERT_AWAIT_READY(resourceView.UpdateUnitStatus(unit1.id(), UnitStatus::NORMAL));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 1);
        EXPECT_AWAIT_TRUE([&] () -> bool { return resourceView.GetReuseTimers().count(unit1.id()) == 0; });
        EXPECT_EQ(count,1);
        EXPECT_EQ(disabledAgent.count(unit1.id()), 1);
    }
    {
        // pod not idleToRecyclePod,  enable tenant affinity, but pod is Recovering -> timer not set
        auto unit3 = Get1DResourceUnit();
        unit3.set_status(static_cast<uint32_t>(UnitStatus::RECOVERING));
        viewPtr->SetEnableTenantAffinity(true);
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 0);

        // pod not idleToRecyclePod, enable tenant affinity, pod is Recovering and change to running: don't have instance in it -> timer not set
        ASSERT_AWAIT_READY(resourceView.UpdateUnitStatus(unit3.id(), UnitStatus::NORMAL));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 0);
    }
    {
        // pod not idleToRecyclePod,  enable tenant affinity, but pod is Recovering -> timer not set
        auto unit3 = Get1DResourceUnit();
        unit3.set_status(static_cast<uint32_t>(UnitStatus::RECOVERING));
        auto inst3 = Get1DInstance();
        inst3.set_functionproxyid(LOCAL_RESOUCE_VIEW_ID);
        viewPtr->SetEnableTenantAffinity(true);
        inst3.set_unitid(unit3.id());
        std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
        instances.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
        ASSERT_AWAIT_READY(resourceView.AddInstances(instances));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 0);

        // pod not idleToRecyclePod, enable tenant affinity, pod is Recovering and change to running: have instance in it -> timer is set
        ASSERT_AWAIT_READY(resourceView.UpdateUnitStatus(unit3.id(), UnitStatus::NORMAL));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 1);
        EXPECT_EQ(resourceView.GetAgentUsedMap().count(unit3.id()), 1);
        EXPECT_AWAIT_TRUE([&] () -> bool { return resourceView.GetReuseTimers().count(unit3.id()) == 0; });
        EXPECT_EQ(resourceView.GetAgentUsedMap().count(unit3.id()), 0);
        EXPECT_EQ(count,2);
        EXPECT_EQ(disabledAgent.count(unit3.id()), 1);
    }
}

/*
 * test for idleToRecyclePodCreate while add instance
 * pod has been set timer and in recycle, if not addInstance, pod will be recycled;
 * else agentCacheMap_ have instance
 */
TEST_F(ResourceViewTest, idleToRecyclePodAddInstance)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;
    auto view = resourceView.GetResourceView();
    static const std::string IDLE_TO_RECYCLE = "yr-idle-to-recycle";
    auto idleToRecycleLabels = GenerateIdleToRecycleLabel();
    std::set<std::string> disabledAgent;
    std::atomic<int> count = 0;
    viewPtr->RegisterUnitDisableFunc([&disabledAgent, &count](const std::string &agentID){
        disabledAgent.emplace(agentID);
        count++;
    });

    {
        // pod has been set timer and in recycle, if not addInstance, pod will be recycled;
        auto unit3 = Get1DResourceUnit();
        unit3.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[2]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 1);
        // wait for timer is clear
        EXPECT_AWAIT_TRUE([&] () -> bool { return resourceView.GetReuseTimers().count(unit3.id()) == 0; });
        EXPECT_EQ(count,1);
        EXPECT_EQ(disabledAgent.count(unit3.id()), 1);
    }
    {
        // pod has been set timer and in recycle, addInstance, pod will not be recycled;
        auto unit3 = Get1DResourceUnit();
        unit3.mutable_nodelabels()->insert({IDLE_TO_RECYCLE, idleToRecycleLabels[2]});
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 1);

        auto inst3 = Get1DInstance();
        inst3.set_unitid(unit3.id());
        std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
        instances.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });
        auto ret = resourceView.AddInstances(instances);
        ASSERT_AWAIT_READY(ret);
        EXPECT_TRUE(ret.Get().IsOk());

        auto cache = resourceView.GetAgentCacheMap();
        EXPECT_TRUE(cache.find(unit3.id()) != cache.end());
        EXPECT_TRUE(cache.find(unit3.id())->second.find(inst3.instanceid()) != cache.find(unit3.id())->second.end());
    }
}

/*
 * test for idleToRecyclePodCreate while delete instance
 * 1.pod have instance, delete instance, and still have instance, not delete
 * 2.pod have instance, delete instance, and do not have instance, recycle instance
 */
TEST_F(ResourceViewTest, idleToRecyclePodDeleteInstance)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;
    auto view = resourceView.GetResourceView();
    static const std::string IDLE_TO_RECYCLE = "yr-idle-to-recycle";
    auto idleToRecycleLabels = GenerateIdleToRecycleLabel();
    std::set<std::string> disabledAgent;
    std::atomic<int> count = 0;
    viewPtr->RegisterUnitDisableFunc([&disabledAgent, &count](const std::string &agentID) {
        disabledAgent.emplace(agentID);
        count++;
    });

    // pod has been set timer and in recycle, addInstance, pod will not be recycled;
    auto unit3 = Get1DResourceUnit();
    unit3.mutable_nodelabels()->insert({ IDLE_TO_RECYCLE, idleToRecycleLabels[2] });
    ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit3));
    EXPECT_EQ(resourceView.GetReuseTimers().count(unit3.id()), 1);

    auto inst3 = Get1DInstance();
    auto inst2 = Get1DInstance();
    auto inst1 = Get1DInstance();
    inst1.set_unitid(unit3.id());
    inst2.set_unitid(unit3.id());
    inst3.set_unitid(unit3.id());

    std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
    instances.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    instances.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    instances.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });

    auto ret = resourceView.AddInstances(instances);
    ASSERT_AWAIT_READY(ret);
    EXPECT_TRUE(ret.Get().IsOk());

    // pod have instance, delete instance, and still have instance, not delete
    std::vector<std::string> ids1;
    ids1.push_back(inst1.instanceid());
    ret = resourceView.DeleteInstances(ids1);
    ASSERT_AWAIT_READY(ret);
    EXPECT_TRUE(ret.Get().IsOk());
    auto cache = resourceView.GetAgentCacheMap();
    EXPECT_TRUE(cache.find(unit3.id()) != cache.end());
    EXPECT_TRUE(cache.find(unit3.id())->second.find(inst1.instanceid()) == cache.find(unit3.id())->second.end());
    EXPECT_EQ(count, 0);
    EXPECT_EQ(disabledAgent.count(unit3.id()), 0);

    // pod have instance, delete instance, and still have instance, not delete
    std::vector<std::string> ids2;
    ids2.push_back(inst2.instanceid());
    ret = resourceView.DeleteInstances(ids2);
    ASSERT_AWAIT_READY(ret);
    EXPECT_TRUE(ret.Get().IsOk());
    cache = resourceView.GetAgentCacheMap();
    EXPECT_TRUE(cache.find(unit3.id()) != cache.end());
    EXPECT_TRUE(cache.find(unit3.id())->second.find(inst2.instanceid()) == cache.find(unit3.id())->second.end());
    EXPECT_EQ(count, 0);
    EXPECT_EQ(disabledAgent.count(unit3.id()), 0);

    // pod delete instance, and do not have instance, recycle instance
    std::vector<std::string> ids3;
    ids3.push_back(inst3.instanceid());
    ret = resourceView.DeleteInstances(ids3);
    ASSERT_AWAIT_READY(ret);
    EXPECT_TRUE(ret.Get().IsOk());
    EXPECT_AWAIT_TRUE([&]() -> bool { return resourceView.GetReuseTimers().count(unit3.id()) == 0; });
    cache = resourceView.GetAgentCacheMap();
    EXPECT_TRUE(cache.find(unit3.id()) == cache.end());
    EXPECT_EQ(count, 1);
    EXPECT_EQ(disabledAgent.count(unit3.id()), 1);
}

/*
 * test for tenantAffinityPod while delete instance
 * 1.pod not set idlePod, enable tenantAffinity and delete actually instance -> set timer and recycle
 * 2.pod not set idlePod, enable tenantAffinity and delete actually instance, set timer and
 *   then add instance and then delete virtual instance -> set timer and recycle
 * 3.pod not set idlePod, enable tenantAffinity and add unit and then delete virtual instance -> not set timer and not recycle
 * 4.pod not set idlePod, enable tenantAffinity and add unit and then delete instance and add virtual instance,
 *   timer is executed, and then virtual instance is deleted  -> pod need to be recycled
 */
TEST_F(ResourceViewTest, tenantAffinityRecyclePodDeleteInstance)
{
    auto viewPtr = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    auto &resourceView = *viewPtr;
    auto view = resourceView.GetResourceView();
    static const std::string IDLE_TO_RECYCLE = "yr-idle-to-recycle";
    auto idleToRecycleLabels = GenerateIdleToRecycleLabel();
    viewPtr->SetEnableTenantAffinity(true);
    std::set<std::string> disabledAgent;
    std::atomic<int> count = 0;
    viewPtr->RegisterUnitDisableFunc([&disabledAgent, &count](const std::string &agentID) {
        disabledAgent.emplace(agentID);
        count++;
    });

    {
        // 1.pod not set idlePod, enable tenantAffinity and delete actually instance -> set timer and recycle
        auto unit1 = Get1DResourceUnit();
        unit1.mutable_nodelabels()->insert({ IDLE_TO_RECYCLE, idleToRecycleLabels[1] });
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit1));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);

        auto inst3 = Get1DInstance();
        auto inst2 = Get1DInstance();
        auto inst1 = Get1DInstance();
        inst1.set_unitid(unit1.id());
        inst2.set_unitid(unit1.id());
        inst3.set_unitid(unit1.id());
        inst1.set_tenantid("123456");
        inst2.set_tenantid("123456");
        inst3.set_tenantid("123456");

        std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
        instances.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
        instances.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
        instances.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });

        auto ret = resourceView.AddInstances(instances);
        ASSERT_AWAIT_READY(ret);
        EXPECT_TRUE(ret.Get().IsOk());

        std::vector<std::string> ids1{ inst1.instanceid() };
        ret = resourceView.DeleteInstances(ids1);
        ASSERT_AWAIT_READY(ret);
        EXPECT_TRUE(ret.Get().IsOk());
        auto cache = resourceView.GetAgentCacheMap();
        EXPECT_TRUE(cache.find(unit1.id()) != cache.end());
        EXPECT_TRUE(cache.find(unit1.id())->second.find(inst1.instanceid()) == cache.find(unit1.id())->second.end());
        EXPECT_EQ(count, 0);
        EXPECT_EQ(disabledAgent.count(unit1.id()), 0);

        std::vector<std::string> ids3;
        ids3.push_back(inst3.instanceid());
        ids3.push_back(inst2.instanceid());
        ret = resourceView.DeleteInstances(ids3);
        ASSERT_AWAIT_READY(ret);
        EXPECT_TRUE(ret.Get().IsOk());
        EXPECT_AWAIT_TRUE([&]() -> bool { return resourceView.GetReuseTimers().count(unit1.id()) == 0; });

        cache = resourceView.GetAgentCacheMap();
        EXPECT_TRUE(cache.find(unit1.id()) == cache.end());
        EXPECT_EQ(count, 1);
        EXPECT_EQ(disabledAgent.count(unit1.id()), 1);
    }

    {
        // pod not set idlePod, enable tenantAffinity and delete actually instance,
        // set timer and then add instance and then delete virtual instance -> set timer and recycle
        auto unit1 = Get1DResourceUnit();
        unit1.mutable_nodelabels()->insert({ IDLE_TO_RECYCLE, idleToRecycleLabels[1] });
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit1));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);
        count = 0;  // reset

        // mock instance is actually use, and delete
        auto inst2 = Get1DInstance();
        auto inst1 = Get1DInstance();
        inst1.set_unitid(unit1.id());
        inst2.set_unitid(unit1.id());
        inst1.set_tenantid("123456");
        inst2.set_tenantid("123456");

        std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
        instances.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
        auto ret = resourceView.AddInstances(instances);
        ASSERT_AWAIT_READY(ret);

        std::vector<std::string> ids1{ inst1.instanceid() };
        ret = resourceView.DeleteInstances(ids1);
        ASSERT_AWAIT_READY(ret);

        auto cache = resourceView.GetAgentCacheMap();
        // DisableAgent function is not called, but inst1 is removed
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 1);
        EXPECT_TRUE(cache.find(unit1.id()) == cache.end());
        EXPECT_EQ(count, 0);
        EXPECT_EQ(disabledAgent.count(unit1.id()), 0);

        // mock instance is virtual use, and need to be recycled because agent has been used
        std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
        instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
        ret = resourceView.AddInstances(instances2);
        ASSERT_AWAIT_READY(ret);

        std::vector<std::string> ids2{ inst2.instanceid() };
        ASSERT_AWAIT_READY(resourceView.DeleteInstances(ids2, true));

        EXPECT_AWAIT_TRUE([&]() -> bool { return resourceView.GetReuseTimers().count(unit1.id()) == 0; });
        cache = resourceView.GetAgentCacheMap();
        EXPECT_TRUE(cache.find(unit1.id()) == cache.end());
        EXPECT_EQ(count, 1);
        EXPECT_EQ(disabledAgent.count(unit1.id()), 1);
    }

    {
        // pod not set idlePod, enable tenantAffinity and add unit and then delete virtual instance -> not set timer and
        // not recycle
        auto unit1 = Get1DResourceUnit();
        unit1.mutable_nodelabels()->insert({ IDLE_TO_RECYCLE, idleToRecycleLabels[1] });
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit1));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);
        count = 0;  // reset

        // mock instance is virtual use, and delete
        auto inst2 = Get1DInstance();
        auto inst1 = Get1DInstance();
        inst1.set_unitid(unit1.id());
        inst2.set_unitid(unit1.id());
        inst1.set_tenantid("123456");
        inst2.set_tenantid("123456");

        std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
        instances.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
        auto ret = resourceView.AddInstances(instances);
        ASSERT_AWAIT_READY(ret);
        std::vector<std::string> ids1{ inst1.instanceid() };
        ret = resourceView.DeleteInstances(ids1, true);
        ASSERT_AWAIT_READY(ret);
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);

        std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
        instances.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
        ret = resourceView.AddInstances(instances2);
        ASSERT_AWAIT_READY(ret);
        std::vector<std::string> ids2{ inst2.instanceid() };
        ret = resourceView.DeleteInstances(ids2, true);
        ASSERT_AWAIT_READY(ret);
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);
    }

    {
        // pod not set idlePod, enable tenantAffinity and add unit and then delete instance and add virtual instance,
        // timer is executed, and then virtual instance is deleted  -> pod need to be recycled
        auto unit1 = Get1DResourceUnit();
        unit1.mutable_nodelabels()->insert({ IDLE_TO_RECYCLE, idleToRecycleLabels[1] });
        ASSERT_AWAIT_READY(resourceView.AddResourceUnit(unit1));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 0);
        count = 0;  // reset
        auto inst2 = Get1DInstance();
        auto inst1 = Get1DInstance();
        inst1.set_unitid(unit1.id());
        inst2.set_unitid(unit1.id());
        inst1.set_tenantid("123456");
        inst2.set_tenantid("123456");

        // mock instance is actually use, and delete
        std::map<std::string, resource_view::InstanceAllocatedInfo> instances;
        instances.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
        auto ret = resourceView.AddInstances(instances);
        ASSERT_AWAIT_READY(ret);
        std::vector<std::string> ids1{ inst1.instanceid() };
        ret = resourceView.DeleteInstances(ids1, false);
        ASSERT_AWAIT_READY(ret);
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 1);  // timer is set
        EXPECT_EQ(resourceView.GetAgentUsedMap().count(unit1.id()), 1);

        // mock virtual instance add
        std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
        instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
        ret = resourceView.AddInstances(instances2);
        ASSERT_AWAIT_READY(ret);
        // sleep 1.1s wait for timer executed finished, but no pod will be recycled
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        EXPECT_EQ(resourceView.GetReuseTimers().count(unit1.id()), 1);
        EXPECT_EQ(resourceView.GetAgentUsedMap().count(unit1.id()), 1);
        auto cache = resourceView.GetAgentCacheMap();
        EXPECT_TRUE(cache.find(unit1.id()) != cache.end());

        // delete virtual instance , and pod is need to be recycled
        std::vector<std::string> ids2{ inst2.instanceid() };
        ret = resourceView.DeleteInstances(ids2, true);
        ASSERT_AWAIT_READY(ret);
        EXPECT_AWAIT_TRUE([&]() -> bool { return resourceView.GetReuseTimers().count(unit1.id()) == 0; });
        cache = resourceView.GetAgentCacheMap();
        EXPECT_TRUE(cache.find(unit1.id()) == cache.end());
        EXPECT_EQ(count, 1);
        EXPECT_EQ(disabledAgent.count(unit1.id()), 1);
        EXPECT_EQ(resourceView.GetAgentUsedMap().count(unit1.id()), 0);
    }
}

TEST_F(ResourceViewTest, SetBillingPodResourceInstance)
{
    const std::string podMetricsJson = R"(
{
	"enabledMetrics": ["yr_pod_resource"],
	"backends": [{
		"immediatelyExport": {
			"name": "LakeHouse",
			"enable": true,
			"exporters": [{
				"aomAlarmExporter": {
					"enable": true,
					"ip": "127.0.0.1:8080/",
					"port": 9091
				}
			}]
		}
	}]
})";
    metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(podMetricsJson),
                                                               [](std::string backendName) { return "123"; }, {});

    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);
    child->ToReady();

    auto add = parent->AddResourceUnitWithUrl(*child->GetFullResourceView().Get(),
                                              litebus::GetActor(childNode + "-ResourceViewActor")->GetAID().Url());
    ASSERT_TRUE(add.Get().IsOk());
    auto map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_EQ(map.size(), 0);

    ASSERT_TRUE(parent->CheckLocalExistInDomainView(childNode));
    auto localViewInfo = parent->GetLocalInfoInDomain(childNode);
    auto localViewInitTimeOrigin = child->GetResourceView().Get()->viewinittime();

    // enable update
    parent->UpdateIsHeader(true);
    auto unit = Get1DResourceUnit();
    unit.set_ownerid(childNode);
    RecoverResourceView(parent, localViewInitTimeOrigin, unit);

    ASSERT_AWAIT_TRUE(
        []() { return metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap().size() == 1; });
    map = metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetPodResourceMap();
    EXPECT_NE(map.find(unit.id()), map.end());
}

TEST_F(ResourceViewTest, ValidateSchedulerLevel)
{
    // 1.scheduler_level = Non-root domain
    auto parent = resource_view::ResourceView::CreateResourceView(DOMAIN_RESOUCE_VIEW_ID, PARENT_PARAM);
    EXPECT_EQ(parent->GetResourceInfo().Get().schedulerLevel, SCHEDULER_LEVEL::NON_ROOT_DOMAIN);

    // 2.scheduler_level = root domain
    parent->UpdateIsHeader(true);
    EXPECT_EQ(parent->GetResourceInfo().Get().schedulerLevel, SCHEDULER_LEVEL::ROOT_DOMAIN);

    // 3.scheduler_level = local
    auto child = resource_view::ResourceView::CreateResourceView(LOCAL_RESOUCE_VIEW_ID, CHILD_PARAM);
    EXPECT_EQ(child->GetResourceInfo().Get().schedulerLevel, SCHEDULER_LEVEL::LOCAL);
}

TEST_F(ResourceViewTest, TryDelResourceUnitChange)
{

    std::string childNode = LOCAL_RESOUCE_VIEW_ID;
    auto child = resource_view::ResourceView::CreateResourceView(childNode, CHILD_PARAM);

    // 1.add resourceunit
    auto unit = Get1DResourceUnit();
    auto agentId = unit.id();
    GenerateMinimumUnitBucketInfo(unit);
    ASSERT_TRUE(child->AddResourceUnit(unit).Get().IsOk());

    // 2.add resourceunit
    auto unit2 = Get1DResourceUnit();
    GenerateMinimumUnitBucketInfo(unit2);
    auto agentId2 = unit2.id();
    ASSERT_TRUE(child->AddResourceUnit(unit2).Get().IsOk());

    // 3.modify resourceunit -- add instance1(shared)
    auto inst1 = Get1DInstance();
    (*inst1.mutable_schedulerchain()->Add()) = agentId;
    inst1.set_unitid(agentId);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances1;
    instances1.emplace(inst1.instanceid(), resource_view::InstanceAllocatedInfo{ inst1, nullptr });
    ASSERT_TRUE(child->AddInstances(instances1).Get().IsOk());

    // 4.modify resourceunit -- add instance2(shared)
    auto inst2 = Get1DInstance();
    (*inst2.mutable_schedulerchain()->Add()) = agentId;
    inst2.set_unitid(agentId);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances2;
    instances2.emplace(inst2.instanceid(), resource_view::InstanceAllocatedInfo{ inst2, nullptr });
    ASSERT_TRUE(child->AddInstances(instances2).Get().IsOk());

    auto change = child->GetResourceViewChanges();
    auto childView = child->GetFullResourceView().Get();
    EXPECT_EQ(change.Get()->startrevision(), 0);
    EXPECT_EQ(change.Get()->endrevision(), childView->revision());

    // 5.del instance1(shared)
    std::vector<std::string> ids1;
    ids1.push_back(inst1.instanceid());
    ASSERT_TRUE(child->DeleteInstances(ids1).Get().IsOk());

    // 6.del instance2(shared)
    std::vector<std::string> ids2;
    ids2.push_back(inst2.instanceid());
    ASSERT_TRUE(child->DeleteInstances(ids2).Get().IsOk());

    // Test for not del because condition is not satisfied
    child->TryDelResourceUnitChange(2, "AAA");
    childView = child->GetFullResourceView().Get();
    EXPECT_TRUE(child->implActor_->versionChanges_.find(1) != child->implActor_->versionChanges_.end());

    child->TryDelResourceUnitChange(100, childView->viewinittime());
    childView = child->GetFullResourceView().Get();
    EXPECT_TRUE(child->implActor_->versionChanges_.find(1) != child->implActor_->versionChanges_.end());

    child->TryDelResourceUnitChange(2, childView->viewinittime());
    childView = child->GetFullResourceView().Get(); // need get lastest version
    EXPECT_TRUE(child->implActor_->versionChanges_.find(1) == child->implActor_->versionChanges_.end());

    // 7.update status
    ASSERT_TRUE(child->UpdateUnitStatus(agentId, resource_view::UnitStatus::EVICTING).Get().IsOk());

    // 8.modify resourceunit -- add instance3(monopoly)
    auto inst3 = Get1DInstance();
    (*inst3.mutable_schedulerchain()->Add()) = agentId2;
    inst3.mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    inst3.set_unitid(agentId);
    std::map<std::string, resource_view::InstanceAllocatedInfo> instances3;
    instances3.emplace(inst3.instanceid(), resource_view::InstanceAllocatedInfo{ inst3, nullptr });
    ASSERT_TRUE(child->AddInstances(instances3).Get().IsOk());

    // 9.del resourceunit
    ASSERT_TRUE(child->DeleteResourceUnit(agentId).Get().IsOk());
    child->TryDelResourceUnitChange(4, childView->viewinittime());

    childView = child->GetFullResourceView().Get(); // need get lastest version
    EXPECT_TRUE(child->implActor_->versionChanges_.find(3) == child->implActor_->versionChanges_.end());
    EXPECT_TRUE(child->implActor_->versionChanges_.find(4) != child->implActor_->versionChanges_.end());

}

}  // namespace functionsystem::test
