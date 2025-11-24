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

#include "common/constants/actor_name.h"
#include "common/constants/metastore_keys.h"
#include "common/resource_view/view_utils.h"
#include "function_proxy/local_scheduler/local_group_ctrl/local_group_ctrl.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_local_sched_srv.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_resource_group_mgr_actor.h"
#include "mocks/mock_resource_view.h"
#include "mocks/mock_scheduler.h"
#include "mocks/mock_shared_client.h"
#include "mocks/mock_shared_client_manager_proxy.h"
#include "utils/future_test_helper.h"

#include "function_proxy/local_scheduler/bundle_manager/bundle_mgr.h"
#include "function_proxy/local_scheduler/bundle_manager/bundle_mgr_actor.h"

namespace functionsystem::test {
using namespace testing;
class DomainUnderLayerStub : public litebus::ActorBase {
public:
    DomainUnderLayerStub() : litebus::ActorBase("DomainUnderLayerStub")
    {
    }
    ~DomainUnderLayerStub() = default;

    litebus::Future<messages::ScheduleResponse> Reserve(const litebus::AID &dst,
                                                        const std::shared_ptr<messages::ScheduleRequest> &req)
    {
        Send(dst, "Reserve", req->SerializeAsString());
        reservePromises_[req->requestid()] = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
        return reservePromises_[req->requestid()]->GetFuture();
    }

    void OnReserve(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::ScheduleResponse resp;
        resp.ParseFromString(msg);
        if (reservePromises_.find(resp.requestid()) != reservePromises_.end()) {
            (void)reservePromises_[resp.requestid()]->SetValue(resp);
            (void)reservePromises_.erase(resp.requestid());
        }
    }

    litebus::Future<messages::GroupResponse> UnReserve(const litebus::AID &dst,
                                                       const std::shared_ptr<messages::ScheduleRequest> &req)
    {
        Send(dst, "UnReserve", req->SerializeAsString());
        unReservePromises_[req->requestid()] = std::make_shared<litebus::Promise<messages::GroupResponse>>();
        return unReservePromises_[req->requestid()]->GetFuture();
    }

    void OnUnReserve(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::GroupResponse resp;
        resp.ParseFromString(msg);
        if (unReservePromises_.find(resp.requestid()) != unReservePromises_.end()) {
            (void)unReservePromises_[resp.requestid()]->SetValue(resp);
            (void)unReservePromises_.erase(resp.requestid());
        }
    }

    litebus::Future<messages::GroupResponse> Bind(const litebus::AID &dst,
                                                  const std::shared_ptr<messages::ScheduleRequest> &req)
    {
        Send(dst, "Bind", req->SerializeAsString());
        if (bindPromises_.find(req->requestid()) != bindPromises_.end()) {
            return bindPromises_[req->requestid()]->GetFuture();
        }
        bindPromises_[req->requestid()] = std::make_shared<litebus::Promise<messages::GroupResponse>>();
        return bindPromises_[req->requestid()]->GetFuture();
    }

    void OnBind(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::GroupResponse resp;
        resp.ParseFromString(msg);
        if (bindPromises_.find(resp.requestid()) != bindPromises_.end()) {
            (void)bindPromises_[resp.requestid()]->SetValue(resp);
            (void)bindPromises_.erase(resp.requestid());
        }
    }

    litebus::Future<messages::GroupResponse> UnBind(const litebus::AID &dst,
                                                    const std::shared_ptr<messages::ScheduleRequest> &req)
    {
        Send(dst, "UnBind", req->SerializeAsString());
        unBindPromises_[req->requestid()] = std::make_shared<litebus::Promise<messages::GroupResponse>>();
        return unBindPromises_[req->requestid()]->GetFuture();
    }

    void OnUnBind(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::GroupResponse resp;
        resp.ParseFromString(msg);
        if (unBindPromises_.find(resp.requestid()) != unBindPromises_.end()) {
            (void)unBindPromises_[resp.requestid()]->SetValue(resp);
            (void)unBindPromises_.erase(resp.requestid());
        }
    }

    litebus::Future<messages::RemoveBundleResponse> RemoveBundle(const litebus::AID &dst,
                                                                 const std::shared_ptr<messages::RemoveBundleRequest> &req)
    {
        Send(dst, "RemoveBundle", req->SerializeAsString());
        removeBundlePromises_[req->requestid()] = std::make_shared<litebus::Promise<messages::RemoveBundleResponse>>();
        return removeBundlePromises_[req->requestid()]->GetFuture();
    }

