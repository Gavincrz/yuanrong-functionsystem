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


#include "common/types/common_state.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "mocks/mock_global_schd.h"
#include "mocks/group_ctrl_stub_actor.h"
#include "mocks/mock_meta_store_client.h"
#include "utils/generate_info.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"
#include "function_master/resource_group_manager/resource_group_manager_driver.h"

namespace functionsystem::resource_group_manager::test {
using namespace functionsystem::test;

std::shared_ptr<messages::ResourceGroupInfo> MakeClusterInfo(const std::string &rgName, const std::string &tenantID, const int bundleCnt)
{
    auto groupInfo = std::make_shared<messages::ResourceGroupInfo>();
    groupInfo->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    groupInfo->set_name(rgName);
    groupInfo->set_tenantid(tenantID);
    for(int i=0; i<bundleCnt; i++) {
        auto bundle = groupInfo->add_bundles();
        bundle->set_bundleid(groupInfo->requestid() + "-" + std::to_string(i));
        bundle->set_tenantid(tenantID);
        bundle->set_rgroupname(rgName);
    }
    return groupInfo;
}

std::shared_ptr<core_service::CreateResourceGroupRequest> MakeRgCreateRequest(const std::string requestID,
                                                                               const std::string &rgName,
                                                                               const std::string &tenantID)
{
    auto request = std::make_shared<core_service::CreateResourceGroupRequest>();
    request->set_requestid(requestID);
    request->mutable_rgroupspec()->set_tenantid(tenantID);
    request->mutable_rgroupspec()->set_name(rgName);
    auto bundle = request->mutable_rgroupspec()->add_bundles();
    *bundle->add_labels()  = "a=b";
    (*bundle->mutable_resources())["cpu"] =  500;
    (*bundle->mutable_resources())["mem"] =  500;
    return request;
}

class MockLocalResourceGroupCtrl : public litebus::ActorBase {
public:
    MockLocalResourceGroupCtrl() : litebus::ActorBase("ResourceGroupCtrlActor")
    {
    }
    ~MockLocalResourceGroupCtrl() = default;

    litebus::Future<core_service::CreateResourceGroupResponse> SendForwardCreateResourceGroup(
        const litebus::AID &to, const std::shared_ptr<core_service::CreateResourceGroupRequest> request)
    {
        createPromise_ = std::make_shared<litebus::Promise<core_service::CreateResourceGroupResponse>>();
        Send(to, "ForwardCreateResourceGroup", request->SerializeAsString());
        return createPromise_->GetFuture();
    }

    litebus::Future<inner_service::ForwardKillResponse > SendForwardDeleteResourceGroup(
        const litebus::AID &to, const std::shared_ptr<inner_service::ForwardKillRequest> request)
    {
        deletePromise_ = std::make_shared<litebus::Promise<inner_service::ForwardKillResponse>>();
        Send(to, "ForwardDeleteResourceGroup", request->SerializeAsString());
        return deletePromise_->GetFuture();
    }

    void OnForwardCreateResourceGroup(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        core_service::CreateResourceGroupResponse rsp;
        if (!rsp.ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse CreateResourceGroupResponse");
            return;
        }
        createPromise_->SetValue(rsp);
    }

    void OnForwardDeleteResourceGroup(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        inner_service::ForwardKillResponse rsp;
        if (!rsp.ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse ForwardKillResponse");
            return;
        }
        deletePromise_->SetValue(rsp);
    }

    void Init() override
    {
        Receive("OnForwardCreateResourceGroup", &MockLocalResourceGroupCtrl::OnForwardCreateResourceGroup);
        Receive("OnForwardDeleteResourceGroup", &MockLocalResourceGroupCtrl::OnForwardDeleteResourceGroup);
    }
    std::shared_ptr<litebus::Promise<core_service::CreateResourceGroupResponse>> createPromise_;
    std::shared_ptr<litebus::Promise<inner_service::ForwardKillResponse>> deletePromise_;
};

class MockLocalBundleMgrActor : public litebus::ActorBase {
public:
    MockLocalBundleMgrActor() : litebus::ActorBase("BundleMgrActor")
    {
    }
    ~MockLocalBundleMgrActor() = default;

    void RemoveBundle(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        messages::RemoveBundleRequest req;
        req.ParseFromString(msg);
        messages::RemoveBundleResponse rsp;
        rsp.set_rgroupname(req.rgroupname());
        rsp.set_requestid(req.requestid());
        rsp.mutable_status()->set_code(MockRemoveBundle());
        Send(from, "OnRemoveBundle", rsp.SerializeAsString());
    }

    litebus::Future<messages::ReportAgentAbnormalResponse> SendReportAgentAbnormalRequest(
        const litebus::AID &to, const std::shared_ptr<messages::ReportAgentAbnormalRequest> request)
    {
        promise_ = std::make_shared<litebus::Promise<messages::ReportAgentAbnormalResponse>>();
        Send(to, "ForwardReportAgentAbnormal", request->SerializeAsString());
        return promise_->GetFuture();
    }

