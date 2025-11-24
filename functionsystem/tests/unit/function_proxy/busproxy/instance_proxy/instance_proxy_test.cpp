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

#include "busproxy/instance_proxy/instance_proxy.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "common/metrics/metrics_adapter.h"
#include "common/metrics/metrics_constants.h"
#include "function_proxy/busproxy/instance_view/instance_view.h"
#include "function_proxy/busproxy/invocation_handler/invocation_handler.h"
#include "function_proxy/common/observer/data_plane_observer/data_plane_observer.h"
#include "mocks/mock_internal_iam.h"
#include "mocks/mock_shared_client.h"
#include "mocks/mock_shared_client_manager_proxy.h"
#include "utils/future_test_helper.h"
#include "common/metrics/metrics_adapter.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace busproxy;
const std::string CUSTOMS_TAG = "CUSTOMS_TAG";
class SimulateObserver : public function_proxy::DataPlaneObserver, public litebus::ActorBase {
public:
    SimulateObserver() : DataPlaneObserver(nullptr), litebus::ActorBase("SimulateObserver"){};
    virtual ~SimulateObserver() = default;

    void SetInstanceView(const std::shared_ptr<InstanceView> instanceView)
    {
        instanceView_ = instanceView;
    }

    void Update(const std::string &instanceID, const resources::InstanceInfo &instanceInfo)
    {
        (void)litebus::Async(GetAID(), &SimulateObserver::AsyncUpdate, instanceID, instanceInfo).Get();
    }

    void Delete(const std::string &instanceID)
    {
        (void)litebus::Async(GetAID(), &SimulateObserver::AsyncDelete, instanceID).Get();
    }

    Status AsyncUpdate(const std::string &instanceID, const resources::InstanceInfo &instanceInfo)
    {
        instanceView_->Update(instanceID, instanceInfo, false);
        return Status::OK();
    }

    Status AsyncDelete(const std::string &instanceID)
    {
        instanceView_->Delete(instanceID, -1);
        return Status::OK();
    }

    litebus::Future<Status> SubscribeInstanceEvent(const std::string &subscriber, const std::string &targetInstance,
                                                   bool ignoreNonExist = false) override
    {
        SendSubscribeInstanceEvent(subscriber, targetInstance, ignoreNonExist);
        return litebus::Async(GetAID(), &SimulateObserver::DoSubscribeInstanceEvent, subscriber, targetInstance,
                              ignoreNonExist);
    }

    Status DoSubscribeInstanceEvent(const std::string &subscriber, const std::string &targetInstance,
                                    bool ignoreNonExist)
    {
        return instanceView_->SubscribeInstanceEvent(subscriber, targetInstance, ignoreNonExist);
    }

    void NotifyMigratingRequest(const std::string &instanceID) override
    {
        litebus::Async(GetAID(), &SimulateObserver::DoNotifyMigratingRequest, instanceID);
    }

    void DoNotifyMigratingRequest(const std::string &instanceID)
    {
        instanceView_->NotifyMigratingRequest(instanceID);
    }

    MOCK_METHOD(litebus::Future<Status>, SendSubscribeInstanceEvent,
                (const std::string &subscriber, const std::string &targetInstance, bool ignoreNonExist));

private:
    std::shared_ptr<InstanceView> instanceView_;
};

resources::InstanceInfo NewInstance(const std::string &instanceID, const std::string &tenantID,
                                    const bool isLowReliability = false)
{
    resources::InstanceInfo ins;
    ins.set_instanceid(instanceID);
    ins.set_tenantid(tenantID);
    ins.mutable_instancestatus()->set_code((int32_t)InstanceState::SCHEDULING);
    ins.set_lowreliability(isLowReliability);
    return ins;
}

SharedStreamMsg CallRequest(const std::string &caller, const std::string &callee, const std::string &requestID,
                            const std::string &route = "")
{
    auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
    auto callreq = msg->mutable_callreq();
    callreq->set_senderid(caller);
    callreq->set_requestid(requestID);
    (*callreq->mutable_createoptions())[CUSTOMS_TAG] = requestID;
    if (!route.empty()) {
        (*callreq->mutable_createoptions())["YR_ROUTE"] = route;
    }
    return msg;
}

SharedStreamMsg CallResult(const std::string &caller, const std::string &requestID)
{
    auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
    auto callresult = msg->mutable_callresultreq();
    callresult->set_requestid(requestID);
    callresult->set_instanceid(caller);
    return msg;
}

void UpdateInstance(resources::InstanceInfo &info, const std::string &instanceID, int32_t status,
                    std::string proxyid)
{
    info.set_instanceid(instanceID);
    info.mutable_instancestatus()->set_code(status);
    info.set_functionproxyid(proxyid);
    info.set_runtimeid(instanceID);
}

