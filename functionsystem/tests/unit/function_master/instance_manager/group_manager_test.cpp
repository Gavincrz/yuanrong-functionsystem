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
#include "function_master/instance_manager/instance_manager_actor.h"
#include "function_master/instance_manager/instance_manager_driver.h"
#include "function_proxy/local_scheduler/instance_control/instance_ctrl_actor.h"
#include "mocks/mock_global_schd.h"
#include "mocks/mock_instance_manager.h"
#include "mocks/mock_instance_operator.h"
#include "mocks/mock_meta_store_client.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/port_helper.h"

namespace functionsystem::instance_manager::test {
using namespace functionsystem::meta_store::test;
using namespace functionsystem::test;

/**

+------------------------------------------------------------+
|       | group-1 (node 1) |  group-2 (node 2)  |  no-group  |
+-------+------------------+--------------------+------------+
| node1 |      inst-1      |       inst-2       |            |
| node2 | inst-3 , inst-4  |                    |            |
| node3 |                  |       inst-5       |   inst-6   |
+-------+------------------+--------------------+------------+

Usecases:

1. listening groups

    PreCondition: None
    Description:  Put group info
    Expected:     Get group info is ok, get the group


2. OnInstancePut
    PreCondition: None
    Description:  Put instance
    Expected:     Get instance info is ok

3. InstanceAbnormal

    PreCondition: Mapping-01
    Description:  GroupManager get instance abnormal event of instance 1
    Expected:     1. mark group1 to be FAILED
                  2. instance 1, 3, 4 to be FATAL

4. LocalAbnormal

    PreCondition: Mapping-01
    Description:  GroupManager get local abnormal event of node 2
    Expected:     1. mark group2 owner=GroupManager
                  2. instance 2, 6 to be FATAL
*/

const std::string GROUP_ID_1 = "group1";
const std::string GROUP_ID_2 = "group2";

const std::string GROUP_KEY_1 = GROUP_PATH_PREFIX + "/" + GROUP_ID_1;
const std::string GROUP_KEY_2 = GROUP_PATH_PREFIX + "/" + GROUP_ID_2;

const std::string NODE_ID_1 = "/sn/proxy/001";  // NOLINT
const std::string NODE_ID_2 = "/sn/proxy/002";  // NOLINT
const std::string NODE_ID_3 = "/sn/proxy/003";  // NOLINT

const std::string INSTANCE_ID_0 = "000";
const std::string INSTANCE_ID_1 = "001";
const std::string INSTANCE_ID_2 = "002";
const std::string INSTANCE_ID_3 = "003";
const std::string INSTANCE_ID_4 = "004";
const std::string INSTANCE_ID_5 = "005";
const std::string INSTANCE_ID_6 = "006";

const std::string DEFAULT_FUNCTION_NAME = "123/0-yr-yr/0";
const std::string DEFAULT_REQUEST_ID = "123456";

std::shared_ptr<messages::GroupInfo> MakeGroupInfo(const std::string &groupID, const std::string &ownerProxyID,
                                                   const GroupState &state, const std::string &parentID)
{
    auto info = std::make_shared<messages::GroupInfo>();
    info->set_groupid(groupID);
    info->set_ownerproxy(ownerProxyID);
    info->set_parentid(parentID);
    info->set_status(static_cast<int32_t>(state));
	info->mutable_groupopts()->set_samerunninglifecycle(true);
    return info;
}

std::shared_ptr<resource_view::InstanceInfo> MakeInstanceInfo(const std::string &instanceID, const std::string &groupID,
                                                              const std::string &nodeID, const InstanceState &state)
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
    info->set_functionproxyid(nodeID);
    info->mutable_instancestatus()->set_code(static_cast<int32_t>(state));
    info->set_version(1);
    return info;
}

class MockInstanceCtrlActorForGroupManagerTest : public litebus::ActorBase {
public:
    MockInstanceCtrlActorForGroupManagerTest(const std::string &nodeID)
        : litebus::ActorBase(nodeID + LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX)
    {
    }

    ~MockInstanceCtrlActorForGroupManagerTest() = default;

    MOCK_METHOD((std::pair<bool, internal::ForwardKillResponse>), MockForwardCustomSignalResponse,
                (const litebus::AID &, const std::string &, const std::string &), ());

    void ForwardCustomSignalRequest(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto ret = MockForwardCustomSignalResponse(from, name, msg);
        if (ret.first) {
            Send(from, "ForwardCustomSignalResponse", ret.second.SerializeAsString());
        }
    }

    std::shared_ptr<litebus::Promise<internal::ForwardKillRequest>>
    ExpectCallMockInstanceCtrlForwardCustomSignalReturnOK() const
    {
        auto mockForwardCustomSignalReceived = std::make_shared<litebus::Promise<internal::ForwardKillRequest>>();
        EXPECT_CALL(*this, MockForwardCustomSignalResponse)
            .WillRepeatedly(
                testing::Invoke([mockForwardCustomSignalReceived](const litebus::AID &from, const std::string &name,
                                                                  const std::string &msg) {
                    internal::ForwardKillRequest fkReq;
                    fkReq.ParseFromString(msg);
                    mockForwardCustomSignalReceived->Set(fkReq);
                    internal::ForwardKillResponse fkRsp;
                    fkRsp.set_requestid(fkReq.requestid());
                    return std::make_pair(true, fkRsp);
                }));
        return mockForwardCustomSignalReceived;
    }

protected:
    void Init() override
    {
        Receive("ForwardCustomSignalRequest", &MockInstanceCtrlActorForGroupManagerTest::ForwardCustomSignalRequest);
    }
};

class MockLocalGroupCtrlActorForGroupManagerTest : public litebus::ActorBase {
public:
    MockLocalGroupCtrlActorForGroupManagerTest(const std::string &nodeID)
        : litebus::ActorBase(LOCAL_GROUP_CTRL_ACTOR_NAME)
    {
    }

    ~MockLocalGroupCtrlActorForGroupManagerTest() = default;

    MOCK_METHOD((std::pair<bool, messages::KillGroupResponse>), MockClearGroupResponse,
                (const litebus::AID &, const std::string &, const std::string &), ());

    void ClearGroup(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto ret = MockClearGroupResponse(from, name, msg);
        if (ret.first) {
            Send(from, "OnClearGroup", ret.second.SerializeAsString());
        }
    }

    std::shared_ptr<litebus::Promise<messages::KillGroup>> ExpectCallMockClearGroupResponseReturnOK() const
    {
        auto mockClearGroupReceived = std::make_shared<litebus::Promise<messages::KillGroup>>();
        EXPECT_CALL(*this, MockClearGroupResponse)
            .WillRepeatedly(testing::Invoke(
                [mockClearGroupReceived](const litebus::AID &from, const std::string &name, const std::string &msg) {
                    messages::KillGroup clearReq;
                    clearReq.ParseFromString(msg);
                    mockClearGroupReceived->Set(clearReq);
                    messages::KillGroupResponse clearRsp;
                    clearRsp.set_groupid(clearReq.groupid());
                    return std::make_pair(true, clearRsp);
                }));
        return mockClearGroupReceived;
    }

protected:
    void Init() override
    {
        Receive("ClearGroup", &MockLocalGroupCtrlActorForGroupManagerTest::ClearGroup);
    }
};

class GroupCachesTest : public ::testing::Test {
};

TEST_F(GroupCachesTest, AddAndDelGroup)
{
    GroupManagerActor::GroupCaches caches;
    caches.AddGroup(GROUP_KEY_1, MakeGroupInfo(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, "--"));
    ASSERT_EQ(caches.GetGroups().size(), static_cast<size_t>(1));               // one group inside it
    ASSERT_EQ(caches.GetNodeGroups(NODE_ID_1).size(), static_cast<size_t>(1));  // one group inside it

    caches.AddGroup(GROUP_KEY_2, MakeGroupInfo(GROUP_ID_2, NODE_ID_2, GroupState::RUNNING, "--"));
    ASSERT_EQ(caches.GetGroups().size(), static_cast<size_t>(2));  // 2 groups inside it
    ASSERT_EQ(caches.nodeName2Groups_.size(), static_cast<size_t>(2));
    ASSERT_TRUE(caches.groups_.find(GROUP_ID_1) != caches.groups_.end());                   // group name exists
    ASSERT_TRUE(caches.groups_.find(GROUP_ID_2) != caches.groups_.end());                   // group name exists
    ASSERT_TRUE(caches.nodeName2Groups_.find(NODE_ID_1) != caches.nodeName2Groups_.end());  // node name exists
    ASSERT_TRUE(caches.nodeName2Groups_.find(NODE_ID_2) != caches.nodeName2Groups_.end());  // node name exists
    ASSERT_TRUE(caches.parent2Groups_.find("--") != caches.nodeName2Groups_.end());
    ASSERT_EQ(caches.parent2Groups_["--"].size(), static_cast<size_t>(2));

    caches.RemoveGroup(GROUP_ID_1);
    ASSERT_EQ(caches.GetGroups().size(), static_cast<size_t>(1));                           // one group inside it
    ASSERT_EQ(caches.nodeName2Groups_.size(), static_cast<size_t>(1));                      // one group inside it
    ASSERT_TRUE(caches.groups_.find(GROUP_ID_1) == caches.groups_.end());                   // group name not exists
    ASSERT_TRUE(caches.nodeName2Groups_.find(NODE_ID_1) == caches.nodeName2Groups_.end());  // node name removed

    caches.RemoveGroup(GROUP_ID_2);
    ASSERT_EQ(caches.GetGroups().size(), static_cast<size_t>(0));  // one group inside it
    ASSERT_TRUE(caches.parent2Groups_.find("--") == caches.nodeName2Groups_.end());
}

