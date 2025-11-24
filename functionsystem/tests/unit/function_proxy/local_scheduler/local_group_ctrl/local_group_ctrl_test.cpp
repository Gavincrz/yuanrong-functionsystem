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

#include "function_proxy/local_scheduler/local_group_ctrl/local_group_ctrl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/constants/actor_name.h"
#include "common/resource_view/view_utils.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_local_sched_srv.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_resource_view.h"
#include "mocks/mock_scheduler.h"
#include "mocks/mock_shared_client.h"
#include "mocks/mock_shared_client_manager_proxy.h"
#include "utils/future_test_helper.h"

#include "function_proxy/local_scheduler/local_group_ctrl/local_group_ctrl_actor.h"

namespace functionsystem::test {
using namespace testing;
class DomainUnderlayerStub : public litebus::ActorBase {
public:
    DomainUnderlayerStub() : litebus::ActorBase("DomainUnderlayerStub")
    {
    }
    ~DomainUnderlayerStub() = default;

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

    litebus::Future<messages::KillGroupResponse> ClearGroup(const litebus::AID &dst,
                                                            const std::shared_ptr<messages::KillGroup> &req)
    {
        Send(dst, "ClearGroup", req->SerializeAsString());
        killGroupPromises_[req->groupid()] = std::make_shared<litebus::Promise<messages::KillGroupResponse>>();
        return killGroupPromises_[req->groupid()]->GetFuture();
    }

    void OnClearGroup(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::KillGroupResponse resp;
        resp.ParseFromString(msg);
        if (killGroupPromises_.find(resp.groupid()) != killGroupPromises_.end()) {
            (void)killGroupPromises_[resp.groupid()]->SetValue(resp);
            (void)killGroupPromises_.erase(resp.groupid());
        }
    }

    void Init() override
    {
        Receive("OnReserve", &DomainUnderlayerStub::OnReserve);
        Receive("OnBind", &DomainUnderlayerStub::OnBind);
        Receive("OnUnReserve", &DomainUnderlayerStub::OnUnReserve);
        Receive("OnUnBind", &DomainUnderlayerStub::OnUnBind);
        Receive("OnClearGroup", &DomainUnderlayerStub::OnClearGroup);
    }

private:
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::ScheduleResponse>>> reservePromises_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::GroupResponse>>> unReservePromises_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::GroupResponse>>> bindPromises_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::GroupResponse>>> unBindPromises_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<messages::KillGroupResponse>>> killGroupPromises_;
};

class LocalGroupCtrlTest : public ::testing::Test {
public:
    void SetUp() override
    {
        clientManager_ = std::make_shared<MockSharedClientManagerProxy>();
        auto resourceViewMgr = std::make_shared<resource_view::ResourceViewMgr>();
        primary_ = MockResourceView::CreateMockResourceView();
        virtual_ = MockResourceView::CreateMockResourceView();
        resourceViewMgr->primary_ = primary_;
        resourceViewMgr->virtual_ = virtual_;
        mockScheduler_ = std::make_shared<MockScheduler>();
        mockLocalSchedSrv_ = std::make_shared<MockLocalSchedSrv>();
        mockInstanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        EXPECT_CALL(*mockInstanceCtrl_, RegisterClearGroupInstanceCallBack).WillRepeatedly(Return());
        EXPECT_CALL(*mockInstanceCtrl_, IsInstanceRunning).WillRepeatedly(Return(false));
        mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>("");
        localGroupCtrlActor_ =
            std::make_shared<LocalGroupCtrlActor>(LOCAL_GROUP_CTRL_ACTOR_NAME, "nodeA", mockMetaStoreClient_);
        localGroupCtrlActor_->BindScheduler(mockScheduler_);
        localGroupCtrlActor_->BindControlInterfaceClientManager(clientManager_);
        localGroupCtrlActor_->BindResourceView(resourceViewMgr);
        localGroupCtrlActor_->BindInstanceCtrl(mockInstanceCtrl_);
        localGroupCtrlActor_->BindLocalSchedSrv(mockLocalSchedSrv_);
        const auto &aid = litebus::Spawn(localGroupCtrlActor_);

        localGroupCtrl_ = std::make_shared<LocalGroupCtrl>(localGroupCtrlActor_);
        localGroupCtrl_->ToReady();
        // Ensure that Actor is ready before performing Send action.
        EXPECT_AWAIT_READY(litebus::Async(aid, &LocalGroupCtrlActor::IsReady));

        underlayerSrv_ = std::make_shared<DomainUnderlayerStub>();
        litebus::Spawn(underlayerSrv_);
    }

    void TearDown() override
    {
        litebus::Terminate(localGroupCtrlActor_->GetAID());
        litebus::Terminate(underlayerSrv_->GetAID());
        litebus::Await(localGroupCtrlActor_);
        litebus::Await(underlayerSrv_);
        clientManager_ = nullptr;
        localGroupCtrl_ = nullptr;
        mockScheduler_ = nullptr;
        mockLocalSchedSrv_ = nullptr;
        mockInstanceCtrl_ = nullptr;
        mockMetaStoreClient_ = nullptr;
        localGroupCtrlActor_ = nullptr;
        underlayerSrv_ = nullptr;
        primary_ = nullptr;
        virtual_ = nullptr;
    }

    void Start()
    {
        auto getResponse = std::make_shared<GetResponse>();
        EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(AsyncReturn(getResponse)));
        auto future = localGroupCtrl_->Sync().Then([=](const Status &) { return localGroupCtrl_->Recover(); });
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }

protected:
    std::shared_ptr<LocalGroupCtrl> localGroupCtrl_;
    std::shared_ptr<LocalGroupCtrlActor> localGroupCtrlActor_;
    std::shared_ptr<MockSharedClientManagerProxy> clientManager_;
    std::shared_ptr<MockSharedClient> sharedClientMgr_;
    std::shared_ptr<MockScheduler> mockScheduler_;
    std::shared_ptr<MockInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockLocalSchedSrv> mockLocalSchedSrv_;
    std::shared_ptr<MockResourceView> primary_;
    std::shared_ptr<MockResourceView> virtual_;
    std::shared_ptr<MockMetaStoreClient> mockMetaStoreClient_;
    std::shared_ptr<DomainUnderlayerStub> underlayerSrv_;
};

