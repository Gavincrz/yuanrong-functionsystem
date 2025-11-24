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

#include "local_scheduler/instance_control/posix_api_handler/posix_api_handler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "async/async.hpp"
#include "async/future.hpp"
#include "common/logs/logging.h"
#include "common/proto/pb/posix_pb.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_local_sched_srv.h"
#include "mocks/mock_local_group_ctrl.h"
#include "mocks/mock_resource_group_ctrl.h"
#include "mocks/mock_shared_client.h"
#include "mocks/mock_shared_client_manager_proxy.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using namespace runtime_rpc;
using namespace local_scheduler;

class PosixAPIHandlerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        mockInstanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        mockLocalSchedSrv_ = std::make_shared<MockLocalSchedSrv>();
        mockSharedClientManagerProxy_ = std::make_shared<MockSharedClientManagerProxy>();
        mockLocalGroupCtrl_ = std::make_shared<MockGroupCtrl>();
        mockResourceGroupCtrl_ = std::make_shared<MockResourceGroupCtrl>();
    }

    void TearDown() override
    {
        mockInstanceCtrl_ = nullptr;
        mockSharedClientManagerProxy_ = nullptr;
    }

    void CreateTest(const std::string &requestID, const std::string &traceID, const StatusCode &code,
                    const std::string &message, const std::string &instanceID)
    {
        PosixAPIHandler::BindInstanceCtrl(mockInstanceCtrl_);
        PosixAPIHandler::BindControlClientManager(mockSharedClientManagerProxy_);
        auto rsp = GenScheduleRsp(code, message, instanceID, requestID);
        FunctionMeta functionMeta{};
        EXPECT_CALL(*mockInstanceCtrl_, GetFuncMeta).WillRepeatedly(testing::Return(std::move(functionMeta)));
        EXPECT_CALL(*mockInstanceCtrl_, GetActorAID).WillRepeatedly(testing::Return(litebus::AID()));
        EXPECT_CALL(*mockInstanceCtrl_, Schedule)
            .WillOnce(testing::Invoke(
                [rsp](const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                      const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise) {
                    runtimePromise->SetValue(rsp);
                    return messages::ScheduleResponse{};
                }));

        std::string from = "runtimeB";
        auto request = GenCreateReq(requestID, traceID);
        auto future = PosixAPIHandler::Create(from, request);
        auto response = future.Get()->creatersp();
        EXPECT_EQ(response.code(), static_cast<int>(code));
        EXPECT_EQ(response.message(), message);
        EXPECT_EQ(response.instanceid(), instanceID);
    }

    void CreateWithDeviceTest(const std::string &requestID, const std::string &traceID, const StatusCode &code,
                    const std::string &message, const std::string &instanceID,
                    litebus::Future<litebus::Option<FunctionMeta>> functionMeta)
    {
        PosixAPIHandler::BindInstanceCtrl(mockInstanceCtrl_);
        PosixAPIHandler::BindControlClientManager(mockSharedClientManagerProxy_);
        auto rsp = GenScheduleRsp(code, message, instanceID, requestID);
        EXPECT_CALL(*mockInstanceCtrl_, Schedule).WillOnce(testing::Invoke([rsp](
                const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise) {
                runtimePromise->SetValue(rsp);
                return messages::ScheduleResponse{};
            }));

        EXPECT_CALL(*mockInstanceCtrl_, GetFuncMeta).WillRepeatedly(testing::Return(std::move(functionMeta)));
        std::string from = "runtimeB";
        auto request = GenCreateReq(requestID, traceID, "saxpy");
        auto future = PosixAPIHandler::Create(from, request);
        auto response = future.Get()->creatersp();
        EXPECT_EQ(response.code(), static_cast<int>(code));
        EXPECT_EQ(response.message(), message);
        EXPECT_EQ(response.instanceid(), instanceID);
    }

    void KillTest(const std::string &instanceID, int32_t signal, const common::ErrorCode &code,
                  const std::string &message)
    {
        PosixAPIHandler::BindInstanceCtrl(mockInstanceCtrl_);
        PosixAPIHandler::BindControlClientManager(mockSharedClientManagerProxy_);

        auto killRsp = GenKillRsp(code, message);

        EXPECT_CALL(*mockInstanceCtrl_, Kill).WillOnce(testing::Return(killRsp));

        std::string from = "runtimeB";
        auto request = GenKillReq(instanceID, signal);
        auto future = PosixAPIHandler::Kill(from, request);
        auto response = future.Get()->killrsp();
        EXPECT_EQ(response.code(), code);
        EXPECT_EQ(response.message(), message);
    }

    void EmptyInstanceCtlTest(const std::string &instanceID)
    {
        PosixAPIHandler::BindInstanceCtrl(nullptr);
        PosixAPIHandler::BindControlClientManager(mockSharedClientManagerProxy_);
        std::string from = instanceID;
        auto result = PosixAPIHandler::Exit(from, std::make_shared<StreamingMessage>());
        auto result1 = PosixAPIHandler::Exit(from, std::make_shared<StreamingMessage>());
        auto request = GenKillReq(instanceID, -1);
        auto future = PosixAPIHandler::Kill(from, request);
        auto response = future.Get()->killrsp();
        EXPECT_EQ(response.code(), common::ERR_LOCAL_SCHEDULER_ABNORMAL);
    }

    void CallResultCheckTest()
    {
        auto callResult = std::make_shared<functionsystem::CallResult>();
        auto res = PosixAPIHandler::CallResult("", callResult);
        EXPECT_EQ(res.Get().first, false);
        PosixAPIHandler::BindInstanceCtrl(mockInstanceCtrl_);
        callResult->set_requestid("rq1");
        callResult->set_instanceid("ins1");
        CallResultAck ack;
        ack.set_code(static_cast<common::ErrorCode>(StatusCode::LS_REQUEST_NOT_FOUND));
        EXPECT_CALL(*mockInstanceCtrl_, CallResult).WillOnce(testing::Return(ack));
        auto res1 = PosixAPIHandler::CallResult("", callResult);
        EXPECT_EQ(res1.Get().first, false);
    }

