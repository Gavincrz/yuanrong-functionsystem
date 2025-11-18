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

#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <regex>
#include <stdexcept>
#include <tuple>

#include "file_utils.h"
#include "file_sink.h"

namespace observability {

namespace metrics::common {
using namespace yr_spdlog::sinks;
using namespace yr_spdlog;
using details::os::filename_to_str;
using details::os::path_exists;

const int RANDOM_LOWER_BOUND = 100;
const int RANDOM_UPPER_BOUND = 999;

FileSink::FileSink(const std::string &basicFileName, std::size_t singleFileMaxSize, std::size_t maxFileNum,
                   bool rotate, bool compress)
    : basicFileName_(basicFileName),
      singleFileMaxSize_(singleFileMaxSize),
      maxFileNum_(maxFileNum),
      fileHelper_(),
      rotate_(rotate),
      compress_(compress)
{
    if (singleFileMaxSize_ == 0) {
        std::cerr << "rotating sink constructor: singleFileMaxSize_ arg cannot be zero" << std::endl;
    }
    CheckFileExist();
}

// calc filename according to index and file extension if exists.
// e.g. GetFileNameByIndex("logs/mylog.data, 3) => "logs/mylog.3.data".
std::string FileSink::GetFileNameByIndex(const std::string &filename, std::size_t index)
{
    if (index == 0u) {
        return filename;
    }

    std::string basename;
    std::string ext;
    std::tie(basename, ext) = details::file_helper::split_by_extension(filename);
    return fmt::format(SPDLOG_FILENAME_T("{}.{}{}"), basename, index, ext);
}

const std::string &FileSink::FileName() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return fileHelper_.filename();
}

void FileSink::flush_()
{
    fileHelper_.flush();
}

// delete the target if exists, and rename the src file  to target
// return true on success, false otherwise.
bool FileSink::RenameFile(const std::string &srcFileName, const std::string &targetFileName) const
{
    // try to delete the target file in case it already exists.
    (void)details::os::remove(targetFileName);
    return details::os::rename(srcFileName, targetFileName) == 0;
}

void FileSink::sink_it_(const yr_spdlog::details::log_msg &msg)
{
    if (fileHelper_.filename().empty()) {
        fileHelper_.open(GetFileNameByIndex(basicFileName_, 0));
        currentSize_ = fileHelper_.size();  // expensive. called only once
    }
    memory_buf_t formatted;
    base_sink<std::mutex>::formatter_->format(msg, formatted);
    auto newSize = currentSize_ + formatted.size();
    if (newSize <= currentSize_) {
        std::cerr << formatted.size() << " formatted is too big or zero" << std::endl;
        return;
    }
    // rotate if the new estimated file size exceeds max size.
    // rotate only if the real size > 0 to better deal with full disk.
    // we only check the real size when new_size > singleFileMaxSize_ because it is relatively expensive.
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
 * metrics.data -> metrics.1.data
 * metrics.1.data -> metrics.2.data
 * metrics.2.data -> metrics.3.data
 * metrics.3.data -> delete
 * */
void FileSink::Rotate()
{
    fileHelper_.close();
    if (!compress_) {
        std::queue<std::string>().swap(fileQueue_);
    }
    for (auto i = maxFileNum_; i > 0; --i) {
        std::string src = GetFileNameByIndex(basicFileName_, i - 1);
        if (!path_exists(src)) {
            continue;
        }
        std::string target = GetFileNameByIndex(basicFileName_, i);
        if (!RenameFile(src, target)) {
            fileHelper_.reopen(true);
            currentSize_ = 0;
            std::cerr <<
                "metric_file_sink: failed renaming " << src << " to " << target << std::endl;
        } else {
            if (compress_) {
                Compress(target);
            } else {
                fileQueue_.push(target);
            }
        }
        if (fileQueue_.size() >= maxFileNum_) {
            std::string oldest = fileQueue_.front();
            fileQueue_.pop();
            DeleteFile(oldest);
        }
    }
    fileHelper_.reopen(true);
}

void FileSink::Compress(const std::string &file)
{
    int64_t timestamp;
    GetFileModifiedTime(file, timestamp);

    // e.g: <metrics>.1.data -> <metrics>.{TIME}.data -> <metrics>.{TIME}.data.gz
    std::string basename;
    std::string ext;
    std::string idx;
    std::tie(basename, ext) = yr_spdlog::details::file_helper::split_by_extension(file);
    std::tie(basename, idx) = yr_spdlog::details::file_helper::split_by_extension(basename);

    std::mt19937 mt(rd_());
    std::uniform_int_distribution<> dis(RANDOM_LOWER_BOUND, RANDOM_UPPER_BOUND);

    std::string targetFile = basename + "." + std::to_string(timestamp) + std::to_string(dis(mt)) + ext;
    if (!RenameFile(file, targetFile)) {
        std::cerr << "metric_file_sink: failed renaming " << file << " to " << targetFile << std::endl;
    }
    std::string gzFile = targetFile + ".gz";
    // Compress the file and delete the origin file, just keep the compress files.
    int ret = CompressFile(targetFile, gzFile);
    if (ret != 0) {
        std::cerr <<  "metric_file_sink: failed compressing " << targetFile << " to " << gzFile;
    }
    DeleteFile(targetFile);
    fileQueue_.push(gzFile);
}

bool Cmp(const std::string &s1, const std::string &s2)
{
    std::regex pattern("[0-9]+");
    std::smatch match1;
    std::smatch match2;
    (void)regex_search(s1, match1, pattern);
    (void)regex_search(s2, match2, pattern);

    return match1[0] < match2[0];
}

void FileSink::CheckFileExist()
{
    std::string basename;
    std::string ext;
    std::tie(basename, ext) = yr_spdlog::details::file_helper::split_by_extension(basicFileName_);

    std::vector<std::string> files;
    std::stringstream ss;
    ss << basename << "\\." << "*[0-9]\\." << ext.substr(1);
    if (compress_) {
        ss << "\\.gz";
    }
    std::string pattern = ss.str();
    Glob(pattern, files);

    sort(files.begin(), files.end(), Cmp);
    size_t i = std::max(files.size() - maxFileNum_, static_cast<size_t>(0));
    while (i < files.size()) {
        fileQueue_.push(files[i]);
        i++;
    }
}

}  // namespace exporters::metrics

}  // namespace observability