    void ForwardReportAgentAbnormalResponse(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        messages::ReportAgentAbnormalResponse rsp;
        rsp.ParseFromString(msg);
        promise_->SetValue(rsp);
    }

    MOCK_METHOD(int32_t, MockRemoveBundle, ());

    void Init() override
    {
        Receive("RemoveBundle", &MockLocalBundleMgrActor::RemoveBundle);
        Receive("ForwardReportAgentAbnormalResponse", &MockLocalBundleMgrActor::ForwardReportAgentAbnormalResponse);
    }

    std::shared_ptr<litebus::Promise<messages::ReportAgentAbnormalResponse>> promise_;
};

class ResourceGroupManagerActorTest : public ::testing::Test {
protected:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
        uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
        localAddress_ = "127.0.0.1:" + std::to_string(port);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        etcdSrvDriver_->StopServer();
    }

    void SetUp() override
    {
        metaStoreClient_ = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
        auto groupInfo1 = MakeClusterInfo("defaultRG", "defaultTenant", 1);
        groupInfo1->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
        groupInfo1->mutable_bundles(0)->set_functionproxyid("default");
        groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));

        scheduler_ = std::make_shared<MockGlobalSched>();
        groupCtrlStub_ = std::make_shared<DomainGroupCtrlActorStub>(DOMAIN_GROUP_CTRL_ACTOR_NAME);
        litebus::Spawn(groupCtrlStub_);
        localResourceGroupCtrl_ = std::make_shared<MockLocalResourceGroupCtrl>();
        litebus::Spawn(localResourceGroupCtrl_);
        localBundleMgr_ = std::make_shared<MockLocalBundleMgrActor>();
        litebus::Spawn(localBundleMgr_);

        rgManagerActor_ = std::make_shared<ResourceGroupManagerActor>(metaStoreClient_, scheduler_);
        rgManagerActor_->groupOperator_->TxnResourceGroup(groupInfo1).Get();
        rgMangerDriver_ =  std::make_shared<ResourceGroupManagerDriver>(rgManagerActor_);
        rgMangerDriver_->Start();
    }
    void TearDown() override
    {
        metaStoreClient_->Delete("/", {false, true}).Get(3000);
        litebus::Terminate(groupCtrlStub_->GetAID());
        litebus::Await(groupCtrlStub_->GetAID());
        litebus::Terminate(localResourceGroupCtrl_->GetAID());
        litebus::Await(localResourceGroupCtrl_->GetAID());
        litebus::Terminate(localBundleMgr_->GetAID());
        litebus::Await(localBundleMgr_->GetAID());
        if (rgMangerDriver_ != nullptr) {
            rgMangerDriver_->Stop();
            rgMangerDriver_->Await();
        }
        rgManagerActor_ = nullptr;
        rgMangerDriver_ = nullptr;
        metaStoreClient_ = nullptr;
        scheduler_ = nullptr;
        groupCtrlStub_ = nullptr;
        localResourceGroupCtrl_ = nullptr;
        localBundleMgr_ = nullptr;
    }

    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::string metaStoreServerHost_;
    inline static std::string localAddress_;
    std::shared_ptr<MetaStoreClient> metaStoreClient_{ nullptr };
    std::shared_ptr<MockGlobalSched> scheduler_{ nullptr };
    std::shared_ptr<DomainGroupCtrlActorStub> groupCtrlStub_ {nullptr};
    std::shared_ptr<MockLocalResourceGroupCtrl> localResourceGroupCtrl_ {nullptr};
    std::shared_ptr<MockLocalBundleMgrActor> localBundleMgr_ { nullptr};

    std::shared_ptr<ResourceGroupManagerActor> rgManagerActor_{nullptr};
    std::shared_ptr<ResourceGroupManagerDriver> rgMangerDriver_{ nullptr};
};