protected:
    std::shared_ptr<StreamingMessage> GenCreateReq(const std::string &requestID, const std::string &traceID,
                                                   const std::string& binaryFunctionName="")
    {
        auto request = std::make_unique<StreamingMessage>();
        request->mutable_createreq()->set_requestid(requestID);
        request->mutable_createreq()->set_traceid(traceID);
        return request;
    }

    messages::ScheduleResponse GenScheduleRsp(StatusCode code, const std::string &message,
                                              const std::string &instanceID, const std::string &requestID)
    {
        messages::ScheduleResponse rsp;
        rsp.set_code(code);
        rsp.set_message(message);
        rsp.set_instanceid(instanceID);
        rsp.set_requestid(requestID);
        return rsp;
    }

    std::shared_ptr<StreamingMessage> GenKillReq(const std::string &instanceID, int32_t signal)
    {
        auto request = std::make_unique<StreamingMessage>();
        request->mutable_killreq()->set_instanceid(instanceID);
        request->mutable_killreq()->set_signal(signal);
        return request;
    }

    KillResponse GenKillRsp(const common::ErrorCode &code, const std::string &message)
    {
        KillResponse killRsp;
        killRsp.set_code(code);
        killRsp.set_message(message);
        return killRsp;
    }

    std::shared_ptr<StreamingMessage> GenExitReq(const common::ErrorCode &code)
    {
        auto request = std::make_unique<StreamingMessage>();
        request->mutable_exitreq()->set_code(code);
        request->mutable_exitreq()->set_message("ok");
        return request;
    }

    ExitResponse GenExitRsp(const common::ErrorCode &code, const std::string &message)
    {
        ExitResponse killRsp;
        killRsp.set_code(code);
        killRsp.set_message(message);
        return killRsp;
    }

protected:
    std::shared_ptr<MockInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockLocalSchedSrv> mockLocalSchedSrv_;
    std::shared_ptr<MockSharedClientManagerProxy> mockSharedClientManagerProxy_;
    std::shared_ptr<MockGroupCtrl> mockLocalGroupCtrl_;
    std::shared_ptr<MockResourceGroupCtrl> mockResourceGroupCtrl_;
};