    void OnRemoveBundle(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::RemoveBundleResponse resp;
        resp.ParseFromString(msg);
        if (removeBundlePromises_.find(resp.requestid()) != removeBundlePromises_.end()) {
            (void)removeBundlePromises_[resp.requestid()]->SetValue(resp);
            (void)removeBundlePromises_.erase(resp.requestid());
        }
    }

    void Init() override
    {
        Receive("OnReserve", &DomainUnderLayerStub::OnReserve);
        Receive("OnBind", &DomainUnderLayerStub::OnBind);
        Receive("OnUnReserve", &DomainUnderLayerStub::OnUnReserve);
        Receive("OnUnBind", &DomainUnderLayerStub::OnUnBind);
        Receive("OnRemoveBundle", &DomainUnderLayerStub::OnRemoveBundle);
    }

private:
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::ScheduleResponse>>> reservePromises_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::GroupResponse>>> unReservePromises_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::GroupResponse>>> bindPromises_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::GroupResponse>>> unBindPromises_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::RemoveBundleResponse>>>
        removeBundlePromises_;
};

class BundleMgrTest : public ::testing::Test {
public:
    void SetUp() override
    {
        clientManager_ = std::make_shared<MockSharedClientManagerProxy>();
        mockScheduler_ = std::make_shared<MockScheduler>();
        mockLocalSchedSrv_ = std::make_shared<MockLocalSchedSrv>();
        mockInstanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        EXPECT_CALL(*mockInstanceCtrl_, RegisterClearGroupInstanceCallBack).WillRepeatedly(Return());
        mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>("");
        auto resourceViewMgr = std::make_shared<resource_view::ResourceViewMgr>();
        primary_ = MockResourceView::CreateMockResourceView();
        virtual_ = MockResourceView::CreateMockResourceView();
        resourceViewMgr->primary_ = primary_;
        resourceViewMgr->virtual_ = virtual_;

        BundleManagerActorParam param = {
            .actorName = BUNDLE_MGR_ACTOR_NAME,
            .nodeId = "nodeA",
            .metaStoreClient = mockMetaStoreClient_,
            .reservedTimeout = 120000
        };
        bundleMgrActor_ = std::make_shared<BundleMgrActor>(param);
        bundleMgrActor_->BindScheduler(mockScheduler_);
        bundleMgrActor_->BindResourceViewMgr(resourceViewMgr);
        bundleMgrActor_->BindInstanceCtrl(mockInstanceCtrl_);
        bundleMgrActor_->BindLocalSchedSrv(mockLocalSchedSrv_);
        litebus::Spawn(bundleMgrActor_);
        bundleMgr_ = std::make_shared<BundleMgr>(bundleMgrActor_);
        bundleMgr_->ToReady();
        underlayerSrv_ = std::make_shared<DomainUnderLayerStub>();
        litebus::Spawn(underlayerSrv_);
    }

    void TearDown() override
    {
        litebus::Terminate(bundleMgrActor_->GetAID());
        litebus::Terminate(underlayerSrv_->GetAID());
        litebus::Await(bundleMgrActor_);
        litebus::Await(underlayerSrv_);
        clientManager_ = nullptr;
        bundleMgr_ = nullptr;
        mockScheduler_ = nullptr;
        mockLocalSchedSrv_ = nullptr;
        mockInstanceCtrl_ = nullptr;
        mockMetaStoreClient_ = nullptr;
        bundleMgrActor_ = nullptr;
        underlayerSrv_ = nullptr;
    }

