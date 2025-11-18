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

#ifndef IAM_SERVER_DRIVER_IAM_DRIVER_H
#define IAM_SERVER_DRIVER_IAM_DRIVER_H

#include "common/http/http_server.h"
#include "common/utils/module_driver.h"
#include "iam/iam_actor/iam_actor.h"
#include "iam/internal_iam/internal_iam.h"

namespace functionsystem::iamserver {

struct IAMStartParam {
    InternalIAM::Param internalIAMParam;
    std::string nodeID;
    std::string ip;
    std::string metaStoreAddress;
};

class IAMDriver : public ModuleDriver {
public:
    IAMDriver(IAMStartParam param, const std::shared_ptr<MetaStoreClient> &metaClient);
    ~IAMDriver() override = default;

    Status Start() override;
    Status Stop() override;
    void Await() override;

protected:
    Status Create();

private:
    IAMStartParam param_;
    std::shared_ptr<HttpServer> httpServer_;
    std::shared_ptr<InternalIAM> internalIAM_;
    std::shared_ptr<IAMActor> iamActor_;
};
} // functionsystem::iamserver
#endif // IAM_SERVER_DRIVER_IAM_DRIVER_H