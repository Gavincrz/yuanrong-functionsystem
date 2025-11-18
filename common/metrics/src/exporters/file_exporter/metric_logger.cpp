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
#include "exporters/file_exporter/include/metric_logger.h"

#include <iomanip>
#include <iostream>

#include "exporters/file_exporter/include/file_utils.h"
#include "exporters/file_exporter/include/metric_file_sink.h"
#include "spdlog/async.h"

namespace observability {
namespace metrics {

MetricLogger::MetricLogger(const FileParam &fileParam)
{
    fileParam_ = fileParam;
    CreateLogger(fileParam);
}

MetricLogger::~MetricLogger()
{
    Flush();
}

void MetricLogger::Record(const std::string &metricString)
{
    try {
        logger->info(metricString);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << std::endl;
    }
}

void MetricLogger::Flush() noexcept
{
    try {
        logger->flush();
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << std::endl;
    }
}

std::string FormatTimePoint()
{
    using std::chrono::system_clock;
    std::time_t tt = system_clock::to_time_t(system_clock::now());
    struct std::tm ptm {};
    (void)::localtime_r(&tt, &ptm);

    std::stringstream ss;
    ss << std::put_time(&ptm, "%Y%m%d%H%M%S");

    return ss.str();
}

std::string GetFullPath(const FileParam &fileParam)
{
    auto fullPath = fileParam.fileDir + "/" + fileParam.fileName + ".data";
    return fullPath;
}

void MetricLogger::CreateLogger(const FileParam &fileParam)
{
    try {
        yr_spdlog::drop(asyncLoggerName);
        std::string fileFullPath = GetFullPath(fileParam);

        logger = yr_spdlog::create_async<observability::metrics::MetricFileSink>(asyncLoggerName, fileFullPath,
                                                                              fileParam.maxSize, fileParam.maxFileNum);

        logger->set_level(yr_spdlog::level::info);
        logger->set_pattern("%v");
    } catch (const yr_spdlog::spdlog_ex &ex) {
        std::cerr << "failed to init logger:" << ex.what() << std::endl << std::flush;
    }
}

}  // namespace metrics
}  // namespace observability
