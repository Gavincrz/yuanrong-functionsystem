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

#include "iam_driver.h"

#include <utility>

#include "common/constants/actor_name.h"
#include "common/meta_store_adapter/meta_store_monitor_factory.h"

namespace functionsystem::iamserver {
IAMDriver::IAMDriver(functionsystem::iamserver::IAMStartParam param,
                     const std::shared_ptr<MetaStoreClient> &metaClient)
    : param_(std::move(param))
{
    internalIAM_ = std::make_shared<InternalIAM>(param_.internalIAMParam);
    internalIAM_->BindMetaStoreClient(metaClient);
}

Status IAMDriver::Start()
{
    YRLOG_INFO("Start IAMDriver, nodeID: {}, ip: {}", param_.nodeID, param_.ip);

    // create
    if (auto status(Create()); status != StatusCode::SUCCESS) {
        YRLOG_ERROR("IAMDriver start Create failed, err: {}", status.ToString());
        return status;
    }

    // register url-handler to http server
    if (auto registerStatus(httpServer_->RegisterRoute(iamActor_)); registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("IAMDriver start RegisterRoute failed, err: {}", registerStatus.ToString());
        return registerStatus;
    }

    // spawn actors
    litebus::Spawn(httpServer_);
    litebus::Spawn(iamActor_);
    YRLOG_INFO("IAMDriver start success");
    return Status::OK();
}

Status IAMDriver::Create()
{
    if (internalIAM_->IsIAMEnabled() && !internalIAM_->Init()) {
        YRLOG_ERROR("failed to start internal IAM");
        return Status(StatusCode::FAILED, "start internal IAM failed.");
    }

    // create iam actor
    iamActor_ = std::make_shared<IAMActor>(IAM_ACTOR);
    if (iamActor_ == nullptr) {
        return Status(StatusCode::FAILED, "create iam actor failed.");
    }
    iamActor_->BindInternalIAM(internalIAM_);
    iamActor_->SetAuthKey();
    auto metaStoreObserver = std::make_shared<IAMMetaStoreObserver>(iamActor_);
    auto monitor = MetaStoreMonitorFactory::GetInstance().GetMonitor(param_.metaStoreAddress);
    if (monitor != nullptr) {
        monitor->RegisterHealthyObserver(metaStoreObserver);
    }
    // create http server
    httpServer_ = std::make_shared<HttpServer>("iam-server");
    if (httpServer_ == nullptr) {
        return Status(StatusCode::FAILED, "create http server failed.");
    }
    if (auto registerStatus(httpServer_->RegisterRoute(std::make_shared<DefaultHealthyRouter>(param_.nodeID)));
        registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register health check api router failed.");
    }
    return Status::OK();
}

Status IAMDriver::Stop()
{
    if (httpServer_ != nullptr) {
        litebus::Terminate(httpServer_->GetAID());
    }

    if (iamActor_ != nullptr) {
        litebus::Terminate(iamActor_->GetAID());
    }

    internalIAM_ = nullptr;

    return Status::OK();
}

void IAMDriver::Await()
{
    if (httpServer_ != nullptr) {
        litebus::Await(httpServer_->GetAID());
    }

    if (iamActor_ != nullptr) {
        litebus::Await(iamActor_->GetAID());
    }
}
} // functionsystem::iamserver