    void Start()
    {
        auto getResponse = std::make_shared<GetResponse>();
        EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
        auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }

protected:
    std::shared_ptr<BundleMgr> bundleMgr_;
    std::shared_ptr<BundleMgrActor> bundleMgrActor_;
    std::shared_ptr<MockSharedClientManagerProxy> clientManager_;
    std::shared_ptr<MockSharedClient> sharedClientMgr_;
    std::shared_ptr<MockScheduler> mockScheduler_;
    std::shared_ptr<MockInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockLocalSchedSrv> mockLocalSchedSrv_;
    std::shared_ptr<MockResourceView> primary_;
    std::shared_ptr<MockResourceView> virtual_;
    std::shared_ptr<MockMetaStoreClient> mockMetaStoreClient_;
    std::shared_ptr<DomainUnderLayerStub> underlayerSrv_;
};

struct BundleCollectionPair {
    KeyValue kv;
    std::shared_ptr<messages::BundleCollection> collection;
};

BundleCollectionPair NewBundlesJson(std::unordered_map<std::string, messages::BundleInfo> bundles)
{
    auto collection = std::make_shared<messages::BundleCollection>();
    collection->mutable_bundles()->insert(bundles.begin(), bundles.end());

    KeyValue kv;
    kv.set_key("yr/bundle/nodeA");
    std::string jsonStr;
    (void)google::protobuf::util::MessageToJsonString(*collection, &jsonStr);
    kv.set_value(jsonStr);
    return {kv, collection};
}

std::unordered_map<std::string, messages::BundleInfo> GetBundles()
{
    messages::BundleInfo bundle1;
    bundle1.set_bundleid("bundle1");
    bundle1.set_functionagentid("agent1");
    bundle1.set_rgroupname("virtual");
    bundle1.set_parentrgroupname("primary");
    messages::BundleInfo bundle2;
    bundle2.set_bundleid("bundle2");
    bundle2.set_functionagentid("agent1");
    bundle2.set_rgroupname("virtual");
    bundle2.set_parentrgroupname("primary");
    messages::BundleInfo bundle3;
    bundle3.set_bundleid("bundle3");
    bundle3.set_functionagentid("agent2");
    bundle3.set_rgroupname("virtual");
    bundle3.set_parentrgroupname("primary");

    messages::BundleCollection collection;
    std::unordered_map<std::string, messages::BundleInfo> m;
    m[bundle1.bundleid()] = bundle1;
    m[bundle2.bundleid()] = bundle2;
    m[bundle3.bundleid()] = bundle3;
    return m;
}

// start bundle mgr with empty
TEST_F(BundleMgrTest, BundleMgrStartedWithEmpty)
{
    auto getResponse = std::make_shared<GetResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
}

// start bundle manager when get persisted data fails
TEST_F(BundleMgrTest, BundleMgrStartWhenGetDataFail)
{
    auto getResponse = std::make_shared<GetResponse>();
    getResponse->status = Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
}

// start bundle manager with invalid value
TEST_F(BundleMgrTest, BundleMgrStartedWithInvalidData)
{
    auto getResponse = std::make_shared<GetResponse>();
    KeyValue kv;
    kv.set_key("/yr/bundle/nodeA");
    kv.set_value("xxxxxxx");
    getResponse->kvs.emplace_back(kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
}

// start bundle manager with valid data
TEST_F(BundleMgrTest, BundleMgrStartedWithValidData)
{
    auto getResponse = std::make_shared<GetResponse>();
    auto pair = NewBundlesJson(GetBundles());
    getResponse->kvs.emplace_back(pair.kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(static_cast<int>(bundleMgrActor_->bundles_.size()), 3);
    EXPECT_TRUE(bundleMgrActor_->bundles_.find("bundle1") != bundleMgrActor_->bundles_.end());
}

// request with invalid msg
TEST_F(BundleMgrTest, InvalidReserveAndBind)
{
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_)).Times(0);
    EXPECT_CALL(*mockMetaStoreClient_, Put).Times(0);
    EXPECT_CALL(*primary_, GetResourceViewChanges()).Times(0);
    EXPECT_CALL(*virtual_, GetResourceViewChanges()).Times(0);
    EXPECT_CALL(*primary_, DeleteInstances).Times(0);
    EXPECT_CALL(*virtual_, AddResourceUnit).Times(0);
    bundleMgrActor_->Reserve(litebus::AID(), "Reserve", "xxx");
    bundleMgrActor_->Bind(litebus::AID(), "Bind", "xxx");
    bundleMgrActor_->UnReserve(litebus::AID(), "UnReserve", "xxx");
    bundleMgrActor_->UnBind(litebus::AID(), "UnBind", "xxx");
}

/**
* This request requires resources from primary resource view, then create unit in virtual resource view
* @return std::shared_ptr<messages::ScheduleRequest>
 */
std::shared_ptr<messages::ScheduleRequest> CreateScheduleRequest()
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->set_traceid("traceID");
    scheduleReq->set_requestid("request-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    scheduleReq->mutable_instance()->set_instanceid("7_rgroup1_0_tenant1");
    scheduleReq->mutable_instance()->set_tenantid("tenant1");
    scheduleReq->mutable_instance()->mutable_scheduleoption()->set_rgroupname("primary");
    scheduleReq->mutable_instance()->mutable_labels()->Add("label_inst1");
    scheduleReq->mutable_instance()->mutable_labels()->Add("rgroup:rgname1");
    scheduleReq->mutable_instance()->mutable_labels()->Add("rg_rgname1_bundle:0");
    return scheduleReq;
}

// Reserve successful & UnReserve successful
TEST_F(BundleMgrTest, ReserveAndUnReserveSuccessful)
{
    auto scheduleReq = CreateScheduleRequest();
    auto allocatedPromise = std::make_shared<litebus::Promise<Status>>();
    allocatedPromise->SetValue(Status(StatusCode::FAILED));

    schedule_decision::ScheduleResult scheduleResult;
    scheduleResult.id = "agent";
    scheduleResult.code = 0,
    scheduleResult.realIDs = {5};
    scheduleResult.unitID = "rgroup0-0-xxx";
    auto name = resource_view::NPU_RESOURCE_NAME + "/310";
    auto &vectors = scheduleResult.allocatedVectors[name];
    auto &cg = (*vectors.mutable_values())[resource_view::HETEROGENEOUS_MEM_KEY];
    for (int i = 0; i< 8; i++) {
        (*cg.mutable_vectors())["uuid"].add_values(1010);
    }

    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(schedule_decision::ScheduleResult{ "agent", 0, {}, {}, "", {}, {}, allocatedPromise }))
        .WillOnce(Return(scheduleResult));
    auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
    EXPECT_CALL(*primary_, GetResourceViewChanges()).WillRepeatedly(Return(changes));
    EXPECT_CALL(*virtual_, GetResourceViewChanges()).WillRepeatedly(Return(changes));