TEST_F(ResourceGroupManagerActorTest, ClusterAndBundleTest)
{
    auto groupInfo1 = MakeClusterInfo("rg1", "tenant001", 1);
    auto groupInfo2 = MakeClusterInfo("rg1", "tenant002", 2);
    auto groupInfo = rgManagerActor_->GetResourceGroupInfo("rg1", "tenant001");
    EXPECT_TRUE(groupInfo == nullptr);
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    groupInfo = rgManagerActor_->GetResourceGroupInfo("rg2", "tenant001");
    EXPECT_TRUE(groupInfo == nullptr);
    rgManagerActor_->AddResourceGroupInfo(groupInfo2);
    groupInfo = rgManagerActor_->GetResourceGroupInfo("rg1", "tenant002");
    EXPECT_FALSE(groupInfo == nullptr);
    EXPECT_EQ(rgManagerActor_->member_->bundleInfos.size(), 3);
    EXPECT_EQ(rgManagerActor_->member_->proxyID2BundleIDs.size(), 0);
    groupInfo->mutable_bundles(0)->set_functionproxyid("node001");
    groupInfo->mutable_bundles(1)->set_functionproxyid("node002");
    rgManagerActor_->AddResourceGroupInfo(groupInfo);
    EXPECT_EQ(rgManagerActor_->member_->bundleInfos.size(), 3);
    EXPECT_EQ(rgManagerActor_->member_->proxyID2BundleIDs.size(), 2);
    auto bundleIndex = rgManagerActor_->GetBundleIndex("bundle001");
    EXPECT_TRUE(bundleIndex == nullptr);
    auto index = std::make_shared<BundleIndex>();
    index->tenantID = "tenant002";
    index->groupName = "rg1";
    index->index = 3;
    rgManagerActor_->member_->bundleInfos["bundle001"] = index;
    bundleIndex = rgManagerActor_->GetBundleIndex("bundle001");
    EXPECT_TRUE(bundleIndex == nullptr);
    index->groupName = "rg3";
    rgManagerActor_->member_->bundleInfos["bundle001"] = index;
    bundleIndex = rgManagerActor_->GetBundleIndex("bundle001");
    EXPECT_TRUE(bundleIndex == nullptr);
    index->groupName = "rg1";
    index->index = 1;
    rgManagerActor_->member_->bundleInfos["bundle001"] = index;
    bundleIndex = rgManagerActor_->GetBundleIndex("bundle001");
    EXPECT_TRUE(bundleIndex == nullptr);
    EXPECT_EQ(rgManagerActor_->member_->bundleInfos.size(), 3);
    rgManagerActor_->DeleteResourceGroupInfo(groupInfo1);
    EXPECT_EQ(rgManagerActor_->member_->bundleInfos.size(), 2);
    rgManagerActor_->DeleteResourceGroupInfo(groupInfo2);
    EXPECT_EQ(rgManagerActor_->member_->bundleInfos.size(), 0);
    EXPECT_EQ(rgManagerActor_->member_->resourceGroups.size(), 0);
    EXPECT_EQ(rgManagerActor_->member_->proxyID2BundleIDs.size(), 0);
    rgManagerActor_->DeleteResourceGroupInfo(groupInfo2);
}

TEST_F(ResourceGroupManagerActorTest, ResourceGroupOperatorTest)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    rgManagerActor_->groupOperator_->metaStoreClient_ = mockMetaClient;
    auto groupInfo1 = MakeClusterInfo("rg1", "tenant001", 1);
    // 1. put error
    auto putResp = std::make_shared<PutResponse>();
    putResp->status = Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaClient, Put).WillOnce(testing::Return(putResp));
    auto future = rgManagerActor_->groupOperator_->TxnResourceGroup(groupInfo1);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().StatusCode(), common::ErrorCode::ERR_ETCD_OPERATION_ERROR);
    // 2.delete error
    auto deleteResp = std::make_shared<DeleteResponse>();
    deleteResp->status = Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaClient, Delete).WillOnce(testing::Return(deleteResp));
    future = rgManagerActor_->groupOperator_->DeleteResourceGroup(groupInfo1);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().StatusCode(), common::ErrorCode::ERR_ETCD_OPERATION_ERROR);
    // 3. sync error
    auto getResp = std::make_shared<GetResponse>();
    getResp->status = Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaClient, Get).WillOnce(testing::Return(getResp));
    auto getFuture = rgManagerActor_->groupOperator_->SyncResourceGroups();
    ASSERT_AWAIT_READY(getFuture);
    EXPECT_TRUE(getFuture.Get()->kvs.empty());
    getResp->kvs = {};
    getResp->status = Status::OK();
    EXPECT_CALL(*mockMetaClient, Get).WillOnce(testing::Return(getResp));
    getFuture = rgManagerActor_->groupOperator_->SyncResourceGroups();
    ASSERT_AWAIT_READY(getFuture);
    EXPECT_TRUE(getFuture.Get()->kvs.empty());
}

