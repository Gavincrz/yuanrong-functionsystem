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

#ifndef TEST_UNIT_UTILS_PORT_HELPER_H
#define TEST_UNIT_UTILS_PORT_HELPER_H

#include <random>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace functionsystem::test {

inline std::string GetEnv(const char* name, const std::string& defaultVal)
{
    if (const char *env = std::getenv(name)) {
        return env;
    }
    return defaultVal;
}

inline uint16_t GetPortEnv(const char* name, uint16_t defaultPort)
{
    try {
        std::string env = GetEnv(name, std::to_string(defaultPort));
        size_t pos;
        int port = std::stoi(env, &pos);

        if (pos != env.length()) {
            throw std::invalid_argument("Invalid characters in port");
        }

        if (port < 1 || port > 65535) {
            throw std::out_of_range("Port out of valid range");
        }

        return static_cast<uint16_t>(port);
    } catch (const std::exception &e) {
        throw std::runtime_error(std::string("Environment variable ") + name + " error: " + e.what());
    }
}

inline int FindAvailablePort()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(10240, 65535);

    int sock;
    struct sockaddr_in addr;

    while (true) {
        int port = dist(gen);
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1)
            continue;

        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(sock);
            return port;
        }
        close(sock);
    }
}

}  // namespace functionsystem::test

#endif  // TEST_UNIT_UTILS_PORT_HELPER_H