TEST_F(GroupCachesTest, AddAndDelIntance)
{
    auto groupInfo1 = MakeGroupInfo(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, "--");
    auto groupInfo2 = MakeGroupInfo(GROUP_ID_2, NODE_ID_2, GroupState::RUNNING, "--");
    auto instanceInfo1 = MakeInstanceInfo(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
    auto instanceInfo2 = MakeInstanceInfo(INSTANCE_ID_2, GROUP_ID_2, NODE_ID_2, InstanceState::RUNNING);
    auto instanceKey1 = INSTANCE_PATH_PREFIX + "/" + INSTANCE_ID_1;
    auto instanceKey2 = INSTANCE_PATH_PREFIX + "/" + INSTANCE_ID_2;

    GroupManagerActor::GroupCaches caches;
    caches.AddGroup(GROUP_KEY_1, groupInfo1);
    caches.AddGroup(GROUP_KEY_2, groupInfo2);
    ASSERT_TRUE(caches.GetGroups().size() == static_cast<size_t>(2));  // 2 groups inside it
    ASSERT_TRUE(caches.GetNodeGroups(NODE_ID_1).size() == static_cast<size_t>(1));
    ASSERT_TRUE(caches.GetNodeGroups(NODE_ID_2).size() == static_cast<size_t>(1));

    caches.AddGroupInstance(GROUP_ID_1, instanceKey1, instanceInfo1);
    ASSERT_TRUE(caches.groups_.size() == static_cast<size_t>(2));
    ASSERT_TRUE(caches.groupID2Instances_.size() == static_cast<size_t>(1));
    ASSERT_TRUE(caches.groupID2Instances_.find(GROUP_ID_1) != caches.groupID2Instances_.end());
    ASSERT_TRUE(caches.groupID2Instances_.find(GROUP_ID_1)->second.find(instanceKey1) !=
                caches.groupID2Instances_.find(GROUP_ID_1)->second.end());  // instance2 in group2

    caches.AddGroupInstance(GROUP_ID_2, instanceKey2, instanceInfo2);
    ASSERT_TRUE(caches.groups_.size() == static_cast<size_t>(2));             // 2 groups
    ASSERT_TRUE(caches.groupID2Instances_.size() == static_cast<size_t>(2));  // 2 instances in differnet groups
    ASSERT_TRUE(caches.groupID2Instances_.find(GROUP_ID_2) != caches.groupID2Instances_.end());  // group2 exists
    ASSERT_TRUE(caches.groupID2Instances_.find(GROUP_ID_2)->second.find(instanceKey2) !=
                caches.groupID2Instances_.find(GROUP_ID_2)->second.end());  // instance2 in group2

    caches.RemoveGroupInstance(instanceKey1, instanceInfo1);
    ASSERT_TRUE(caches.groups_.size() == static_cast<size_t>(2));             // 2 groups
    ASSERT_TRUE(caches.groupID2Instances_.size() == static_cast<size_t>(1));  // 2 instances in differnet groups
    ASSERT_TRUE(caches.groupID2Instances_.find(GROUP_ID_2) != caches.groupID2Instances_.end());  // group2 exists
    ASSERT_TRUE(caches.groupID2Instances_.find(GROUP_ID_2)->second.find(instanceKey2) !=
                caches.groupID2Instances_.find(GROUP_ID_2)->second.end());  // instance2 in group2
}

class GroupManagerTest : public ::testing::Test {
protected:
    inline static std::string metaStoreServerHost_;
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    std::shared_ptr<MockInstanceCtrlActorForGroupManagerTest> instCtrlActor1{ nullptr }, instCtrlActor2{ nullptr },
        instCtrlActor3{ nullptr };
    std::shared_ptr<MockLocalGroupCtrlActorForGroupManagerTest> localGroupctlActor1{ nullptr };

    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        etcdSrvDriver_->StopServer();
    }

    void SetUp() override
    {
        instCtrlActor1 = std::make_shared<MockInstanceCtrlActorForGroupManagerTest>(NODE_ID_1);
        instCtrlActor2 = std::make_shared<MockInstanceCtrlActorForGroupManagerTest>(NODE_ID_2);
        instCtrlActor3 = std::make_shared<MockInstanceCtrlActorForGroupManagerTest>(NODE_ID_3);

        localGroupctlActor1 = std::make_shared<MockLocalGroupCtrlActorForGroupManagerTest>(NODE_ID_1);

        ASSERT_TRUE(litebus::Spawn(instCtrlActor1).OK());
        ASSERT_TRUE(litebus::Spawn(instCtrlActor2).OK());
        ASSERT_TRUE(litebus::Spawn(instCtrlActor3).OK());
        ASSERT_TRUE(litebus::Spawn(localGroupctlActor1).OK());
    }

    void TearDown() override
    {
        // clear meta info
        auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        ASSERT_TRUE(
            client->Delete(GROUP_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true }).Get()->status.IsOk());
        ASSERT_TRUE(
            client->Delete(INSTANCE_PATH_PREFIX, DeleteOption{ .prevKv = false, .prefix = true }).Get()->status.IsOk());

        litebus::Terminate(instCtrlActor1->GetAID());
        litebus::Terminate(instCtrlActor2->GetAID());
        litebus::Terminate(instCtrlActor3->GetAID());
        litebus::Terminate(localGroupctlActor1->GetAID());

        litebus::Await(instCtrlActor1->GetAID());
        litebus::Await(instCtrlActor2->GetAID());
        litebus::Await(instCtrlActor3->GetAID());
        litebus::Await(localGroupctlActor1->GetAID());
    }

protected:
    void PutInstance(const std::string &instanceID, const std::string &groupID, const std::string &nodeID,
                     const InstanceState &state)
    {
        auto instance = MakeInstanceInfo(instanceID, groupID, nodeID, state);
        std::string jsonString;
        ASSERT_TRUE(TransToJsonFromInstanceInfo(jsonString, *instance));

        auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        ASSERT_TRUE(client->Put(INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + instance->instanceid(), jsonString, {}).Get()->status.IsOk());
    }

    void DelInstance(const std::string &instanceID)
    {
        auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        ASSERT_TRUE(client->Delete(INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + instanceID, {}).Get()->status.IsOk());
    }

    void PutGroup(const std::string &groupID, const std::string &ownerProxyID, const GroupState &state,
                  const std::string &parentID)
    {
        auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        std::string jsonString;
        auto info = MakeGroupInfo(groupID, ownerProxyID, state, parentID);
        ASSERT_TRUE(TransToJsonFromGroupInfo(jsonString, *info));
        ASSERT_TRUE(client->Put(GROUP_PATH_PREFIX + "/" + groupID, jsonString, {}).Get()->status.IsOk());
    }

    void PutGroup(std::shared_ptr<messages::GroupInfo> &groupInfo)
    {
        auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        std::string jsonString;
        ASSERT_TRUE(TransToJsonFromGroupInfo(jsonString, *groupInfo));
        ASSERT_TRUE(client->Put(GROUP_PATH_PREFIX + "/" + groupInfo->groupid(), jsonString, {}).Get()->status.IsOk());
    }

    void DelGroup(const std::string &groupID)
    {
        auto client = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        ASSERT_TRUE(client->Delete(GROUP_PATH_PREFIX + "/" + groupID, {}).Get()->status.IsOk());
    }

    void AddRequestsToGroup(std::shared_ptr<messages::GroupInfo> &groupInfo,
                            const std::vector<std::string>& instanceIds) {
        for (const auto& id : instanceIds) {
            auto request = groupInfo->add_requests();
            request->mutable_instance()->set_instanceid(id);
            request->mutable_instance()->set_function(DEFAULT_FUNCTION_NAME);
            request->mutable_instance()->set_requestid(DEFAULT_REQUEST_ID);
        }
    }

    void PutDefaultGroupsAndInstances()
    {
        // +------------------------------------------------------------+
        // |       | group-1 (node 1) |  group-2 (node 2)  |  no-group  |
        // +-------+------------------+--------------------+------------+
        // | node1 |      inst-1      |       inst-2       |            |
        // | node2 | inst-3 , inst-4  |                    |            |
        // | node3 |                  |       inst-5       |   inst-6   |
        // +-------+------------------+--------------------+------------+


        auto groupInfo1 = MakeGroupInfo(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, "");
        AddRequestsToGroup(groupInfo1, {INSTANCE_ID_1, INSTANCE_ID_3, INSTANCE_ID_4});
        PutGroup(groupInfo1);

        auto groupInfo2 = MakeGroupInfo(GROUP_ID_2, NODE_ID_2, GroupState::RUNNING, "");
        AddRequestsToGroup(groupInfo2, {INSTANCE_ID_2, INSTANCE_ID_5});
        PutGroup(groupInfo2);

        PutInstance(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
        PutInstance(INSTANCE_ID_2, GROUP_ID_2, NODE_ID_1, InstanceState::RUNNING);
        PutInstance(INSTANCE_ID_3, GROUP_ID_1, NODE_ID_2, InstanceState::RUNNING);
        PutInstance(INSTANCE_ID_4, GROUP_ID_1, NODE_ID_2, InstanceState::RUNNING);
        PutInstance(INSTANCE_ID_5, GROUP_ID_2, NODE_ID_3, InstanceState::RUNNING);
        PutInstance(INSTANCE_ID_6, "", NODE_ID_3, InstanceState::RUNNING);
    }

    void CheckGroupState(const std::string groupID, const GroupState &state)
    {
        auto groupInfoInEtcdFuture = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ })
                ->Get(GROUP_PATH_PREFIX + "/" + groupID, {});
        ASSERT_AWAIT_READY(groupInfoInEtcdFuture);
        ASSERT_TRUE(groupInfoInEtcdFuture.Get()->status.IsOk());
        ASSERT_TRUE(groupInfoInEtcdFuture.Get()->kvs.size() == 1);
        auto info = messages::GroupInfo{};
        ASSERT_TRUE(TransToGroupInfoFromJson(info, groupInfoInEtcdFuture.Get()->kvs[0].value()));
        ASSERT_EQ(info.status(), static_cast<int32_t>(state));
    }

    GroupManagerActor::GroupCaches AsyncGetGroupCaches(std::shared_ptr<GroupManagerActor> groupMgrActor)
    {
        litebus::Future<GroupManagerActor::GroupCaches> f =
            litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::GetCurrentGroupCaches);
        return f.Get();
    }
};

