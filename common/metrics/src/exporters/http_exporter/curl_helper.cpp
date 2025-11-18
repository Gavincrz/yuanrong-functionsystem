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

#include "metrics/exporters/http_exporter/curl_helper.h"

#include "curl/curl.h"

#include <iostream>

namespace observability::exporters::metrics {
const int HTTP_REQUEST_ERROR = -1;
const long TIMEOUT = 3L;
CurlHelper::CurlHelper()
{
    auto error = curl_global_init(CURL_GLOBAL_ALL);
    if (error) {
        std::cerr << "<PrometheusPushExporter> failed to initialize global curl!" << std::endl;
        return;
    }
    curl_ = curl_easy_init();
    if (!curl_) {
        curl_global_cleanup();
        std::cerr << "<PrometheusPushExporter> failed to initialize easy curl!" << std::endl;
        return;
    }
}

CurlHelper::~CurlHelper()
{
    curl_slist_free_all(httpHeader_);

    curl_easy_cleanup(curl_);
    curl_global_cleanup();

    if (httpHeader_ != nullptr) {
        httpHeader_ = nullptr;
    }

    if (curl_ != nullptr) {
        curl_ = nullptr;
    }
}

long CurlHelper::SendRequest(HttpRequestMethod method, const std::string &url, const std::ostringstream &ossBody)
{
    if (!curl_) {
        return HTTP_REQUEST_ERROR;
    }
    std::lock_guard<std::mutex> l(mutex_);
    curl_easy_reset(curl_);
    std::string urlWithScheme = url;
    if (sslConfig_.isSSLEnable_) {
        urlWithScheme = "https://" + urlWithScheme;
    }

    (void)curl_easy_setopt(curl_, CURLOPT_URL, urlWithScheme.c_str());
    (void)curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, httpHeader_);
    (void)curl_easy_setopt(curl_, CURLOPT_TIMEOUT, TIMEOUT);

    auto bodyString = ossBody.str();
    if (!bodyString.empty()) {
        (void)curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, bodyString.size());
        (void)curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, bodyString.data());
    } else {
        (void)curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, 0L);
    }

    switch (method) {
        case HttpRequestMethod::POST:
            (void)curl_easy_setopt(curl_, CURLOPT_POST, 1L);
            break;
        case HttpRequestMethod::PUT:
            (void)curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "PUT");
            break;
        case HttpRequestMethod::GET:
        case HttpRequestMethod::DELETE:
        default:
            break;
    }

    if (sslConfig_.isSSLEnable_) {
        (void)curl_easy_setopt(curl_, CURLOPT_CAINFO, sslConfig_.rootCertFile_.c_str());
        (void)curl_easy_setopt(curl_, CURLOPT_SSLCERT, sslConfig_.certFile_.c_str());
        (void)curl_easy_setopt(curl_, CURLOPT_SSLKEY, sslConfig_.keyFile_.c_str());
        (void)curl_easy_setopt(curl_, CURLOPT_KEYPASSWD, sslConfig_.passphrase_.GetData());
        (void)curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 2L);
    }

    auto curlError = curl_easy_perform(curl_);
    long responseCode = 0;
    (void)curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &responseCode);
    if (curlError != CURLE_OK) {
        auto errMsg = curl_easy_strerror(curlError);
        std::cerr << "Curl error, error code: " << static_cast<long>(curlError) << ", errMsg: " << errMsg << std::endl;
        return -static_cast<long>(curlError);
    }
    return responseCode;
}

void CurlHelper::SetSSLConfig(const SSLConfig &sslConfig)
{
    sslConfig_ = sslConfig;
}

void CurlHelper::SetHttpHeader(const char header[])
{
    httpHeader_ = curl_slist_append(httpHeader_, header);
}

}  // namespace observability::exporters::metrics