class InstanceProxyTest : public ::testing::Test {
public:
    void SetUp() override
    {
        observer_ = std::make_shared<SimulateObserver>();

        instanceView_ = std::make_shared<InstanceView>("local");
        proxyView_ = std::make_shared<ProxyView>();
        mockSharedClientManagerProxy_ = std::make_shared<MockSharedClientManagerProxy>();
        instanceView_->BindDataInterfaceClientManager(mockSharedClientManagerProxy_);
        instanceView_->BindProxyView(proxyView_);

        observer_->SetInstanceView(instanceView_);
        litebus::Spawn(observer_);

        proxyView_->Update(local_, std::make_shared<proxy::Client>(observer_->GetAID()));
        proxyView_->Update(remote_, std::make_shared<proxy::Client>(observer_->GetAID()));

        InstanceProxy::BindObserver(observer_);
        RequestDispatcher::BindDataInterfaceClientManager(mockSharedClientManagerProxy_);
        InvocationHandler::BindInstanceProxy(std::make_shared<busproxy::InstanceProxyWrapper>());
        MemoryControlConfig config;
        InvocationHandler::BindMemoryMonitor(std::make_shared<MemoryMonitor>(config));
        functionsystem::metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetEnabledInstruments(
            { metrics::YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY });
    }

    void TearDown() override
    {
        InvocationHandler::StopMemoryMonitor();
        litebus::AID callerProxy("callerIns", observer_->GetAID().Url());
        litebus::Terminate(callerProxy);
        litebus::Await(callerProxy);
        litebus::AID calleeProxy("calleeIns", observer_->GetAID().Url());
        litebus::Terminate(calleeProxy);
        litebus::Await(calleeProxy);
        litebus::Terminate(observer_->GetAID());
        litebus::Await(observer_->GetAID());
        instanceView_->Delete("callerIns", -1);
        instanceView_->Delete("calleeIns", -1);
        instanceInfo_.clear();
        InstanceProxy::BindObserver(nullptr);
        RequestDispatcher::BindInternalIAM(nullptr);
        RequestDispatcher::BindDataInterfaceClientManager(nullptr);
        instanceView_->BindDataInterfaceClientManager(nullptr);
        instanceView_ = nullptr;
        proxyView_ = nullptr;
        observer_ = nullptr;
        tenantID_ = "";

        functionsystem::metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetEnabledInstruments({});
    }

    void SetTenantID(const std::string &tenantID)
    {
        tenantID_ = tenantID;
    }

protected:
    std::shared_ptr<MockSharedClient> PrepareCaller(const std::string &callerIns)
    {
        auto instanceInfo = NewInstance(callerIns, tenantID_);
        UpdateInstance(instanceInfo, callerIns, (int32_t)InstanceState::RUNNING, local_);
        auto mockSharedClient = std::make_shared<MockSharedClient>();
        EXPECT_CALL(*mockSharedClientManagerProxy_, NewDataInterfacePosixClient(callerIns, _, _))
            .WillOnce(Return(mockSharedClient));
        observer_->Update(callerIns, instanceInfo);
        instanceInfo_[callerIns] = instanceInfo;
        return mockSharedClient;
    }

