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

#include "function_proxy/common/state_machine/instance_control_view.h"

#include <gtest/gtest.h>

#include "common/etcd_service/etcd_service_driver.h"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_observer.h"
#include "mocks/mock_txn_transaction.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

using namespace testing;

const std::string TEST_NODE_ID = "test node id";

class InstanceControlViewTest : public ::testing::Test {
public:
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::string metaStoreServerHost_;

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
};

TEST_F(InstanceControlViewTest, NewInstanceWithStateScheduling)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    auto mockMetaClient = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
    TxnResponse response;
    response.status = Status::OK();
    response.responses.emplace_back(TxnOperationResponse());
    instanceControlView.BindMetaStoreClient(mockMetaClient);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string instanceID = "instanceID";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(
        static_cast<int32_t>(InstanceState::SCHEDULING));
    scheduleReq->mutable_instance()->set_function(function);
    auto generated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(generated);
    auto generatedID = generated.Get().instanceID;
    EXPECT_EQ(generatedID, instanceID);
    auto stateMachine = instanceControlView.GetInstance(generatedID);
    EXPECT_EQ(stateMachine->GetOwner(), TEST_NODE_ID);
}

TEST_F(InstanceControlViewTest, NewInstanceWithStateInvalid)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string instanceID = "instanceID";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::CREATING));
    scheduleReq->mutable_instance()->set_function(function);
    auto generated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(generated);
    auto generatedID = generated.Get().instanceID;
    EXPECT_EQ(generatedID, "");
}

TEST_F(InstanceControlViewTest, NewInstanceWithStateNew)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    auto mockMetaClient = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
    TxnResponse response;
    response.status = Status::OK();
    response.responses.emplace_back(TxnOperationResponse());
    instanceControlView.BindMetaStoreClient(mockMetaClient);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string instanceID = "";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::NEW));
    scheduleReq->mutable_instance()->set_function(function);
    auto generated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(generated);
    auto generatedID = generated.Get().instanceID;
    EXPECT_NE(generatedID, "");
    auto stateMachine = instanceControlView.GetInstance(generatedID);
    EXPECT_EQ(stateMachine->GetOwner(), TEST_NODE_ID);
}

TEST_F(InstanceControlViewTest, NewInstanceWithDuplicate)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    auto mockMetaClient = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
    TxnResponse response;
    response.status = Status::OK();
    response.responses.emplace_back(TxnOperationResponse());
    instanceControlView.BindMetaStoreClient(mockMetaClient);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string instanceID = "";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::NEW));
    scheduleReq->mutable_instance()->set_function(function);
    EXPECT_FALSE(instanceControlView.IsRescheduledRequest(scheduleReq)); // isn't rescheduled request before NewInstance
    auto generated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(generated);
    auto generatedID = generated.Get().instanceID;
    EXPECT_NE(generatedID, "");
    auto stateMachine = instanceControlView.GetInstance(generatedID);
    EXPECT_EQ(stateMachine->GetOwner(), TEST_NODE_ID);

    auto duplicateGenerated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(duplicateGenerated);
    auto duplicateGeneratedID = duplicateGenerated.Get().instanceID;
    EXPECT_EQ(generatedID, duplicateGeneratedID);
    stateMachine = instanceControlView.GetInstance(generatedID);
    EXPECT_EQ(stateMachine->GetOwner(), TEST_NODE_ID);

    instanceControlView.DelInstance(generatedID);
    EXPECT_TRUE(instanceControlView.IsRescheduledRequest(scheduleReq)); // is rescheduled request after NewInstance
    generated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(generated);
    generatedID = generated.Get().instanceID;
    EXPECT_NE(generatedID, "");
}

