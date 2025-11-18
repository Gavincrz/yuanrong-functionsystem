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

#ifndef IAM_SERVER_CONSTANTS_H
#define IAM_SERVER_CONSTANTS_H

namespace functionsystem::iamserver {
const std::string SPLIT_SYMBOL = "_";
const std::string SPLIT_SYMBOL_TIMESTAMP = "+";

const uint32_t CHECK_EXPIRED_INTERVAL = 2 * 60 * 1000;  // unit: ms, check cred every 2 min
const uint32_t TIME_AHEAD_OF_EXPIRED = 10 * 60; // unit: s, update cred 10 min before expired
const int32_t WATCH_TIMEOUT_MS = 30000;

const uint32_t MS_SECOND = 1000;
const uint32_t MIN_AHEAD_TIME_FACTOR = 3;
const uint32_t MIN_EXPIRED_FACTOR = 2;
}

#endif  // IAM_SERVER_CONSTANTS_H