struct GroupInfoPair {
    KeyValue kv;
    std::shared_ptr<messages::GroupInfo> info;
};

GroupInfoPair NewGroupInfoJson(std::string groupID, std::string ownerProxy, GroupState state, int numInstance)
{
    auto info = std::make_shared<messages::GroupInfo>();
    info->set_groupid(groupID);
    info->set_ownerproxy(ownerProxy);
    info->set_status(static_cast<int32_t>(state));
    info->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    for (int i = 0; i < numInstance; ++i) {
        auto request = info->add_requests();
        request->mutable_instance()->set_instanceid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request->set_requestid(info->requestid() + "-" + std::to_string(i));
        request->mutable_instance()->set_requestid(request->requestid());
    }
    KeyValue kv;
    kv.set_key(GROUP_PATH_PREFIX + "/" + info->requestid() + "/" + info->groupid());
    std::string jsonStr;
    (void)google::protobuf::util::MessageToJsonString(*info, &jsonStr);
    kv.set_value(jsonStr);
    return { kv, info };
}

// group schedule not started
TEST_F(LocalGroupCtrlTest, LocalGroupCtrlNotStarted)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_INNER_COMMUNICATION);
}

// start local group Ctrl with empty
TEST_F(LocalGroupCtrlTest, LocalGroupCtrlStartedWithEmpty)
{
    auto getResponse = std::make_shared<GetResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(AsyncReturn(getResponse)));
    auto future = localGroupCtrl_->Sync().Then([=](const Status &) { return localGroupCtrl_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
}

// start local group Ctrl with failure get groupInfo
TEST_F(LocalGroupCtrlTest, LocalGroupCtrlStartedWithFailureGroupInfo)
{
    auto getResponse = std::make_shared<GetResponse>();
    getResponse->status = Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(AsyncReturn(getResponse)));
    auto future = localGroupCtrl_->Sync().Then([=](const Status &) { return localGroupCtrl_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
}

// start local group Ctrl with invalid value
TEST_F(LocalGroupCtrlTest, LocalGroupCtrlStartedWithInvalidGroupInfo)
{
    auto getResponse = std::make_shared<GetResponse>();
    KeyValue kv;
    kv.set_key("/yr/group/requestID/groupID");
    kv.set_value("xxxxxxx");
    getResponse->kvs.emplace_back(kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(AsyncReturn(getResponse)));
    auto future = localGroupCtrl_->Sync().Then([=](const Status &) { return localGroupCtrl_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
}

// start local group Ctrl with differ groupInfo
TEST_F(LocalGroupCtrlTest, LocalGroupCtrlStartedWithDifferGroupInfo)
{
    auto getResponse = std::make_shared<GetResponse>();
    auto kv1 = NewGroupInfoJson("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), "nodeA",
                                    GroupState::SCHEDULING, 3);
    auto kv2 = NewGroupInfoJson("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), "nodeB",
                                    GroupState::RUNNING, 3);
    auto kv3 = NewGroupInfoJson("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), "nodeA",
                                    GroupState::FAILED, 3);
    getResponse->kvs.emplace_back(kv1.kv);
    getResponse->kvs.emplace_back(kv2.kv);
    getResponse->kvs.emplace_back(kv3.kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(AsyncReturn(getResponse)));
    // for SCHEDULING
    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback).WillRepeatedly(Return());
    messages::GroupResponse resp;
    resp.set_code(SUCCESS);
    resp.set_message("SUCCESS");
    EXPECT_CALL(*mockLocalSchedSrv_, ForwardGroupSchedule).WillOnce(Return(AsyncReturn(resp)));
    // for FAILED
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient).WillOnce(Return(nullptr));
    auto future = localGroupCtrl_->Sync().Then([=](const Status &) { return localGroupCtrl_->Recover(); });
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    auto ctx = localGroupCtrlActor_->GetGroupCtx(kv1.info->requestid());
    auto peFuture = ctx->persistingPromise.GetFuture();
    ASSERT_AWAIT_READY(peFuture);
    EXPECT_EQ(peFuture.Get()->code(), int32_t(SUCCESS));
}

// group schedule invalid designated instanceID
TEST_F(LocalGroupCtrlTest, GroupScheduleWithDesignatedInstanceID)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    auto createRequest = createRequests->add_requests();
    createRequest->set_designatedinstanceid("designatedInstanceID");
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
}

// group schedule invalid detached lifecycle opt
TEST_F(LocalGroupCtrlTest, GroupScheduleWithDetachedInstanceOpt)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    auto createRequest = createRequests->add_requests();
    (*createRequest->mutable_createoptions())["lifecycle"] = "detached";
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
}

// group schedule invalid detached lifecycle opt
TEST_F(LocalGroupCtrlTest, GroupScheduleWithInvalidAffinity)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    auto createRequest = createRequests->add_requests();
    *createRequest->mutable_schedulingops()->mutable_scheduleaffinity()->mutable_instance()
        ->mutable_requiredantiaffinity() = Selector(false, { { In("key", { "value" }) } });
    createRequest = createRequests->add_requests();
    *createRequest->mutable_schedulingops()->mutable_scheduleaffinity()->mutable_instance()
        ->mutable_requiredaffinity() = Selector(false, { { In("key1", { "value" }) } });
    createRequests->mutable_groupopt()->set_grouppolicy(common::GroupPolicy::StrictPack);
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
}