class MockGroupCaches : public GroupManagerActor::GroupCaches {
public:
    MOCK_METHOD((std::unordered_map<std::string, GroupKeyInfoPair>), GetGroups, (), ());
    MOCK_METHOD((void), AddGroup, (const std::string groupKey, const std::shared_ptr<messages::GroupInfo> &group),
                (override));
    MOCK_METHOD((void), RemoveGroup, (const std::string &groupID), (override));
    MOCK_METHOD((void), AddGroupInstance,
                (const std::string &groupID, const std::string &instanceKey,
                 const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo),
                (override));
    MOCK_METHOD((void), RemoveGroupInstance,
                (const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo),
                (override));
};

#define DEFAULT_START_INSTANCE_MANAGER_DRIVER(recoverEnable)                                               \
    auto scheduler = std::make_shared<MockGlobalSched>();                                                  \
    auto groupMgrActor = std::make_shared<GroupManagerActor>(                                              \
        MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }), scheduler);                      \
    auto groupMgr = std::make_shared<GroupManager>(groupMgrActor);                                         \
    auto instanceMgrActor = std::make_shared<InstanceManagerActor>(                                        \
        MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }), scheduler, groupMgr,             \
        InstanceManagerStartParam{ .runtimeRecoverEnable = (recoverEnable), .isMetaStoreEnable = false }); \
    auto instanceMgr = std::make_shared<InstanceManager>(instanceMgrActor);                                \
    groupMgrActor->BindInstanceManager(instanceMgr);                                                       \
    auto instanceMgrDriver = std::make_shared<InstanceManagerDriver>(instanceMgrActor, groupMgrActor);     \
    instanceMgrDriver->Start();

#define DEFAULT_STOP_INSTANCE_MANAGER_DRIVER \
    instanceMgrDriver->Stop();               \
    instanceMgrDriver->Await();

/**
 * check if put instance can receive, and can finish as expected
 */
TEST_F(GroupManagerTest, PutAndDelGroupOK)
{
    DEFAULT_START_INSTANCE_MANAGER_DRIVER(false);

    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
        .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
            auto inst = std::make_shared<InstanceInfo>();
            inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
            return std::make_pair("", inst);
        }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);

    // Given: the group manager is the leader
    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));

    {
        litebus::Future<std::string> argGroupKey;

        // When: group is put into metastore
        PutGroup(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, INSTANCE_ID_1);
        ASSERT_AWAIT_TRUE([=]() { return groupMgrActor->member_->groupCaches->GetGroups().find(GROUP_ID_1) != groupMgrActor->member_->groupCaches->GetGroups().end(); });
    }

    {
        litebus::Future<std::string> argGroupKey;
        // When: group 2 is put into metastore
        PutGroup(GROUP_ID_2, NODE_ID_2, GroupState::RUNNING, INSTANCE_ID_1);
        ASSERT_AWAIT_TRUE([=]() { return groupMgrActor->member_->groupCaches->GetGroups().find(GROUP_ID_2) != groupMgrActor->member_->groupCaches->GetGroups().end(); });
    }

    {
        litebus::Future<std::string> argGroupID;
        // When: group 2 is delete from metastore
        DelGroup(GROUP_ID_1);
        ASSERT_AWAIT_TRUE([=]() { return groupMgrActor->member_->groupCaches->GetGroups().find(GROUP_ID_1) == groupMgrActor->member_->groupCaches->GetGroups().end(); });
    }

    {
        litebus::Future<std::string> argGroupID;
        // When: group 2 is delete from metastore
        DelGroup(GROUP_ID_2);
        ASSERT_AWAIT_TRUE([=]() { return groupMgrActor->member_->groupCaches->GetGroups().find(GROUP_ID_2) == groupMgrActor->member_->groupCaches->GetGroups().end(); });
    }

    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}

/**
 * check if put instance can receive, and can finish as expected
 */
TEST_F(GroupManagerTest, PutAndDelInstanceOK)
{
    DEFAULT_START_INSTANCE_MANAGER_DRIVER(false);
    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
        .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
            auto inst = std::make_shared<InstanceInfo>();
            inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
            return std::make_pair("", inst);
        }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);

    // Given: 2 groups already in
    auto groupInfo1 = MakeGroupInfo(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, "not-exist");
    auto groupInfo2 = MakeGroupInfo(GROUP_ID_2, NODE_ID_2, GroupState::RUNNING, "not-exist");
    groupMgrActor->member_->groupCaches->groups_[GROUP_ID_1] = { GROUP_PATH_PREFIX + "/" + GROUP_ID_1, groupInfo1 };
    groupMgrActor->member_->groupCaches->nodeName2Groups_[NODE_ID_1] = { { GROUP_PATH_PREFIX + "/" + GROUP_ID_1, groupInfo1 } };
    groupMgrActor->member_->groupCaches->groups_[GROUP_ID_2] = { GROUP_PATH_PREFIX + "/" + GROUP_ID_2, groupInfo2 };
    groupMgrActor->member_->groupCaches->nodeName2Groups_[NODE_ID_2] = { { GROUP_PATH_PREFIX + "/" + GROUP_ID_2, groupInfo2 } };

    {
        // When: put an instance
        PutInstance(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
        ASSERT_AWAIT_TRUE([=]() { return groupMgrActor->member_->groupCaches->groupID2Instances_.find(GROUP_ID_1) != groupMgrActor->member_->groupCaches->groupID2Instances_.end(); });
    }

    {
        // When: delete an instance
        DelInstance(INSTANCE_ID_1);
        ASSERT_AWAIT_TRUE([=]() { return groupMgrActor->member_->groupCaches->groupID2Instances_.find(GROUP_ID_1) == groupMgrActor->member_->groupCaches->groupID2Instances_.end(); });
    }

    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}

/**
 * instance abnormal,
    1. set group to fatal;
    2. send ForwardSignal to all instanceCtrlActor;
 */
TEST_F(GroupManagerTest, InstanceAbnormalNotRecoverable)
{
    DEFAULT_START_INSTANCE_MANAGER_DRIVER(false);

    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
        .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
            auto inst = std::make_shared<InstanceInfo>();
            inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
            return std::make_pair("", inst);
        }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);

    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    EXPECT_CALL(*scheduler, GetLocalAddress)
        .WillRepeatedly(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));
    auto mockForwardCustomSignalReceived = std::make_shared<litebus::Promise<internal::ForwardKillRequest>>();
    EXPECT_CALL(*instCtrlActor2, MockForwardCustomSignalResponse)
        .Times(2)
        .WillRepeatedly(testing::Invoke([mockForwardCustomSignalReceived](
                                            const litebus::AID &from, const std::string &name, const std::string &msg) {
            internal::ForwardKillRequest fkReq;
            fkReq.ParseFromString(msg);
            mockForwardCustomSignalReceived->Set(fkReq);
            return std::make_pair(true, internal::ForwardKillResponse());
        }));

    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));

    PutDefaultGroupsAndInstances();

    {
        //     Op: set instance 01 fatal,
        // Expect: wait local receive forward kill signal with signal GROUP_EXIT_SIGNAL
        //         check if group is set to FAILED
        PutInstance(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::FATAL);

        ASSERT_AWAIT_READY(mockForwardCustomSignalReceived->GetFuture());
        auto localRecvedForwardKillReq = mockForwardCustomSignalReceived->GetFuture().Get();
        ASSERT_TRUE(localRecvedForwardKillReq.has_req());
        ASSERT_TRUE(localRecvedForwardKillReq.req().signal() == GROUP_EXIT_SIGNAL);

        auto groupInfoInEtcdFuture = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ })
                                         ->Get(GROUP_PATH_PREFIX + "/" + GROUP_ID_1, {});
        ASSERT_AWAIT_READY(groupInfoInEtcdFuture);
        ASSERT_TRUE(groupInfoInEtcdFuture.Get()->status.IsOk());
        ASSERT_TRUE(groupInfoInEtcdFuture.Get()->kvs.size() == 1);
        auto groupInfo = messages::GroupInfo{};
        ASSERT_TRUE(TransToGroupInfoFromJson(groupInfo, groupInfoInEtcdFuture.Get()->kvs[0].value()));
        ASSERT_EQ(groupInfo.status(), static_cast<int32_t>(GroupState::FAILED));
    }

    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}