TEST_F(ResourceGroupManagerActorTest, CreateDeleteResourceGroupSuccess)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    auto request = std::make_shared<core_service::CreateResourceGroupRequest>();
    request->set_requestid("request001");
    request->mutable_rgroupspec()->set_tenantid("tenant1");
    request->mutable_rgroupspec()->set_name("rg1");
    messages::GroupResponse rsp;
    rsp.set_requestid("rg1-request001");
    for (int i = 0; i < 11; i++) {
        auto bundle = request->mutable_rgroupspec()->add_bundles();
        *bundle->add_labels()  = "a=b";
        (*bundle->mutable_resources())["cpu"] =  500;
        (*bundle->mutable_resources())["mem"] =  500;
        messages::ScheduleResult result;
        result.set_nodeid("node00" + std::to_string(i));
        (*rsp.mutable_scheduleresults())["3_rg1_request001_" + std::to_string(i)] = result;
    }
    NodeInfo info{.name = "", .address= localAddress_};
    EXPECT_CALL(*scheduler_, GetRootDomainInfo()).WillOnce(testing::Return(litebus::Option<NodeInfo>(info)));
    EXPECT_CALL(*groupCtrlStub_, MockForwardGroupSchedule).WillOnce(testing::Return(rsp.SerializeAsString()));
    // 1. create rg
    auto future = localResourceGroupCtrl_->SendForwardCreateResourceGroup(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    auto groupInfo = rgManagerActor_->GetResourceGroupInfo("rg1", "tenant1");
    EXPECT_FALSE(groupInfo == nullptr);
    EXPECT_EQ(groupInfo->status().code(), static_cast<int32_t>(ResourceGroupState::CREATED));
    EXPECT_EQ(groupInfo->owner(), PRIMARY_TAG);
    EXPECT_EQ(groupInfo->bundles(0).functionproxyid(), "node000");
    EXPECT_EQ(groupInfo->bundles(0).status().code(), static_cast<int32_t>(ResourceGroupState::CREATED));
    EXPECT_EQ(groupInfo->bundles(10).functionproxyid(), "node0010");
    EXPECT_EQ(groupInfo->bundles(10).status().code(), static_cast<int32_t>(ResourceGroupState::CREATED));
    // ---- queryResourceGroup -----
    // 1. query all
    auto queryResourceGroupRequest = std::make_shared<messages::QueryResourceGroupRequest>();
    queryResourceGroupRequest->set_requestid("query1");
    auto queryRgFut = rgManagerActor_->QueryResourceGroup(queryResourceGroupRequest);
    EXPECT_AWAIT_READY(queryRgFut);
    auto queryRes = queryRgFut.Get();
    EXPECT_TRUE(static_cast<int>(queryRes.rgroup().size()) >= 1);
    bool rg1Existed = false;
    for (auto it : queryRes.rgroup()) {
        if (it.name() == "rg1") {
            rg1Existed = true;
            break;
        }
    }
    EXPECT_TRUE(rg1Existed);
    // 2. query with name(existent)
    queryResourceGroupRequest->set_rgroupname("rg1");
    queryRgFut = rgManagerActor_->QueryResourceGroup(queryResourceGroupRequest);
    EXPECT_AWAIT_READY(queryRgFut);
    queryRes = queryRgFut.Get();
    EXPECT_EQ(static_cast<int>(queryRes.rgroup().size()), 1);
    EXPECT_EQ(queryRes.rgroup()[0].name(), "rg1");
    // 3. query with name(non existent)
    queryResourceGroupRequest->set_rgroupname("rg2");
    queryRgFut = rgManagerActor_->QueryResourceGroup(queryResourceGroupRequest);
    EXPECT_AWAIT_READY(queryRgFut);
    queryRes = queryRgFut.Get();
    EXPECT_EQ(static_cast<int>(queryRes.rgroup().size()), 0);
    // ---- queryResourceGroup end -----

    // 2. delete rg not found
    auto killReq = std::make_shared<inner_service::ForwardKillRequest>();
    killReq->set_requestid("killReq-001");
    killReq->mutable_req()->set_instanceid("rg2");
    auto killFuture = localResourceGroupCtrl_->SendForwardDeleteResourceGroup(rgManagerActor_->GetAID(), killReq);
    ASSERT_AWAIT_READY(killFuture);
    EXPECT_EQ(killFuture.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_INSTANCE_NOT_FOUND));
    // 3. delete rg success
    killReq->mutable_req()->set_instanceid("rg1");
    EXPECT_CALL(*scheduler_, GetLocalAddress).WillRepeatedly(
        testing::Return(litebus::Option<std::string>(localAddress_)));
    EXPECT_CALL(*localBundleMgr_, MockRemoveBundle).Times(11).WillRepeatedly(testing::Return(0));
    killFuture = localResourceGroupCtrl_->SendForwardDeleteResourceGroup(rgManagerActor_->GetAID(), killReq);
    ASSERT_AWAIT_READY(killFuture);
    EXPECT_EQ(killFuture.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
}

TEST_F(ResourceGroupManagerActorTest, SlaveBusinessTest)
{
    auto member = std::make_shared<ResourceGroupManagerActor::Member>();
    auto slaveBusiness = std::make_shared<ResourceGroupManagerActor::SlaveBusiness>(member, rgManagerActor_);
    slaveBusiness->OnChange();
    EXPECT_TRUE(slaveBusiness->OnLocalAbnormal("").Get().IsOk());
    auto reportReq = std::make_shared<messages::ReportAgentAbnormalRequest>();
    reportReq->set_requestid("req-001");
    slaveBusiness->ForwardReportUnitAbnormal(localBundleMgr_->GetAID(), reportReq);

    auto request = MakeRgCreateRequest("request001", "rg1", "tenant1");
    auto future = localResourceGroupCtrl_->SendForwardCreateResourceGroup(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_INNER_COMMUNICATION));

    auto killReq = std::make_shared<inner_service::ForwardKillRequest>();
    killReq->set_requestid("killReq-001");
    killReq->mutable_req()->set_instanceid("rg2");
    auto killFuture = localResourceGroupCtrl_->SendForwardDeleteResourceGroup(rgManagerActor_->GetAID(), killReq);
    ASSERT_AWAIT_READY(killFuture);
    EXPECT_EQ(killFuture.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_INNER_COMMUNICATION));
}

