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

#include "exporters/file_exporter/include/metric_file_sink.h"

#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/fmt/fmt.h>
#include <stdexcept>
#include <tuple>

#include "exporters/file_exporter/include/file_utils.h"

namespace observability {
namespace metrics {
using namespace yr_spdlog::sinks;
using namespace yr_spdlog;
using details::os::filename_to_str;
using details::os::path_exists;

MetricFileSink::MetricFileSink(const filename_t &basicFileName, std::size_t singleFileMaxSize, std::size_t maxFileNum,
                               bool rotateOnOpen)
    : basicFileName_(std::move(basicFileName)),
      singleFileMaxSize_(singleFileMaxSize),
      maxFileNum_(maxFileNum),
      fileHelper_()
{
    if (singleFileMaxSize_ == 0) {
        throw_spdlog_ex("rotating sink constructor: singleFileMaxSize_ arg cannot be zero");
    }

    try {
        fileHelper_.open(GetFileNameByIndex(basicFileName_, 0));
        currentSize_ = fileHelper_.size();  // expensive. called only once
    } catch (const yr_spdlog::spdlog_ex &ex) {
        std::cerr << "failed to open file: " << GetFileNameByIndex(basicFileName_, 0)
                  << ", reason: " << ex.what() << std::endl;
    }

    if (rotateOnOpen && currentSize_ > 0) {
        Rotate();
        currentSize_ = 0;
    }
}

// calc filename according to index and file extension if exists.
// e.g. GetFileNameByIndex("logs/mylog.data, 3) => "logs/mylog.3.data".
filename_t MetricFileSink::GetFileNameByIndex(const filename_t &filename, std::size_t index)
{
    if (index == 0u) {
        return filename;
    }

    filename_t basename;
    filename_t ext;
    std::tie(basename, ext) = details::file_helper::split_by_extension(filename);
    return fmt::format(SPDLOG_FILENAME_T("{}.{}{}"), basename, index, ext);
}

filename_t MetricFileSink::FileName() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return fileHelper_.filename();
}

void MetricFileSink::flush_()
{
    fileHelper_.flush();
}

// delete the target if exists, and rename the src file  to target
// return true on success, false otherwise.
bool MetricFileSink::RenameFile(const filename_t &srcFileName, const filename_t &targetFileName) const
{
    // try to delete the target file in case it already exists.
    (void)details::os::remove(targetFileName);
    return details::os::rename(srcFileName, targetFileName) == 0;
}

void MetricFileSink::sink_it_(const details::log_msg &msg)
{
    memory_buf_t formatted;
    base_sink<std::mutex>::formatter_->format(msg, formatted);
    auto newSize = currentSize_ + formatted.size();
    if (newSize <= currentSize_) {
        std::cerr << formatted.size() << " formatted is too big or zero" << std::endl;
        return;
    }
    // rotate if the new estimated file size exceeds max size.
    // rotate only if the real size > 0 to better deal with full disk.
    // we only check the real size when newSize > singleFileMaxSize_ because it is relatively expensive.
    if (newSize > singleFileMaxSize_) {
        fileHelper_.flush();
        if (fileHelper_.size() > 0) {
            Rotate();
            newSize = formatted.size();
        }
    }
    fileHelper_.write(formatted);
    currentSize_ = newSize;
}

/**
 * file transfer:
 * metric.data -> metric.1.data
 * metric.1.data -> metric.2.data
 * metric.2.data -> metric.3.data
 * metric.3.data -> delete
 * */
void MetricFileSink::Rotate()
{
    fileHelper_.close();
    for (auto i = maxFileNum_; i > 0; --i) {
        filename_t src = GetFileNameByIndex(basicFileName_, i - 1);
        if (!path_exists(src)) {
            continue;
        }
        filename_t target = GetFileNameByIndex(basicFileName_, i);
        if (!RenameFile(src, target)) {
            fileHelper_.reopen(true);
            currentSize_ = 0;
            throw_spdlog_ex(
                "metric_file_sink: failed renaming " + filename_to_str(src) + " to " + filename_to_str(target), errno);
        } else {
            Compress(target);
        }
    }
    fileHelper_.reopen(true);
}

void MetricFileSink::Compress(const filename_t &file) const
{
    int64_t timestamp;
    GetFileModifiedTime(file, timestamp);

    // e.g: <filename>.1.data -> <filename>.{TIME}.data -> <filename>.{TIME}.data.gz
    std::string basename, ext, idx;
    std::tie(basename, ext) = yr_spdlog::details::file_helper::split_by_extension(file);
    std::tie(basename, idx) = yr_spdlog::details::file_helper::split_by_extension(basename);
    std::string targetFile = basename + "." + std::to_string(timestamp) + ext;
    if (!RenameFile(file, targetFile)) {
        throw_spdlog_ex(
            "metric_file_sink: failed renaming " + filename_to_str(file) + " to " + filename_to_str(targetFile), errno);
    }
    std::string gzFile = targetFile + ".gz";
    // Compress the file and delete the origin file, we just need the compress files!
    int ret = CompressFile(targetFile, gzFile);
    if (ret != 0) {
        throw_spdlog_ex(
            "metric_file_sink: failed compressing " + filename_to_str(targetFile) + " to " + filename_to_str(gzFile),
            errno);
    }
    DeleteFile(targetFile);
}

}  // namespace metrics

}  // namespace observability
