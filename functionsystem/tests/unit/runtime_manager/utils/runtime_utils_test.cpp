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

#include <sys/stat.h>
#include "common/utils/files.h"
#include "common/utils/exec_utils.h"
#include "exec/exec.hpp"
#include "gtest/gtest.h"
#include "runtime_manager/utils/std_redirector.h"
#include "runtime_manager/utils/utils.h"
#include "utils/future_test_helper.h"

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

class RuntimeUtilsTest : public ::testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(RuntimeUtilsTest, JoinToStringTest)
{
    std::vector<std::string> vec = { "a", "b", "c", "d", "e", "f", "g" };
    std::string expectStr = "a=b=c=d=e=f=g";
    std::string result = Utils::JoinToString(vec, "=");
    EXPECT_EQ(result, expectStr);
}

TEST_F(RuntimeUtilsTest, TrimPrefixTest)
{
    std::string str = "abcstring";
    std::string expectStr = "string";
    std::string result = Utils::TrimPrefix(str, "abc");
    EXPECT_EQ(result, expectStr);
}

TEST_F(RuntimeUtilsTest, SplitByFuncTest)
{
    std::string str = "\r\n\r\n\rabc\r\n\r\n\r10%\r\n\r\n\r20%\r\n\r\n\r123\r\n\r\n\ra";
    auto result = Utils::SplitByFunc(str, [](const char &ch) -> bool {
        return ch == '\n' || ch == '\r';
    });
    EXPECT_EQ(result.at(0), "abc");
    EXPECT_EQ(result.at(1), "10%");
    EXPECT_EQ(result.at(2), "20%");
    EXPECT_EQ(result.at(3), "123");
    EXPECT_EQ(result.at(4), "a");
}

class RuntimeStdRedirectorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        litebus::os::Rm("/tmp/stdout.log");
    }

    void TearDown() override
    {
    }
protected:
    std::vector<std::string> RemoveEmptyLines(const std::string &filename)
    {
        std::vector<std::string> lines;
        std::ifstream file(filename);
        std::string line;

        if (file.is_open()) {
            while (getline(file, line)) {
                line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
                if (!line.empty()) {
                    lines.push_back(line);
                }
            }
            file.close();
        } else {
            std::cerr << "Unable to open file: " << filename << std::endl;
        }
        return lines;
    }
};

TEST_F(RuntimeStdRedirectorTest, StdLogCreateTest)
{
    auto redirector = std::make_shared<StdRedirector>("/tmp", "stdout.log");
    litebus::Spawn(redirector);
    litebus::Async(redirector->GetAID(), &StdRedirector::Start);
    ASSERT_AWAIT_TRUE([=]() { return FileExists("/tmp/stdout.log"); });
    litebus::Terminate(redirector->GetAID());
    litebus::Await(redirector->GetAID());
    litebus::os::Rm("/tmp/stdout.log");
}