    void CallTest(const std::string &callerIns, const std::string &calleeIns, bool isCalleeLocal, bool isLowReliability = false)
    {
        auto mockSharedClient = PrepareCaller(callerIns);
        litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
        ASSERT_AWAIT_TRUE([&]() { return litebus::GetActor(callerProxy) != nullptr; });

        auto calleeInfo = NewInstance(calleeIns, tenantID_, isLowReliability);
        observer_->Update(calleeIns, calleeInfo);
        litebus::AID calleeProxy(calleeIns, observer_->GetAID().Url());
        std::string route = "";
        if (isLowReliability) {
            route = observer_->GetAID().Url();
        }

        // the 1st invoke before creating
        auto callBeforeCreating = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns,.tenantID=tenantID_}, calleeIns,
                                                 CallRequest(callerIns, calleeIns, "Request-1", route), nullptr);
        // the 2nd invoke before creating
        auto duplicateCallBeforeCreating = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns, .tenantID=tenantID_}, calleeIns,
                                                          CallRequest(callerIns, calleeIns, "Request-1", route), nullptr);

        // update instance to creating
        UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::CREATING, isCalleeLocal ? local_ : remote_);
        observer_->Update(calleeIns, calleeInfo);
        instanceInfo_[calleeIns] = calleeInfo;

        // invoke 1 times before running
        auto callBeforeRunning = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                                CallRequest(callerIns, calleeIns, "Request-2", route), nullptr);
        // update instance to running
        UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::RUNNING, isCalleeLocal ? local_ : remote_);
        if (isCalleeLocal) {
            auto mockCalleeSharedClient = std::make_shared<MockSharedClient>();
            EXPECT_CALL(*mockSharedClientManagerProxy_, NewDataInterfacePosixClient(calleeIns, _, _))
                .WillOnce(Return(mockCalleeSharedClient));
            EXPECT_CALL(*mockCalleeSharedClient, Call(_))
                .WillRepeatedly(Invoke([](const SharedStreamMsg &request) -> litebus::Future<SharedStreamMsg> {
                    auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
                    auto callrsp = msg->mutable_callrsp();
                    callrsp->set_code(::common::ErrorCode::ERR_NONE);
                    return msg;
                }));
        }

        if (!isLowReliability) {
            observer_->Update(calleeIns, calleeInfo);
            instanceInfo_[calleeIns] = calleeInfo;
        }
        ASSERT_AWAIT_SET(callBeforeCreating);
        ASSERT_AWAIT_SET(duplicateCallBeforeCreating);
        ASSERT_AWAIT_SET(callBeforeRunning);
        EXPECT_TRUE(callBeforeCreating.Get()->has_callrsp() &&
                    callBeforeCreating.Get()->callrsp().code() == common::ERR_NONE);
        EXPECT_TRUE(duplicateCallBeforeCreating.Get()->has_callrsp() &&
                    duplicateCallBeforeCreating.Get()->callrsp().code() == common::ERR_NONE);
        EXPECT_TRUE(callBeforeRunning.Get()->has_callrsp() &&
                    callBeforeRunning.Get()->callrsp().code() == common::ERR_NONE);

        if (!isCalleeLocal) {
            auto billingInvokeOption =functionsystem::metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInvokeOption("Request-1");
            EXPECT_TRUE(billingInvokeOption.instanceID == calleeIns);
        }

        // the 1st invoke after running
        auto callAfterRunning =
            litebus::Async(isCalleeLocal ? calleeProxy : callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns, .tenantID=tenantID_}, calleeIns,
                           CallRequest(callerIns, calleeIns, "Request-3", route), nullptr);
        // the 2nd invoke after running
        auto duplicateCallAfterRunning =
            litebus::Async(isCalleeLocal ? calleeProxy : callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns, .tenantID=tenantID_}, calleeIns,
                           CallRequest(callerIns, calleeIns, "Request-3", route), nullptr);

        ASSERT_AWAIT_SET(callAfterRunning);
        EXPECT_TRUE(callAfterRunning.Get()->has_callrsp() &&
                    callAfterRunning.Get()->callrsp().code() == common::ERR_NONE);
        ASSERT_AWAIT_SET(duplicateCallAfterRunning);
        EXPECT_TRUE(duplicateCallAfterRunning.Get()->has_callrsp() &&
                    duplicateCallAfterRunning.Get()->callrsp().code() == common::ERR_NONE);

        // call result
        EXPECT_CALL(*mockSharedClient, NotifyResult(_)).WillRepeatedly(Return(runtime::NotifyResponse()));
        EXPECT_CALL(*mockSharedClient, IsDone()).WillRepeatedly(Return(false));

        auto callResultBeforeCreating =
            litebus::Async(isCalleeLocal ? callerProxy : calleeProxy, &InstanceProxy::CallResult, calleeIns, callerIns,
                           CallResult(callerIns, "Request-1"), nullptr);
        auto callResultBeforeRunning =
            litebus::Async(isCalleeLocal ? callerProxy : calleeProxy, &InstanceProxy::CallResult, calleeIns, callerIns,
                           CallResult(callerIns, "Request-2"), nullptr);
        auto callResultAfterRunning =
            litebus::Async(isCalleeLocal ? callerProxy : calleeProxy, &InstanceProxy::CallResult, calleeIns, callerIns,
                           CallResult(callerIns, "Request-3"), nullptr);

        ASSERT_AWAIT_SET(callResultBeforeCreating);
        ASSERT_AWAIT_SET(callResultBeforeRunning);
        ASSERT_AWAIT_SET(callResultAfterRunning);
        EXPECT_TRUE(callResultBeforeCreating.Get()->has_callresultack());
        EXPECT_TRUE(callResultBeforeRunning.Get()->has_callresultack());
        EXPECT_TRUE(callResultAfterRunning.Get()->has_callresultack());
        {
            auto option =
                functionsystem::metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInvokeOption(
                    "Request-1");
            EXPECT_EQ(option.instanceID, calleeIns);
            EXPECT_EQ(option.invokeOptions[CUSTOMS_TAG], "Request-1");
        }
        {
            auto option =
                functionsystem::metrics::MetricsAdapter::GetInstance().GetMetricsContext().GetBillingInvokeOption(
                    "Request-2");
            EXPECT_EQ(option.instanceID, calleeIns);
            EXPECT_EQ(option.invokeOptions[CUSTOMS_TAG], "Request-2");
        }
    }

protected:
    std::shared_ptr<SimulateObserver> observer_;
    std::shared_ptr<InstanceView> instanceView_;
    std::shared_ptr<ProxyView> proxyView_;
    std::shared_ptr<MockSharedClientManagerProxy> mockSharedClientManagerProxy_;
    std::string local_ = "local";
    std::string remote_ = "remote";
    std::string tenantID_ = "";
    std::unordered_map<std::string, resources::InstanceInfo> instanceInfo_;
};

/**
 * Feature: invoke test
 * Description: 模拟 bus proxy invoke 调用
 * Steps:
 * 1. callee instance scheduling -> creating (local) -> running
 * 2. invoke 2 times before creating
 * 3. invoke 1 times before running
 * 4. invoke 2 times after running
 * Expectation: all invoke return successful
 */