// group schedule invalid instance num
TEST_F(LocalGroupCtrlTest, GroupScheduleWithInvalidInstanceNum)
{
    Start();
    {
        auto createRequests = std::make_shared<CreateRequests>();
        createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        createRequests->set_traceid("group-traceID");
        auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
    }
    {
        auto createRequests = std::make_shared<CreateRequests>();
        createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        createRequests->set_traceid("group-traceID");
        for (auto i = 0; i < 257; ++i) {
            createRequests->add_requests();
        }
        auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
    }
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithInvalidRangeInstanceScheduleParam)
{
	Start();
	{
		auto createRequests = std::make_shared<CreateRequests>();
		createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
		createRequests->set_traceid("group-traceID");
		auto request = createRequests->add_requests();
		request->mutable_schedulingops()->mutable_range()->set_max(1);
		request->mutable_schedulingops()->mutable_range()->set_min(2);
		auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
		ASSERT_AWAIT_READY(future);
		EXPECT_EQ(future.IsOK(), true);
		EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
        EXPECT_EQ(future.Get()->message(), "[invalid range param max(1), should bigger than min(2)]");
	}
    {
        auto createRequests = std::make_shared<CreateRequests>();
        createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        createRequests->set_traceid("group-traceID");
        auto request = createRequests->add_requests();
        request->mutable_schedulingops()->mutable_range()->set_min(-2);
        auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
        EXPECT_EQ(future.Get()->message(), "[invalid range param min(-2), should bigger than 0]");
    }
	{
		auto createRequests = std::make_shared<CreateRequests>();
		createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
		createRequests->set_traceid("group-traceID");
		auto request = createRequests->add_requests();
		request->mutable_schedulingops()->mutable_range()->set_max(-2);
		auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
		ASSERT_AWAIT_READY(future);
		EXPECT_EQ(future.IsOK(), true);
		EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
        EXPECT_EQ(future.Get()->message(), "[invalid range param max(-2), should bigger than 0]");
	}
    {
        auto createRequests = std::make_shared<CreateRequests>();
        createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        createRequests->set_traceid("group-traceID");
        auto request = createRequests->add_requests();
        request->mutable_schedulingops()->mutable_range()->set_step(-2);
        auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
        EXPECT_EQ(future.Get()->message(), "[invalid range param step(-2), should bigger than 0]");
    }
	{
	    auto createRequests = std::make_shared<CreateRequests>();
	    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
	    createRequests->set_traceid("group-traceID");
	    auto request = createRequests->add_requests();
	    request->mutable_schedulingops()->mutable_range()->set_max(257);
	    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
	    ASSERT_AWAIT_READY(future);
	    EXPECT_EQ(future.IsOK(), true);
	    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
        EXPECT_EQ(future.Get()->message(), "[invalid range param max(257), should be range (0, 256]]");
	}
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithInvalidRangeInstanceScheduleRequestNum)
{
	Start();
	auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
	createRequests->set_traceid("group-traceID");
    for (auto i = 0; i < 2; ++i) {
		auto request = createRequests->add_requests();
		request->mutable_schedulingops()->mutable_range()->set_max(1);
	}
	auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithRangeInstanceScheduleUseDefaultParamLocalSuccessful)
{
	Start();
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
	createRequests->set_traceid("group-traceID");
	createRequests->clear_requests();
	auto request = createRequests->add_requests();
    request->mutable_schedulingops()->mutable_range();
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(256).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put)
        .WillOnce(Return(AsyncReturn(putResponse)))
        .WillOnce(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < 256; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
        .WillRepeatedly(DoAll(
            Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                      InstanceReadyCallBack callback) { callback(Status::OK()); })));
    EXPECT_CALL(*mockInstanceCtrl_, ToCreating).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));
    auto future = localGroupCtrl_->GroupSchedule("srcInstnceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithRangeInstanceScheduleUseMaxParamLocalSuccessful)
{
    Start();
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    createRequests->clear_requests();
    auto request = createRequests->add_requests();
    request->mutable_schedulingops()->mutable_range();
    request->mutable_schedulingops()->mutable_range()->set_max(256);
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(256).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put)
        .WillOnce(Return(AsyncReturn(putResponse)))
        .WillOnce(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < 256; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
        .WillRepeatedly(DoAll(
            Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                      InstanceReadyCallBack callback) { callback(Status::OK()); })));
    EXPECT_CALL(*mockInstanceCtrl_, ToCreating).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));
    auto future = localGroupCtrl_->GroupSchedule("srcInstnceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithRangeInstanceScheduleSuccessful)
{
	Start();
	auto createRequests = std::make_shared<CreateRequests>();
	createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
	createRequests->set_traceid("group-traceID");
	createRequests->clear_requests();
	auto request = createRequests->add_requests();
	request->mutable_schedulingops()->mutable_range()->set_max(3);
	request->mutable_schedulingops()->mutable_range()->set_min(1);
    request->mutable_schedulingops()->mutable_range()->set_step(1);
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put)
        .WillOnce(Return(AsyncReturn(putResponse)))
        .WillOnce(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < 3; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
        .WillRepeatedly(DoAll(
            Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                      InstanceReadyCallBack callback) { callback(Status::OK()); })));
    EXPECT_CALL(*mockInstanceCtrl_, ToCreating).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));
    auto future = localGroupCtrl_->GroupSchedule("srcInstnceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);
    EXPECT_EQ(localGroupCtrlActor_->groupCtxs_.size(), (size_t)1);
    // clear group info
    std::string requestID;
    std::string groupID;
    for(auto &ctx : localGroupCtrlActor_->groupCtxs_) {
        requestID = ctx.first;
        groupID = ctx.second->groupInfo->groupid();
        break;
    }
    auto clearGroupReq = std::make_shared<messages::KillGroup>();
    clearGroupReq->set_grouprequestid(requestID);
    clearGroupReq->set_groupid(groupID);
    EXPECT_CALL(*mockInstanceCtrl_, DeleteSchedulingInstance)
        .Times(3)
        .WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto clearFuture = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::ClearGroup,
                                      localGroupCtrlActor_->GetAID(), clearGroupReq);
    ASSERT_AWAIT_READY(clearFuture);
    EXPECT_EQ(clearFuture.IsOK(), true);
    EXPECT_EQ(localGroupCtrlActor_->groupCtxs_.size(), 0);
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithRangeInstanceAndNormalRequestSuccessful)
{
	Start();
	auto createRequests = std::make_shared<CreateRequests>();
	createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
	createRequests->set_traceid("group-traceID");
	createRequests->clear_requests();
	auto request = createRequests->add_requests();
	request->mutable_schedulingops()->mutable_range()->set_max(5);
	request->mutable_schedulingops()->mutable_range()->set_min(1);
	request->mutable_schedulingops()->mutable_range()->set_step(1);
	(void)createRequests->add_requests();
	auto future = localGroupCtrl_->GroupSchedule("srcInstnceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_PARAM_INVALID);
}

// group schedule success
TEST_F(LocalGroupCtrlTest, GroupScheduleLocalSuccessful)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    createRequests->clear_requests();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    const int num = 3;
    for (int i = 0; i < num; ++i) {
        (void)createRequests->add_requests();
    }

    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put)
        .WillOnce(Return(AsyncReturn(putResponse)))
        .WillOnce(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    for (int i = 0; i < num; ++i) {
        (void)result.results.emplace_back(schedule_decision::ScheduleResult{ "agent", 0, "" });
    }
    schedule_decision::GroupScheduleResult allocatedFailedResult;
    allocatedFailedResult.code = 0;
    for (int i = 0; i < num; ++i) {
        auto allocatedPromise = std::make_shared<litebus::Promise<Status>>();
        allocatedPromise->SetValue(Status(StatusCode::FAILED));
        (void)allocatedFailedResult.results.emplace_back(
            schedule_decision::ScheduleResult{ "agent", 0, "", {}, "", {}, {}, allocatedPromise });
    }
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_))
        .WillOnce(Return(AsyncReturn(allocatedFailedResult)))
        .WillOnce(Return(AsyncReturn(result)));
    EXPECT_CALL(*primary_, DeleteInstances).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
        .WillRepeatedly(DoAll(
            Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                      InstanceReadyCallBack callback) { callback(Status::OK()); })));
    EXPECT_CALL(*mockInstanceCtrl_, ToCreating).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    auto future1 = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
    ASSERT_AWAIT_READY(future1);
    EXPECT_EQ(future1.IsOK(), true);
    EXPECT_EQ(future1.Get()->code(), common::ErrorCode::ERR_NONE);
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);
}

