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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/inotify.h>

#include "common/file_monitor/file_monitor.h"
#include "common/utils/exec_utils.h"
#include "common/utils/files.h"
#include "utils/os_utils.hpp"
#include "utils/future_test_helper.h"

namespace functionsystem::test {

using namespace std;
using namespace functionsystem;

const std::string FILE_PATH = "/home/sn/function/config";
const std::string FILE_NAME = "system-function-config.json";

const std::string content1 = R"(
{
     "0-system-faascontroller": {
       "tenantID":"12345678901234561234567890123456",
       "version":"001",
       "memory": 666,
       "cpu": 666,
       "instanceNum": 1,
       "schedulingOps": {},
       "createOptions": {},
     }
}
)";

const std::string content2 = R"(
{
     "0-system-faascontroller": {
       "tenantID":"12345678901234561234567890123456",
       "version":"002",
       "memory": 666,
       "cpu": 666,
       "instanceNum": 1,
       "schedulingOps": {},
       "createOptions": {},
     }
}
)";

class FileMonitorTest : public ::testing::Test {
protected:
    std::shared_ptr<functionsystem::FileMonitor> monitor_{ nullptr };
};

TEST_F(FileMonitorTest, AddWatchTest)
{
    if (!litebus::os::ExistPath(FILE_PATH)) {
        litebus::os::Mkdir(FILE_PATH);
    }
    auto file = FILE_PATH + "/" + FILE_NAME;
    EXPECT_TRUE(Write(file, content1));

    std::function<void(const std::string &, const std::string &, uint32_t)> callback =
        [](const std::string &path, const std::string &name, uint32_t mask) {
            EXPECT_EQ(name, FILE_NAME);
            if (mask & IN_MODIFY) {
                YRLOG_INFO("callback: path({}), name({}), mask({})", path, name, mask);
            }
        };

    monitor_ = std::make_shared<FileMonitor>();
    auto future = monitor_->Start();
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get(), true);

    future = monitor_->AddWatch(FILE_PATH, callback);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get(), true);

    std::thread modifyFile([file]() {
        std::ofstream out;
        out.open(file);
        out.flush();
        out << content2;
        out.close();
    });
    modifyFile.join();
    EXPECT_EQ(Read(file), content2);

    future = monitor_->RemoveWatch(FILE_PATH);
    EXPECT_AWAIT_READY(future);
    EXPECT_EQ(future.Get(), true);

    monitor_->Stop();
    litebus::os::Rmdir(FILE_PATH);
}

}  // namespace functionsystem::test