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

#include "etcd_service_driver.h"

#include <thread>

#include "async/async.hpp"
#include "common/logs/logging.h"
#include "grpcpp/server_builder.h"
#include "stubs/etcd_service/etcd_kv_service.h"
#include "stubs/etcd_service/etcd_lease_service.h"
#include "stubs/etcd_service/etcd_watch_service.h"

namespace functionsystem::meta_store::test {
void EtcdServiceDriver::StartServer(const std::string &address)
{
    // stop server before start server.
    StopServer();

    kvActor_ = std::make_shared<meta_store::KvServiceActor>();
    litebus::AID kvActorAid = litebus::Spawn(kvActor_);

    kvAccessorActor_ = std::make_shared<meta_store::KvServiceAccessorActor>(kvActorAid);
    litebus::Spawn(kvAccessorActor_);

    leaseActor_ = std::make_shared<meta_store::LeaseServiceActor>(kvActorAid);
    litebus::AID leaseActorAid = litebus::Spawn(leaseActor_);

    litebus::Async(kvActorAid, &meta_store::KvServiceActor::AddLeaseServiceActor, leaseActorAid).Get();
    litebus::Async(leaseActorAid, &meta_store::LeaseServiceActor::Start).Get();

    litebus::Promise<bool> promise;
    std::thread thread = std::thread([&address, &promise, this]() {
        EtcdKvService kvService(kvActor_);
        EtcdWatchService watchService(kvActor_);
        EtcdLeaseService leaseService(leaseActor_);

        ::grpc::ServerBuilder builder;
        builder.RegisterService(&kvService);
        builder.RegisterService(&leaseService);
        builder.RegisterService(&watchService);

        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        std::unique_ptr<grpc::Server> server = builder.BuildAndStart();  // start server
        server_ = std::move(server);                                     // unique_ptr to shared_ptr
        promise.SetValue(true);                                          // finish init

        server_->Wait();
    });
    thread.detach();
    promise.GetFuture().Get();  // wait for init
    YRLOG_DEBUG("MetaStoreService start successfully.");
}

void EtcdServiceDriver::StopServer()
{
    if (server_ != nullptr) {
        server_->Shutdown(std::chrono::system_clock::now());
        server_ = nullptr;
    }

    if (leaseActor_ != nullptr) {
        litebus::Async(leaseActor_->GetAID(), &meta_store::LeaseServiceActor::Stop).Get();
        litebus::Terminate(leaseActor_->GetAID());  // after Stop
        litebus::Await(leaseActor_->GetAID());
    }

    if (kvAccessorActor_ != nullptr) {
        litebus::Terminate(kvAccessorActor_->GetAID());
        litebus::Await(kvAccessorActor_->GetAID());
    }

    if (kvActor_ != nullptr) {
        litebus::Terminate(kvActor_->GetAID());
        litebus::Await(kvActor_->GetAID());
    }
}
}  // namespace functionsystem::meta_store::test