TEST_F(RuntimeStdRedirectorTest, RedirectorLogTest)
{
    auto redirector = std::make_shared<StdRedirector>("/tmp", "stdout.log");
    litebus::Spawn(redirector);

    auto origin = umask(0000);
    auto future = litebus::Async(redirector->GetAID(), &StdRedirector::Start);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    auto permission = GetPermission("/tmp/stdout.log");
    EXPECT_TRUE(permission.IsSome());
    EXPECT_TRUE(permission.Get().owner == 6);
    EXPECT_TRUE(permission.Get().group == 4);
    EXPECT_TRUE(permission.Get().others == 0);

    litebus::Try<std::shared_ptr<litebus::Exec>> s = litebus::Exec::CreateExec(
        "echo output1; sleep 1; /usr/bin/cp a b; /usr/bin/cp a b; /usr/bin/cp a b; /usr/bin/cp a b;", litebus::None(),
        litebus::ExecIO::CreateFDIO(STDIN_FILENO), litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO());
    litebus::Async(redirector->GetAID(), &StdRedirector::StartRuntimeStdRedirection, "runtimeID", "instanceID",
                   s.Get()->GetOut(), s.Get()->GetErr());
    ASSERT_AWAIT_TRUE([=]() { return !s.Get()->GetStatus().IsInit(); });
    ASSERT_AWAIT_TRUE([=]() {
        auto output = litebus::os::Read("/tmp/stdout.log");
        if (output.IsNone()) {
            return false;
        }
        auto lines = RemoveEmptyLines("/tmp/stdout.log");
        return lines.size() >= 5;
    });
    auto info = StdRedirector::GetStdLog("/tmp/stdout.log", "runtimeID", INFO_LEVEL, 1);
    auto err = StdRedirector::GetStdLog("/tmp/stdout.log", "runtimeID", ERROR_LEVEL, 20, 3);
    EXPECT_EQ(info.find("runtimeID") != info.npos, true);
    EXPECT_EQ(info.find(INFO_LEVEL) != info.npos, true);
    EXPECT_EQ(err.find("runtimeID") != info.npos, true);
    EXPECT_EQ(err.find(ERROR_LEVEL) != info.npos, true);
    EXPECT_TRUE(litebus::strings::Split(err, "\n").size() < 4);

    auto output = litebus::os::Read("/tmp/stdout.log");
    EXPECT_EQ(output.IsNone(), false);
    auto msg = output.Get();
    std::cout << "msg: \n" << msg << std::endl;

    litebus::Terminate(redirector->GetAID());
    litebus::Await(redirector->GetAID());
    umask(origin);
}

TEST_F(RuntimeStdRedirectorTest, RedirectorLogRegularlyTest)
{
    auto redirector = std::make_shared<StdRedirector>("/tmp", "stdout.log", 1024*1024, 100);
    litebus::Spawn(redirector);
    auto future = litebus::Async(redirector->GetAID(), &StdRedirector::Start);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);

    litebus::Try<std::shared_ptr<litebus::Exec>> s = litebus::Exec::CreateExec(
        "echo output1; /usr/bin/cp a b; /usr/bin/cp a b;sleep 2;", litebus::None(),
        litebus::ExecIO::CreateFDIO(STDIN_FILENO), litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO());
    litebus::Async(redirector->GetAID(), &StdRedirector::StartRuntimeStdRedirection, "runtimeID", "instanceID",
                   s.Get()->GetOut(), s.Get()->GetErr());
    ASSERT_AWAIT_TRUE([=]() {
        sleep(1);
        auto output = litebus::os::Read("/tmp/stdout.log");
        if (output.IsNone()) {
            return false;
        }
        const auto &msg = output.Get();
        return msg.find(INFO_LEVEL) != msg.npos && msg.find(ERROR_LEVEL) != msg.npos;
    });
    auto info = StdRedirector::GetStdLog("/tmp/stdout.log", "runtimeID", INFO_LEVEL, 1);
    auto err = StdRedirector::GetStdLog("/tmp/stdout.log", "runtimeID", ERROR_LEVEL, 2);
    EXPECT_EQ(info.find("runtimeID") != info.npos, true);
    EXPECT_EQ(info.find(INFO_LEVEL) != info.npos, true);
    EXPECT_EQ(err.find("runtimeID") != info.npos, true);
    EXPECT_EQ(err.find(ERROR_LEVEL) != info.npos, true);

    auto output = litebus::os::Read("/tmp/stdout.log");
    EXPECT_EQ(output.IsNone(), false);
    auto msg = output.Get();
    std::cout << "msg: \n" << msg << std::endl;
    auto lines = RemoveEmptyLines("/tmp/stdout.log");
    EXPECT_EQ(lines.size(), size_t(3));

    litebus::Terminate(redirector->GetAID());
    litebus::Await(redirector->GetAID());
    litebus::os::Rm("/tmp/stdout.log");
}