TEST_F(InstanceProxyTest, CallLocalTest)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    CallTest(callerIns, calleeIns, true);
}

TEST_F(InstanceProxyTest, CallLocalWithAuthorizeTest)
{
    function_proxy::InternalIAM::Param param_{ true, "" };
    auto mockInternalIam = std::make_shared<MockInternalIAM>(param_);
    RequestDispatcher::BindInternalIAM(mockInternalIam);
    EXPECT_CALL(*mockInternalIam, IsIAMEnabled).WillRepeatedly(Return(true));
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    SetTenantID("tenant123");
    litebus::Future<function_proxy::AuthorizeParam> authParam;
    litebus::Future<function_proxy::AuthorizeParam> authParam1;
    EXPECT_CALL(*mockInternalIam, Authorize).WillOnce(testing::DoAll(FutureArg<0>(&authParam), Return(Status::OK())))
        .WillOnce(testing::DoAll(FutureArg<0>(&authParam1), Return(Status::OK())));
    CallTest(callerIns, calleeIns, true);
    ASSERT_AWAIT_READY(authParam);
    ASSERT_AWAIT_READY(authParam1);
    EXPECT_EQ(authParam.Get().callerTenantID, "");
    EXPECT_EQ(authParam.Get().calleeTenantID, "tenant123");
    EXPECT_EQ(authParam1.Get().callerTenantID, "tenant123");
    EXPECT_EQ(authParam1.Get().calleeTenantID, "tenant123");
}

TEST_F(InstanceProxyTest, CallAuthorizeFailTest)
{
    function_proxy::InternalIAM::Param param_{ true, "" };
    auto mockInternalIam = std::make_shared<MockInternalIAM>(param_);
    RequestDispatcher::BindInternalIAM(mockInternalIam);
    EXPECT_CALL(*mockInternalIam, IsIAMEnabled).WillRepeatedly(Return(true));
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    auto mockSharedClient = PrepareCaller(callerIns);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    ASSERT_AWAIT_TRUE([&]() { return litebus::GetActor(callerProxy) != nullptr; });
    auto calleeInfo = NewInstance(calleeIns, tenantID_);
    observer_->Update(calleeIns, calleeInfo);
    litebus::AID calleeProxy(calleeIns, observer_->GetAID().Url());

    EXPECT_CALL(*mockInternalIam, Authorize).WillOnce(Return(Status(StatusCode::FAILED)));
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::RUNNING, local_);
    auto mockCalleeSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*mockSharedClientManagerProxy_, NewDataInterfacePosixClient(calleeIns, _, _))
        .WillOnce(Return(mockCalleeSharedClient));
    EXPECT_CALL(*mockCalleeSharedClient, Call(_))
        .WillRepeatedly(Invoke([](const SharedStreamMsg &request) -> litebus::Future<SharedStreamMsg> {
            auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
            auto callrsp = msg->mutable_callrsp();
            callrsp->set_code(::common::ErrorCode::ERR_NONE);
            return msg;
        }));
    observer_->Update(calleeIns, calleeInfo);
    instanceInfo_[calleeIns] = calleeInfo;
    auto firstCall = litebus::Async(callerProxy, &InstanceProxy::Call,
                                    busproxy::CallerInfo{ .instanceID = callerIns, .tenantID = tenantID_ }, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-authorize-failed"), nullptr);
    ASSERT_AWAIT_SET(firstCall);
    EXPECT_TRUE(firstCall.Get()->has_callrsp() && firstCall.Get()->callrsp().code() == common::ERR_AUTHORIZE_FAILED);
}

/**
 * Feature: invoke test
 * Description: 模拟 bus proxy invoke 调用
 * Steps:
 * 1. instance scheduling -> creating (remote) -> running
 * 2. invoke 2 times before creating
 * 3. invoke 1 times before running
 * 4. invoke 2 times after running
 * Expectation: all invoke return successful
 */
TEST_F(InstanceProxyTest, CallRemoteTest)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";

    auto calleeProxyActor = std::make_shared<InstanceProxy>(calleeIns, "");
    calleeProxyActor->InitDispatcher();
    auto info = std::make_shared<InstanceRouterInfo>();
    info->isReady = true;
    info->isLocal = true;
    info->runtimeID = calleeIns;
    info->proxyID = remote_;
    auto mockCalleeSharedClient = std::make_shared<MockSharedClient>();
    info->localClient = mockCalleeSharedClient;
    calleeProxyActor->NotifyChanged(calleeIns, info);
    litebus::Spawn(calleeProxyActor);
    EXPECT_CALL(*mockCalleeSharedClient, Call(_))
        .WillRepeatedly(Invoke([](const SharedStreamMsg &request) -> litebus::Future<SharedStreamMsg> {
            auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
            auto callrsp = msg->mutable_callrsp();
            callrsp->set_code(::common::ErrorCode::ERR_NONE);
            return msg;
        }));

    CallTest(callerIns, calleeIns, false);
}