TEST_F(InstanceControlViewTest, NewInstanceWithDistributeDuplicate)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    auto mockMetaClient = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
    TxnResponse response;
    response.status = Status::OK();
    instanceControlView.BindMetaStoreClient(mockMetaClient);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string instanceID = "";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::NEW));
    scheduleReq->mutable_instance()->set_function(function);
    auto generated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(generated);
    auto generatedID = generated.Get().instanceID;
    EXPECT_NE(generatedID, "");
    EXPECT_EQ(generated.Get().isDuplicate, false);

    generated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(generated);
    EXPECT_EQ(generated.Get().instanceID, generatedID);
    EXPECT_EQ(generated.Get().isDuplicate, true);
}

TEST_F(InstanceControlViewTest, ListenUpdateInstanceRemote)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string instanceID = "instance id";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(
        static_cast<int32_t>(InstanceState::SCHEDULING));
    scheduleReq->mutable_instance()->set_function(function);
    scheduleReq->mutable_instance()->set_functionproxyid("1");
    instanceControlView.Update(instanceID, scheduleReq->instance(), false);
    EXPECT_EQ(instanceControlView.GetInstance(instanceID)->GetOwner(), "1");
    EXPECT_TRUE(instanceControlView.GetInstance(instanceID)->GetUpdateByRouteInfo());
}

TEST_F(InstanceControlViewTest, ListenUpdateInstanceLocal)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string instanceID = "instance id";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(
        static_cast<int32_t>(InstanceState::SCHEDULING));
    scheduleReq->mutable_instance()->set_function(function);
    auto mockMetaClient = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
    TxnResponse response;
    response.status = Status::OK();
    response.responses.emplace_back(TxnOperationResponse());
    instanceControlView.BindMetaStoreClient(mockMetaClient);
    instanceControlView.NewInstance(scheduleReq);

    instanceControlView.Update(instanceID, scheduleReq->instance(), false);
    EXPECT_EQ(instanceControlView.GetInstance(instanceID)->GetOwner(), TEST_NODE_ID);
    EXPECT_FALSE(instanceControlView.GetInstance(instanceID)->GetUpdateByRouteInfo());
}

/**
 * Feature: DelInstanceTest
 * Description: DelInstance Success while put etcd succeess, DelInstance failed while put etcd failed,
 * Steps:
 * 1. Create instanceControlView bind mockMetaClient
 * 2. InsertRequestFuture, DeleteRequestFuture
 *
 * Expectation:
 * 1. StatusCode::ERR_NONE
 */
