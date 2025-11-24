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

#include <grpcpp/channel.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>

#include "common/logs/logging.h"
#include "common/posix_client/shared_client/shared_client_manager.h"
#include "common/posix_client/shared_client/posix_stream_manager_proxy.h"
#include "common/proto/pb/posix/bus_service.grpc.pb.h"
#include "common/proto/pb/posix/bus_service.pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/status/status.h"
#include "function_proxy/common/iam/internal_iam.h"
#include "gmock/gmock-actions.h"
#include "gmock/gmock-generated-actions.h"
#include "gmock/gmock-more-actions.h"
#include "gmock/gmock-spec-builders.h"
#include "grpc_server/bus_service/bus_service.h"
#include "instance_control/instance_ctrl_actor.h"
#include "mocks/mock_control_interface_client_manager_proxy.h"
#include "mocks/mock_function_agent_mgr.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_observer.h"
#include "mocks/mock_shared_client.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_resource_view.h"
#include "mocks/mock_local_sched_srv.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace local_scheduler;
using namespace ::testing;

class GrpcServerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        functionsystem::function_proxy::InternalIAM::Param param;
        param.isEnableIAM = false;
        internalIAM_ = std::make_shared<functionsystem::function_proxy::InternalIAM>(param);
        controlPlaneObserver_ = std::make_shared<MockObserver>();
        sharedClientMgr_ = std::make_shared<SharedClientManager>("SharedPosixClientMgr");
        litebus::Spawn(sharedClientMgr_);
        sharedPosixClientManager_ = std::make_shared<PosixStreamManagerProxy>(sharedClientMgr_->GetAID());
        RuntimeConfig runtimeConfig{ .runtimeHeartbeatEnable = "true",
                                     .runtimeMaxHeartbeatTimeoutTimes = 3,
                                     .runtimeHeartbeatTimeoutMS = 2000,
                                     .runtimeInitCallTimeoutMS = 3000,
                                     .runtimeShutdownTimeoutSeconds = 3};
        InstanceCtrlConfig instanceCtrlconfig{};
        instanceCtrlconfig.runtimeConfig = runtimeConfig;
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        std::string etcdAddress = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        auto metaClient = std::make_shared<MockMetaStoreClient>(etcdAddress);
        funcAgentMgr_ = std::make_shared<MockFunctionAgentMgr>("funcAgentMgr", std::move(metaClient));
        resourceViewMgr_ = std::make_shared<resource_view::ResourceViewMgr>();
        resourceViewMgr_->primary_ = MockResourceView::CreateMockResourceView();
        resourceViewMgr_->virtual_ = MockResourceView::CreateMockResourceView();
        auto nodeID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        instanceCtrl_ = InstanceCtrl::Create(nodeID, instanceCtrlconfig);
        instanceCtrl_->Start(funcAgentMgr_, resourceViewMgr_, controlPlaneObserver_);
        instanceCtrl_->BindInternalIAM(internalIAM_);
        mockLocalSchedSrv_ = std::make_shared<MockLocalSchedSrv>();
    }
    void TearDown() override
    {
        litebus::Terminate(sharedClientMgr_->GetAID());
        litebus::Await(sharedClientMgr_->GetAID());

        internalIAM_ = nullptr;
        controlPlaneObserver_ = nullptr;
        resourceViewMgr_ = nullptr;
        funcAgentMgr_ = nullptr;
        instanceCtrl_ = nullptr;
        sharedPosixClientManager_ = nullptr;
        sharedClientMgr_ = nullptr;
        mockLocalSchedSrv_ = nullptr;
    }

protected:
    std::shared_ptr<functionsystem::test::MockObserver> controlPlaneObserver_;
    std::shared_ptr<PosixStreamManagerProxy> sharedPosixClientManager_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
    std::shared_ptr<MockLocalSchedSrv> mockLocalSchedSrv_;
    std::shared_ptr<functionsystem::function_proxy::InternalIAM> internalIAM_{ nullptr };
    std::shared_ptr<SharedClientManager> sharedClientMgr_;

    std::shared_ptr<MockFunctionAgentMgr> funcAgentMgr_;
    std::shared_ptr<ResourceViewMgr> resourceViewMgr_;
};

/**
 * Feature: GrpcServerTest DiscoverDriverStatus
 * Description: Call DiscoverDriver service.
 * Steps:
 * 1. Create a DiscoverDriverRequest.
 * 2. Call DiscoverDriver service.
 * 3. Mock PutInstance returning Status with error code.
 * 4. Call DiscoverDriver service again.
 *
 * Expectation:
 * 1. Get a grpc::Status with OK StatusCode.
 * 2. Get a grpc::Status with ERR_INNER_SYSTEM_ERROR StatusCode
 *    after call service again .
 */