    {
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Reserve,
                                     bundleMgrActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        auto result = future.Get();
        EXPECT_EQ(result.code(), 0);
        EXPECT_TRUE(bundleMgrActor_->reserveResult_.find(scheduleReq->requestid()) != bundleMgrActor_->reserveResult_.end());
        auto reserveRes = bundleMgrActor_->reserveResult_[scheduleReq->requestid()];
        EXPECT_EQ(reserveRes.bundleInfo.bundleid(), scheduleReq->mutable_instance()->instanceid());
        EXPECT_EQ(reserveRes.bundleInfo.functionagentid(), scheduleResult.id);
        EXPECT_EQ(reserveRes.bundleInfo.parentid(), scheduleResult.unitID);
        auto resources = reserveRes.bundleInfo.resources().resources();
        EXPECT_EQ(resources.at(name).type(), resources::Value_Type::Value_Type_VECTORS);
        EXPECT_EQ(resources.at(name).name(), name);
        EXPECT_EQ(resources.at(name).vectors().values().at(resource_view::HETEROGENEOUS_MEM_KEY)
                      .vectors().at("uuid").values().at(0), 1010);
        EXPECT_EQ(static_cast<int>(reserveRes.bundleInfo.labels().size()), 3);
        EXPECT_EQ(reserveRes.bundleInfo.rgroupname(), "rgroup1");

        // duplicate request
        future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Reserve,
                                bundleMgrActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().code(), 0);
    }

    {
        EXPECT_CALL(*primary_, DeleteInstances).Times(1);
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::UnReserve,
                                     bundleMgrActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().code(), 0);
        auto reserveRes = bundleMgrActor_->reserveResult_;
        EXPECT_TRUE(reserveRes.empty());
    }
}

// reserve failed(schedule failed)
TEST_F(BundleMgrTest, ReserveFails)
{
    auto scheduleReq = CreateScheduleRequest();
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(schedule_decision::ScheduleResult{ "agent", StatusCode::RESOURCE_NOT_ENOUGH, {} }));

    auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
    EXPECT_CALL(*primary_, GetResourceViewChanges()).WillRepeatedly(Return(changes));
    EXPECT_CALL(*virtual_, GetResourceViewChanges()).WillRepeatedly(Return(changes));

    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Reserve,
                                 bundleMgrActor_->GetAID(), scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    auto result = future.Get();
    EXPECT_EQ(result.code(), StatusCode::RESOURCE_NOT_ENOUGH);
    auto reserveRes = bundleMgrActor_->reserveResult_;
    EXPECT_TRUE(reserveRes.empty());
}