TEST_F(ResourceGroupManagerActorTest, CreateResourceGroupFail)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    rgManagerActor_->defaultRescheduleInterval_ = 100;
    auto request = MakeRgCreateRequest("request001", "rg1", "tenant1");
    auto groupInfo1 = MakeClusterInfo("rg1", "tenant1", 1);
    groupInfo1->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    groupInfo1->mutable_bundles(0)->set_functionproxyid("node001");
    groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    // 1. resource group repeated
    auto future = localResourceGroupCtrl_->SendForwardCreateResourceGroup(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_PARAM_INVALID));
    rgManagerActor_->DeleteResourceGroupInfo(groupInfo1);

    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    rgManagerActor_->groupOperator_->metaStoreClient_ = mockMetaClient;
    // 2. trans pending failed
    auto putResp = std::make_shared<PutResponse>();
    putResp->status = Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaClient, Put).WillOnce(testing::Return(putResp));
    future = localResourceGroupCtrl_->SendForwardCreateResourceGroup(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_ETCD_OPERATION_ERROR));
    rgManagerActor_->DeleteResourceGroupInfo(groupInfo1);
    // 3. schedule get error rsp
    putResp->status = Status::OK();
    EXPECT_CALL(*mockMetaClient, Put).Times(2).WillOnce(testing::Return(putResp)).WillOnce(testing::Return(putResp));

    messages::GroupResponse rsp;
    rsp.set_requestid("rg1-request001");
    rsp.set_code(static_cast<int32_t>(common::ErrorCode::ERR_RESOURCE_NOT_ENOUGH));
    NodeInfo info{.name = "", .address= localAddress_};
    EXPECT_CALL(*scheduler_, GetRootDomainInfo()).WillOnce(testing::Return(litebus::Option<NodeInfo>(info)));
    EXPECT_CALL(*groupCtrlStub_, MockForwardGroupSchedule).WillOnce(testing::Return(rsp.SerializeAsString()));
    future = localResourceGroupCtrl_->SendForwardCreateResourceGroup(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_RESOURCE_NOT_ENOUGH));
    EXPECT_EQ(rgManagerActor_->GetResourceGroupInfo("rg1", "tenant1")->mutable_status()->code(),
              static_cast<int32_t>(ResourceGroupState::FAILED));
    rgManagerActor_->DeleteResourceGroupInfo(groupInfo1);
    // 4. put created failed
    putResp->status = Status::OK();
    auto errPutResp = std::make_shared<PutResponse>();
    errPutResp->status =  Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaClient, Put).WillOnce(testing::Return(putResp)).WillOnce(testing::Return(errPutResp));
    messages::ScheduleResult result;
    result.set_nodeid("node001");
    messages::GroupResponse rsp1;
    rsp1.set_requestid("rg1-request001");
    (*rsp1.mutable_scheduleresults())["rg1_request001_0"] = result;
    EXPECT_CALL(*scheduler_, GetRootDomainInfo()).WillOnce(testing::Return(litebus::Option<NodeInfo>(info)));
    EXPECT_CALL(*groupCtrlStub_, MockForwardGroupSchedule).WillOnce(testing::Return(rsp1.SerializeAsString()));
    future = localResourceGroupCtrl_->SendForwardCreateResourceGroup(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_ETCD_OPERATION_ERROR));
}