class LocalAbnormalNotRecoverableTest : public GroupManagerTest, public testing::WithParamInterface<GroupState> {
};

INSTANTIATE_TEST_SUITE_P(GroupManagerTestLocalAbnormalNotRecoverableGroupStates, LocalAbnormalNotRecoverableTest,
                         testing::Values(GroupState::SCHEDULING, GroupState::RUNNING, GroupState::FAILED));

/**
 * local abnormal,
    1. set owning group to fatal
    2. set owning group owner to GROUP_MANAGER
 */
TEST_P(LocalAbnormalNotRecoverableTest, LocalAbnormal_NotRecoverable)
{
    auto groupState = GetParam();

    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    EXPECT_CALL(*mockMetaClient, Watch).WillRepeatedly(testing::Return(litebus::Future<std::shared_ptr<Watcher>>()));
    EXPECT_CALL(*mockMetaClient, Get).WillRepeatedly(testing::Return(litebus::Future<std::shared_ptr<GetResponse>>()));

    auto mockGlobalScheduler = std::make_shared<MockGlobalSched>();
    auto groupMgrActor = std::make_shared<GroupManagerActor>(mockMetaClient, mockGlobalScheduler);
    auto groupMgr = std::make_shared<GroupManager>(groupMgrActor);
    auto instanceMgrActor =
        std::make_shared<InstanceManagerActor>(MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }),
                                               mockGlobalScheduler, groupMgr, InstanceManagerStartParam{});
    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
        .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
            auto inst = std::make_shared<InstanceInfo>();
            inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
            return std::make_pair("", inst);
        }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);
    auto instanceMgrDriver = std::make_shared<InstanceManagerDriver>(instanceMgrActor, groupMgrActor);
    instanceMgrDriver->Start();

    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    EXPECT_CALL(*mockGlobalScheduler, GetLocalAddress)
        .WillRepeatedly(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));
    auto mockForwardCustomSignalReceived = std::make_shared<litebus::Promise<internal::ForwardKillRequest>>();
    EXPECT_CALL(*instCtrlActor1, MockForwardCustomSignalResponse)
        .WillRepeatedly(testing::Invoke([mockForwardCustomSignalReceived](
                                            const litebus::AID &from, const std::string &name, const std::string &msg) {
            internal::ForwardKillRequest fkReq;
            fkReq.ParseFromString(msg);
            mockForwardCustomSignalReceived->Set(fkReq);
            return std::make_pair(true, internal::ForwardKillResponse());
        }));

    litebus::Async(instanceMgrActor->GetAID(), &InstanceManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(instanceMgrActor->GetAID()));
    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));
    {
        //     Op: node id 1 is abnormal ( and instance manager RECOVER_ENABLE=false )
        // Expect: group 1 { owner=>GROUP_MANAGER_OWNER }, status is still running
        //         instance manager will set other instances to FATAL, and then trigger instance abnormal event
        auto mockPutRequestInvoked = std::make_shared<litebus::Promise<std::string>>();
        EXPECT_CALL(*mockMetaClient, Put)
            .WillOnce(testing::Invoke(
                [mockPutRequestInvoked](const std::string &key, const std::string &value, const PutOption &option) {
                    mockPutRequestInvoked->SetValue(value);
                    return std::make_shared<PutResponse>();
                }));

        auto mockForwardCustomSignalReceived = std::make_shared<litebus::Promise<internal::ForwardKillRequest>>();
        EXPECT_CALL(*instCtrlActor1, MockForwardCustomSignalResponse)
            .WillRepeatedly(
                testing::Invoke([mockForwardCustomSignalReceived](const litebus::AID &from, const std::string &name,
                                                                  const std::string &msg) {
                    internal::ForwardKillRequest fkReq;
                    fkReq.ParseFromString(msg);
                    mockForwardCustomSignalReceived->Set(fkReq);
                    return std::make_pair(true, internal::ForwardKillResponse());
                }));

        auto info1 = std::make_shared<messages::GroupInfo>();
        info1->set_groupid(GROUP_ID_1);
        info1->set_ownerproxy(NODE_ID_1);
        info1->set_status(static_cast<int32_t>(groupState));

        auto info2 = std::make_shared<messages::GroupInfo>();
        info2->set_groupid(GROUP_ID_2);
        info2->set_ownerproxy(NODE_ID_2);
        info2->set_status(static_cast<int32_t>(GroupState::RUNNING));

        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnGroupPut, GROUP_PATH_PREFIX + "/" + GROUP_ID_1,
                       info1);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnGroupPut, GROUP_PATH_PREFIX + "/" + GROUP_ID_2,
                       info2);

        auto instanceInfo1 = MakeInstanceInfo("001", GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnInstancePut,
                       INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + INSTANCE_ID_1, instanceInfo1);

        // trigger on local fault
        litebus::Async(instanceMgrActor->GetAID(), &InstanceManagerActor::OnLocalSchedFault, NODE_ID_1);

        // CHECK: always hand over the group ownership
        ASSERT_AWAIT_READY(mockPutRequestInvoked->GetFuture());
        auto infoStr = mockPutRequestInvoked->GetFuture().Get();
        messages::GroupInfo infoRecv;
        ASSERT_TRUE(TransToGroupInfoFromJson(infoRecv, infoStr));
        ASSERT_TRUE(infoRecv.ownerproxy() == GROUP_MANAGER_OWNER);

        if (groupState == GroupState::SCHEDULING || groupState == GroupState::FAILED) {
            // CHECK: if scheduling, will set group to FAILED
            // CHECK: if failed, do nothing
            ASSERT_TRUE(infoRecv.status() == static_cast<int32_t>(GroupState::FAILED));
        } else if (groupState == GroupState::RUNNING) {
            // CHECK: if running, do nothing
            ASSERT_TRUE(infoRecv.status() == static_cast<int32_t>(GroupState::RUNNING));
        }

        if (groupState == GroupState::SCHEDULING) {
            // CHECK: if scheduling, will send signal to all instances inside the group
            ASSERT_AWAIT_READY(mockForwardCustomSignalReceived->GetFuture());
            auto localRecvedForwardKillReq = mockForwardCustomSignalReceived->GetFuture().Get();
            ASSERT_TRUE(localRecvedForwardKillReq.has_req());
            ASSERT_TRUE(localRecvedForwardKillReq.req().signal() == GROUP_EXIT_SIGNAL);
        }
    }
    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}

class OuterKillerActor : public litebus::ActorBase {
public:
    OuterKillerActor() : litebus::ActorBase("outer-kill-actor"){};
    void Init() override
    {
        Receive("OnKillGroup", &OuterKillerActor::OnKillGroup);
    }

    void SendKillGroup(const litebus::AID &to, const std::shared_ptr<messages::KillGroup> &req)
    {
        Send(to, "KillGroup", req->SerializeAsString());
    }

    void OnKillGroup(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("OuterKillerActor get kill response from {}", from.HashString());
        auto killRsp = messages::KillGroupResponse{};
        if (killRsp.ParseFromString(msg)) {
            OnKillGroupCallback(killRsp);
            return;
        }
        YRLOG_ERROR("failed to parse kill response");
    }
    MOCK_METHOD(void, OnKillGroupCallback, (const messages::KillGroupResponse &), ());
};

/**
 * local abnormal,
    1. set owning group to fatal
    2. set owning group owner to GROUP_MANAGER
 */