// group schedule failed by etcd put failed
TEST_F(LocalGroupCtrlTest, GroupScheduleFailedByETCDFailed)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    createRequests->clear_requests();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    const int num = 3;
    for (int i = 0; i < num; ++i) {
        (void)createRequests->add_requests();
    }
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    putResponse->status = Status(StatusCode::FAILED);
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(AsyncReturn(putResponse)));
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_ETCD_OPERATION_ERROR);
}

// group schedule to scheduling failed
TEST_F(LocalGroupCtrlTest, GroupScheduleFailedByToSchedulingFailed)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    createRequests->clear_requests();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    const int num = 3;
    for (int i = 0; i < num; ++i) {
        (void)createRequests->add_requests();
    }
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling)
        .Times(3)
        .WillRepeatedly(Return(AsyncReturn(Status(StatusCode::FAILED))));
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_INNER_SYSTEM_ERROR);
}

// group schedule local decision failed & forward schedule successful
TEST_F(LocalGroupCtrlTest, GroupScheduleForwardSuccessful)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    createRequests->clear_requests();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    const int num = 3;
    for (int i = 0; i < num; ++i) {
        (void)createRequests->add_requests();
    }
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put)
        .WillOnce(Return(AsyncReturn(putResponse)))
        .WillOnce(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH);
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
    messages::GroupResponse resp;
    resp.set_requestid(createRequests->requestid());
    resp.set_code(SUCCESS);
    resp.set_message("SUCCESS");
    EXPECT_CALL(*mockLocalSchedSrv_, ForwardGroupSchedule).WillOnce(Return(AsyncReturn(resp)));
    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
        .WillRepeatedly(DoAll(
            Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                      InstanceReadyCallBack callback) { callback(Status::OK()); })));
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithRangeInstanceScheduleForwardSuccessful)
{
    Start();
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    createRequests->clear_requests();
    auto request = createRequests->add_requests();
    request->mutable_schedulingops()->mutable_range()->set_max(3);
    request->mutable_schedulingops()->mutable_range()->set_min(1);
    request->mutable_schedulingops()->mutable_range()->set_step(1);
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put)
        .WillOnce(Return(AsyncReturn(putResponse)))
        .WillOnce(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH);
	EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
	messages::GroupResponse resp;
	resp.set_requestid(createRequests->requestid());
	resp.set_code(SUCCESS);
    resp.set_rangesuccessnum(3);
	resp.set_message("SUCCESS");
	EXPECT_CALL(*mockLocalSchedSrv_, ForwardGroupSchedule).WillOnce(Return(AsyncReturn(resp)));
	EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
		.WillRepeatedly(DoAll(
			Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
					  InstanceReadyCallBack callback) { callback(Status::OK()); })));
	auto mockSharedClient = std::make_shared<MockSharedClient>();
	EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
	litebus::Promise<runtime::NotifyRequest> notifyCalled;
	EXPECT_CALL(*mockSharedClient, NotifyResult(_))
		.WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
			notifyCalled.SetValue(request);
			return runtime::NotifyResponse();
		}));
	auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
	ASSERT_AWAIT_READY(future);
	EXPECT_EQ(future.IsOK(), true);
	EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
	ASSERT_AWAIT_READY(notifyCalled.GetFuture());
	EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithRangeInstanceScheduleForwardGetLessInstanceSuccessful)
{
    Start();
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    createRequests->clear_requests();
    auto request = createRequests->add_requests();
    request->mutable_schedulingops()->mutable_range()->set_max(3);
    request->mutable_schedulingops()->mutable_range()->set_min(1);
    request->mutable_schedulingops()->mutable_range()->set_step(1);
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put)
        .WillOnce(Return(AsyncReturn(putResponse)))
        .WillOnce(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH);
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
    messages::GroupResponse resp;
    resp.set_requestid(createRequests->requestid());
    resp.set_code(SUCCESS);
    resp.set_rangesuccessnum(2);
    resp.set_message("SUCCESS");
    EXPECT_CALL(*mockLocalSchedSrv_, ForwardGroupSchedule).WillOnce(Return(AsyncReturn(resp)));
    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
    .WillRepeatedly(DoAll(
    	Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    	          InstanceReadyCallBack callback) { callback(Status::OK()); })));
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);
}