TEST_F(ResourceGroupManagerActorTest, CreateResourceGroupForwardFail)
{
    rgManagerActor_->defaultRescheduleInterval_ = 100;
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    auto request = MakeRgCreateRequest("request001", "rg1", "tenant1");
    // 4. put created failed
    messages::ScheduleResult result;
    result.set_nodeid("node001");
    messages::GroupResponse rsp1;
    rsp1.set_requestid("rg1-request001");
    (*rsp1.mutable_scheduleresults())["rg1_request001_0"] = result;
    NodeInfo info{.name = "", .address= localAddress_};
    EXPECT_CALL(*scheduler_, GetRootDomainInfo()).WillOnce(testing::Return(litebus::None())).WillOnce(testing::Return(litebus::Option<NodeInfo>(info)));
    EXPECT_CALL(*groupCtrlStub_, MockForwardGroupSchedule).WillOnce(testing::Return(rsp1.SerializeAsString()));
    auto future = localResourceGroupCtrl_->SendForwardCreateResourceGroup(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
}

TEST_F(ResourceGroupManagerActorTest, DeletePendingResourceGroup)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    auto groupInfo1 = MakeClusterInfo("rg1", "tenant1", 2);
    groupInfo1->mutable_status()->set_code(static_cast<int32_t>(BundleState::PENDING));
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    auto killReq = std::make_shared<inner_service::ForwardKillRequest>();
    killReq->set_requestid("killReq-001");
    killReq->mutable_req()->set_instanceid("rg1");

    // delete pending resource group -> delete request will hold on
    rgManagerActor_->HandleForwardDeleteResourceGroup(rgManagerActor_->GetAID(), killReq);
    auto it = rgManagerActor_->member_->toDeleteResourceGroups.find("tenant1_rg1");
    EXPECT_TRUE(it != rgManagerActor_->member_->toDeleteResourceGroups.end());

    // when creation process done, trigger deletion
    // 1. group schedule is failed
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    rgManagerActor_->groupOperator_->metaStoreClient_ = mockMetaClient;
    auto deleteResp = std::make_shared<DeleteResponse>();
    deleteResp->status = Status(StatusCode::SUCCESS);
    EXPECT_CALL(*mockMetaClient, Delete).WillOnce(testing::Return(deleteResp));
    messages::GroupResponse groupResp;
    groupResp.set_code(StatusCode::FAILED);
    auto promise = std::make_shared<litebus::Promise<core_service::CreateResourceGroupResponse>>();
    rgManagerActor_->ForwardGroupScheduleDone(groupResp, "reqId", "rg1", "tenant1", promise);
    it = rgManagerActor_->member_->toDeleteResourceGroups.find("tenant1_rg1");
    EXPECT_TRUE(it == rgManagerActor_->member_->toDeleteResourceGroups.end());
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("rg1", "tenant1") == nullptr; });

    // 2. group schedule is successful
    groupInfo1->mutable_status()->set_code(static_cast<int32_t>(BundleState::PENDING));
    groupInfo1->mutable_bundles(0)->set_functionproxyid("node001");
    groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::PENDING));
    groupInfo1->mutable_bundles(1)->set_functionproxyid("node001");
    groupInfo1->mutable_bundles(1)->mutable_status()->set_code(static_cast<int32_t>(BundleState::PENDING));
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    killReq = std::make_shared<inner_service::ForwardKillRequest>();
    killReq->set_requestid("killReq-001");
    killReq->mutable_req()->set_instanceid("rg1");

    rgManagerActor_->HandleForwardDeleteResourceGroup(rgManagerActor_->GetAID(), killReq);
    it = rgManagerActor_->member_->toDeleteResourceGroups.find("tenant1_rg1");
    EXPECT_TRUE(it != rgManagerActor_->member_->toDeleteResourceGroups.end());
    mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    rgManagerActor_->groupOperator_->metaStoreClient_ = mockMetaClient;
    EXPECT_CALL(*scheduler_, GetLocalAddress).WillOnce(testing::Return(litebus::Option<std::string>(localAddress_)));
    EXPECT_CALL(*localBundleMgr_, MockRemoveBundle).WillOnce(testing::Return(0));
    deleteResp = std::make_shared<DeleteResponse>();
    deleteResp->status = Status(StatusCode::SUCCESS);
    EXPECT_CALL(*mockMetaClient, Delete).WillOnce(testing::Return(deleteResp));
    EXPECT_CALL(*mockMetaClient, Put).Times(0);
    groupResp.set_code(StatusCode::SUCCESS);
    messages::ScheduleResult result;
    result.set_nodeid("node001");
    (*groupResp.mutable_scheduleresults())[groupInfo1->bundles(0).bundleid()] = result;
    (*groupResp.mutable_scheduleresults())[groupInfo1->bundles(1).bundleid()] = result;
    promise = std::make_shared<litebus::Promise<core_service::CreateResourceGroupResponse>>();
    rgManagerActor_->ForwardGroupScheduleDone(groupResp, "reqId", "rg1", "tenant1", promise);
    it = rgManagerActor_->member_->toDeleteResourceGroups.find("tenant1_rg1");
    EXPECT_TRUE(it == rgManagerActor_->member_->toDeleteResourceGroups.end());
}

TEST_F(ResourceGroupManagerActorTest, DeleteResourceGroupFail)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    auto groupInfo1 = MakeClusterInfo("rg1", "tenant1", 2);
    groupInfo1->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    groupInfo1->mutable_bundles(0)->set_functionproxyid("node001");
    groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    groupInfo1->mutable_bundles(1)->set_functionproxyid("node001");
    groupInfo1->mutable_bundles(1)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    auto killReq = std::make_shared<inner_service::ForwardKillRequest>();
    killReq->set_requestid("killReq-001");
    killReq->mutable_req()->set_instanceid("rg1");
    // 1. get local failed
    EXPECT_CALL(*scheduler_, GetLocalAddress).WillOnce(testing::Return(litebus::None()));
    auto killFuture = localResourceGroupCtrl_->SendForwardDeleteResourceGroup(rgManagerActor_->GetAID(), killReq);
    ASSERT_AWAIT_READY(killFuture);
    EXPECT_EQ(killFuture.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    // 2. remove bundle failed
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    EXPECT_CALL(*scheduler_, GetLocalAddress).WillOnce(testing::Return(litebus::Option<std::string>(localAddress_)));
    EXPECT_CALL(*localBundleMgr_, MockRemoveBundle).WillOnce(testing::Return(static_cast<int32_t>(common::ErrorCode::ERR_INNER_SYSTEM_ERROR)));
    killFuture = localResourceGroupCtrl_->SendForwardDeleteResourceGroup(rgManagerActor_->GetAID(), killReq);
    ASSERT_AWAIT_READY(killFuture);
    EXPECT_EQ(killFuture.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_INNER_SYSTEM_ERROR));
    // 3. delete from etcd failed
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    EXPECT_CALL(*scheduler_, GetLocalAddress).WillOnce(testing::Return(litebus::Option<std::string>(localAddress_)));
    EXPECT_CALL(*localBundleMgr_, MockRemoveBundle).WillOnce(testing::Return(0));
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    rgManagerActor_->groupOperator_->metaStoreClient_ = mockMetaClient;
    auto deleteResp = std::make_shared<DeleteResponse>();
    deleteResp->status = Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaClient, Delete).WillOnce(testing::Return(deleteResp));
    killFuture = localResourceGroupCtrl_->SendForwardDeleteResourceGroup(rgManagerActor_->GetAID(), killReq);
    ASSERT_AWAIT_READY(killFuture);
    EXPECT_EQ(killFuture.Get().code(), static_cast<int32_t>(common::ErrorCode::ERR_ETCD_OPERATION_ERROR));
}