TEST_F(GroupManagerTest, KillGroup)
{
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    EXPECT_CALL(*mockMetaClient, Watch).WillRepeatedly(testing::Return(litebus::Future<std::shared_ptr<Watcher>>()));
    EXPECT_CALL(*mockMetaClient, Get).WillRepeatedly(testing::Return(litebus::Future<std::shared_ptr<GetResponse>>()));

    auto mockGlobalScheduler = std::make_shared<MockGlobalSched>();
    auto groupMgrActor = std::make_shared<GroupManagerActor>(mockMetaClient, mockGlobalScheduler);
    auto groupMgr = std::make_shared<GroupManager>(groupMgrActor);
    auto instanceMgrActor =
        std::make_shared<InstanceManagerActor>(MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }),
                                               mockGlobalScheduler, groupMgr, InstanceManagerStartParam{});

    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
        .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
            auto inst = std::make_shared<InstanceInfo>();
            inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
            return std::make_pair("", inst);
        }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);

    auto instanceMgrDriver = std::make_shared<InstanceManagerDriver>(instanceMgrActor, groupMgrActor);
    instanceMgrDriver->Start();

    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    EXPECT_CALL(*mockGlobalScheduler, GetLocalAddress)
        .WillRepeatedly(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));
    auto mockForwardCustomSignalReceived = std::make_shared<litebus::Promise<internal::ForwardKillRequest>>();
    EXPECT_CALL(*instCtrlActor1, MockForwardCustomSignalResponse)
        .WillRepeatedly(testing::Invoke([mockForwardCustomSignalReceived](
                                            const litebus::AID &from, const std::string &name, const std::string &msg) {
            internal::ForwardKillRequest fkReq;
            fkReq.ParseFromString(msg);
            mockForwardCustomSignalReceived->Set(fkReq);

            internal::ForwardKillResponse fkRsp;
            fkRsp.set_requestid(fkReq.requestid());
            return std::make_pair(true, fkRsp);
        }));
    EXPECT_CALL(*instCtrlActor2, MockForwardCustomSignalResponse)
        .WillRepeatedly(testing::Invoke([](const litebus::AID &from, const std::string &name, const std::string &msg) {
            internal::ForwardKillRequest fkReq;
            fkReq.ParseFromString(msg);
            internal::ForwardKillResponse fkRsp;
            fkRsp.set_requestid(fkReq.requestid());
            return std::make_pair(true, fkRsp);
        }));
    auto clearGroupFuture = localGroupctlActor1->ExpectCallMockClearGroupResponseReturnOK()->GetFuture();

    litebus::Async(instanceMgrActor->GetAID(), &InstanceManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(instanceMgrActor->GetAID()));
    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));
    {
        //     Op: kill group
        // Expect: will send signal to all instances, and then delete the group info after signal send
        // auto mockPutRequestInvoked = std::make_shared<litebus::Promise<std::string>>();

        auto mockDeleteRequestInvoked = std::make_shared<litebus::Promise<std::string>>();
        EXPECT_CALL(*mockMetaClient, Delete)
            .WillOnce(testing::Invoke([mockDeleteRequestInvoked](const std::string &key, const DeleteOption &option) {
                YRLOG_DEBUG("mock meta client get delete request of {}", key);
                mockDeleteRequestInvoked->SetValue(key);
                return std::make_shared<DeleteResponse>();
            }));

        {
            // put groups info, cannot use etcd since we mocked it
            auto info1 = std::make_shared<messages::GroupInfo>();
            info1->set_groupid(GROUP_ID_1);
            info1->set_ownerproxy(NODE_ID_1);
            info1->set_status(static_cast<int32_t>(GroupState::RUNNING));

            auto info2 = std::make_shared<messages::GroupInfo>();
            info2->set_groupid(GROUP_ID_2);
            info2->set_ownerproxy(NODE_ID_2);
            info2->set_status(static_cast<int32_t>(GroupState::RUNNING));

            litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnGroupPut,
                           GROUP_PATH_PREFIX + "/" + GROUP_ID_1, info1);
            litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnGroupPut,
                           GROUP_PATH_PREFIX + "/" + GROUP_ID_2, info2);
        }

        auto instanceInfo1 = MakeInstanceInfo(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnInstancePut,
                       INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + INSTANCE_ID_1, instanceInfo1);

        auto instanceInfo2 = MakeInstanceInfo(INSTANCE_ID_2, GROUP_ID_2, NODE_ID_2, InstanceState::RUNNING);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnInstancePut,
                       INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + INSTANCE_ID_2, instanceInfo2);

        auto instanceInfo3 = MakeInstanceInfo(INSTANCE_ID_3, GROUP_ID_1, NODE_ID_2, InstanceState::RUNNING);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnInstancePut,
                       INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + INSTANCE_ID_2, instanceInfo3);

        auto outerKillerActor = std::make_shared<OuterKillerActor>();
        ASSERT_TRUE(litebus::Spawn(outerKillerActor).OK());
        auto respPromise = std::make_shared<litebus::Promise<messages::KillGroupResponse>>();
        EXPECT_CALL(*outerKillerActor, OnKillGroupCallback)
            .WillOnce(testing::Invoke(
                [&respPromise](const messages::KillGroupResponse &rsp) { respPromise->SetValue(rsp); }));

        // let killer send KillGroup
        auto killGroupReq = std::make_shared<messages::KillGroup>();
        killGroupReq->set_groupid(GROUP_ID_1);
        litebus::Async(outerKillerActor->GetAID(), &OuterKillerActor::SendKillGroup, groupMgrActor->GetAID(),
                       killGroupReq);

        // will forward kill signal to instance
        ASSERT_AWAIT_READY(mockForwardCustomSignalReceived->GetFuture());
        auto sentKillReq = mockForwardCustomSignalReceived->GetFuture().Get();
        ASSERT_TRUE(sentKillReq.has_req());
        ASSERT_TRUE(sentKillReq.req().signal() == SHUT_DOWN_SIGNAL);

        // will send kill group response back to outer killer
        ASSERT_AWAIT_READY(respPromise->GetFuture());
        auto kgRsp = respPromise->GetFuture().Get();
        YRLOG_INFO("kill group response: {}", kgRsp.DebugString());

        ASSERT_AWAIT_READY(mockDeleteRequestInvoked->GetFuture());
        ASSERT_AWAIT_READY(clearGroupFuture);
    }
    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}

// SlaveBusiness test cases
TEST_F(GroupManagerTest, SlaveBusinessTest)  // NOLINT
{
    auto groupCaches = std::make_shared<GroupManagerActor::GroupCaches>();
    auto member = std::make_shared<GroupManagerActor::Member>();
    member->groupCaches = groupCaches;
    auto instanceMgrActor = std::make_shared<GroupManagerActor>(nullptr, nullptr);
    auto slaveBusiness = std::make_shared<GroupManagerActor::SlaveBusiness>(member, instanceMgrActor);

    auto info = MakeInstanceInfo("", "", "", InstanceState::RUNNING);
    slaveBusiness->KillGroup(litebus::AID(), "", "");
    slaveBusiness->OnForwardCustomSignalResponse(litebus::AID(), "", "");
    slaveBusiness->OnInstanceAbnormal("", info);
    slaveBusiness->OnChange();
    slaveBusiness->OnLocalAbnormal("");
    slaveBusiness->OnInstancePut("", info);
    slaveBusiness->OnInstanceDelete("", info);
}

TEST_F(GroupManagerTest, GroupExitWithParentInstance)
{
    // Init part, use mockMetaClient, use mockGlobalScheduler, and update them to leader
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    EXPECT_CALL(*mockMetaClient, Watch).WillRepeatedly(testing::Return(litebus::Future<std::shared_ptr<Watcher>>()));
    EXPECT_CALL(*mockMetaClient, Get).WillRepeatedly(testing::Return(litebus::Future<std::shared_ptr<GetResponse>>()));

    auto mockGlobalScheduler = std::make_shared<MockGlobalSched>();
    auto groupMgrActor = std::make_shared<GroupManagerActor>(mockMetaClient, mockGlobalScheduler);
    auto groupMgr = std::make_shared<GroupManager>(groupMgrActor);
    auto instanceMgrActor =
        std::make_shared<InstanceManagerActor>(MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }),
                                               mockGlobalScheduler, groupMgr, InstanceManagerStartParam{});
    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
        .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
            auto inst = std::make_shared<InstanceInfo>();
            inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
            return std::make_pair("", inst);
        }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);
    auto instanceMgrDriver = std::make_shared<InstanceManagerDriver>(instanceMgrActor, groupMgrActor);
    instanceMgrDriver->Start();

    litebus::Async(instanceMgrActor->GetAID(), &InstanceManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(instanceMgrActor->GetAID()));
    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));

    {
        // Case 1. when instance delete, group manager also clear the group created by it
        litebus::Future<std::string> delArg;
        EXPECT_CALL(*mockMetaClient, Delete)
            .WillOnce(testing::DoAll(FutureArg<0>(&delArg), testing::Return(std::make_shared<DeleteResponse>())));
        auto clearGroupFuture = localGroupctlActor1->ExpectCallMockClearGroupResponseReturnOK()->GetFuture();
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        EXPECT_CALL(*mockGlobalScheduler, GetLocalAddress)
            .WillOnce(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));

        // Given: master state, 2 groups with parent ( instance-1 / instance-2 )
        auto instance1 = MakeInstanceInfo(INSTANCE_ID_1, "", NODE_ID_1, InstanceState::RUNNING);
        auto group1 = MakeGroupInfo(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, INSTANCE_ID_1);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnGroupPut, GROUP_PATH_PREFIX + "/" + GROUP_ID_1,
                       group1);

        // When:  kill instance-id-1
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnInstanceDelete,
                       INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + INSTANCE_ID_1, instance1);

        // Then:  the group info should be deleted
        ASSERT_AWAIT_READY(delArg);
        ASSERT_EQ(delArg.Get(), GROUP_PATH_PREFIX + "/" + GROUP_ID_1);
        ASSERT_AWAIT_READY(clearGroupFuture);
    }

    {
        // Case 2. when instance fatal, group manager set the group created by it to FAILED
        litebus::Future<std::string> putArg1, putArg2;
        EXPECT_CALL(*mockMetaClient, Put)
            .WillOnce(testing::DoAll(FutureArg<0>(&putArg1), FutureArg<1>(&putArg2),
                                     testing::Return(std::make_shared<PutResponse>())));

        // Given: master state, 1 groups with parent ( instance-1 / instance-2 )
        auto instance1 = MakeInstanceInfo(INSTANCE_ID_2, "", NODE_ID_2, InstanceState::FATAL);
        auto group1 = MakeGroupInfo(GROUP_ID_2, NODE_ID_2, GroupState::RUNNING, INSTANCE_ID_2);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnGroupPut, GROUP_PATH_PREFIX + "/" + GROUP_ID_2,
                       group1);

        // When:  instance-id-1 fatal
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnInstanceAbnormal,
                       INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + INSTANCE_ID_2, instance1);

        // Then:  the group info should be deleted
        ASSERT_AWAIT_READY(putArg1);
        ASSERT_EQ(putArg1.Get(), GROUP_PATH_PREFIX + "/" + GROUP_ID_2);

        ASSERT_AWAIT_READY(putArg2);
        messages::GroupInfo groupInfo;
        ASSERT_TRUE(TransToGroupInfoFromJson(groupInfo, putArg2.Get()));
        ASSERT_EQ(groupInfo.status(), static_cast<int32_t>(GroupState::FAILED));
    }

    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}