// group schedule local decision failed & forward schedule failed
TEST_F(LocalGroupCtrlTest, GroupScheduleForwardFailed)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    createRequests->clear_requests();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    const int num = 3;
    for (int i = 0; i < num; ++i) {
        (void)createRequests->add_requests();
    }
    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).Times(2).WillRepeatedly(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH);
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
    messages::GroupResponse resp;
    resp.set_code(static_cast<int32_t>(StatusCode::ERR_GROUP_SCHEDULE_FAILED));
    EXPECT_CALL(*mockLocalSchedSrv_, ForwardGroupSchedule).WillOnce(Return(AsyncReturn(resp)));
    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
        .WillRepeatedly(DoAll(
            Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                      InstanceReadyCallBack callback) {})));
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    // for mock, rsp maybe modified by forward response
    EXPECT_TRUE(future.Get()->code() == common::ErrorCode::ERR_NONE || future.Get()->code() == common::ErrorCode::ERR_GROUP_SCHEDULE_FAILED);
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_GROUP_SCHEDULE_FAILED);
}

TEST_F(LocalGroupCtrlTest, GroupScheduleWithRangeInstanceScheduleForwardFailed)
{
	Start();
	auto createRequests = std::make_shared<CreateRequests>();
	createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
	createRequests->set_traceid("group-traceID");
	createRequests->clear_requests();
	auto request = createRequests->add_requests();
	request->mutable_schedulingops()->mutable_range()->set_max(3);
	request->mutable_schedulingops()->mutable_range()->set_min(1);
	request->mutable_schedulingops()->mutable_range()->set_step(1);
	EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put)
        .WillOnce(Return(AsyncReturn(putResponse)))
        .WillOnce(Return(AsyncReturn(putResponse)));
    schedule_decision::GroupScheduleResult result;
    result.code = static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH);
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
    messages::GroupResponse resp;
    resp.set_code(static_cast<int32_t>(StatusCode::ERR_GROUP_SCHEDULE_FAILED));
	EXPECT_CALL(*mockLocalSchedSrv_, ForwardGroupSchedule)
		.WillOnce(Return(AsyncReturn(resp)));
	EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
		.WillRepeatedly(DoAll(
			Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
	                  InstanceReadyCallBack callback) {})));
	EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
	auto mockSharedClient = std::make_shared<MockSharedClient>();
	EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_)).WillOnce(Return(AsyncReturn(
        (std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient)))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
	EXPECT_CALL(*mockSharedClient, NotifyResult(_))
		.WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
			notifyCalled.SetValue(request);
			return runtime::NotifyResponse();
	}));
	auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
	ASSERT_AWAIT_READY(future);
	EXPECT_EQ(future.IsOK(), true);
	EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_GROUP_SCHEDULE_FAILED);
	ASSERT_AWAIT_READY(notifyCalled.GetFuture());
	EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_GROUP_SCHEDULE_FAILED);
}

// group schedule local decision failed & forward schedule successful & to running failed
TEST_F(LocalGroupCtrlTest, GroupScheduleRuningFailed)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    // group schedule local decision failed & forward schedule successful & to running failed
    {
        createRequests->clear_requests();
        createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        const int num = 3;
        for (int i = 0; i < num; ++i) {
            (void)createRequests->add_requests();
        }

        EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
        auto putResponse = std::make_shared<PutResponse>();
        EXPECT_CALL(*mockMetaStoreClient_, Put)
            .WillOnce(Return(AsyncReturn(putResponse)))
            .WillOnce(Return(AsyncReturn(putResponse)));
        schedule_decision::GroupScheduleResult result;
        result.code = static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH);
        EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(AsyncReturn(result)));
        messages::GroupResponse resp;
        resp.set_code(SUCCESS);
        resp.set_message("SUCCESS");
        EXPECT_CALL(*mockLocalSchedSrv_, ForwardGroupSchedule).WillOnce(Return(AsyncReturn(resp)));
        EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
            .WillRepeatedly(DoAll(
                Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                          InstanceReadyCallBack callback) { callback(Status(StatusCode::ERR_USER_CODE_LOAD)); })));
        auto mockSharedClient = std::make_shared<MockSharedClient>();
        EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance).Times(3).WillRepeatedly(Return(AsyncReturn(Status::OK())));
        EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
            .WillOnce(
                Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
        litebus::Promise<runtime::NotifyRequest> notifyCalled;
        EXPECT_CALL(*mockSharedClient, NotifyResult(_))
            .WillOnce(
                Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
                    notifyCalled.SetValue(request);
                    return runtime::NotifyResponse();
                }));
        auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
        ASSERT_AWAIT_READY(notifyCalled.GetFuture());
        EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_GROUP_SCHEDULE_FAILED);
    }
}

