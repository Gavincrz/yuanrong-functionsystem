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
#include <gtest/gtest.h>

#include <litebus.hpp>

const std::string LITEBUS_TCP_URL("tcp://127.0.0.1:8081");  // NOLINT
const std::string LITEBUS_UDP_URL("udp://127.0.0.1:8081");  // NOLINT

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    auto res = litebus::Initialize("", "", "", "");
    if (res != BUS_OK) {
        std::cerr << "failed to initialize litebus!" << std::endl;
        return -1;
    }

    int code = RUN_ALL_TESTS();
    litebus::TerminateAll();
    litebus::Finalize();
    return code;
}