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

#ifndef UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_LOCAL_SCHED_SRV_ACTOR_TEST_DRIVER_H
#define UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_LOCAL_SCHED_SRV_ACTOR_TEST_DRIVER_H

#include <actor/actor.hpp>
#include <async/future.hpp>

#include "common/explorer/explorer.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem::test {
class LocalSchedSrvActorTestDriver : public litebus::ActorBase {
public:
    LocalSchedSrvActorTestDriver() : ActorBase("LocalSchedSrvActorTestDriver"){};
    ~LocalSchedSrvActorTestDriver() override = default;

    // test Scheduler interface of LocalSchedSrvActor
    litebus::Future<messages::ScheduleResponse> Schedule(const litebus::AID &to, const messages::ScheduleRequest &req);
    void ResponseSchedule(const litebus::AID &from, std::string &&name, std::string &&msg);

    // test UpdateSchedTopoView interface of LocalSchedSrvActor
    void UpdateSchedTopoView(const litebus::AID &to, const messages::ScheduleTopology &topo);
    litebus::Future<litebus::AID> GetDomainSchedulerAID(const litebus::AID &to);

    // test Register interface of LocalSchedSrvActor
    litebus::Future<Status> Register(const litebus::AID &to);

    // test NotifyDsHealthy interface of LocalSchedSrvActor
    litebus::Future<Status> NotifyDsHealthy(const litebus::AID &to, bool healthy);

    // test ForwardSchedule interface of LocalSchedSrvActor
    litebus::Future<messages::ScheduleResponse> ForwardSchedule(const litebus::AID &to,
                                                                const std::shared_ptr<messages::ScheduleRequest> &req);

    litebus::Future<Status> TryCancelSchedule(const litebus::AID &to,
                                              const std::shared_ptr<messages::CancelSchedule> &req);

    void UpdateMasterInfo(const litebus::AID &to, const explorer::LeaderInfo &leaderInfo);

protected:
    void Init() override;
    void Finalize() override
    {
    }

private:
    std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleRspPromise_;
};

}  // namespace functionsystem::test
#endif  // UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_LOCAL_SCHED_SRV_ACTOR_TEST_DRIVER_H