TEST_F(InstanceControlViewTest, DelInstanceTest)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    auto mockMetaClient = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);

    instanceControlView.BindMetaStoreClient(mockMetaClient);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string instanceID = "";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::NEW));
    scheduleReq->mutable_instance()->set_function(function);

    auto mockTxnTransaction = std::make_shared<MockTxnTransaction>(litebus::AID());
    EXPECT_CALL(*mockMetaClient, BeginTransaction).WillRepeatedly(testing::Return(mockTxnTransaction));

    auto txnResponseSuccess = std::make_shared<TxnResponse>();
    txnResponseSuccess->success = true;
    txnResponseSuccess->responses.emplace_back(TxnOperationResponse());
    txnResponseSuccess->responses.emplace_back(TxnOperationResponse());

    EXPECT_CALL(*mockTxnTransaction, Commit)
        .WillRepeatedly(Return(litebus::Future<std::shared_ptr<TxnResponse>>(txnResponseSuccess)));

    auto generated = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(generated);
    auto generatedID = generated.Get().instanceID;
    EXPECT_EQ(instanceControlView.GetInstance(generatedID)->GetOwner(), TEST_NODE_ID);

    auto machine = instanceControlView.GetInstance(generatedID);
    machine->TransitionTo(TransContext{ InstanceState::SCHEDULING, 0, "", true });
    machine->TransitionTo(TransContext{ InstanceState::CREATING, 0, "", true });
    machine->TransitionTo(TransContext{ InstanceState::RUNNING, 0, "", true });
    machine->TransitionTo(TransContext{ InstanceState::EXITING, 0, "", true });

    // mock delete failed for etcd error
    auto txnResponseFail = std::make_shared<TxnResponse>();
    txnResponseFail->status = Status(StatusCode::ERR_ETCD_OPERATION_ERROR);
    EXPECT_CALL(*mockTxnTransaction, Commit)
        .WillOnce(Return(litebus::Future<std::shared_ptr<TxnResponse>>(txnResponseFail)));
    auto status = instanceControlView.DelInstance(generatedID);
    EXPECT_FALSE(status.Get().IsOk());
    instanceControlView.OnDelInstance(generatedID, "", false);
    EXPECT_TRUE(instanceControlView.GetInstance(generatedID) != nullptr);

    // mock delete success
    auto deleteTxnResp = TxnOperationResponse();
    auto deleteResponse = DeleteResponse();
    deleteResponse.deleted = true;
    deleteTxnResp.operationType = TxnOperationType::OPERATION_DELETE;
    deleteTxnResp.response = deleteResponse;
    auto deleteResponseSuccess = std::make_shared<TxnResponse>();
    deleteResponseSuccess->success = true;
    deleteResponseSuccess->responses.emplace_back(deleteTxnResp);
    deleteResponseSuccess->responses.emplace_back(deleteTxnResp);

    EXPECT_CALL(*mockTxnTransaction, Commit)
        .WillOnce(Return(litebus::Future<std::shared_ptr<TxnResponse>>(deleteResponseSuccess)));

    auto observer = std::make_shared<MockObserver>();
    InstanceStateMachine::BindControlPlaneObserver(observer);
    EXPECT_CALL(*observer, DelInstanceEvent(generatedID, _)).WillOnce(testing::Return(Status::OK()));

    status = instanceControlView.DelInstance(generatedID);
    EXPECT_TRUE(status.Get().IsOk());
    instanceControlView.OnDelInstance(generatedID, "", true);
    EXPECT_FALSE(instanceControlView.GetInstance(generatedID) != nullptr);

    InstanceStateMachine::BindControlPlaneObserver(nullptr);
}

/**
 * Feature: InsertRequestFuture and DeleteRequestFuture success
 * Description: InsertRequestFuture and DeleteRequestFuture success
 * Steps:
 * 1. Create instanceControlView bind mockMetaClient
 * 2. InsertRequestFuture, DeleteRequestFuture
 *
 * Expectation:
 * 1. StatusCode::ERR_NONE
 */
TEST_F(InstanceControlViewTest, HandleReqestFuture)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    auto mockMetaClient = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost_ });
    TxnResponse response;
    response.status = Status::OK();
    response.responses.emplace_back(TxnOperationResponse());
    instanceControlView.BindMetaStoreClient(mockMetaClient);
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    const std::string requestID = "req";

    litebus::Future<messages::ScheduleResponse> fut;
    messages::ScheduleResponse rsp;
    fut.SetValue(rsp);
    instanceControlView.InsertRequestFuture(requestID, rsp, {});
    instanceControlView.DeleteRequestFuture(requestID);
    instanceControlView.ReleaseOwner(requestID);
}

/**
 * Feature: TryExitInstance Successand failed
 * Description: TryExitInstance Success and failed
 * Steps:
 * 1. Update an instance
 * 2. TryExitInstance and get success
 * 3. Try a NonExitInstance and get failed
 *
 * Expectation:
 * 1. StatusCode::ERR_NONE
 */
TEST_F(InstanceControlViewTest, TryExitInstanceNoInstance)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    std::string instanceID = "instanceID";
    instanceControlView.SetOwner(instanceID);
    resources::InstanceInfo instanceInfo;
    instanceInfo.set_functionproxyid("proxyid");
    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::EXITING));
    instanceControlView.Update(instanceID, instanceInfo, false);
    auto fut = instanceControlView.TryExitInstance(instanceID);
    ASSERT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().IsOk());

    fut = instanceControlView.TryExitInstance("instanceIDA");
    ASSERT_AWAIT_READY(fut);
    EXPECT_TRUE(fut.Get().IsError());
}

