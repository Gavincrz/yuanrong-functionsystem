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

#ifndef OBSERVABILITY_EXPORTERS_CURL_HELPER_H
#define OBSERVABILITY_EXPORTERS_CURL_HELPER_H

#include <mutex>
#include <sstream>

#include "metrics/exporters/common/ssl_config.h"

extern "C" {
class curl_slist;
}

namespace observability::exporters::metrics {

enum class HttpRequestMethod { GET, POST, PUT, DELETE };

class CurlHelper {
public:
    CurlHelper(const CurlHelper &) = delete;
    CurlHelper(CurlHelper &&) = delete;
    CurlHelper &operator=(const CurlHelper &) = delete;
    CurlHelper &operator=(CurlHelper &&) = delete;

    CurlHelper();
    ~CurlHelper();

    long SendRequest(HttpRequestMethod method, const std::string &url, const std::ostringstream &ossBody);

    void SetSSLConfig(const SSLConfig &sslConfig);

    void SetHttpHeader(const char header[]);

private:
    void *curl_{ nullptr };
    curl_slist *httpHeader_{ nullptr };
    SSLConfig sslConfig_;
    std::mutex mutex_{};
};

}  // namespace observability::exporters::metrics

#endif  // OBSERVABILITY_EXPORTERS_CURL_HELPER_H