TEST_F(InstanceProxyTest, CallLowAbilityTest)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";

    auto calleeProxyActor = std::make_shared<InstanceProxy>(calleeIns, "");
    calleeProxyActor->InitDispatcher();
    auto info = std::make_shared<InstanceRouterInfo>();
    info->isReady = true;
    info->isLocal = true;
    info->runtimeID = calleeIns;
    info->proxyID = remote_;
    auto mockCalleeSharedClient = std::make_shared<MockSharedClient>();
    info->localClient = mockCalleeSharedClient;
    calleeProxyActor->NotifyChanged(calleeIns, info);
    litebus::Spawn(calleeProxyActor);
    EXPECT_CALL(*mockCalleeSharedClient, Call(_))
        .WillRepeatedly(Invoke([](const SharedStreamMsg &request) -> litebus::Future<SharedStreamMsg> {
            auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
            auto callrsp = msg->mutable_callrsp();
            callrsp->set_code(::common::ErrorCode::ERR_NONE);
            return msg;
        }));

    CallTest(callerIns, calleeIns, false, true);
}

/**
 * Feature: NotifyChanged test
 * Description: when instance put event comes, do sth. according to instance info
 */
TEST_F(InstanceProxyTest, NotifyChanged)
{
    std::string calleeIns = "calleeIns";
    auto calleeProxyActor = std::make_shared<InstanceProxy>(calleeIns, "");
    calleeProxyActor->InitDispatcher();
    auto info = std::make_shared<InstanceRouterInfo>();
    info->isReady = true;
    info->isLocal = true;
    info->runtimeID = calleeIns;
    info->proxyID = remote_;
    litebus::Spawn(calleeProxyActor);

    // instance is local, dataInterfaceClient is nullptr
    calleeProxyActor->NotifyChanged(calleeIns, info);
    EXPECT_EQ(calleeProxyActor->selfDispatcher_->dataInterfaceClient_, nullptr);
    EXPECT_FALSE(calleeProxyActor->selfDispatcher_->isReady_);

    // instance is local, dataInterfaceClient exists
    auto mockCalleeSharedClient = std::make_shared<MockSharedClient>();
    calleeProxyActor->selfDispatcher_->dataInterfaceClient_ = mockCalleeSharedClient;
    calleeProxyActor->NotifyChanged(calleeIns, info);
    EXPECT_TRUE(calleeProxyActor->selfDispatcher_->isReady_);
    EXPECT_CALL(*mockCalleeSharedClient, Call(_))
        .WillRepeatedly(Invoke([](const SharedStreamMsg &request) -> litebus::Future<SharedStreamMsg> {
            auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
            auto callrsp = msg->mutable_callrsp();
            callrsp->set_code(::common::ErrorCode::ERR_NONE);
            return msg;
        }));
}

TEST_F(InstanceProxyTest, NotifyRemoteChanged)
{
    std::string calleeIns = "calleeIns";
    auto callerProxyActor = std::make_shared<InstanceProxy>("callerIns", "");
    callerProxyActor->InitDispatcher();
    auto info = std::make_shared<InstanceRouterInfo>();
    info->isReady = true;
    info->isLocal = false;
    info->runtimeID = calleeIns;
    info->proxyID = remote_;
    litebus::Spawn(callerProxyActor);

    // instance is remote
    callerProxyActor->NotifyChanged(calleeIns, info);
    EXPECT_EQ(callerProxyActor->remoteDispatchers_[calleeIns]->remoteAid_.Name(), "");
    EXPECT_FALSE(callerProxyActor->remoteDispatchers_[calleeIns]->isReady_);
    info->remote = callerProxyActor->GetAID();
    callerProxyActor->NotifyChanged(calleeIns, info);
    EXPECT_TRUE(callerProxyActor->remoteDispatchers_[calleeIns]->isReady_);
}

/**
 * Feature: invoke test
 * Description: invoke unexist instance
 * Expectation: all invoke return failed
 */
TEST_F(InstanceProxyTest, CallNotExistInstance)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";

    EXPECT_CALL(*observer_, SendSubscribeInstanceEvent(_, _, false)).WillOnce(Return(Status::OK()));
    auto mockSharedClient = PrepareCaller(callerIns);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    ASSERT_AWAIT_TRUE([&]() { return litebus::GetActor(callerProxy) != nullptr; });
    // invoke 1 times after failed
    auto fisrtCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-not-existed-instance"), nullptr);
    ASSERT_AWAIT_SET(fisrtCall);
    EXPECT_TRUE(fisrtCall.Get()->has_callrsp() && fisrtCall.Get()->callrsp().code() == common::ERR_INSTANCE_NOT_FOUND);
}

/**
 * Feature: invoke test
 * Description: invoke fatal instance
 * 1. instance scheduling -> creating (local) -> running -> failed -> fatal
 * 2. invoke 2 times before creating
 * 3. invoke 1 times before running
 * 4. invoke 2 times after running
 * 5. invoke 1 times before fatal
 * 6. invoke 1 times after fatal
 * Expectation: all invoke return failed
 */
