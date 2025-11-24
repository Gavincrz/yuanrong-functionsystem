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

#ifndef TEST_UNIT_DOMAIN_SCHEDULER_UNDERLAYER_SCHEDULER_MANAGER_UNDERLAYER_STUB_H
#define TEST_UNIT_DOMAIN_SCHEDULER_UNDERLAYER_SCHEDULER_MANAGER_UNDERLAYER_STUB_H

#include <gmock/gmock.h>

#include <async/async.hpp>
#include <litebus.hpp>

#include "common/heartbeat/heartbeat_client.h"
#include "domain_scheduler/underlayer_scheduler_manager/underlayer_sched_mgr_actor.h"

namespace functionsystem::test {

class MockUnderlayer : public litebus::ActorBase {
public:
    MockUnderlayer(const std::string &name) : ActorBase(name)
    {
    }
    ~MockUnderlayer() = default;

    void SendRegister(const litebus::AID &aid)
    {
        messages::Register req;
        req.set_name(GetAID().Name());
        req.set_address(GetAID().UnfixUrl());
        (*req.mutable_resources())[0] = resource_view::ResourceUnit();
        (*req.mutable_resources())[1] = resource_view::ResourceUnit();
        (void)Send(aid, "Register", req.SerializeAsString());
    }

    void SendRequest(const litebus::AID &from, std::string name, std::string msg)
    {
        Send(from, std::move(name), std::move(msg));
    }

    void Registered(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockRegistered(from, name, msg);
    }
    MOCK_METHOD3(MockRegistered, void(const litebus::AID &, std::string, std::string));

    void ResponseForwardSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseForwardSchedule(from, name, msg);
    }
    MOCK_METHOD3(MockResponseForwardSchedule, void(const litebus::AID &, std::string, std::string));

    void Schedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto rsp = MockSchedule(from, name, msg);
        Send(from, "ResponseSchedule", std::move(rsp));
    }
    MOCK_METHOD3(MockSchedule, std::string(const litebus::AID &, std::string, std::string));

    void ResponseNotifySchedAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseNotifySchedAbnormal(from, name, msg);
    }
    MOCK_METHOD3(MockResponseNotifySchedAbnormal, void(const litebus::AID &, std::string, std::string));

    void ResponseNotifyWorkerStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseNotifyWorkerStatus(from, name, msg);
    }
    MOCK_METHOD3(MockResponseNotifyWorkerStatus, void(const litebus::AID &, std::string, std::string));

    void ClosePingPong()
    {
        pingpong_->Stop();
        pingpong_ = nullptr;
    }

    void StartPingPong()
    {
        (void)pingpong_->Start(
            litebus::AID(HEARTBEAT_OBSERVER_BASENAME + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME, GetAID().Url()));
    }

    void DeletePodResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto deletePodResponse = std::make_shared<messages::DeletePodResponse>();
        if (!deletePodResponse->ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse request for DeletePod.");
            return;
        }
        MockDeletePodResponse(deletePodResponse);
    }
    MOCK_METHOD1(MockDeletePodResponse, void(const std::shared_ptr<messages::DeletePodResponse>));

    void PreemptInstance(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        ASSERT_EQ(req->ParseFromString(msg), true);
        messages::EvictAgentAck rsp;
        rsp.set_requestid(req->requestid());
        Send(from, "PreemptInstancesResponse", rsp.SerializeAsString());
        MockPreemptInstanceRequest(req);
    }
    MOCK_METHOD1(MockPreemptInstanceRequest, void(const std::shared_ptr<messages::EvictAgentRequest> &req));

protected:
    void Init() override
    {
        Receive("Registered", &MockUnderlayer::Registered);
        Receive("Schedule", &MockUnderlayer::Schedule);
        Receive("ResponseForwardSchedule", &MockUnderlayer::ResponseForwardSchedule);
        Receive("ResponseNotifySchedAbnormal", &MockUnderlayer::ResponseNotifySchedAbnormal);
        Receive("ResponseNotifyWorkerStatus", &MockUnderlayer::ResponseNotifyWorkerStatus);
        Receive("DeletePodResponse", &MockUnderlayer::DeletePodResponse);
        Receive("PreemptInstances", &MockUnderlayer::PreemptInstance);
        pingpong_ = std::make_unique<HeartbeatClientDriver>(GetAID().Name(), [this](const litebus::AID &lostDst) {});
    }

private:
    std::unique_ptr<HeartbeatClientDriver> pingpong_;
};

class MockLocalGroupCtrl : public litebus::ActorBase {
public:
    MockLocalGroupCtrl(const std::string &name) : litebus::ActorBase(name)
    {
    }
    ~MockLocalGroupCtrl() = default;

    void Reserve(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto rsp = MockReserve();
        Send(from, "OnReserve", std::move(rsp));
    }
    MOCK_METHOD(std::string, MockReserve, ());

    void UnReserve(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto rsp = MockUnReserve();
        Send(from, "OnUnReserve", std::move(rsp));
    }
    MOCK_METHOD(std::string, MockUnReserve, ());

    void Bind(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto rsp = MockBind();
        Send(from, "OnBind", std::move(rsp));
    }
    MOCK_METHOD(std::string, MockBind, ());

    void UnBind(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto rsp = MockUnBind();
        Send(from, "OnUnBind", std::move(rsp));
    }
    MOCK_METHOD(std::string, MockUnBind, ());

protected:
    void Init() override
    {
        Receive("Reserve", &MockLocalGroupCtrl::Reserve);
        Receive("UnReserve", &MockLocalGroupCtrl::UnReserve);
        Receive("Bind", &MockLocalGroupCtrl::Bind);
        Receive("UnBind", &MockLocalGroupCtrl::UnBind);
    }
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_DOMAIN_SCHEDULER_UNDERLAYER_SCHEDULER_MANAGER_UNDERLAYER_STUB_H
