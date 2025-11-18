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

#ifndef OBSERVABILITY_METRICS_METRIC_FILE_SINK_H
#define OBSERVABILITY_METRICS_METRIC_FILE_SINK_H

#include <chrono>
#include <mutex>
#include <string>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/synchronous_factory.h>

namespace observability {
namespace metrics {
using namespace yr_spdlog;
using namespace yr_spdlog::sinks;

class MetricFileSink final : public base_sink<std::mutex> {
public:
    MetricFileSink(const filename_t &basicFileName, std::size_t singleFileMaxSize,
                   std::size_t maxFileNum, bool rotateOnOpen = false);
    static filename_t GetFileNameByIndex(const filename_t &filename, std::size_t index);
    filename_t FileName() const;

protected:
    void flush_() override;

    bool RenameFile(const filename_t &srcFileName, const filename_t &targetFileName) const;

    void sink_it_(const details::log_msg &msg) override;

private:
    void Rotate();
    void Compress(const filename_t &file) const;

    filename_t basicFileName_;
    std::size_t currentSize_;
    std::size_t singleFileMaxSize_;
    std::size_t maxFileNum_;
    details::file_helper fileHelper_;

    mutable std::mutex mutex_;
};

}
}
#endif  // OBSERVABILITY_METRICS_METRIC_FILE_SINK_H