TEST_F(InstanceProxyTest, CallFatalInstance)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    CallTest(callerIns, calleeIns, true);
    auto calleeInfo = instanceInfo_[calleeIns];
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::FAILED, local_);
    observer_->Update(calleeIns, calleeInfo);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    // invoke 1 times after failed
    auto fisrtCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-after-failed"), nullptr);

    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::FATAL, local_);
    calleeInfo.mutable_instancestatus()->set_errcode((int32_t)common::ERR_USER_FUNCTION_EXCEPTION);
    observer_->Update(calleeIns, calleeInfo);
    ASSERT_AWAIT_SET(fisrtCall);
    EXPECT_TRUE(fisrtCall.Get()->has_callrsp() && fisrtCall.Get()->callrsp().code() == common::ERR_USER_FUNCTION_EXCEPTION);

    auto secondCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-after-fatal"), nullptr);
    ASSERT_AWAIT_SET(secondCall);
    EXPECT_TRUE(secondCall.Get()->has_callrsp() &&
                secondCall.Get()->callrsp().code() == common::ERR_USER_FUNCTION_EXCEPTION);
    EXPECT_TRUE(secondCall.Get()->has_callrsp() && secondCall.Get()->callrsp().message().find(
                                                       "instance occurs fatal error, cause by") != std::string::npos);
}

/**
 * Feature: invoke test
 * Description: invoke fatal instance
 * 1. instance scheduling -> creating (local) -> running -> failed -> scheduling -> creating (local) -> running
 * 2. invoke 2 times before creating
 * 3. invoke 1 times before running
 * 4. invoke 2 times after running
 * 5. invoke 1 times before fatal
 * 6. invoke 1 times after fatal
 * Expectation: all invoke return failed
 */
TEST_F(InstanceProxyTest, CallRecoveredInstance)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    CallTest(callerIns, calleeIns, true);
    auto calleeInfo = instanceInfo_[calleeIns];
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::FAILED, local_);
    observer_->Update(calleeIns, calleeInfo);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    // invoke 1 times after failed
    auto fisrtCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-after-failed"), nullptr);

    CallTest(callerIns, calleeIns, true);
    ASSERT_AWAIT_SET(fisrtCall);
    EXPECT_TRUE(fisrtCall.Get()->has_callrsp() && fisrtCall.Get()->callrsp().code() == common::ERR_NONE);
}

/**
 * Feature: invoke test
 * Description: invoke while caller is on init
 * 1. callee instance scheduling -> creating (local) -> running
 * 2. caller instance creating
 * 3. invoke 1 times after callee running
 * Expectation: invoke return successful
 */
TEST_F(InstanceProxyTest, InitCallInstanceBeforeReady)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    auto instanceInfo = NewInstance(callerIns, tenantID_);
    UpdateInstance(instanceInfo, callerIns, (int32_t)InstanceState::CREATING, local_);
    auto mockSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*mockSharedClientManagerProxy_, GetDataInterfacePosixClient(callerIns))
        .WillOnce(Return(mockSharedClient));
    observer_->Update(callerIns, instanceInfo);
    instanceInfo_[callerIns] = instanceInfo;

    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    ASSERT_AWAIT_TRUE([&]() { return litebus::GetActor(callerProxy) != nullptr; });

    auto calleeInfo = NewInstance(calleeIns, tenantID_);
    observer_->Update(calleeIns, calleeInfo);
    litebus::AID calleeProxy(calleeIns, observer_->GetAID().Url());
    // update instance to running
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::RUNNING, local_);
    auto mockCalleeSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*mockSharedClientManagerProxy_, NewDataInterfacePosixClient(calleeIns, _, _))
        .WillOnce(Return(mockCalleeSharedClient));
    EXPECT_CALL(*mockCalleeSharedClient, Call(_))
        .WillRepeatedly(Invoke([](const SharedStreamMsg &request) -> litebus::Future<SharedStreamMsg> {
            auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
            auto callrsp = msg->mutable_callrsp();
            callrsp->set_code(::common::ErrorCode::ERR_NONE);
            return msg;
        }));
    observer_->Update(calleeIns, calleeInfo);

    // invoke 1 times after failed
    auto fisrtCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-1"), nullptr);

    ASSERT_AWAIT_SET(fisrtCall);
    EXPECT_TRUE(fisrtCall.Get()->has_callrsp() && fisrtCall.Get()->callrsp().code() == common::ERR_NONE);

    // call result
    EXPECT_CALL(*mockSharedClient, NotifyResult(_)).WillRepeatedly(Return(runtime::NotifyResponse()));

    auto fisrtCallResult = litebus::Async(calleeProxy, &InstanceProxy::CallResult, calleeIns, callerIns,
                                          CallResult(callerIns, "Request-1"), nullptr);
    ASSERT_AWAIT_SET(fisrtCallResult);
    EXPECT_TRUE(fisrtCallResult.Get()->has_callresultack());
}