TEST_F(GroupManagerTest, GroupPutWithParentAbnormal)
{
    // Prepare: start group manager, start mock instance manager
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    EXPECT_CALL(*mockMetaClient, Watch).WillRepeatedly(testing::Return(litebus::Future<std::shared_ptr<Watcher>>()));
    EXPECT_CALL(*mockMetaClient, Get).WillRepeatedly(testing::Return(litebus::Future<std::shared_ptr<GetResponse>>()));

    auto mockGlobalScheduler = std::make_shared<MockGlobalSched>();
    mockGlobalScheduler->ReturnDefaultLocalAddress();

    auto groupMgrActor = std::make_shared<GroupManagerActor>(mockMetaClient, mockGlobalScheduler);
    auto groupMgr = std::make_shared<GroupManager>(groupMgrActor);
    auto instanceMgrActor =
        std::make_shared<InstanceManagerActor>(MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }),
                                               mockGlobalScheduler, groupMgr, InstanceManagerStartParam{});
    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    groupMgrActor->BindInstanceManager(mockInstanceMgr);

    auto instanceMgrDriver = std::make_shared<InstanceManagerDriver>(instanceMgrActor, groupMgrActor);
    instanceMgrDriver->Start();

    litebus::Async(instanceMgrActor->GetAID(), &InstanceManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(instanceMgrActor->GetAID()));
    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));

    {
        // Given: some group/instance records already in memory, and mocks the parent instance is missing
        auto instanceInfo1 = MakeInstanceInfo(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnInstancePut,
                       INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + INSTANCE_ID_1, instanceInfo1);

        litebus::Future<std::string> faInstID;
        EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
            .WillOnce(testing::DoAll(FutureArg<0>(&faInstID), testing::Return(std::make_pair("", nullptr))));

        litebus::Future<std::string> faDeleteKey;
        EXPECT_CALL(*mockMetaClient, Delete)
            .WillOnce(testing::DoAll(FutureArg<0>(&faDeleteKey), testing::Return(std::make_shared<DeleteResponse>())));

        auto mockForwardCustomSignalReceived = instCtrlActor1->ExpectCallMockInstanceCtrlForwardCustomSignalReturnOK();
        auto clearGroupFuture = localGroupctlActor1->ExpectCallMockClearGroupResponseReturnOK()->GetFuture();

        // When : put the group
        //        PutGroup(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, INSTANCE_ID_1);
        auto groupInfo = MakeGroupInfo(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, INSTANCE_ID_1);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnGroupPut, GROUP_PATH_PREFIX + "/" + GROUP_ID_1,
                       groupInfo);

        // Then : the instanceID1 will be checked,
        ASSERT_AWAIT_READY(faInstID);
        EXPECT_EQ(faInstID.Get(), INSTANCE_ID_1);

        //        the group info will be deleted,
        ASSERT_AWAIT_READY(faDeleteKey);
        EXPECT_EQ(faDeleteKey.Get(), GROUP_PATH_PREFIX + "/" + GROUP_ID_1);

        //        and the instances will be killed with SHUT_DOWN_SIGNAL
        ASSERT_AWAIT_READY(mockForwardCustomSignalReceived->GetFuture());
        EXPECT_EQ(mockForwardCustomSignalReceived->GetFuture().Get().req().signal(), SHUT_DOWN_SIGNAL);
        ASSERT_AWAIT_READY(clearGroupFuture);
    }

    {
        // Given: some group/instance records already in memory, and mocks the parent instance is FATAL
        auto instanceInfo1 = MakeInstanceInfo(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnInstancePut,
                       INSTANCE_PATH_PREFIX + "/123/function/0-yr-yr/version/0/defaultaz/123456/" + INSTANCE_ID_1, instanceInfo1);

        EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
            .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
                auto inst = std::make_shared<InstanceInfo>();
                inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::FATAL));
                return std::make_pair("", inst);
            }));

        auto mockForwardCustomSignalReceived = instCtrlActor1->ExpectCallMockInstanceCtrlForwardCustomSignalReturnOK();

        litebus::Future<std::string> faPutValue;
        EXPECT_CALL(*mockMetaClient, Put)
            .WillOnce(testing::DoAll(FutureArg<1>(&faPutValue), testing::Return(std::make_shared<PutResponse>())));

        // When : put the group
        //        PutGroup(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, INSTANCE_ID_1);
        auto groupInfo = MakeGroupInfo(GROUP_ID_1, NODE_ID_1, GroupState::RUNNING, INSTANCE_ID_2);
        litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::OnGroupPut, GROUP_PATH_PREFIX + "/" + GROUP_ID_1,
                       groupInfo);

        // Then : the group info will be set to FATAL, and the instances will be set to FATAL too
        //        should also check the error message is correct
        ASSERT_AWAIT_READY(faPutValue);
        EXPECT_THAT(faPutValue.Get(), testing::HasSubstr("\"status\":2"));
        ASSERT_AWAIT_READY(mockForwardCustomSignalReceived->GetFuture());
        EXPECT_EQ(mockForwardCustomSignalReceived->GetFuture().Get().req().signal(),
                  static_cast<int32_t>(GROUP_EXIT_SIGNAL));
        EXPECT_EQ(mockForwardCustomSignalReceived->GetFuture().Get().req().instanceid(), INSTANCE_ID_1);
        EXPECT_THAT(mockForwardCustomSignalReceived->GetFuture().Get().req().payload(), testing::HasSubstr(GROUP_ID_1));
        EXPECT_THAT(mockForwardCustomSignalReceived->GetFuture().Get().req().payload(),
                    testing::HasSubstr(INSTANCE_ID_2));
    }

    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}