TEST_F(RuntimeStdRedirectorTest, RedirectorLogMaxLogLengthTest)
{
    auto redirector = std::make_shared<StdRedirector>("/tmp", "stdout.log", 1, 10000);
    litebus::Spawn(redirector);
    auto future = litebus::Async(redirector->GetAID(), &StdRedirector::Start);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);

    litebus::Try<std::shared_ptr<litebus::Exec>> s = litebus::Exec::CreateExec(
        "echo output1; /usr/bin/cp a b; /usr/bin/cp a b;sleep 2;", litebus::None(),
        litebus::ExecIO::CreateFDIO(STDIN_FILENO), litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO());
    litebus::Async(redirector->GetAID(), &StdRedirector::StartRuntimeStdRedirection, "runtimeID", "instanceID",
                   s.Get()->GetOut(), s.Get()->GetErr());
    ASSERT_AWAIT_TRUE([=]() {
        sleep(1);
        auto output = litebus::os::Read("/tmp/stdout.log");
        if (output.IsNone()) {
            return false;
        }
        const auto &msg = output.Get();
        return msg.find(INFO_LEVEL) != msg.npos && msg.find(ERROR_LEVEL) != msg.npos;
    });
    auto info = StdRedirector::GetStdLog("/tmp/stdout.log", "runtimeID", INFO_LEVEL, 1);
    auto err = StdRedirector::GetStdLog("/tmp/stdout.log", "runtimeID", ERROR_LEVEL, 2);
    EXPECT_EQ(info.find("runtimeID") != info.npos, true);
    EXPECT_EQ(info.find(INFO_LEVEL) != info.npos, true);
    EXPECT_EQ(err.find("runtimeID") != info.npos, true);
    EXPECT_EQ(err.find(ERROR_LEVEL) != info.npos, true);

    auto output = litebus::os::Read("/tmp/stdout.log");
    EXPECT_EQ(output.IsNone(), false);
    auto msg = output.Get();
    std::cout << "msg: \n" << msg << std::endl;
    auto lines = RemoveEmptyLines("/tmp/stdout.log");
    EXPECT_EQ(lines.size(), size_t(3));
    litebus::Terminate(redirector->GetAID());
    litebus::Await(redirector->GetAID());
    litebus::os::Rm("/tmp/stdout.log");
}

// Note: this case run more than 30s, set `export NOT_SKIP_LONG_TESTS=1` when run it, and not run on CI by default
TEST_F(RuntimeStdRedirectorTest, RedirectorStdLogRollingCompressTest)
{
    const char* skip_test = std::getenv("NOT_SKIP_LONG_TESTS");
    if (skip_test == nullptr || std::string(skip_test) != "1") {
        GTEST_SKIP() << "Long-running tests are skipped by default";
    }

    StdRedirectParam param = {
        .maxLogLength = 1024, // KB, cache size
        .flushDuration = 10,
        .stdRollingMaxFileSize = 1, // MB
        .stdRollingMaxFiles = 3
    };
    auto redirector = std::make_shared<StdRedirector>("/tmp", "stdout.log", param);
    litebus::Spawn(redirector);
    auto future = litebus::Async(redirector->GetAID(), &StdRedirector::Start);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);

    litebus::Try<std::shared_ptr<litebus::Exec>> s = litebus::Exec::CreateExec(
        "for i in {1..15000}; do echo output1; /usr/bin/cp a b; /usr/bin/cp a b; done", litebus::None(),
        litebus::ExecIO::CreateFDIO(STDIN_FILENO), litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO());
    litebus::Async(redirector->GetAID(), &StdRedirector::StartRuntimeStdRedirection, "runtimeID", "instanceID",
                   s.Get()->GetOut(), s.Get()->GetErr());

    sleep(35); // 30s start compress
    {
        std::string command = "ls /tmp/stdout* | wc -l"; // find rolling compression log files
        auto result = ExecuteCommand(command);
        if (!result.error.empty()) {
            YRLOG_ERROR("execute command {} failed, error: {}", command, result.error);
            return;
        }

        YRLOG_INFO("command {} output is {}", command, result.output);
        std::istringstream iss(result.output);
        std::string line;
        std::getline(iss, line);
        std::istringstream linestream(line);
        int count;
        linestream >> count;
        EXPECT_FALSE(line.empty());
        EXPECT_TRUE(count == 3 || count == 4); // when '/tmp/stdout.log' in use, it is 4
    }

    {
        std::string command = "ls /tmp/stdout*.log.gz | wc -l"; // find rolling compression log files
        auto result = ExecuteCommand(command);
        if (!result.error.empty()) {
            YRLOG_ERROR("execute command {} failed, error: {}", command, result.error);
            return;
        }

        YRLOG_INFO("command {} output is {}", command, result.output);
        std::istringstream iss(result.output);
        std::string line;
        std::getline(iss, line);
        std::istringstream linestream(line);
        int count;
        linestream >> count;
        EXPECT_FALSE(line.empty());
        EXPECT_TRUE(count > 0);
    }

    litebus::Terminate(redirector->GetAID());
    litebus::Await(redirector->GetAID());
}

