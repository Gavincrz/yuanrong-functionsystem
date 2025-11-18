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

#ifndef OBSERVABILITY_EXPORTERS_METRIC_FILE_SINK_H
#define OBSERVABILITY_EXPORTERS_METRIC_FILE_SINK_H

#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/synchronous_factory.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/sink.h>

#include <chrono>
#include <mutex>
#include <queue>
#include <random>
#include <string>

namespace observability::metrics::common {

class FileSink final : public yr_spdlog::sinks::base_sink<std::mutex> {
public:
    FileSink(const std::string &basicFileName, std::size_t singleFileMaxSize, std::size_t maxFileNum,
             bool rotate = true, bool compress = false);
    static std::string GetFileNameByIndex(const std::string &filename, std::size_t index);
    const std::string &FileName() const;

protected:
    void flush_() override;

    bool RenameFile(const std::string &srcFileName, const std::string &targetFileName) const;

    void sink_it_(const yr_spdlog::details::log_msg &msg) override;

private:
    void Rotate();
    void Compress(const std::string &file);
    void CheckFileExist();
    std::string basicFileName_;
    std::size_t currentSize_{ 0 };
    std::size_t singleFileMaxSize_;
    std::size_t maxFileNum_;
    yr_spdlog::details::file_helper fileHelper_;
    bool rotate_{ true };
    bool compress_{ false };

    std::queue<std::string> fileQueue_;

    std::random_device rd_;
    mutable std::mutex mutex_;
};
}  // namespace observability::exporters::metrics
#endif  // OBSERVABILITY_EXPORTERS_METRIC_FILE_SINK_H