TEST_F(PosixAPIHandlerTest, CreateWithEmptyInstanceCtrl)
{
    PosixAPIHandler::BindInstanceCtrl(nullptr);
    std::string from = "runtimeB";
    auto request = GenCreateReq("requestA", "trace123");

    auto future = PosixAPIHandler::Create(from, std::move(request));
    ASSERT_EQ(future.Get()->has_creatersp(), true);
    auto response = future.Get()->creatersp();
    EXPECT_EQ(response.code(), common::ERR_LOCAL_SCHEDULER_ABNORMAL);
}

TEST_F(PosixAPIHandlerTest, CreateSuccess)
{
    auto requestID = "requestA";
    auto traceID = "trace123";

    auto successCode = StatusCode::SUCCESS;
    auto successMsg = "schedule success";
    auto instanceID = "instanceA";

    CreateTest(requestID, traceID, successCode, successMsg, instanceID);
}

TEST_F(PosixAPIHandlerTest, CreateFailed)
{
    auto requestID = "requestA";
    auto traceID = "trace123";

    auto failedCode = StatusCode::ERR_INNER_SYSTEM_ERROR;
    auto failedMsg = "schedule failed";
    auto instanceID = "instanceA";

    CreateTest(requestID, traceID, failedCode, failedMsg, instanceID);
}

TEST_F(PosixAPIHandlerTest, CreateFailedWithInvalidInstanceId)
{
    auto requestID = "requestA";
    auto traceID = "trace123";
    auto failedCode = StatusCode::ERR_PARAM_INVALID;
    auto failedMsg = "invalid designated instanceid";

    std::vector<std::string> invalidChars = {"\"", "\'", ";", "\\", "|", "&", "$", ">", "<", "`"};
    for (const auto& ch : invalidChars) {
        std::string instanceID = "instanceA" + ch;
        CreateTest(requestID, traceID, failedCode, failedMsg, instanceID);
    }
}

TEST_F(PosixAPIHandlerTest, CreateWithNamedFunctionSuccess)
{
    auto requestID = "requestA";
    auto traceID = "trace123";

    auto successCode = StatusCode::SUCCESS;
    auto successMsg = "schedule success";
    auto instanceID = "instanceA";

    FunctionMeta functionMeta{};
    functionMeta.extendedMetaData.deviceMetaData = { .hbm = 1000, .latency = 120, .stream = 100,
                                                    .count = 8};
    CreateWithDeviceTest(requestID, traceID, successCode, successMsg, instanceID, functionMeta);
}

TEST_F(PosixAPIHandlerTest, KillSuccess)
{
    auto instanceID = "instanceA";
    auto signal = 1;

    auto successCode = common::ERR_NONE;
    auto successMsg = "kill success";

    KillTest(instanceID, signal, successCode, successMsg);
}

TEST_F(PosixAPIHandlerTest, KillFailed)
{
    auto instanceID = "instanceA";
    auto signal = 1;

    auto failedCode = common::ERR_INSTANCE_NOT_FOUND;
    auto failedMsg = "kill failed";

    KillTest(instanceID, signal, failedCode, failedMsg);
}

TEST_F(PosixAPIHandlerTest, EmptyInstanceCtrl)
{
    auto instanceID = "instance1";
    EmptyInstanceCtlTest(instanceID);
}

TEST_F(PosixAPIHandlerTest, CallResultCheck)
{
    CallResultCheckTest();
}