// Timeout to bind(Reserve successful then timeout)
TEST_F(BundleMgrTest, ReserveAndTimoutToReserve)
{
    litebus::Terminate(bundleMgrActor_->GetAID());
    litebus::Await(bundleMgrActor_->GetAID());
    BundleManagerActorParam param = {
        .actorName = BUNDLE_MGR_ACTOR_NAME,
        .nodeId = "nodeA",
        .metaStoreClient = mockMetaStoreClient_,
        .reservedTimeout = 100
    };
    auto bundleMgrActor = std::make_shared<BundleMgrActor>(param);
    bundleMgrActor->BindScheduler(mockScheduler_);
    auto resourceViewMgr = std::make_shared<resource_view::ResourceViewMgr>();
    auto primaryView = MockResourceView::CreateMockResourceView();
    auto virtualView = MockResourceView::CreateMockResourceView();
    resourceViewMgr->primary_ = primaryView;
    resourceViewMgr->virtual_ = virtualView;
    bundleMgrActor->BindResourceViewMgr(resourceViewMgr);
    bundleMgrActor->BindInstanceCtrl(mockInstanceCtrl_);
    litebus::Spawn(bundleMgrActor);
    auto bundleMgr = std::make_shared<BundleMgr>(bundleMgrActor_);
    bundleMgr->ToReady();

    auto scheduleReq = CreateScheduleRequest();
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(schedule_decision::ScheduleResult{ "agent", 0, {} }));
    auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
    EXPECT_CALL(*primaryView, GetResourceViewChanges()).WillRepeatedly(Return(changes));
    EXPECT_CALL(*virtualView, GetResourceViewChanges()).WillRepeatedly(Return(changes));
    litebus::Future<std::vector<std::string>> deletedIns;
    EXPECT_CALL(*primaryView, DeleteInstances).WillOnce(DoAll(FutureArg<0>(&deletedIns), Return(Status::OK())));
    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Reserve,
                                 bundleMgrActor->GetAID(), scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    auto result = future.Get();
    EXPECT_EQ(result.code(), 0);
    ASSERT_AWAIT_READY(deletedIns);
    EXPECT_EQ(deletedIns.IsOK(), true);
    EXPECT_EQ(deletedIns.Get().size(), static_cast<long unsigned int>(1));
    litebus::Terminate(bundleMgrActor->GetAID());
    litebus::Await(bundleMgrActor->GetAID());
}

// Bind failed (no reserve result)
TEST_F(BundleMgrTest, BindFailedByNoReserve)
{
    auto scheduleReq = CreateScheduleRequest();
    auto reserveRes = bundleMgrActor_->reserveResult_;
    EXPECT_TRUE(reserveRes.empty());
    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Bind,
                                 bundleMgrActor_->GetAID(), scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get().code(), StatusCode::ERR_INNER_SYSTEM_ERROR);
}

// Reserve successful & Bind successful & UnBind successful
TEST_F(BundleMgrTest, ReserveAndBindAndUnBindSuccessful)
{
    auto scheduleReq = CreateScheduleRequest();
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(schedule_decision::ScheduleResult{ "agent", 0, {} }));
    auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
    EXPECT_CALL(*primary_, GetResourceViewChanges()).WillRepeatedly(Return(changes));
    EXPECT_CALL(*virtual_, GetResourceViewChanges()).WillRepeatedly(Return(changes));

    {
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Reserve,
                                     bundleMgrActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        auto result = future.Get();
        EXPECT_EQ(result.code(), 0);
    }

    {
        litebus::Future<ResourceUnit> unitFuture;
        EXPECT_CALL(*virtual_, AddResourceUnit).WillOnce(DoAll(FutureArg<0>(&unitFuture), Return(Status::OK())));
        auto putResponse = std::make_shared<PutResponse>();
        putResponse->status = Status(StatusCode::SUCCESS);
        EXPECT_CALL(*mockMetaStoreClient_, Put).WillRepeatedly(Return(putResponse));
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Bind, bundleMgrActor_->GetAID(),
                                     scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().code(), 0);
        ASSERT_AWAIT_READY(unitFuture);
        EXPECT_EQ(unitFuture.IsOK(), true);
        auto unit = unitFuture.Get();
        EXPECT_TRUE(unit.nodelabels().find("rg_rgname1_bundle") != unit.nodelabels().end());
        EXPECT_TRUE(unit.nodelabels().find("rg_rgname1_bundle")->second.items().count("0") != 0);
        EXPECT_TRUE(unit.nodelabels().find(TENANT_ID) != unit.nodelabels().end());
        EXPECT_TRUE(unit.nodelabels().find(TENANT_ID)->second.items().count("tenant1") != 0);
    }

    {
        auto delResponse = std::make_shared<DeleteResponse>();
        delResponse->status = Status(StatusCode::SUCCESS);
        EXPECT_CALL(*mockMetaStoreClient_, Delete).WillOnce(Return(delResponse));
        EXPECT_CALL(*primary_, DeleteInstances).Times(1);
        EXPECT_CALL(*virtual_, DeleteResourceUnit).Times(1);
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::UnBind, bundleMgrActor_->GetAID(),
                                     scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().code(), 0);
        EXPECT_TRUE(bundleMgrActor_->bundles_.find(scheduleReq->mutable_instance()->instanceid())
                    == bundleMgrActor_->bundles_.end());
    }
}

