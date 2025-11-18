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
#ifndef OBSERVABILITY_METRIC_LOGGER_H
#define OBSERVABILITY_METRIC_LOGGER_H

#include <thread>

#include <spdlog/spdlog.h>

#include "common/include/singleton.h"

namespace observability {
namespace metrics {

const unsigned int DEFAULT_MAX_ASYNC_QUEUE_SIZE = 1024;
const unsigned int DEFAULT_ASYNC_THREAD_COUNT = 1;
const uint32_t DEFAULT_MAX_FILE_NUM = 3;
const int64_t SIZE_MEGA_BYTES = 1024 * 1024;                // 1 MB
const int DEFAULT_MAX_SIZE = 100;                           // 100 MB
const std::string DEFAULT_FILE_NAME = "yr_metrics";

struct FileParam {
    std::string fileDir;                                            // file path
    std::string fileName = DEFAULT_FILE_NAME;                       // file name
    uint32_t maxFileNum = DEFAULT_MAX_FILE_NUM;                     // max file numbers
    int64_t maxSize = DEFAULT_MAX_SIZE * SIZE_MEGA_BYTES;           // single file max size// old log file keep days
    unsigned int maxAsyncQueueSize = DEFAULT_MAX_ASYNC_QUEUE_SIZE;  // spd config for queue size
    unsigned int asyncThreadCount = DEFAULT_ASYNC_THREAD_COUNT;     // spd config for thread count
};

class MetricLogger : public Singleton<MetricLogger> {
public:
    explicit MetricLogger(const FileParam &fileParam);
    ~MetricLogger() override;
    void Record(const std::string &metricString);
    void Flush() noexcept;

private:
    void CreateLogger(const FileParam &fileParam);

    std::shared_ptr<yr_spdlog::logger> logger;
    const std::string asyncLoggerName = "metric_logger";

    FileParam fileParam_;
};

}  // namespace metrics
}  // namespace observability
#endif  // OBSERVABILITY_METRIC_LOGGER_H