TEST_F(RuntimeStdRedirectorTest, RedirectorLogTest_StdLog_Error)
{
    auto redirector = std::make_shared<StdRedirector>("/tmp", "stdout2.log");
    litebus::Spawn(redirector);
    auto future = litebus::Async(redirector->GetAID(), &StdRedirector::Start);
    ASSERT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get().IsOk());

    std::string command = "rm -rf /tmp/stdout2.log"; // stub for test
    (void)std::system(command.c_str());
    ASSERT_AWAIT_TRUE([=]() { return !FileExists("/tmp/stdout2.log"); });
    redirector->logFileNotExist_ = false;

    litebus::Try<std::shared_ptr<litebus::Exec>> s = litebus::Exec::CreateExec(
        "echo output1; /usr/bin/cp a b; /usr/bin/cp a b; /usr/bin/cp a b; /usr/bin/cp a b;", litebus::None(),
        litebus::ExecIO::CreateFDIO(STDIN_FILENO), litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO());
    litebus::Async(redirector->GetAID(), &StdRedirector::StartRuntimeStdRedirection, "runtimeID", "instanceID",
                   s.Get()->GetOut(), s.Get()->GetErr());
    ASSERT_AWAIT_TRUE([=]() { return !s.Get()->GetStatus().IsInit(); });
}

TEST_F(RuntimeStdRedirectorTest, FlushToStd)
{
    litebus::os::Rm("/tmp/stdout.log");
    auto param = StdRedirectParam{};
    param.exportMode = functionsystem::runtime_manager::STD_EXPORTER;
    auto redirector = std::make_shared<StdRedirector>("/tmp", "stdout.log", param);
    litebus::Spawn(redirector);
    auto future = litebus::Async(redirector->GetAID(), &StdRedirector::Start);
    ASSERT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get().IsOk());

    litebus::Try<std::shared_ptr<litebus::Exec>> s = litebus::Exec::CreateExec(
        "echo output1; /usr/bin/cp a b; /usr/bin/cp a b; /usr/bin/cp a b; /usr/bin/cp a b;", litebus::None(),
        litebus::ExecIO::CreateFDIO(STDIN_FILENO), litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO());
    litebus::Async(redirector->GetAID(), &StdRedirector::StartRuntimeStdRedirection, "runtimeID", "instanceID",
                   s.Get()->GetOut(), s.Get()->GetErr());
    ASSERT_AWAIT_TRUE([=]() {
        sleep(1);
        auto output = litebus::os::Read("/tmp/stdout.log");
        if (output.IsNone()) {
            return false;
        }
        return output.Get().empty();
    });
    litebus::Terminate(redirector->GetAID());
    litebus::Await(redirector->GetAID());
}

}  // namespace functionsystem::test