// Bind failed caused by etcd err
TEST_F(BundleMgrTest, BindFailedCausedByEtcdErr)
{
    auto scheduleReq = CreateScheduleRequest();
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(schedule_decision::ScheduleResult{ "agent", 0, {} }));

    auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
    EXPECT_CALL(*primary_, GetResourceViewChanges()).WillRepeatedly(Return(changes));
    EXPECT_CALL(*virtual_, GetResourceViewChanges()).WillRepeatedly(Return(changes));

    {
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Reserve,
                                     bundleMgrActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        auto result = future.Get();
        EXPECT_EQ(result.code(), 0);
    }

    auto putResponse = std::make_shared<PutResponse>();
    putResponse->status = Status(StatusCode::ERR_ETCD_OPERATION_ERROR, "Err in etcd");
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));
    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::Bind,
                                 bundleMgrActor_->GetAID(), scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get().code(), StatusCode::ERR_ETCD_OPERATION_ERROR);
    auto reserveRes = bundleMgrActor_->reserveResult_;
    EXPECT_TRUE(reserveRes.empty());
}

// metastore recover
TEST_F(BundleMgrTest, OnHealthyStatusTest)
{
    BundleManagerActorParam param = {
        .actorName = BUNDLE_MGR_ACTOR_NAME + "-OnHealthyStatusTest",
        .nodeId = "nodeA",
        .metaStoreClient = mockMetaStoreClient_
    };
    auto bundleMgrActor = std::make_shared<BundleMgrActor>(param);
    bundleMgrActor->BindInstanceCtrl(mockInstanceCtrl_);
    litebus::Spawn(bundleMgrActor);
    auto bundleMgr = std::make_shared<BundleMgr>(bundleMgrActor);
    bundleMgr->ToReady();

    auto getResponse = std::make_shared<GetResponse>();
    auto pair = NewBundlesJson(GetBundles());
    getResponse->kvs.emplace_back(pair.kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto delResponse = std::make_shared<DeleteResponse>();
    delResponse->status = Status(StatusCode::SUCCESS);
    EXPECT_CALL(*mockMetaStoreClient_, Delete).WillOnce(Return(delResponse));
    messages::BundleInfo bundle4;
    bundle4.set_bundleid("bundle4");
    bundleMgrActor->bundles_["bundle4"] = bundle4;

    bundleMgr->OnHealthyStatus(Status::OK());
    ASSERT_AWAIT_TRUE([&]() -> bool {
        auto future = litebus::Async(bundleMgrActor->GetAID(), &BundleMgrActor::GetBundles);
        return future.Get().find("bundle4") == future.Get().end();
    });
    litebus::Terminate(bundleMgrActor->GetAID());
    litebus::Await(bundleMgrActor->GetAID());
}

TEST_F(BundleMgrTest, RemoveBundleTest)
{
    std::string rGroupName1 = "rg1";
    std::string rGroupName2 = "rg2";
    std::string tenantId = "tenant1";
    messages::BundleInfo bundle1;
    bundle1.set_bundleid("bundle1");
    bundle1.set_parentrgroupname("primary");
    bundle1.set_rgroupname(rGroupName1);
    bundle1.set_tenantid("tenant1");
    messages::BundleInfo bundle2;
    bundle2.set_bundleid("bundle2");
    bundle2.set_parentid("bundle1");
    bundle2.set_parentrgroupname(rGroupName1);
    bundle2.set_rgroupname(rGroupName2);
    bundleMgrActor_->bundles_[bundle1.bundleid()] = bundle1;
    bundleMgrActor_->bundles_[bundle2.bundleid()] = bundle2;

    auto removeBundleReq = std::make_shared<messages::RemoveBundleRequest>();
    removeBundleReq->set_requestid("req1");
    removeBundleReq->set_rgroupname(rGroupName1);
    removeBundleReq->set_tenantid(tenantId);
    {
        resource_view::ResourceUnit unit2;
        resource_view::InstanceInfo ins21;
        ins21.set_instanceid("ins21");
        ins21.mutable_scheduleoption()->set_target(resources::CreateTarget::INSTANCE);
        (*unit2.mutable_instances())["ins21"] = ins21;
        unit2.set_id("bundle2");

        resource_view::ResourceUnit unit1;
        resource_view::InstanceInfo ins11;
        ins11.set_instanceid("bundle2");
        ins11.mutable_scheduleoption()->set_target(resources::CreateTarget::RESOURCE_GROUP);
        (*unit1.mutable_instances())["bundle2"] = ins11;
        unit1.set_id("bundle1");

        litebus::Option<resource_view::ResourceUnit> unitOpt1(unit1);
        litebus::Option<resource_view::ResourceUnit> unitOpt2(unit2);
        EXPECT_CALL(*virtual_, GetResourceUnit).WillOnce(Return(unitOpt2))
            .WillOnce(Return(unitOpt1));
        EXPECT_CALL(*virtual_, DeleteResourceUnit).Times(2);
        EXPECT_CALL(*virtual_, DeleteInstances).Times(1);
        EXPECT_CALL(*primary_, DeleteInstances).Times(1);
        auto delResponse = std::make_shared<DeleteResponse>();
        delResponse->status = Status(StatusCode::SUCCESS);
        EXPECT_CALL(*mockMetaStoreClient_, Delete).WillOnce(Return(delResponse));
        EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance).Times(1).WillRepeatedly(Return(Status::OK()));

        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderLayerStub::RemoveBundle,
                                     bundleMgrActor_->GetAID(), removeBundleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        auto result = future.Get();
        EXPECT_EQ(result.status().code(), 0);
        EXPECT_EQ(static_cast<int>(bundleMgrActor_->bundles_.size()), 0);
    }
}