TEST_F(ResourceGroupManagerActorTest, OnLocalAbnormal)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    // 1. node not found
    auto future = rgManagerActor_->OnLocalAbnormal("node001");
    ASSERT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get().IsOk());
    auto groupInfo1 = MakeClusterInfo("rg1", "tenant001", 1);
    groupInfo1->mutable_bundles(0)->set_functionproxyid("node002");
    groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    auto groupInfo2 = MakeClusterInfo("rg2", "tenant001", 1);
    groupInfo2->mutable_bundles(0)->set_functionproxyid("node002");
    groupInfo2->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    rgManagerActor_->AddResourceGroupInfo(groupInfo2);
    NodeInfo info{.name = "", .address= localAddress_};
    EXPECT_CALL(*scheduler_, GetRootDomainInfo()).Times(2).WillRepeatedly(testing::Return(litebus::Option<NodeInfo>(info)));

    messages::ScheduleResult result;
    result.set_nodeid("node001");
    messages::GroupResponse rsp;
    rsp.set_requestid(groupInfo1->name() + "-" + groupInfo1->requestid());
    (*rsp.mutable_scheduleresults())[groupInfo1->bundles(0).bundleid()] = result;

    messages::GroupResponse rsp1;
    rsp1.set_requestid(groupInfo2->name() + "-" +groupInfo2->requestid());
    (*rsp1.mutable_scheduleresults())[groupInfo2->bundles(0).bundleid()] = result;
    EXPECT_CALL(*groupCtrlStub_, MockForwardGroupSchedule).WillOnce(testing::Return(rsp.SerializeAsString())).WillOnce(testing::Return(rsp1.SerializeAsString()));
    rgManagerActor_->OnLocalAbnormal("node002");
    ASSERT_AWAIT_TRUE([=]() -> bool { return groupInfo1->bundles(0).functionproxyid() == "node001"; });
    ASSERT_AWAIT_TRUE([=]() -> bool { return groupInfo1->bundles(0).status().code() == static_cast<int32_t>(BundleState::CREATED); });
    ASSERT_AWAIT_TRUE([=]() -> bool { return groupInfo2->bundles(0).status().code() == static_cast<int32_t>(BundleState::CREATED); });
}


TEST_F(ResourceGroupManagerActorTest, ForwardReportUnitAbnormal)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    auto groupInfo1 = MakeClusterInfo("rg1", "tenant001", 1);
    groupInfo1->mutable_bundles(0)->set_functionproxyid("node002");
    groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    auto groupInfo2 = MakeClusterInfo("rg2", "tenant001", 1);
    groupInfo2->mutable_bundles(0)->set_functionproxyid("node002");
    groupInfo2->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    rgManagerActor_->AddResourceGroupInfo(groupInfo2);
    // 1. send report abnormal
    auto request = std::make_shared<messages::ReportAgentAbnormalRequest>();
    request->set_requestid("request001");
    *request->add_bundleids() = groupInfo1->mutable_bundles(0)->bundleid();
    *request->add_bundleids() = groupInfo2->mutable_bundles(0)->bundleid();
    *request->add_bundleids() = "not-exist";

    NodeInfo info{.name = "", .address= localAddress_};
    EXPECT_CALL(*scheduler_, GetRootDomainInfo()).Times(2).WillRepeatedly(testing::Return(litebus::Option<NodeInfo>(info)));

    messages::ScheduleResult result;
    result.set_nodeid("node001");
    messages::GroupResponse rsp;
    rsp.set_requestid(groupInfo1->name() + "-" + groupInfo1->requestid());
    (*rsp.mutable_scheduleresults())[groupInfo1->bundles(0).bundleid()] = result;

    messages::GroupResponse rsp1;
    rsp1.set_requestid(groupInfo2->name() + "-" +groupInfo2->requestid());
    (*rsp1.mutable_scheduleresults())[groupInfo2->bundles(0).bundleid()] = result;
    EXPECT_CALL(*groupCtrlStub_, MockForwardGroupSchedule).WillOnce(testing::Return(rsp.SerializeAsString())).WillOnce(testing::Return(rsp1.SerializeAsString()));
    auto future = localBundleMgr_->SendReportAgentAbnormalRequest(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    ASSERT_AWAIT_TRUE([=]() -> bool { return groupInfo1->bundles(0).status().code() == static_cast<int32_t>(BundleState::CREATED); });
    ASSERT_AWAIT_TRUE([=]() -> bool { return groupInfo2->bundles(0).status().code() == static_cast<int32_t>(BundleState::CREATED); });
}

