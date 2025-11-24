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

#include "etcd_watch_srv_actor.h"

#include "common/logs/logging.h"
#include "meta_store_common.h"

namespace functionsystem::meta_store::test {
bool EtcdWatchSrvActor::Cancel(Stream *grpcStream, const ::etcdserverpb::WatchCancelRequest &req)
{
    const int64_t watchId = req.watch_id();
    if (observers_.find(grpcStream) != observers_.end()) {
        if (auto observer(observers_[grpcStream].find(watchId)); observer != observers_[grpcStream].end()) {
            observers_[grpcStream].erase(observer);
            if (observers_[grpcStream].empty()) {
                observers_.erase(grpcStream);
            }
        }
    }
    YRLOG_DEBUG("start process cancel, watch id: {}, this: {}", watchId, (void *)this);

    ::etcdserverpb::WatchResponse response;
    response.set_cancel_reason("by user");
    response.set_canceled(true);

    return grpcStream->Write(response);
}

bool EtcdWatchSrvActor::Create(Stream *grpcStream, const ::etcdserverpb::WatchCreateRequest &request)
{
    int64_t index;
    if (indexByClient_.find(grpcStream) != indexByClient_.end()) {
        index = indexByClient_[grpcStream];
        indexByClient_[grpcStream]++;
    } else {
        index = 0;
        indexByClient_[grpcStream] = 1;
    }
    observers_[grpcStream][index] = request;
    YRLOG_DEBUG("start process create, watch id: {}, key: {}, stream: {}, this: {}", index, request.key(),
                (void *)grpcStream, (void *)this);

    ::etcdserverpb::WatchResponse response;
    response.set_watch_id(index);
    response.set_created(true);

    return grpcStream->Write(response);
}

bool EtcdWatchSrvActor::RemoveClient(Stream *grpcStream)
{
    if (observers_.find(grpcStream) != observers_.end()) {
        observers_.erase(grpcStream);
    }
    if (indexByClient_.find(grpcStream) != indexByClient_.end()) {
        indexByClient_.erase(grpcStream);
    }
    return true;
}

bool EtcdWatchSrvActor::Response(Stream *grpcStream)
{
    ::etcdserverpb::WatchResponse response;
    YRLOG_DEBUG("start process response");
    return grpcStream->Write(response);
}

void EtcdWatchSrvActor::OnPut(const mvccpb::KeyValue &kv, const mvccpb::KeyValue &prevKv)
{
    YRLOG_DEBUG("start process onPut, key: {}, observers size: {}, this: {}", kv.key(), observers_.size(),
                (void *)this);
    for (auto &observer : observers_) {
        const auto &client = observer.first;
        for (auto &watcher : observer.second) {
            const auto &watchId = watcher.first;
            const auto &request = watcher.second;
            if (std::find(request.filters().begin(), request.filters().end(),
                          ::etcdserverpb::WatchCreateRequest_FilterType::WatchCreateRequest_FilterType_NOPUT)
                != request.filters().end()) {
                continue;
            }

            YRLOG_DEBUG("OnPut, filter for key: {}, request key: {}, request range end: {}", kv.key(), request.key(),
                        request.range_end());
            if (request.range_end().empty()) {
                if (kv.key() != request.key()) {
                    continue;
                }
            } else {
                if (kv.key() < request.key() || kv.key() >= request.range_end()) {
                    continue;
                }
            }

            YRLOG_DEBUG("OnPut, watch id: {}, key: {}", watchId, request.key());
            ::etcdserverpb::WatchResponse response;
            auto header = response.mutable_header();
            header->set_cluster_id(META_STORE_CLUSTER_ID);

            response.set_watch_id(watchId);
            ::mvccpb::Event *event = response.add_events();
            event->set_type(::mvccpb::Event_EventType::Event_EventType_PUT);

            mvccpb::KeyValue *mutableKv = event->mutable_kv();
            mutableKv->set_key(kv.key());
            mutableKv->set_lease(kv.lease());
            mutableKv->set_value(kv.value());
            mutableKv->set_version(kv.version());
            mutableKv->set_mod_revision(kv.mod_revision());
            mutableKv->set_create_revision(kv.create_revision());

            if (request.prev_kv()) {
                mvccpb::KeyValue *mutablePrevKv = event->mutable_prev_kv();
                mutablePrevKv->set_key(prevKv.key());
                mutablePrevKv->set_lease(prevKv.lease());
                mutablePrevKv->set_version(prevKv.version());
                mutablePrevKv->set_value(prevKv.value());
                mutablePrevKv->set_mod_revision(prevKv.mod_revision());
                mutablePrevKv->set_create_revision(prevKv.create_revision());
            }

            if (client != nullptr) {
                client->Write(response);
            }
        }
    }
}

void EtcdWatchSrvActor::OnDeleteList(std::shared_ptr<std::vector<::mvccpb::KeyValue>> kvs)
{
    YRLOG_DEBUG("start process OnDeleteList, this: {}", (void *)this);
    for (auto &observer : observers_) {
        const auto &client = observer.first;
        for (auto &watcher : observer.second) {
            const auto &watchId = watcher.first;
            const auto &request = watcher.second;
            if (std::find(request.filters().begin(), request.filters().end(),
                          ::etcdserverpb::WatchCreateRequest_FilterType::WatchCreateRequest_FilterType_NODELETE)
                != request.filters().end()) {
                continue;
            }

            ::etcdserverpb::WatchResponse response;
            auto header = response.mutable_header();
            header->set_cluster_id(META_STORE_CLUSTER_ID);

            response.set_watch_id(watchId);
            for (const auto &item : *kvs) {
                if (request.range_end().empty()) {
                    if (item.key() != request.key()) {
                        continue;
                    }
                } else {
                    if (item.key() < request.key() || item.key() >= request.range_end()) {
                        continue;
                    }
                }

                ::mvccpb::Event *event = response.add_events();
                event->set_type(::mvccpb::Event_EventType::Event_EventType_DELETE);

                mvccpb::KeyValue *mutableKv = event->mutable_kv();
                mutableKv->set_key(item.key());
                mutableKv->set_mod_revision(item.mod_revision() + 1);

                if (request.prev_kv()) {
                    mvccpb::KeyValue *mutablePrevKv = event->mutable_prev_kv();
                    mutablePrevKv->set_key(item.key());
                    mutablePrevKv->set_value(item.value());
                    mutablePrevKv->set_version(item.version());
                    mutablePrevKv->set_mod_revision(item.mod_revision());
                    mutablePrevKv->set_lease(item.lease());
                    mutablePrevKv->set_create_revision(item.create_revision());
                }
            }

            if (client != nullptr) {
                client->Write(response);
            }
        }
    }
}

void EtcdWatchSrvActor::OnDelete(const mvccpb::KeyValue &prevKv)
{
    YRLOG_DEBUG("start process OnDelete, key: {}, this: {}", prevKv.key(), (void *)this);
    for (auto &observer : observers_) {
        const auto &client = observer.first;
        for (auto &watcher : observer.second) {
            const auto &watchId = watcher.first;
            const auto &request = watcher.second;
            if (std::find(request.filters().begin(), request.filters().end(),
                          ::etcdserverpb::WatchCreateRequest_FilterType::WatchCreateRequest_FilterType_NODELETE)
                != request.filters().end()) {
                continue;
            }

            if (request.range_end().empty()) {
                if (prevKv.key() != request.key()) {
                    continue;
                }
            } else {
                if (prevKv.key() < request.key() || prevKv.key() >= request.range_end()) {
                    continue;
                }
            }

            ::etcdserverpb::WatchResponse response;
            auto header = response.mutable_header();
            header->set_cluster_id(META_STORE_CLUSTER_ID);

            response.set_watch_id(watchId);
            ::mvccpb::Event *event = response.add_events();
            event->set_type(::mvccpb::Event_EventType::Event_EventType_DELETE);

            mvccpb::KeyValue *mutableKv = event->mutable_kv();
            mutableKv->set_mod_revision(prevKv.mod_revision() + 1);
            mutableKv->set_key(prevKv.key());

            if (request.prev_kv()) {
                mvccpb::KeyValue *mutablePrevKv = event->mutable_prev_kv();
                mutablePrevKv->set_key(prevKv.key());
                mutablePrevKv->set_value(prevKv.value());
                mutablePrevKv->set_lease(prevKv.lease());
                mutablePrevKv->set_version(prevKv.version());
                mutablePrevKv->set_mod_revision(prevKv.mod_revision());
                mutablePrevKv->set_create_revision(prevKv.create_revision());
            }

            if (client != nullptr) {
                client->Write(response);
            }
        }
    }
}
}  // namespace functionsystem::meta_store::test