TEST_F(BundleMgrTest, SyncBundlesTest)
{
    auto getResponse = std::make_shared<GetResponse>();
    auto pair = NewBundlesJson(GetBundles());
    getResponse->kvs.emplace_back(pair.kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));

    auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(bundleMgrActor_->bundles_.size(), static_cast<uint32_t>(3));
    EXPECT_NE(bundleMgrActor_->bundles_.find("bundle1"), bundleMgrActor_->bundles_.end());

    EXPECT_EQ(bundleMgrActor_->agentBundles_.size(), static_cast<uint32_t>(2));
    EXPECT_NE(bundleMgrActor_->agentBundles_.find("agent1"), bundleMgrActor_->agentBundles_.end());

    EXPECT_CALL(*primary_, AddInstances).WillOnce(Return(Status::OK())).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*virtual_, AddResourceUnit).WillOnce(Return(Status::OK())).WillOnce(Return(Status::OK()));
    auto status = bundleMgr_->SyncBundles("agent1");
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsOk());

    EXPECT_CALL(*primary_, AddInstances).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*virtual_, AddResourceUnit).WillOnce(Return(Status(StatusCode::FAILED)));
    status = bundleMgr_->SyncBundles("agent2");
    ASSERT_AWAIT_TRUE([&]() { return status.IsError(); });
}

TEST_F(BundleMgrTest, SyncFailedBundlesTest)
{
    auto mockResourceGroupActor = std::make_shared<MockResourceGroupActor>();
    litebus::Spawn(mockResourceGroupActor);

    auto getResponse = std::make_shared<GetResponse>();
    auto pair = NewBundlesJson(GetBundles());
    getResponse->kvs.emplace_back(pair.kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(bundleMgrActor_->bundles_.size(), static_cast<uint32_t>(3));

    explorer::LeaderInfo leaderInfo{
        .address = mockResourceGroupActor->GetAID().UnfixUrl(),
    };
    bundleMgrActor_->UpdateMasterInfo(leaderInfo);

    // sync agent 1 failed
    std::unordered_map<std::string, messages::FuncAgentRegisInfo> agentMap;
    agentMap["agent2"] = {};

    litebus::Future<messages::ReportAgentAbnormalRequest> req;
    messages::ReportAgentAbnormalResponse resp;
    resp.set_code(0);
    EXPECT_CALL(*mockResourceGroupActor, MockForwardReportAgentAbnormal)
        .WillOnce(testing::DoAll(FutureArg<0>(&req), Return(resp)));

    EXPECT_CALL(*primary_, DeleteInstances).WillOnce(Return(Status::OK())).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*virtual_, DeleteResourceUnit).WillOnce(Return(Status::OK())).WillOnce(Return(Status::OK()));
    auto putResponse = std::make_shared<PutResponse>();
    putResponse->status = Status(StatusCode::SUCCESS);
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));

    auto status = bundleMgr_->SyncFailedBundles(agentMap);
    ASSERT_AWAIT_READY(req);
    EXPECT_EQ(req.Get().bundleids_size(), 2);
    EXPECT_EQ(req.Get().bundleids().at(0), "bundle1");
    EXPECT_EQ(req.Get().bundleids().at(1), "bundle2");
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsOk());
    EXPECT_EQ(bundleMgrActor_->bundles_.size(), static_cast<uint32_t>(1));

    // sync agent 2 failed
    agentMap.clear();
    req = {};
    resp.set_code(-1);
    resp.set_message("failed to put");
    EXPECT_CALL(*mockResourceGroupActor, MockForwardReportAgentAbnormal)
        .WillOnce(testing::DoAll(FutureArg<0>(&req), Return(resp)));

    status = bundleMgr_->SyncFailedBundles(agentMap);
    ASSERT_AWAIT_READY(req);
    EXPECT_EQ(req.Get().bundleids_size(), 1);
    EXPECT_EQ(req.Get().bundleids().at(0), "bundle3");
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsError());
    EXPECT_EQ(bundleMgrActor_->bundles_.size(), static_cast<uint32_t>(1));

    litebus::Terminate(mockResourceGroupActor->GetAID());
    litebus::Await(mockResourceGroupActor->GetAID());
}