// invalid msg
TEST_F(LocalGroupCtrlTest, InvalidReserveAndBind)
{
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_)).Times(0);
    EXPECT_CALL(*primary_, DeleteInstances).Times(0);
    EXPECT_CALL(*mockInstanceCtrl_, ToCreating).Times(0);
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance).Times(0);
    localGroupCtrlActor_->Reserve(litebus::AID(), "Reserve", "xxx");
    localGroupCtrlActor_->Bind(litebus::AID(), "Bind", "xxx");
    localGroupCtrlActor_->UnReserve(litebus::AID(), "UnReserve", "xxx");
    localGroupCtrlActor_->UnBind(litebus::AID(), "UnBind", "xxx");
}

std::shared_ptr<messages::ScheduleRequest> NewScheduleRequest()
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->set_traceid("traceID");
    scheduleReq->set_requestid("request-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    scheduleReq->mutable_instance()->set_instanceid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    scheduleReq->mutable_instance()->set_groupid("groupID-123456");
    return scheduleReq;
}

// Reserve successful & UnReserve successful
TEST_F(LocalGroupCtrlTest, ReserveAndUnReserveSuccessful)
{
    auto scheduleReq = NewScheduleRequest();
    auto allocatedPromise = std::make_shared<litebus::Promise<Status>>();
    allocatedPromise->SetValue(Status(StatusCode::FAILED));
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(
            Return(AsyncReturn(schedule_decision::ScheduleResult{ "agent", 0, {}, {}, "", {}, {}, allocatedPromise })))
        .WillOnce(Return(AsyncReturn(schedule_decision::ScheduleResult{ "agent", 0, {} })));

    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    {
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Reserve,
                                     localGroupCtrlActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        auto result = future.Get();
        EXPECT_EQ(result.code(), 0);

        // duplicate request
        future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Reserve,
                                localGroupCtrlActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().code(), 0);
    }

    {
        EXPECT_CALL(*primary_, DeleteInstances).Times(1);
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::UnReserve,
                                     localGroupCtrlActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().code(), 0);
    }
}

// reserve failed
TEST_F(LocalGroupCtrlTest, ReserveFailed)
{
    auto scheduleReq = NewScheduleRequest();
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(
            Return(AsyncReturn(schedule_decision::ScheduleResult{ "agent", StatusCode::RESOURCE_NOT_ENOUGH, {} })));

    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Reserve,
                                 localGroupCtrlActor_->GetAID(), scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    auto result = future.Get();
    EXPECT_EQ(result.code(), StatusCode::RESOURCE_NOT_ENOUGH);
}

// Bind failed by no reserve
TEST_F(LocalGroupCtrlTest, BindFailedByNoReserve)
{
    auto scheduleReq = NewScheduleRequest();
    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Bind, localGroupCtrlActor_->GetAID(),
                                 scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get().code(), StatusCode::ERR_INNER_SYSTEM_ERROR);
}

// Reserve successful & Bind successful & UnBind successful
TEST_F(LocalGroupCtrlTest, ReserveAndBindAndUnBindSuccessful)
{
    auto scheduleReq = NewScheduleRequest();

    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(AsyncReturn(schedule_decision::ScheduleResult{ "agent", 0, {} })));
    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    {
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Reserve,
                                     localGroupCtrlActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        auto result = future.Get();
        EXPECT_EQ(result.code(), 0);
    }

    EXPECT_CALL(*mockInstanceCtrl_, ToCreating).WillRepeatedly(Return(AsyncReturn(Status::OK())));
    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Bind, localGroupCtrlActor_->GetAID(),
                                 scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get().code(), 0);

    ASSERT_AWAIT_READY(litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Bind,
                                      localGroupCtrlActor_->GetAID(), scheduleReq));

    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance).WillOnce(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*primary_, DeleteInstances).Times(1);
    future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::UnBind, localGroupCtrlActor_->GetAID(),
                            scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get().code(), 0);
}

// Bind failed by etcd failed
TEST_F(LocalGroupCtrlTest, BindFailedByToCreating)
{
    auto scheduleReq = NewScheduleRequest();

    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(AsyncReturn(schedule_decision::ScheduleResult{ "agent", 0, {} })));

    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    {
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Reserve,
                                     localGroupCtrlActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        auto result = future.Get();
        EXPECT_EQ(result.code(), 0);
    }

    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance).WillOnce(Return(AsyncReturn(Status::OK())));
    EXPECT_CALL(*mockInstanceCtrl_, ToCreating)
        .WillOnce(Return(AsyncReturn(Status(StatusCode::ERR_ETCD_OPERATION_ERROR))));
    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Bind, localGroupCtrlActor_->GetAID(),
                                 scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get().code(), StatusCode::ERR_ETCD_OPERATION_ERROR);
}

// Bind failed by etcd txn
TEST_F(LocalGroupCtrlTest, BindFailedByToCreatingTxnFailedAlreadyScheduleToAnother)
{
    auto scheduleReq = NewScheduleRequest();

    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(AsyncReturn(schedule_decision::ScheduleResult{ "agent", 0, {} })));
    auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    {
        auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Reserve,
                                     localGroupCtrlActor_->GetAID(), scheduleReq);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        auto result = future.Get();
        EXPECT_EQ(result.code(), 0);
    }
    EXPECT_CALL(*primary_, DeleteInstances).Times(1);
    EXPECT_CALL(*mockInstanceCtrl_, ToCreating)
        .WillOnce(Return(AsyncReturn(Status(StatusCode::ERR_INSTANCE_DUPLICATED))));
    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Bind, localGroupCtrlActor_->GetAID(),
                                 scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get().code(), StatusCode::SUCCESS);
}