/**
 * Feature: invoke test
 * Description: invoke fatal instance
 * 1. instance scheduling -> creating (local) -> running -> failed -> delete
 * 2. invoke 2 times before creating
 * 3. invoke 1 times before running
 * 4. invoke 2 times after running
 * 5. invoke 1 times before fatal
 * 6. invoke 1 times after fatal
 * Expectation: all invoke return failed
 */
TEST_F(InstanceProxyTest, CallDeleteInstance)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    CallTest(callerIns, calleeIns, true);
    auto calleeInfo = instanceInfo_[calleeIns];
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::FAILED, local_);
    observer_->Update(calleeIns, calleeInfo);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    // invoke 1 times after failed
    auto fisrtCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-after-failed"), nullptr);

    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::EVICTING, local_);
    calleeInfo.mutable_instancestatus()->set_errcode(common::ERR_INSTANCE_NOT_FOUND);
    observer_->Update(calleeIns, calleeInfo);

    // invoke 1 times after failed
    auto secondCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-after-exiting"), nullptr);

    observer_->Delete(calleeIns);

    ASSERT_AWAIT_SET(fisrtCall);
    EXPECT_TRUE(fisrtCall.Get()->has_callrsp() && fisrtCall.Get()->callrsp().code() == common::ERR_INSTANCE_EXITED);

    ASSERT_AWAIT_SET(secondCall);
    EXPECT_TRUE(secondCall.Get()->has_callrsp() && secondCall.Get()->callrsp().code() == common::ERR_INSTANCE_NOT_FOUND);

    auto thirdCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                     CallRequest(callerIns, calleeIns, "Request-after-delete"), nullptr);
    ASSERT_AWAIT_SET(thirdCall);
    EXPECT_TRUE(thirdCall.Get()->has_callrsp() && thirdCall.Get()->callrsp().code() == common::ERR_INSTANCE_NOT_FOUND);
}

/**
 * Feature: invoke sub-health instance test
 * Description: invoke sub-health instance
 * 1. instance scheduling -> creating (local) -> running -> sub-health -> running -> delete
 * 2. invoke 2 times before creating
 * 3. invoke 1 time before running
 * 4. invoke 2 times after running
 * 5. invoke 1 time before sub-health
 * 6. invoke 1 time after sub-health
 * 7. invoke 1 time after recover from sub-health
 * Expectation: all invoke return failed
 */
TEST_F(InstanceProxyTest, CallSubHealthInstance)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    CallTest(callerIns, calleeIns, true);
    auto calleeInfo = instanceInfo_[calleeIns];
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::SUB_HEALTH, local_);
    calleeInfo.mutable_instancestatus()->set_msg("sub-health");
    calleeInfo.mutable_instancestatus()->set_errcode(StatusCode::ERR_INSTANCE_SUB_HEALTH);
    observer_->Update(calleeIns, calleeInfo);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    // invoke 1 times after sub-health
    auto firstCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-after-sub-health"), nullptr);

    ASSERT_AWAIT_READY(firstCall);
    EXPECT_TRUE(firstCall.Get()->has_callrsp());
    EXPECT_EQ(firstCall.Get()->callrsp().code(), common::ERR_INSTANCE_SUB_HEALTH);

    auto mockCalleeSharedClient = std::make_shared<MockSharedClient>();
    EXPECT_CALL(*mockSharedClientManagerProxy_, NewDataInterfacePosixClient(calleeIns, _, _))
        .WillOnce(Return(mockCalleeSharedClient));
    EXPECT_CALL(*mockCalleeSharedClient, Call(_))
        .WillRepeatedly(Invoke([](const SharedStreamMsg &request) -> litebus::Future<SharedStreamMsg> {
            auto msg = std::make_shared<runtime_rpc::StreamingMessage>();
            auto callrsp = msg->mutable_callrsp();
            callrsp->set_code(::common::ErrorCode::ERR_NONE);
            return msg;
        }));
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::RUNNING, local_);
    observer_->Update(calleeIns, calleeInfo);

    // invoke 1 times after recover from sub-health
    auto secondCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                     CallRequest(callerIns, calleeIns, "Request-after-health"), nullptr);

    ASSERT_AWAIT_READY(secondCall);
    EXPECT_TRUE(secondCall.Get()->has_callrsp());
    EXPECT_EQ(secondCall.Get()->callrsp().code(), common::ERR_NONE);

    observer_->Delete(calleeIns);
}

/**
 * Feature: invoke evicted instance test
 * Description: invoke sub-health instance
 * 1. instance scheduling -> creating (local) -> running -> sub-health -> running -> delete
 * 2. invoke 2 times before creating
 * 3. invoke 1 time before running
 * 4. invoke 2 times after running
 * 5. invoke 1 time when evicting
 * 6. invoke 1 time after evicted
 * Expectation: all invoke return failed
 */
