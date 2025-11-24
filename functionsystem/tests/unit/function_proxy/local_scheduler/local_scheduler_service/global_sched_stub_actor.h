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

#ifndef UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_GLOBAL_SCHEDULER_STUB_H
#define UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_GLOBAL_SCHEDULER_STUB_H

#include <gmock/gmock.h>

#include <actor/actor.hpp>
#include <async/future.hpp>

namespace functionsystem::test {
class GlobalSchedStubActor : public litebus::ActorBase {
public:
    explicit GlobalSchedStubActor(const std::string &name) : ActorBase(name)
    {
    }
    ~GlobalSchedStubActor() = default;

    virtual void Register(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("receive register info from({})", std::string(from));
        auto registeredMsg = MockRegister();
        Send(from, "Registered", std::move(registeredMsg));
    }

    virtual void UnRegister(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("receive unRegister info from({})", std::string(from));
        auto registeredMsg = MockUnRegister();
        Send(from, "UnRegistered", std::move(registeredMsg));
    }

    litebus::Future<std::string> SendEvictAgent(const litebus::AID &local, std::string msg)
    {
        evictPromise_ = std::make_shared<litebus::Promise<std::string>>();
        Send(local, "EvictAgent", std::move(msg));
        return evictPromise_->GetFuture();
    }

    void EvictAck(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        evictPromise_->SetValue(msg);
    }

    litebus::Future<std::string> SendPreemptInstance(const litebus::AID &local, std::string msg)
    {
        preemptPromise_ = std::make_shared<litebus::Promise<std::string>>();
        Send(local, "PreemptInstances", std::move(msg));
        return preemptPromise_->GetFuture();
    }

    void PreemptInstanceResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        preemptPromise_->SetValue(msg);
    }

    void NotifyEvictResult(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::EvictAgentResult result;
        result.ParseFromString(msg);
        auto ack = messages::EvictAgentResultAck();
        ack.set_agentid(result.agentid());
        Send(from, "NotifyEvictResultAck", ack.SerializeAsString());
        evictResultPromise_->SetValue(result);
    }

    litebus::Future<messages::EvictAgentResult> InitEvictResult()
    {
        evictResultPromise_ = std::make_shared<litebus::Promise<messages::EvictAgentResult>>();
        return evictResultPromise_->GetFuture();
    }

    void UpdateSchedTopoView(const litebus::AID &to, const messages::ScheduleTopology &topo)
    {
        Send(to, "UpdateSchedTopoView", std::move(topo.SerializeAsString()));
    }

    MOCK_METHOD0(MockRegister, std::string());
    MOCK_METHOD0(MockUnRegister, std::string());

protected:
    void Init() override
    {
        Receive("Register", &GlobalSchedStubActor::Register);
        Receive("UnRegister", &GlobalSchedStubActor::UnRegister);
        Receive("EvictAck", &GlobalSchedStubActor::EvictAck);
        Receive("NotifyEvictResult", &GlobalSchedStubActor::NotifyEvictResult);
        Receive("PreemptInstancesResponse", &GlobalSchedStubActor::PreemptInstanceResponse);
    }
    void Finalize() override
    {
    }
private:
    std::shared_ptr<litebus::Promise<std::string>> evictPromise_;
    std::shared_ptr<litebus::Promise<std::string>> preemptPromise_;
    std::shared_ptr<litebus::Promise<messages::EvictAgentResult>> evictResultPromise_;
};
}  // namespace functionsystem::test

#endif  // UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_GLOBAL_SCHEDULER_STUB_H