// Reserve successful & UnReserve successful
TEST_F(LocalGroupCtrlTest, ReserveAndTimoutToReserve)
{
    litebus::Terminate(localGroupCtrlActor_->GetAID());
    litebus::Await(localGroupCtrlActor_->GetAID());
    auto localGroupCtrlActor =
        std::make_shared<LocalGroupCtrlActor>(LOCAL_GROUP_CTRL_ACTOR_NAME, "nodeA", mockMetaStoreClient_, 100);
    localGroupCtrlActor->BindScheduler(mockScheduler_);
    auto resourceViewMgr = std::make_shared<resource_view::ResourceViewMgr>();
    resourceViewMgr->primary_ = primary_;
    resourceViewMgr->virtual_ = virtual_;
    localGroupCtrlActor->BindResourceView(resourceViewMgr);
    localGroupCtrlActor->BindInstanceCtrl(mockInstanceCtrl_);
    litebus::Spawn(localGroupCtrlActor);
    auto localGroupCtrl = std::make_shared<LocalGroupCtrl>(localGroupCtrlActor_);
    localGroupCtrl->ToReady();

    auto scheduleReq = NewScheduleRequest();

    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_))
        .WillOnce(Return(AsyncReturn(schedule_decision::ScheduleResult{ "agent", 0, {} })));

    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    litebus::Future<std::vector<std::string>> deletedIns;
    EXPECT_CALL(*primary_, DeleteInstances)
        .WillOnce(DoAll(FutureArg<0>(&deletedIns), Return(AsyncReturn(Status::OK()))));
    auto future = litebus::Async(underlayerSrv_->GetAID(), &DomainUnderlayerStub::Reserve,
                                 localGroupCtrlActor->GetAID(), scheduleReq);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    auto result = future.Get();
    EXPECT_EQ(result.code(), 0);
    ASSERT_AWAIT_READY(deletedIns);
    EXPECT_EQ(deletedIns.IsOK(), true);
    EXPECT_EQ(deletedIns.Get().size(), static_cast<long unsigned int>(1));
    litebus::Terminate(localGroupCtrlActor->GetAID());
    litebus::Await(localGroupCtrlActor->GetAID());
}

// fallback metastore recover test
TEST_F(LocalGroupCtrlTest, OnHealthyStatusTest)
{
    auto localGroupCtrlActor = std::make_shared<LocalGroupCtrlActor>(
        LOCAL_GROUP_CTRL_ACTOR_NAME + "-OnHealthyStatusTest", "nodeA", mockMetaStoreClient_);
    localGroupCtrlActor->BindInstanceCtrl(mockInstanceCtrl_);
    litebus::Spawn(localGroupCtrlActor);
    auto localGroupCtrl = std::make_shared<LocalGroupCtrl>(localGroupCtrlActor);
    Status status(StatusCode::FAILED);
    localGroupCtrl->OnHealthyStatus(status);
    localGroupCtrl->OnHealthyStatus(Status::OK());
    localGroupCtrl->ToReady();

    auto getResponse = std::make_shared<GetResponse>();
    auto kv1 = NewGroupInfoJson("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), "nodeA",
                                GroupState::SCHEDULING, 3);
    auto kv2 = NewGroupInfoJson("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), "nodeB",
                                GroupState::RUNNING, 3);
    auto kv3 = NewGroupInfoJson("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), "nodeA",
                                GroupState::FAILED, 3);
    auto kv4 = NewGroupInfoJson("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), "nodeA",
                                GroupState::SCHEDULING, 3);
    getResponse->kvs.emplace_back(kv1.kv);
    getResponse->kvs.emplace_back(kv2.kv);
    getResponse->kvs.emplace_back(kv3.kv);
    EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(AsyncReturn(getResponse)));
    auto deleteResponse = std::make_shared<DeleteResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Delete)
        .WillOnce(Return(AsyncReturn(deleteResponse)))
        .WillOnce(Return(AsyncReturn(deleteResponse)));
    localGroupCtrlActor->NewGroupCtx(kv4.info);
    localGroupCtrl->OnHealthyStatus(Status::OK());
    ASSERT_AWAIT_TRUE([&]() -> bool {
        auto future =
            litebus::Async(localGroupCtrlActor->GetAID(), &LocalGroupCtrlActor::GetGroupCtx, kv4.info->requestid());
        return future.Get() == nullptr;
    });
    litebus::Terminate(localGroupCtrlActor->GetAID());
    litebus::Await(localGroupCtrlActor->GetAID());
}