// group create
TEST_F(PosixAPIHandlerTest, GroupCreate)
{
    {
        auto request = std::make_shared<StreamingMessage>();
        request->mutable_createreqs()->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request->mutable_createreqs()->set_traceid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request->mutable_createreqs()->add_requests()->set_requestid(
            litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        auto future = PosixAPIHandler::GroupCreate("instanceID", request);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get()->has_creatersps(), true);
        EXPECT_EQ(future.Get()->creatersps().code(), common::ERR_INNER_COMMUNICATION);
    }
    {
        auto request = std::make_shared<StreamingMessage>();
        request->mutable_createreqs()->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request->mutable_createreqs()->set_traceid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request->mutable_createreqs()->add_requests()->set_requestid(
            litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        PosixAPIHandler::BindLocalGroupCtrl(mockLocalGroupCtrl_);
        auto responses = std::make_shared<CreateResponses>();
        EXPECT_CALL(*mockLocalGroupCtrl_, GroupSchedule).WillOnce(::testing::Return(responses));
        auto future = PosixAPIHandler::GroupCreate("instanceID", request);
        ASSERT_AWAIT_READY(future);
        EXPECT_EQ(future.Get()->has_creatersps(), true);
        EXPECT_EQ(future.Get()->creatersps().code(), common::ERR_NONE);
    }
}

TEST_F(PosixAPIHandlerTest, InvalidPriorityCreate)
{
    PosixAPIHandler::SetMaxPriority(0);
    auto request = GenCreateReq("requestID", "traceID");
    request->mutable_createreq()->mutable_schedulingops()->set_priority(5);
    auto future = PosixAPIHandler::Create("from", request);
    auto response = future.Get()->creatersp();
    EXPECT_EQ(response.code(), static_cast<int>(common::ERR_PARAM_INVALID));

    PosixAPIHandler::SetMaxPriority(5);
    auto requests = std::make_shared<StreamingMessage>();
    requests->mutable_createreqs()->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    requests->mutable_createreqs()->set_traceid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    requests->mutable_createreqs()->add_requests()->mutable_schedulingops()->set_priority(0);
    requests->mutable_createreqs()->add_requests()->mutable_schedulingops()->set_priority(1);
    requests->mutable_createreqs()->add_requests()->mutable_schedulingops()->set_priority(2);
    future = PosixAPIHandler::GroupCreate("from", requests);
    auto responses = future.Get()->creatersps();
    EXPECT_EQ(responses.code(), static_cast<int>(common::ERR_PARAM_INVALID));
}

TEST_F(PosixAPIHandlerTest, CreateResourceGroup)
{
    auto request = std::make_shared<StreamingMessage>();
    request->mutable_rgroupreq()->set_requestid("rgRequest");
    request->mutable_rgroupreq()->set_traceid("rgTrace");
    std::string from = "runtimeB";
    auto future = PosixAPIHandler::CreateResourceGroup(from, request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get()->has_rgrouprsp(), true);
    auto rgrsp = future.Get()->rgrouprsp();
    EXPECT_EQ(rgrsp.code(), common::ERR_INNER_SYSTEM_ERROR);

    PosixAPIHandler::BindResourceGroupCtrl(mockResourceGroupCtrl_);
    auto rsp = std::make_shared<CreateResourceGroupResponse>();
    rsp->set_code(common::ERR_NONE);

    EXPECT_CALL(*mockResourceGroupCtrl_, Create).WillOnce(testing::Return(rsp));
    future = PosixAPIHandler::CreateResourceGroup(from, request);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get()->has_rgrouprsp(), true);
    rgrsp = future.Get()->rgrouprsp();
    EXPECT_EQ(rgrsp.code(), common::ERR_NONE);
}

TEST_F(PosixAPIHandlerTest, ExitTest)
{
    PosixAPIHandler::BindInstanceCtrl(mockInstanceCtrl_);
    PosixAPIHandler::BindControlClientManager(mockSharedClientManagerProxy_);

    auto exitRsp = GenExitRsp(common::ERR_NONE, "ok");
    EXPECT_CALL(*mockInstanceCtrl_, Exit).WillOnce(testing::Return(exitRsp));

    std::string from = "runtimeB";
    auto future = PosixAPIHandler::Exit(from, GenExitReq(common::ERR_NPU_FAULT_ERROR));
    ASSERT_AWAIT_READY(future);
    auto response = future.Get()->exitrsp();
    EXPECT_EQ(response.code(), common::ERR_NONE);
    EXPECT_EQ(response.message(), "ok");
}

}  // namespace functionsystem::test