TEST_F(BundleMgrTest, NotifyFailedAgentTest)
{
    auto getResponse = std::make_shared<GetResponse>();
    auto pair = NewBundlesJson(GetBundles());
    getResponse->kvs.emplace_back(pair.kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(bundleMgrActor_->bundles_.size(), static_cast<uint32_t>(3));

    auto mockResourceGroupActor = std::make_shared<MockResourceGroupActor>();
    litebus::Spawn(mockResourceGroupActor);

    explorer::LeaderInfo leaderInfo{
        .address = mockResourceGroupActor->GetAID().UnfixUrl(),
    };
    bundleMgrActor_->UpdateMasterInfo(leaderInfo);

    litebus::Future<messages::ReportAgentAbnormalRequest> req;
    messages::ReportAgentAbnormalResponse resp;
    resp.set_code(0);
    EXPECT_CALL(*mockResourceGroupActor, MockForwardReportAgentAbnormal)
        .WillOnce(testing::DoAll(FutureArg<0>(&req), Return(resp)));

    EXPECT_CALL(*primary_, DeleteInstances).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*virtual_, DeleteResourceUnit).WillOnce(Return(Status::OK()));
    auto putResponse = std::make_shared<PutResponse>();
    putResponse->status = Status(StatusCode::SUCCESS);
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));

    auto status = bundleMgr_->NotifyFailedAgent("agent2");
    ASSERT_AWAIT_READY(req);
    EXPECT_EQ(req.Get().bundleids_size(), 1);
    EXPECT_EQ(req.Get().bundleids().at(0), "bundle3");
    ASSERT_AWAIT_READY(status);
    EXPECT_TRUE(status.Get().IsOk());
    EXPECT_EQ(bundleMgrActor_->bundles_.size(), static_cast<uint32_t>(2));

    litebus::Terminate(mockResourceGroupActor->GetAID());
    litebus::Await(mockResourceGroupActor->GetAID());
}

TEST_F(BundleMgrTest, UpdateBundleStatusTest)
{
    auto getResponse = std::make_shared<GetResponse>();
    auto pair = NewBundlesJson(GetBundles());
    getResponse->kvs.emplace_back(pair.kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
    auto future = bundleMgr_->Sync().Then([=](const Status &) { return bundleMgr_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(bundleMgrActor_->bundles_.size(), static_cast<uint32_t>(3));

    EXPECT_CALL(*virtual_, UpdateUnitStatus("bundle1", UnitStatus::NORMAL)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*virtual_, UpdateUnitStatus("bundle2", UnitStatus::NORMAL)).WillOnce(Return(Status::OK()));
    bundleMgr_->UpdateBundlesStatus("agent1", UnitStatus::NORMAL);
}

TEST_F(BundleMgrTest, GenBundle)
{
    auto req = CreateScheduleRequest();
    schedule_decision::ScheduleResult scheduleResult;
    scheduleResult.id = "agent";
    scheduleResult.code = 0,
    scheduleResult.realIDs = {5};
    scheduleResult.unitID = "rgroup0-0-xxx";

    // invalid bundleId
    req->mutable_instance()->set_instanceid("rgroup1_0_tenant1");
    auto bundle = bundleMgrActor_->GenBundle(req, scheduleResult);
    EXPECT_EQ(bundle.rgroupname(), "");
    auto resourceView = bundleMgrActor_->GetResourceView(bundle.rgroupname());
    EXPECT_TRUE(resourceView == nullptr);

    // invalid rgNameLen
    req->mutable_instance()->set_instanceid("100_rgroup1_0_tenant1");
    bundle = bundleMgrActor_->GenBundle(req, scheduleResult);
    EXPECT_EQ(bundle.rgroupname(), "");
    resourceView = bundleMgrActor_->GetResourceView(bundle.rgroupname());
    EXPECT_TRUE(resourceView == nullptr);

    // valid rgNameLen
    req->mutable_instance()->set_instanceid("8_rgroup01_0_tenant1");
    bundle = bundleMgrActor_->GenBundle(req, scheduleResult);
    EXPECT_EQ(bundle.rgroupname(), "rgroup01");
    resourceView = bundleMgrActor_->GetResourceView(bundle.rgroupname());
    EXPECT_TRUE(resourceView != nullptr);
}
}  // namespace functionsystem::test