TEST_F(ResourceGroupManagerActorTest, ForwardReportUnitAbnormalFail)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    auto groupInfo1 = MakeClusterInfo("rg1", "tenant001", 1);
    groupInfo1->mutable_bundles(0)->set_functionproxyid("node002");
    groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    auto groupInfo2 = MakeClusterInfo("rg2", "tenant001", 1);
    groupInfo2->mutable_bundles(0)->set_functionproxyid("node002");
    groupInfo2->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    rgManagerActor_->AddResourceGroupInfo(groupInfo2);

    auto request = std::make_shared<messages::ReportAgentAbnormalRequest>();
    request->set_requestid("request001");
    *request->add_bundleids() = groupInfo1->mutable_bundles(0)->bundleid();
    *request->add_bundleids() = groupInfo2->mutable_bundles(0)->bundleid();

    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    rgManagerActor_->groupOperator_->metaStoreClient_ = mockMetaClient;
    // 1. put metastore error
    auto putResp = std::make_shared<PutResponse>();
    putResp->status = Status::OK();
    auto errPutResp = std::make_shared<PutResponse>();
    errPutResp->status =  Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaClient, Put).WillOnce(testing::Return(putResp)).WillOnce(testing::Return(errPutResp)).WillRepeatedly(testing::Return(putResp));
    NodeInfo info{.name = "", .address= localAddress_};
    EXPECT_CALL(*scheduler_, GetRootDomainInfo()).Times(2).WillRepeatedly(testing::Return(litebus::Option<NodeInfo>(info)));

    messages::ScheduleResult result;
    result.set_nodeid("node001");
    messages::GroupResponse rsp;
    rsp.set_requestid(groupInfo1->name() + "-" + groupInfo1->requestid());
    (*rsp.mutable_scheduleresults())[groupInfo1->bundles(0).bundleid()] = result;

    messages::GroupResponse rsp1;
    rsp1.set_requestid(groupInfo2->name() + "-" +groupInfo2->requestid());
    (*rsp1.mutable_scheduleresults())[groupInfo2->bundles(0).bundleid()] = result;
    EXPECT_CALL(*groupCtrlStub_, MockForwardGroupSchedule).WillOnce(testing::Return(rsp.SerializeAsString())).WillOnce(testing::Return(rsp1.SerializeAsString()));
    auto future = localBundleMgr_->SendReportAgentAbnormalRequest(rgManagerActor_->GetAID(), request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), static_cast<int32_t>(StatusCode::ERR_ETCD_OPERATION_ERROR));
    ASSERT_AWAIT_TRUE([=]() -> bool { return groupInfo1->bundles(0).status().code() == static_cast<int32_t>(BundleState::CREATED); });
    ASSERT_AWAIT_TRUE([=]() -> bool { return groupInfo2->bundles(0).status().code() == static_cast<int32_t>(BundleState::CREATED); });

    rgManagerActor_->RescheduleResourceGroup("t1", "rg1");
    groupInfo1 = MakeClusterInfo("rg1", "tenant001", 1);
    groupInfo1->mutable_bundles(0)->set_functionproxyid("node002");
    groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    rgManagerActor_->DeleteResourceGroupInfo(groupInfo1);
    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    rgManagerActor_->RescheduleResourceGroup("tenant001", "rg1");
}

TEST_F(ResourceGroupManagerActorTest, SyncTest)
{
    rgManagerActor_->UpdateLeaderInfo(GetLeaderInfo(rgManagerActor_->GetAID()));
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("defaultRG", "defaultTenant") != nullptr; });
    auto groupInfo1 = MakeClusterInfo("rg001", "tenant001", 1);
    groupInfo1->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    groupInfo1->mutable_bundles(0)->set_functionproxyid("node0001");
    groupInfo1->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));

    auto groupInfo2 = MakeClusterInfo("rg002", "tenant002", 1);
    groupInfo2->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    groupInfo2->mutable_bundles(0)->set_functionproxyid("node0001");
    groupInfo2->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));

    auto groupInfo3 = MakeClusterInfo("rg003", "tenant003", 1);
    groupInfo3->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
    groupInfo3->mutable_bundles(0)->set_functionproxyid("node0003");
    groupInfo3->mutable_bundles(0)->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));

    rgManagerActor_->AddResourceGroupInfo(groupInfo1);
    rgManagerActor_->AddResourceGroupInfo(groupInfo2);
    rgManagerActor_->AddResourceGroupInfo(groupInfo3);
    rgManagerActor_->groupOperator_->TxnResourceGroup(groupInfo2).Get();
    rgManagerActor_->Sync();
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("rg001", "tenant001") == nullptr; });
    ASSERT_AWAIT_TRUE([=]() -> bool { return rgManagerActor_->GetResourceGroupInfo("rg003", "tenant003") == nullptr; });
}
}