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

#ifndef TEST_INTEGRATION_UTILS_H
#define TEST_INTEGRATION_UTILS_H

#include "async/try.hpp"
#include "exec/exec.hpp"

namespace functionsystem::test {
inline litebus::Try<std::shared_ptr<litebus::Exec>> CreateProcess(const std::string &path,
                                                                  const std::vector<std::string> &args)
{
    litebus::Try<std::shared_ptr<litebus::Exec>> process = litebus::Exec::CreateExec(
        path, args,
        litebus::None(),
        litebus::ExecIO::CreateFDIO(STDIN_FILENO),
        litebus::ExecIO::CreateFDIO(STDOUT_FILENO),
        litebus::ExecIO::CreateFDIO(STDERR_FILENO)
    );

    // Need to check process is alive.
    sleep(1);
    return process;
}

inline void KillProcess(pid_t pid, int sig)
{
    ::kill(pid, sig);
    // Need to check process has been killed.
}
} // namespace functionsystem::test

#endif  // TEST_INTEGRATION_UTILS_H