TEST_F(GrpcServerTest, DiscoverDriverStatus)
{
    // stub
    auto mockControlInterfaceClientManagerProxy = std::make_shared<MockControlInterfaceClientManagerProxy>();
    RuntimeConfig runtimeConfig{ .runtimeHeartbeatEnable = "true",
                                 .runtimeMaxHeartbeatTimeoutTimes = 3,
                                 .runtimeHeartbeatTimeoutMS = 2000,
                                 .runtimeInitCallTimeoutMS = 3000,
                                 .runtimeShutdownTimeoutSeconds = 3};
    InstanceCtrlConfig instanceCtrlconfig{};
    instanceCtrlconfig.runtimeConfig = runtimeConfig;
    auto mockInstanceCtrl = std::make_shared<MockInstanceCtrl>(
        std::make_shared<InstanceCtrlActor>("mockInstanceCtrl", "nodeID", instanceCtrlconfig));
    mockInstanceCtrl->BindControlInterfaceClientManager(mockControlInterfaceClientManagerProxy);
    mockInstanceCtrl->BindInternalIAM(internalIAM_);
    litebus::Future<Status> statusFut;
    statusFut.SetValue(Status());
    EXPECT_CALL(*controlPlaneObserver_, PutInstance).WillOnce(Return(statusFut));

    // 1. Create a DiscoverDriverRequest.
    bus_service::DiscoverDriverRequest request = bus_service::DiscoverDriverRequest();
    request.set_driverip("127.0.0.1");
    request.set_driverport("21011");
    request.set_jobid("jobID");

    // 2. Call DiscoverDriver service.
    BusServiceParam param{
        .nodeID = "nodeID",
        .controlPlaneObserver = controlPlaneObserver_,
        .controlInterfaceClientMgr = mockControlInterfaceClientManagerProxy,
        .instanceCtrl = mockInstanceCtrl,
        .localSchedSrv = mockLocalSchedSrv_,
        .isEnableServerMode = true,
        .hostIP = "127.0.0.1",
    };
    EXPECT_CALL(*mockLocalSchedSrv_, IsRegisteredToGlobal).WillRepeatedly(Return(Status::OK()));
    auto service = BusService(std::move(param));
    ::grpc::ServerContext context;
    ::bus_service::DiscoverDriverResponse response;
    ::grpc::Status status = service.DiscoverDriver(&context, &request, &response);
    EXPECT_EQ(status.ok(), true);
    EXPECT_EQ(response.nodeid(), "nodeID");
    EXPECT_EQ(response.hostip(), "127.0.0.1");

    // 3. Mock PutInstance returning Status with error code.
    statusFut = litebus::Future<Status>();
    statusFut.SetValue(Status(Status(StatusCode::FAILED, "")));
    EXPECT_CALL(*controlPlaneObserver_, PutInstance).WillOnce(Return(statusFut));

    // 4. Call DiscoverDriver service again.
    status = service.DiscoverDriver(&context, &request, &response);
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INTERNAL);
};

TEST_F(GrpcServerTest, DiscoverDriverFail)
{
    bus_service::DiscoverDriverRequest request = bus_service::DiscoverDriverRequest();
    BusServiceParam param{};
    auto service = BusService(std::move(param));
    ::grpc::ServerContext context;
    ::bus_service::DiscoverDriverResponse response;
    ::grpc::Status status = service.DiscoverDriver(&context, &request, &response);
    EXPECT_EQ(status.ok(), false);
};

TEST_F(GrpcServerTest, DiscoverDriverWaitRegisteredTimeout)
{
    // 2. Call DiscoverDriver service.
    BusServiceParam param{
        .nodeID = "nodeID",
        .controlPlaneObserver = controlPlaneObserver_,
        .controlInterfaceClientMgr = nullptr,
        .instanceCtrl = nullptr,
        .localSchedSrv = mockLocalSchedSrv_,
        .isEnableServerMode = true,
    };
    EXPECT_CALL(*mockLocalSchedSrv_, IsRegisteredToGlobal).WillOnce(Invoke([](){
        return litebus::Future<Status>();
    }));
    auto service = BusService(std::move(param));
    service.waitRegisteredTimeout_ = 10;
    ::grpc::ServerContext context;
    // 1. Create a DiscoverDriverRequest.
    bus_service::DiscoverDriverRequest request = bus_service::DiscoverDriverRequest();
    request.set_driverip("127.0.0.1");
    request.set_driverport("21011");
    request.set_jobid("jobID");
    ::bus_service::DiscoverDriverResponse response;
    ::grpc::Status status = service.DiscoverDriver(&context, &request, &response);
    EXPECT_EQ(status.ok(), false);
    EXPECT_EQ(status.error_message(), "function_proxy is not ready for driver register");
};

}  // namespace functionsystem::test