/**
 * Feature: DeleteInstance
 * Description: DeleteInstance
 * Steps:
 * 1. Update an instance
 * 2. TryExitInstance and get success
 * 3. Try a NonExitInstance and get failed
 *
 * Expectation:
 * 1. StatusCode::ERR_NONE
 */
TEST_F(InstanceControlViewTest, DeleteInstance)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    std::string instanceID = "instanceID";
    instanceControlView.SetOwner(instanceID);
    resources::InstanceInfo instanceInfo;
    instanceInfo.set_functionproxyid("proxyid");
    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::EXITING));
    (*instanceInfo.mutable_extensions())[INSTANCE_MOD_REVISION] = "1000";
    instanceControlView.Update(instanceID, instanceInfo, false);
    instanceControlView.Delete("instanceIDA", 100);
    instanceControlView.Delete("instanceIDA");
    instanceControlView.Delete(instanceID);

    instanceID = "instanceIDABC";
    instanceControlView.SetOwner(instanceID);
    instanceInfo.set_instanceid(instanceID);
    instanceInfo.set_functionproxyid("proxyid");
    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::CREATING));
    instanceControlView.Update(instanceID, instanceInfo, false);
    instanceControlView.Delete(instanceID);
}

/**
 * Feature: New instance which is already scheduling
 * Description: New instance which is already scheduling
 * Steps:
 * 1. Update an instance whose instance state is InstanceState::SCHEDULING
 * 2. New an instance
 *
 * Expectation:
 * 1. Duplicate is true
 */
TEST_F(InstanceControlViewTest, IsDuplicateScheduling)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    std::string instanceID = "instanceID";
    std::string requestID = "req";
    instanceControlView.SetOwner(instanceID);
    resources::InstanceInfo instanceInfo;
    instanceInfo.set_functionproxyid("proxyid");
    instanceInfo.set_requestid(requestID);
    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::SCHEDULING));
    instanceControlView.Update(instanceID, instanceInfo, false);

    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->set_requestid(requestID);
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->set_requestid(requestID);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(
        static_cast<int32_t>(InstanceState::SCHEDULING));

    auto res = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(res);
    EXPECT_TRUE(res.Get().preState == InstanceState::SCHEDULING);

    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::FAILED));
    instanceControlView.Update(instanceID, instanceInfo, false);
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(
        static_cast<int32_t>(InstanceState::SCHEDULING));

    res = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(res);
    EXPECT_TRUE(res.Get().preState == InstanceState::SCHEDULING);

    instanceControlView.ReleaseOwner(instanceID);
    res = instanceControlView.NewInstance(scheduleReq);
    ASSERT_AWAIT_READY(res);
    EXPECT_TRUE(res.Get().preState == InstanceState::SCHEDULING);
}

TEST_F(InstanceControlViewTest, OldUpdateInstanceEvent)
{
    InstanceControlView instanceControlView(TEST_NODE_ID, false);
    std::string instanceID = "instanceID";
    std::string requestID = "req";
    instanceControlView.SetOwner(instanceID);
    resources::InstanceInfo instanceInfo;
    instanceInfo.set_functionproxyid("proxyid");
    instanceInfo.set_requestid(requestID);
    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::SCHEDULING));
    instanceControlView.Update(instanceID, instanceInfo, false);

    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::CREATING));
    (*instanceInfo.mutable_extensions())["modRevision"] = "10";
    instanceControlView.Update(instanceID, instanceInfo, false);

    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::SCHEDULING));
    (*instanceInfo.mutable_extensions())["modRevision"] = "1";
    instanceControlView.Update(instanceID, instanceInfo, false);

    auto stateMachine = instanceControlView.GetInstance(instanceID);
    EXPECT_EQ(stateMachine->GetModRevision(), 10);
    EXPECT_EQ(stateMachine->GetInstanceState(), InstanceState::CREATING);
}

}  // namespace functionsystem::test