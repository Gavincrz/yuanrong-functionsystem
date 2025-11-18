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

#ifndef OBSERVABILITY_METRICS_COMMON_LOGS_LOGGING_H
#define OBSERVABILITY_METRICS_COMMON_LOGS_LOGGING_H

#include <csignal>

#include "logs/api/provider.h"

#define METRICS_LOG_DEBUG(...) LOGS_CORE_LOGGER(LOGS_LEVEL_DEBUG, __VA_ARGS__)
#define METRICS_LOG_INFO(...) LOGS_CORE_LOGGER(LOGS_LEVEL_INFO, __VA_ARGS__)
#define METRICS_LOG_WARN(...) LOGS_CORE_LOGGER(LOGS_LEVEL_WARN, __VA_ARGS__)
#define METRICS_LOG_ERROR(...) LOGS_CORE_LOGGER(LOGS_LEVEL_ERROR, __VA_ARGS__)
#define METRICS_LOG_FATAL(...) LOGS_CORE_LOGGER(MetricsGetCoreLogger(), LOGS_LEVEL_FATAL, __VA_ARGS__)

#endif