TEST_F(GroupManagerTest, GroupInfoSyncerTest)
{
    // Init part, use mockMetaClient, use mockGlobalScheduler, and update them to leader
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);

    auto mockGlobalScheduler = std::make_shared<MockGlobalSched>();
    auto groupMgrActor = std::make_shared<GroupManagerActor>(mockMetaClient, mockGlobalScheduler);
    auto groupMgr = std::make_shared<GroupManager>(groupMgrActor);
    auto instanceMgrActor =
        std::make_shared<InstanceManagerActor>(MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ }),
                                               mockGlobalScheduler, groupMgr, InstanceManagerStartParam{});
    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
        .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
            auto inst = std::make_shared<InstanceInfo>();
            inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
            return std::make_pair("", inst);
        }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);
    auto instanceMgrDriver = std::make_shared<InstanceManagerDriver>(instanceMgrActor, groupMgrActor);
    instanceMgrDriver->Start();

    litebus::Async(instanceMgrActor->GetAID(), &InstanceManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(instanceMgrActor->GetAID()));
    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));

    {   // for get failed
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status(StatusCode::FAILED, "");

        auto future = groupMgrActor->GroupInfoSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());
    }

    {   // for get response is empty
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();

        auto future = groupMgrActor->GroupInfoSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
    }

    {   // for get response is empty
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();

        auto future = groupMgrActor->GroupInfoSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
    }

    {   // both in etcd and cache
        auto key1 = R"(/yr/group/ce052e60c86d76ee00/group-6c764080-aa61-4000-8000-000024957149)";
        auto value1 = R"({"requestID":"ce052e60c86d76ee00","traceID":"job-b4465ac5-trace-X","groupID":"group-6c764080-aa61-4000-8000-000024957149","parentID":"0d810043-06a6-4000-8000-00006ac6907d","ownerProxy":"siaphisprh00132","groupOpts":{"timeout":"300","groupName":"3abcdef0008","sameRunningLifecycle":true},"requests":[{"instance":{"instanceID":"d8ab6100-0000-4000-801a-f4f814674753","requestID":"ce052e60c86d76ee00-0","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"tenantId":"12345678901234561234567890123456","DELEGATE_DIRECTORY_QUOTA":"512","RecoverRetryTimes":"0","DATA_AFFINITY_ENABLED":"false"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-6c764080-aa61-4000-8000-000024957149"},"requestID":"ce052e60c86d76ee00-0","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"b4cbac61-0000-4000-8000-b0076050a971","requestID":"ce052e60c86d76ee00-1","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"tenantId":"12345678901234561234567890123456","RecoverRetryTimes":"0","DATA_AFFINITY_ENABLED":"false","DELEGATE_DIRECTORY_QUOTA":"512"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-6c764080-aa61-4000-8000-000024957149"},"requestID":"ce052e60c86d76ee00-1","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"3aad6100-0000-4000-8018-0c3b0e297ae0","requestID":"ce052e60c86d76ee00-2","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"RecoverRetryTimes":"0","DATA_AFFINITY_ENABLED":"false","DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-6c764080-aa61-4000-8000-000024957149"},"requestID":"ce052e60c86d76ee00-2","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"ae610000-0000-4000-bb54-2c1e5cb40d27","requestID":"ce052e60c86d76ee00-3","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false","RecoverRetryTimes":"0"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-6c764080-aa61-4000-8000-000024957149"},"requestID":"ce052e60c86d76ee00-3","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"6282c1dc-d5af-4100-8000-0000006740f0","requestID":"ce052e60c86d76ee00-4","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DELEGATE_DIRECTORY_QUOTA":"512","RecoverRetryTimes":"0","tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-6c764080-aa61-4000-8000-000024957149"},"requestID":"ce052e60c86d76ee00-4","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"07b20ff7-dcb0-4100-8000-000000551b0a","requestID":"ce052e60c86d76ee00-5","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DELEGATE_DIRECTORY_QUOTA":"512","DATA_AFFINITY_ENABLED":"false","tenantId":"12345678901234561234567890123456","RecoverRetryTimes":"0"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-6c764080-aa61-4000-8000-000024957149"},"requestID":"ce052e60c86d76ee00-5","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"d4928db1-6100-4000-8000-0081a0de67af","requestID":"ce052e60c86d76ee00-6","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DATA_AFFINITY_ENABLED":"false","tenantId":"12345678901234561234567890123456","DELEGATE_DIRECTORY_QUOTA":"512","RecoverRetryTimes":"0"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-6c764080-aa61-4000-8000-000024957149"},"requestID":"ce052e60c86d76ee00-6","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}}],"status":2})";


        // in etcd and not in cache
        auto key2 = R"(/yr/group/d9e1da12636d45e400/group-cda5051a-d278-48b3-a100-00000000000d)";
        auto value2 = R"({"requestID":"d9e1da12636d45e400","traceID":"job-b4465ac5-trace-X","groupID":"group-cda5051a-d278-48b3-a100-00000000000d","parentID":"0d810043-06a6-4000-8000-00006ac6907d","ownerProxy":"siaphisprh00132","groupOpts":{"timeout":"300","groupName":"9abcdef0008","sameRunningLifecycle":true},"requests":[{"instance":{"instanceID":"4eb3b461-0000-4000-8000-d2434ffd0ae2","requestID":"d9e1da12636d45e400-0","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false","RecoverRetryTimes":"0","DELEGATE_DIRECTORY_QUOTA":"512"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-cda5051a-d278-48b3-a100-00000000000d"},"requestID":"d9e1da12636d45e400-0","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"288ee2b5-6100-4000-8000-0024482c6b4d","requestID":"d9e1da12636d45e400-1","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false","RecoverRetryTimes":"0","DELEGATE_DIRECTORY_QUOTA":"512"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-cda5051a-d278-48b3-a100-00000000000d"},"requestID":"d9e1da12636d45e400-1","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"adb66100-0000-4000-809f-0d1bd179ea08","requestID":"d9e1da12636d45e400-2","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DATA_AFFINITY_ENABLED":"false","DELEGATE_DIRECTORY_QUOTA":"512","RecoverRetryTimes":"0","tenantId":"12345678901234561234567890123456"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-cda5051a-d278-48b3-a100-00000000000d"},"requestID":"d9e1da12636d45e400-2","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"36d4a234-0c2e-4761-8000-0000000042c6","requestID":"d9e1da12636d45e400-3","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DATA_AFFINITY_ENABLED":"false","RecoverRetryTimes":"0","DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-cda5051a-d278-48b3-a100-00000000000d"},"requestID":"d9e1da12636d45e400-3","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"1fabb861-0000-4000-8000-725edf9bd3a0","requestID":"d9e1da12636d45e400-4","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false","DELEGATE_DIRECTORY_QUOTA":"512","RecoverRetryTimes":"0"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-cda5051a-d278-48b3-a100-00000000000d"},"requestID":"d9e1da12636d45e400-4","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"43b906b9-6100-4000-8000-009cc54e1076","requestID":"d9e1da12636d45e400-5","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456","RecoverRetryTimes":"0","DATA_AFFINITY_ENABLED":"false"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-cda5051a-d278-48b3-a100-00000000000d"},"requestID":"d9e1da12636d45e400-5","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"8d02ba61-0000-4000-8000-7e3fb0844dfe","requestID":"d9e1da12636d45e400-6","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"RecoverRetryTimes":"0","DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-cda5051a-d278-48b3-a100-00000000000d"},"requestID":"d9e1da12636d45e400-6","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}}],"status":2})";

        // in cache and not in etcd
        auto key3 = R"(/yr/group/d4b532ab08a7d4d000/group-5b9f3eba-404e-48a2-a100-0000000000a3)";
        auto value3 = R"({"requestID":"d4b532ab08a7d4d000","traceID":"job-b4465ac5-trace-X","groupID":"group-5b9f3eba-404e-48a2-a100-0000000000a3","parentID":"0d810043-06a6-4000-8000-00006ac6907d","ownerProxy":"siaphisprh00132","groupOpts":{"timeout":"300","groupName":"6abcdef0008","sameRunningLifecycle":true},"requests":[{"instance":{"instanceID":"a3610000-0000-4000-b581-7112ee42b43b","requestID":"d4b532ab08a7d4d000-0","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"RecoverRetryTimes":"0","DATA_AFFINITY_ENABLED":"false","DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-5b9f3eba-404e-48a2-a100-0000000000a3"},"requestID":"d4b532ab08a7d4d000-0","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"b40e9b7a-e614-4461-8000-000000007942","requestID":"d4b532ab08a7d4d000-1","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false","RecoverRetryTimes":"0"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-5b9f3eba-404e-48a2-a100-0000000000a3"},"requestID":"d4b532ab08a7d4d000-1","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"791aa563-ff30-4561-8000-0000000026a6","requestID":"d4b532ab08a7d4d000-2","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false","RecoverRetryTimes":"0","DELEGATE_DIRECTORY_QUOTA":"512"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-5b9f3eba-404e-48a2-a100-0000000000a3"},"requestID":"d4b532ab08a7d4d000-2","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"3e37a9b4-894e-4661-8000-00000000e7ba","requestID":"d4b532ab08a7d4d000-3","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"RecoverRetryTimes":"0","tenantId":"12345678901234561234567890123456","DELEGATE_DIRECTORY_QUOTA":"512","DATA_AFFINITY_ENABLED":"false"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-5b9f3eba-404e-48a2-a100-0000000000a3"},"requestID":"d4b532ab08a7d4d000-3","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"b6d05947-d2a7-4100-8000-0000007c27b9","requestID":"d4b532ab08a7d4d000-4","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"Memory":{"name":"Memory","scalar":{"value":128}},"CPU":{"name":"CPU","scalar":{"value":300}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DELEGATE_DIRECTORY_QUOTA":"512","DATA_AFFINITY_ENABLED":"false","RecoverRetryTimes":"0","tenantId":"12345678901234561234567890123456"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-5b9f3eba-404e-48a2-a100-0000000000a3"},"requestID":"d4b532ab08a7d4d000-4","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"9674a861-0000-4000-8000-ecdcb9363dd8","requestID":"d4b532ab08a7d4d000-5","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456","DATA_AFFINITY_ENABLED":"false","RecoverRetryTimes":"0"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-5b9f3eba-404e-48a2-a100-0000000000a3"},"requestID":"d4b532ab08a7d4d000-5","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}},{"instance":{"instanceID":"3d9ca0a9-6100-4000-8000-00a0ad160bce","requestID":"d4b532ab08a7d4d000-6","function":"12345678901234561234567890123456/0-yr10882-yr-gangschedule/$latest","resources":{"resources":{"CPU":{"name":"CPU","scalar":{"value":300}},"Memory":{"name":"Memory","scalar":{"value":128}}}},"scheduleOption":{"affinity":{"instanceAffinity":{},"resource":{},"instance":{"topologyKey":"agent"}},"extension":{"DELEGATE_DIRECTORY_QUOTA":"512"},"range":{}},"createOptions":{"DELEGATE_DIRECTORY_QUOTA":"512","tenantId":"12345678901234561234567890123456","RecoverRetryTimes":"0","DATA_AFFINITY_ENABLED":"false"},"instanceStatus":{"code":1,"msg":"new instance"},"jobID":"job-b4465ac5","parentID":"0d810043-06a6-4000-8000-00006ac6907d","parentFunctionProxyAID":"siaphisprh00132-LocalSchedInstanceCtrlActor@7.189.31.67:22772","storageType":"s3","scheduleTimes":1,"deployTimes":1,"args":[{"value":"AAAA"},{"value":"AAAAAAAAAAAAAAAAAAAAAAE="}],"gracefulShutdownTime":"-1","tenantID":"12345678901234561234567890123456","groupID":"group-5b9f3eba-404e-48a2-a100-0000000000a3"},"requestID":"d4b532ab08a7d4d000-6","traceID":"job-b4465ac5-trace-X","contexts":{"LabelAffinityScorePlugin":{"preferredAffinityCtx":{}}}}],"status":2})";

        auto group1 = std::make_shared<messages::GroupInfo>();
        ASSERT_TRUE(TransToGroupInfoFromJson(*group1, value1));
        auto group2 = std::make_shared<messages::GroupInfo>();
        ASSERT_TRUE(TransToGroupInfoFromJson(*group2, value2));
        auto group3 = std::make_shared<messages::GroupInfo>();
        ASSERT_TRUE(TransToGroupInfoFromJson(*group3, value3));
        // put into cache
        groupMgrActor->OnGroupPut(key1, group1);
        groupMgrActor->OnGroupPut(key3, group3);
        EXPECT_TRUE(groupMgrActor->member_->groupCaches->GetGroupInfo(group1->groupid()).second);
        EXPECT_TRUE(groupMgrActor->member_->groupCaches->GetGroupInfo(group3->groupid()).second);
        // put into cache
        group1->set_status(0);
        groupMgrActor->OnGroupPut(key1, group1);
        EXPECT_EQ(groupMgrActor->member_->groupCaches->GetGroupInfo(group1->groupid()).first.second->status(),
                  group1->status());

        // mock etcd data
        KeyValue groupKv1;
        groupKv1.set_key(key1) ;
        groupKv1.set_value(value1);

        KeyValue groupKv2;
        groupKv1.set_key(key2) ;
        groupKv1.set_value(value2);

        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        rep->kvs.emplace_back(groupKv1);
        rep->kvs.emplace_back(groupKv2);

        auto future = groupMgrActor->GroupInfoSyncer(rep);
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());

        // test need to be added
        EXPECT_TRUE(groupMgrActor->member_->groupCaches->GetGroupInfo(group2->groupid()).second);

        // test need to be deleted
        EXPECT_FALSE(groupMgrActor->member_->groupCaches->GetGroupInfo(group3->groupid()).second);
    }

    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}