TEST_F(InstanceProxyTest, CallEvictedInstance)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    CallTest(callerIns, calleeIns, true);
    auto calleeInfo = instanceInfo_[calleeIns];
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::EVICTING, local_);
    calleeInfo.mutable_instancestatus()->set_errcode(ERR_INSTANCE_EVICTED);
    observer_->Update(calleeIns, calleeInfo);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    // invoke 1 times when evicting
    auto fisrtCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-when-evicting"), nullptr);

    ASSERT_AWAIT_SET(fisrtCall);
    EXPECT_TRUE(fisrtCall.Get()->has_callrsp() &&
                static_cast<functionsystem::StatusCode>(fisrtCall.Get()->callrsp().code()) == ERR_INSTANCE_EVICTED);

    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::EVICTED, local_);
    calleeInfo.mutable_instancestatus()->set_errcode(ERR_INSTANCE_EVICTED);
    observer_->Update(calleeIns, calleeInfo);
    // invoke 1 times after evicted
    auto secondCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-after-evicted"), nullptr);

    ASSERT_AWAIT_SET(secondCall);
    EXPECT_TRUE(secondCall.Get()->has_callrsp() &&
                static_cast<functionsystem::StatusCode>(secondCall.Get()->callrsp().code()) == ERR_INSTANCE_EVICTED);

    observer_->Delete(calleeIns);
}

TEST_F(InstanceProxyTest, CallResultWithoutCaller)
{
    auto calleeInfo = NewInstance("calleeIns", tenantID_);
    observer_->Update("calleeIns", calleeInfo);
    litebus::AID calleeProxy("calleeIns", observer_->GetAID().Url());
    // update instance to running
    UpdateInstance(calleeInfo, "calleeIns", (int32_t)InstanceState::RUNNING, local_);
    observer_->Update("calleeIns", calleeInfo);
    auto callResultAck =
        litebus::Async(calleeProxy, &InstanceProxy::CallResult, "calleeIns", "callerIns",
                       CallResult("callerIns", "Request-1"), nullptr);
    ASSERT_AWAIT_READY(callResultAck);
    EXPECT_TRUE(callResultAck.Get()->has_callresultack());
    EXPECT_EQ(callResultAck.Get()->callresultack().code(), ::common::ErrorCode::ERR_INSTANCE_NOT_FOUND);
}

/**
 * Feature: invoke test
 * Description: invoke fatal instance
 * 1. instance fatal
 * 2. invoke 1 times after fatal
 * Expectation: all invoke return failed
 */
TEST_F(InstanceProxyTest, CallAlreadyFatal)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";
    auto calleeInfo = NewInstance(calleeIns, tenantID_);
    // update instance to FATAL
    UpdateInstance(calleeInfo, calleeIns, (int32_t)InstanceState::FATAL, local_);
    calleeInfo.mutable_instancestatus()->set_errcode((int32_t)common::ERR_USER_FUNCTION_EXCEPTION);
    observer_->Update(calleeIns, calleeInfo);
    auto mockSharedClient = PrepareCaller(callerIns);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    ASSERT_AWAIT_TRUE([&]() { return litebus::GetActor(callerProxy) != nullptr; });
    // invoke 1 times after failed
    auto fisrtCall = litebus::Async(callerProxy, &InstanceProxy::Call, busproxy::CallerInfo{.instanceID=callerIns}, calleeIns,
                                    CallRequest(callerIns, calleeIns, "Request-after-failed"), nullptr);
    ASSERT_AWAIT_SET(fisrtCall);
    EXPECT_TRUE(fisrtCall.Get()->has_callrsp() && fisrtCall.Get()->callrsp().code() == common::ERR_USER_FUNCTION_EXCEPTION);
}

class ForwardCallActor : public litebus::ActorBase {
public:
    ForwardCallActor() : litebus::ActorBase("ForwardCall-test-actor")
    {
    }
    virtual ~ForwardCallActor() = default;

    void SendForwardCall(const litebus::AID &aid, const SharedStreamMsg &streamMsg)
    {
        (void)Send(aid, "ForwardCall", streamMsg->SerializeAsString());
    }
};

TEST_F(InstanceProxyTest, ForwardCallWithoutCallee)
{
    std::string callerIns = "callerIns";
    std::string calleeIns = "calleeIns";

    auto mockSharedClient = PrepareCaller(callerIns);
    litebus::AID callerProxy(callerIns, observer_->GetAID().Url());
    ASSERT_AWAIT_TRUE([&]() { return litebus::GetActor(callerProxy) != nullptr; });

    auto forwardCallActor = std::make_shared<ForwardCallActor>();
    litebus::Spawn(forwardCallActor);

    bool isFinished = false;
    EXPECT_CALL(*observer_, SendSubscribeInstanceEvent(_, _, true))
        .WillOnce(DoAll(Assign(&isFinished, true), Return(Status::OK())));
    litebus::Async(forwardCallActor->GetAID(), &ForwardCallActor::SendForwardCall, callerProxy,
                   CallRequest(callerIns, calleeIns, "Request-not-existed-instance"));
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });

    litebus::Terminate(forwardCallActor->GetAID());
    litebus::Await(forwardCallActor->GetAID());
}

}  // namespace functionsystem::test