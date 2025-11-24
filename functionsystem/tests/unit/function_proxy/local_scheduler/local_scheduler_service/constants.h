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

#ifndef UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_CONSTANTS_H
#define UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_CONSTANTS_H

#include "common/status/status.h"

namespace functionsystem::test {
const std::string REGISTERED_GLOBAL_SCHED_SUCCESS_MSG = "register global scheduler successful";
const std::string REGISTERED_DOMAIN_SCHED_SUCCESS_MSG = "register domain scheduler successful";

const std::string REGISTERED_GLOBAL_SCHED_FAILED_MSG = "register global scheduler failed";
const std::string REGISTERED_DOMAIN_SCHED_FAILED_MSG = "register domain scheduler failed";

const std::string REGISTERED_DOMAIN_SCHED_NAME = "domain_scheduler_1";
const std::string REGISTERED_DOMAIN_SCHED_ADDRESS = "127.0.0.3";
}

#endif  // UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_CONSTANTS_H
