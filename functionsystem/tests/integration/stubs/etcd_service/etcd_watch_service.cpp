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

#include "etcd_watch_service.h"

#include "async/async.hpp"
#include "common/logs/logging.h"
#include "etcd_watch_srv_actor.h"

namespace functionsystem::meta_store::test {
EtcdWatchService::EtcdWatchService(std::shared_ptr<KvServiceActor> actor) : kvActor_(std::move(actor))
{
    const std::string name = "WatchStreamSrvActor_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    streamActor_ = std::make_shared<EtcdWatchSrvActor>(name);
    litebus::Spawn(streamActor_);
    litebus::Async(kvActor_->GetAID(), &KvServiceActor::AddWatchServiceActor, streamActor_->GetAID()).Get();  // await
}

EtcdWatchService::~EtcdWatchService()
{
    litebus::Async(kvActor_->GetAID(), &KvServiceActor::RemoveWatchServiceActor).Get();  // await
    litebus::Terminate(streamActor_->GetAID());
    litebus::Await(streamActor_->GetAID());
}

::grpc::Status EtcdWatchService::Watch(::grpc::ServerContext *context, Stream *stream)
{
    YRLOG_DEBUG("watch stream: {}", (void *)stream);
    bool running = true;
    ::etcdserverpb::WatchRequest request;
    while (running && stream->Read(&request)) {
        switch (request.request_union_case()) {
            case ::etcdserverpb::WatchRequest::RequestUnionCase::kCreateRequest: {
                if (!litebus::Async(streamActor_->GetAID(), &EtcdWatchSrvActor::Create, stream,
                                    request.create_request())
                         .Get()) {
                    running = false;  // break while
                    break;
                }
                int64_t revision = request.create_request().start_revision();
                litebus::Async(kvActor_->GetAID(), &KvServiceActor::OnCreateWatcher, revision);
                break;
            }
            case ::etcdserverpb::WatchRequest::RequestUnionCase::kCancelRequest: {
                if (!litebus::Async(streamActor_->GetAID(), &EtcdWatchSrvActor::Cancel, stream,
                                    request.cancel_request())
                         .Get()) {
                    running = false;  // break while
                    break;
                }
                break;
            }
            default: {
                // etcdserverpb::WatchRequest::RequestUnionCase::kProgressRequest
                if (!litebus::Async(streamActor_->GetAID(), &EtcdWatchSrvActor::Response, stream).Get()) {
                    running = false;  // break while
                    break;
                }
                break;
            }
        }
    }

    litebus::Async(streamActor_->GetAID(), &EtcdWatchSrvActor::RemoveClient, stream).Get();  // await

    return grpc::Status::OK;
}
}  // namespace functionsystem::meta_store::test