TEST_F(GroupManagerTest, OnInstancePutTest)
{
    auto mockGlobalScheduler = std::make_shared<MockGlobalSched>();
    auto groupCaches = std::make_shared<GroupManagerActor::GroupCaches>();
    auto member = std::make_shared<GroupManagerActor::Member>();
    member->groupCaches = groupCaches;
    member->globalScheduler = mockGlobalScheduler;
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    EXPECT_CALL(*mockGlobalScheduler, GetLocalAddress)
        .WillOnce(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));
    YRLOG_INFO("port:{}  instCtrlActor1:{}", std::to_string(port), std::string(instCtrlActor1->GetAID()));
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    auto groupMgrActor = std::make_shared<GroupManagerActor>(mockMetaClient, mockGlobalScheduler);
    litebus::Spawn(groupMgrActor);
    auto masterBusiness = std::make_shared<GroupManagerActor::MasterBusiness>(member, groupMgrActor);
    auto info = MakeInstanceInfo(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
    auto instanceKey = INSTANCE_PATH_PREFIX + "/" + INSTANCE_ID_1;
    masterBusiness->member_->groupCaches->AddGroup(GROUP_KEY_1, MakeGroupInfo(GROUP_ID_1, NODE_ID_1,
                                                                              GroupState::FAILED, "--"));
    masterBusiness->member_->groupCaches->AddGroupInstance(info->groupid(), instanceKey, info);

    auto future = masterBusiness->OnInstancePut(instanceKey, info);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    litebus::Terminate(groupMgrActor->GetAID());
    litebus::Await(groupMgrActor->GetAID());
}

TEST_F(GroupManagerTest, OnChangeTest)
{
    auto mockGlobalScheduler = std::make_shared<MockGlobalSched>();
    auto groupCaches = std::make_shared<GroupManagerActor::GroupCaches>();
    auto member = std::make_shared<GroupManagerActor::Member>();
    member->groupCaches = groupCaches;
    member->globalScheduler = mockGlobalScheduler;
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    EXPECT_CALL(*mockGlobalScheduler, GetLocalAddress)
        .WillOnce(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));

    auto mockForwardCustomSignalReceived = std::make_shared<litebus::Promise<internal::ForwardKillRequest>>();
    EXPECT_CALL(*instCtrlActor1, MockForwardCustomSignalResponse)
        .WillRepeatedly(testing::Invoke([mockForwardCustomSignalReceived](
                                            const litebus::AID &from, const std::string &name, const std::string &msg) {
            internal::ForwardKillRequest fkReq;
            fkReq.ParseFromString(msg);
            mockForwardCustomSignalReceived->Set(fkReq);
            internal::ForwardKillResponse fkRsp;
            fkRsp.set_requestid(fkReq.requestid());
            return std::make_pair(true, fkRsp);
        }));
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    auto groupMgrActor = std::make_shared<GroupManagerActor>(mockMetaClient, mockGlobalScheduler);
    litebus::Spawn(groupMgrActor);
    auto masterBusiness = std::make_shared<GroupManagerActor::MasterBusiness>(member, groupMgrActor);
    auto info = MakeInstanceInfo(INSTANCE_ID_1, GROUP_ID_1, NODE_ID_1, InstanceState::RUNNING);
    auto instanceKey = INSTANCE_PATH_PREFIX + "/" + INSTANCE_ID_1;
    masterBusiness->member_->groupCaches->AddGroup(GROUP_KEY_1, MakeGroupInfo(GROUP_ID_1, NODE_ID_1,
                                                                              GroupState::FAILED, "--"));
    masterBusiness->member_->groupCaches->AddGroupInstance(info->groupid(), instanceKey, info);

    masterBusiness->OnChange();
    ASSERT_AWAIT_READY(mockForwardCustomSignalReceived->GetFuture());
    litebus::Terminate(groupMgrActor->GetAID());
    litebus::Await(groupMgrActor->GetAID());
}

// Checks if the master verifies group instance consistency after switching from slave to master upon instance deletion.
TEST_F(GroupManagerTest, VerifyGroupInstanceConsistencyAfterSlaveDeletionEvent) {
    DEFAULT_START_INSTANCE_MANAGER_DRIVER(false);

    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
            .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
                auto inst = std::make_shared<InstanceInfo>();
                inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
                return std::make_pair("", inst);
            }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);

    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    EXPECT_CALL(*scheduler, GetLocalAddress)
            .WillRepeatedly(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));
    auto mockForwardCustomSignalReceived = std::make_shared<litebus::Promise<internal::ForwardKillRequest>>();
    EXPECT_CALL(*instCtrlActor2, MockForwardCustomSignalResponse)
        .Times(2)
        .WillRepeatedly(testing::Invoke([mockForwardCustomSignalReceived](
                const litebus::AID &from, const std::string &name, const std::string &msg) {
            internal::ForwardKillRequest fkReq;
            fkReq.ParseFromString(msg);
            mockForwardCustomSignalReceived->Set(fkReq);
            return std::make_pair(true, internal::ForwardKillResponse());
        }));

    PutDefaultGroupsAndInstances();
    DelInstance(INSTANCE_ID_1);

    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));

    ASSERT_AWAIT_READY(mockForwardCustomSignalReceived->GetFuture());
    CheckGroupState(GROUP_ID_1, GroupState::FAILED);
    CheckGroupState(GROUP_ID_2, GroupState::RUNNING);

    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}

// Validates whether the master ensures group instance consistency after crash recovery,
// especially for deleted instances.
TEST_F(GroupManagerTest, VerifyGroupInstanceConsistencyAfterCrashRecovery) {
    PutDefaultGroupsAndInstances();
    DelInstance(INSTANCE_ID_1);
    DEFAULT_START_INSTANCE_MANAGER_DRIVER(false);

    auto mockInstanceMgr = std::make_shared<MockInstanceManager>();
    EXPECT_CALL(*mockInstanceMgr, GetInstanceInfoByInstanceID)
            .WillRepeatedly(testing::Invoke([](const std::string &instanceID) {
                auto inst = std::make_shared<InstanceInfo>();
                inst->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
                return std::make_pair("", inst);
            }));
    groupMgrActor->BindInstanceManager(mockInstanceMgr);

    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    EXPECT_CALL(*scheduler, GetLocalAddress)
            .WillRepeatedly(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));
    auto mockForwardCustomSignalReceived = std::make_shared<litebus::Promise<internal::ForwardKillRequest>>();
    EXPECT_CALL(*instCtrlActor2, MockForwardCustomSignalResponse)
            .Times(2)
            .WillRepeatedly(testing::Invoke([mockForwardCustomSignalReceived](
                    const litebus::AID &from, const std::string &name, const std::string &msg) {
                internal::ForwardKillRequest fkReq;
                fkReq.ParseFromString(msg);
                mockForwardCustomSignalReceived->Set(fkReq);
                return std::make_pair(true, internal::ForwardKillResponse());
            }));

    auto start = std::chrono::steady_clock::now();
    while (groupMgrActor->member_->groupCaches->GetGroups().size() != 2) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
            EXPECT_TRUE(false) << "Timeout after 5 seconds";
        }
        std::this_thread::yield();
    }

    litebus::Async(groupMgrActor->GetAID(), &GroupManagerActor::UpdateLeaderInfo,
                   GetLeaderInfo(groupMgrActor->GetAID()));

    ASSERT_AWAIT_READY(mockForwardCustomSignalReceived->GetFuture());
    CheckGroupState(GROUP_ID_1, GroupState::FAILED);
    CheckGroupState(GROUP_ID_2, GroupState::RUNNING);

    DEFAULT_STOP_INSTANCE_MANAGER_DRIVER;
}
};  // namespace functionsystem::instance_manager::test