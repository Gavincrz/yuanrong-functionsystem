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
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/constants/signal.h"

#include "common/etcd_service/etcd_service_driver.h"
#include "common/metadata/metadata.h"
#include "common/resource_view/resource_type.h"
#include "common/types/instance_state.h"
#include "common/utils/meta_store_kv_operation.h"
#include "function_master/instance_manager/instance_family_caches.h"
#include "function_master/instance_manager/instance_manager_actor.h"
#include "function_master/instance_manager/instance_manager_driver.h"
#include "function_proxy/local_scheduler/instance_control/instance_ctrl_actor.h"
#include "mocks/mock_instance_operator.h"
#include "mocks/mock_meta_store_client.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"

namespace functionsystem::instance_manager::test {
using namespace functionsystem::meta_store::test;
using namespace functionsystem::test;

class FamilyManagementTest : public ::testing::Test {
};

std::shared_ptr<resource_view::InstanceInfo> MakeInstanceInfo(const std::string &instanceID, const std::string &groupID,
                                                              const std::string &parentID, const std::string &nodeID,
                                                              const InstanceState &state)
{
    auto info = std::make_shared<resource_view::InstanceInfo>();
    info->set_requestid(INSTANCE_PATH_PREFIX + "/" + instanceID);
    info->set_runtimeid("/sn/runtime/001");
    info->set_functionagentid("/sn/agent/001");
    info->set_function("/sn/function/001");
    info->mutable_schedulerchain()->Add("chain01");
    info->mutable_schedulerchain()->Add("chain02");
    info->set_instanceid(instanceID);
    info->set_groupid(groupID);
    info->set_parentid(parentID);
    info->set_functionproxyid(nodeID);
    info->mutable_instancestatus()->set_code(static_cast<int32_t>(state));
    info->set_version(1);
    return info;
}

std::vector<std::shared_ptr<resource_view::InstanceInfo>> MakeInstanceInfos()
{
    // ""
    // └─A
    //   ├─B
    //   └─C
    //     ├─E
    //     └─D
    //       ├─F
    //       └─G
    return { MakeInstanceInfo("A", "", "", "node001", InstanceState::RUNNING),
             MakeInstanceInfo("B", "", "A", "node001", InstanceState::RUNNING),
             MakeInstanceInfo("C", "", "A", "node001", InstanceState::RUNNING),
             MakeInstanceInfo("D", "", "C", "node001", InstanceState::RUNNING),
             MakeInstanceInfo("E", "", "C", "node001", InstanceState::RUNNING),
             MakeInstanceInfo("F", "", "D", "node001", InstanceState::RUNNING),
             MakeInstanceInfo("G", "", "D", "node001", InstanceState::RUNNING) };
}

TEST_F(FamilyManagementTest, AddAndRemoveInstance)  // NOLINT
{
    InstanceFamilyCaches caches;

    // A
    caches.AddInstance(MakeInstanceInfo("A", "", "", "node001", InstanceState::RUNNING));
    auto family = caches.GetFamily();
    ASSERT_EQ(family.size(), 1u);
    ASSERT_TRUE(family.find("A") != family.end());

    // A
    // └-B
    caches.AddInstance(MakeInstanceInfo("B", "", "A", "node001", InstanceState::RUNNING));
    family = caches.GetFamily();
    ASSERT_EQ(family.size(), 2u);
    ASSERT_TRUE(family.find("B") != family.end());
    ASSERT_EQ(family.find("A")->second.childrenInstanceID.size(), 1u);

    // A
    // ├-B
    // └-C
    caches.AddInstance(MakeInstanceInfo("C", "", "A", "node001", InstanceState::RUNNING));
    family = caches.GetFamily();
    ASSERT_EQ(family.size(), 3u);
    ASSERT_TRUE(family.find("C") != family.end());
    ASSERT_EQ(family.find("A")->second.childrenInstanceID.size(), 2u);

    // A
    // ├-B
    // └-C
    //   └-D
    caches.AddInstance(MakeInstanceInfo("D", "", "C", "node001", InstanceState::RUNNING));
    family = caches.GetFamily();
    ASSERT_EQ(family.size(), 4u);
    ASSERT_EQ(family.find("C")->second.childrenInstanceID.size(), 1u);
    ASSERT_EQ(family.find("C")->second.childrenInstanceID.count("D"), 1u);

    // A
    // └-B
    // .      // . means missing
    // └-D
    caches.RemoveInstance("C");
    family = caches.GetFamily();
    ASSERT_EQ(family.size(), 3u);
    ASSERT_TRUE(family.find("C") == family.end());  // C should be deleted
    ASSERT_TRUE(family.find("D") != family.end());  // D should be an orphan
    ASSERT_EQ(family.find("A")->second.childrenInstanceID.size(), 1u);
    ASSERT_EQ(family.find("A")->second.childrenInstanceID.count("B"), 1u);

    // A
    // └-B
    caches.RemoveInstance("D");
    family = caches.GetFamily();
    ASSERT_EQ(family.size(), 2u);
    ASSERT_TRUE(family.find("D") == family.end());  // D should be an orphan
    ASSERT_EQ(family.find("A")->second.childrenInstanceID.size(), 1u);
    ASSERT_EQ(family.find("A")->second.childrenInstanceID.count("B"), 1u);

    caches.RemoveInstance("X");  // no effect

    ASSERT_TRUE(caches.IsInstanceExists("A"));   // check existing
    ASSERT_TRUE(!caches.IsInstanceExists("C"));  // check existing
}

TEST_F(FamilyManagementTest, GetDescendants)  // NOLINT
{
    InstanceFamilyCaches caches;
    auto allInfos = MakeInstanceInfos();
    for (auto info : allInfos) {
        caches.AddInstance(info);
    }

    auto descendantsOfC = caches.GetAllDescendantsOf("C");
    auto idxOfInst = [](const std::list<std::shared_ptr<InstanceInfo>> &lst, const std::string &instanceID) {
        int index = 0;
        for (auto it = lst.begin(); it != lst.end(); it++, index++) {
            if ((*it)->instanceid() == instanceID) {
                return index;
            }
        }
        return -1;
    };

    int idxOfD = idxOfInst(descendantsOfC, "D"), idxOfE = idxOfInst(descendantsOfC, "E"),
        idxOfF = idxOfInst(descendantsOfC, "F"), idxOfG = idxOfInst(descendantsOfC, "G");

    // assert exists and follow bfs order
    ASSERT_TRUE(idxOfD != -1);
    ASSERT_TRUE(idxOfD != -1);
    ASSERT_TRUE(idxOfD != -1);
    ASSERT_TRUE(idxOfD != -1);
    ASSERT_TRUE(idxOfD < idxOfF);
    ASSERT_TRUE(idxOfE < idxOfF);
    ASSERT_TRUE(idxOfD < idxOfG);
    ASSERT_TRUE(idxOfE < idxOfG);
    ASSERT_EQ(descendantsOfC.size(), 4u);

    auto descendantsOfAll = caches.GetAllDescendantsOf("");
    ASSERT_EQ(descendantsOfAll.size(), 7u);

    caches.RemoveInstance("D");
    descendantsOfAll = caches.GetAllDescendantsOf("");
    ASSERT_EQ(descendantsOfAll.size(), 6u);
    EXPECT_TRUE(caches.GetInstance("") == nullptr);
    EXPECT_TRUE(caches.GetInstance("NotExist") == nullptr);
    EXPECT_TRUE(caches.GetInstance("C") != nullptr);
}

TEST_F(FamilyManagementTest, AddAndRemoveDetachedInstance)  // NOLINT
{
    InstanceFamilyCaches caches;
    auto insA = MakeInstanceInfo("A", "", "", "node001", InstanceState::RUNNING);
    auto insB = MakeInstanceInfo("B", "", "A", "node001", InstanceState::RUNNING);
    auto insC = MakeInstanceInfo("C", "", "B", "node001", InstanceState::RUNNING);
    insC->set_detached(true);
    auto insD = MakeInstanceInfo("D", "", "C", "node001", InstanceState::RUNNING);
    auto insE = MakeInstanceInfo("E", "", "D", "node001", InstanceState::RUNNING);
    caches.AddInstance(insA);
    caches.AddInstance(insB);
    caches.AddInstance(insC);
    caches.AddInstance(insD);
    caches.AddInstance(insE);
    EXPECT_EQ(caches.GetAllDescendantsOf("A").size(), 1);
    EXPECT_EQ(caches.GetAllDescendantsOf("C").size(), 2);
    caches.RemoveInstance("C");
    EXPECT_EQ(caches.GetAllDescendantsOf("D").size(), 1);
}
}  // namespace functionsystem::instance_manager::test