// SFMD group schedule success
TEST_F(LocalGroupCtrlTest, SfmdGroupScheduleLocalSuccessful)
{
    auto createRequests = std::make_shared<CreateRequests>();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    createRequests->set_traceid("group-traceID");
    Start();
    createRequests->clear_requests();
    createRequests->set_requestid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());

    const int num = 3;
    for (int i = 0; i < num; ++i) {
        auto request = createRequests->add_requests();
        (*request->mutable_schedulingops()->mutable_resources())["NPU/310/HBM"] = 30;
    }

    EXPECT_CALL(*mockInstanceCtrl_, ToScheduling).Times(3).WillRepeatedly(Return(Status::OK()));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse)).WillOnce(Return(putResponse));

    std::string selectedAgentId1 = "agent1";
    std::string selectedAgentId2 = "agent2";
    std::string selectedAgentId3 = "agent3";
    std::string selectedNodeId = "node1";

    schedule_decision::GroupScheduleResult result;
    result.code = 0;
    std::string cardType = "NPU/310";
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId1, 0, "", {0, 1}, cardType });
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId2, 0, "", {2, 3}, cardType });
    (void)result.results.emplace_back(schedule_decision::ScheduleResult{ selectedAgentId3, 0, "", {0, 4}, cardType });
    EXPECT_CALL(*mockScheduler_, GroupScheduleDecision(_)).WillOnce(Return(result));

    auto localResourceView = std::make_shared<resource_view::ResourceUnit>();
    auto unit1 = view_utils::Get1DResourceUnitWithSpecificNpuNumber({94,100,100,100,100,100,100,100});
    unit1.set_id(selectedAgentId1);
    (*localResourceView->mutable_fragment())[selectedAgentId1] = unit1;
    auto unit2 = view_utils::Get1DResourceUnitWithSpecificNpuNumber({94,100,100,100,100,100,100,100});
    unit2.set_id(selectedAgentId2);
    (*localResourceView->mutable_fragment())[selectedAgentId2] = unit2;
    auto unit3 = view_utils::Get1DResourceUnitWithSpecificNpuNumber({94,100,100,100,100,100,100,100});
    unit3.set_id(selectedAgentId1);
    (*localResourceView->mutable_fragment())[selectedAgentId3] = unit3;
    EXPECT_CALL(*primary_, GetResourceViewCopy).WillRepeatedly(Return(localResourceView));

    localResourceView->set_id(selectedNodeId);

    EXPECT_CALL(*mockInstanceCtrl_, RegisterReadyCallback)
        .WillRepeatedly(DoAll(
            Invoke([](const std::string &instanceID, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
              InstanceReadyCallBack callback) { callback(Status::OK()); })));

    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    EXPECT_CALL(*mockInstanceCtrl_, ToCreating).WillRepeatedly(DoAll(SaveArg<0>(&scheduleReq), Return(Status::OK())));

    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_)).WillOnce(Return(mockSharedClient));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));
    auto future = localGroupCtrl_->GroupSchedule("srcInstanceID", createRequests);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.IsOK(), true);
    EXPECT_EQ(future.Get()->code(), common::ErrorCode::ERR_NONE);
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);

    common::FunctionGroupRunningInfo functionGroupRunningInfo;
    auto jsonOpt = google::protobuf::util::JsonParseOptions();
    jsonOpt.ignore_unknown_fields = true;
    if (!google::protobuf::util::JsonStringToMessage(scheduleReq->instance().createoptions().at(
        "FUNCTION_GROUP_RUNNING_INFO"), &functionGroupRunningInfo, jsonOpt).ok()) {
        EXPECT_EQ(1, 0);
    }

    ASSERT_EQ(functionGroupRunningInfo.serverlist_size(), 1);
    EXPECT_EQ(functionGroupRunningInfo.worldsize(), 3);
    EXPECT_EQ(functionGroupRunningInfo.devicename(), "NPU/310");
    auto &serverList = functionGroupRunningInfo.serverlist(0);
    EXPECT_EQ(serverList.serverid(), selectedNodeId);
    EXPECT_EQ(serverList.devices_size(), 5);

    // key: device id, value: rank id
    std::map<int64_t, int64_t> expectedDeviceRanks = {
        {100, 0}, {101, 1},  {102, 2}, {103, 3}, {104, 4}
    };
    // key: device id, value: device ip
    std::map<int64_t, std::string> expectedDeviceIps = {
        {100, "0.0.0.0"}, {101, "0.0.0.1"},  {102, "0.0.0.2"}, {103, "0.0.0.3"}, {104, "0.0.0.4"}
    };

    for (const auto& device : serverList.devices()) {
        auto deviceId = device.deviceid();
        auto rankId = device.rankid();
        auto deviceIp = device.deviceip();
        EXPECT_EQ(expectedDeviceRanks[deviceId], rankId);
        EXPECT_EQ(expectedDeviceIps[deviceId], deviceIp);
    }
}

// group schedule reponse later than notify
TEST_F(LocalGroupCtrlTest, ResponseLater)
{
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(_))
        .WillOnce(Return(AsyncReturn(std::dynamic_pointer_cast<ControlInterfacePosixClient>(mockSharedClient))));
    litebus::Promise<runtime::NotifyRequest> notifyCalled;
    EXPECT_CALL(*mockSharedClient, NotifyResult(_))
        .WillOnce(Invoke([notifyCalled](runtime::NotifyRequest &&request) -> litebus::Future<runtime::NotifyResponse> {
            notifyCalled.SetValue(request);
            return runtime::NotifyResponse();
        }));

    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));
    auto localGroupCtrlActor = std::make_shared<LocalGroupCtrlActor>(
        LOCAL_GROUP_CTRL_ACTOR_NAME + "-OnHealthyStatusTest", "nodeA", mockMetaStoreClient_);
    localGroupCtrlActor->BindInstanceCtrl(mockInstanceCtrl_);
    localGroupCtrlActor->BindControlInterfaceClientManager(clientManager_);
    litebus::Spawn(localGroupCtrlActor);
    auto localGroupCtrl = std::make_shared<LocalGroupCtrl>(localGroupCtrlActor);
    localGroupCtrl->ToReady();

    auto kv = NewGroupInfoJson("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), "nodeA",
                               GroupState::SCHEDULING, 3);
    auto ctx = localGroupCtrlActor->NewGroupCtx(kv.info);
    localGroupCtrlActor->OnGroupSuccessful(ctx);
    // notifyCalled should not be called
    EXPECT_EQ(notifyCalled.GetFuture().IsInit(), true);
    ctx->persistingPromise.SetValue(std::make_shared<CreateResponses>());
    ASSERT_AWAIT_READY(notifyCalled.GetFuture());
    EXPECT_EQ(notifyCalled.GetFuture().Get().code(), common::ErrorCode::ERR_NONE);
    litebus::Terminate(localGroupCtrlActor->GetAID());
    litebus::Await(localGroupCtrlActor->GetAID());
}

}  // namespace functionsystem::test