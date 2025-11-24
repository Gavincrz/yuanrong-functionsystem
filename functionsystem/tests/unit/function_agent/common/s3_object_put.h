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

#ifndef FUNCTION_AGENT_S3_PUT_OBJECT_H
#define FUNCTION_AGENT_S3_PUT_OBJECT_H

#include <eSDKOBS.h>
#include <securec.h>
#include <sys/stat.h>

#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::test::function_agent {
struct PutObjectData {
    char *buffer;

    uint64_t bufferSize;

    uint64_t offset;

    obs_status status;
};

class S3ObjectPut {
public:
    static void InitObsOptions(obs_options *options, const S3Config &s3Config)
    {
        init_obs_options(options);

        options->bucket_options.host_name = const_cast<char *>(s3Config.endpoint.c_str());
        options->bucket_options.access_key = const_cast<char *>(s3Config.accessKey.c_str());
        options->bucket_options.secret_access_key = const_cast<char *>(s3Config.secretKey.GetData());

        options->bucket_options.protocol = obs_protocol::OBS_PROTOCOL_HTTP;
        options->bucket_options.uri_style = obs_uri_style::OBS_URI_STYLE_PATH;
    }

    static obs_status OnPutObjectStart(const obs_response_properties *properties, void *_data)
    {
        return OBS_STATUS_OK;
    }

    static int OnPutObjectProgress(int bufferSize, char *buffer, void *_data)
    {
        auto *data = (PutObjectData *)_data;

        int toRead = 0;
        if (data->bufferSize) {
            toRead = ((data->bufferSize > (unsigned)bufferSize) ? (unsigned)bufferSize : data->bufferSize);
            memcpy(buffer, data->buffer + data->offset, toRead);
        }

        data->bufferSize -= toRead;
        data->offset += toRead;

        return toRead;
    }

    static void OnPutObjectComplete(obs_status status, const obs_error_details *error, void *_data)
    {
        auto *data = (PutObjectData *)_data;
        data->status = status;
    }

    static Status PutObjectFromBuffer(const std::string &buffer, const std::string &objectID,
                                      const std::string &bucketID, const S3Config &s3Config)
    {
        obs_options options;
        InitObsOptions(&options, s3Config);
        options.bucket_options.bucket_name = const_cast<char *>(bucketID.c_str());

        obs_put_properties properties;
        init_put_properties(&properties);

        PutObjectData data = { nullptr };
        // Initialize the structure that stores the uploaded data
        data.buffer = const_cast<char *>(buffer.c_str());
        data.bufferSize = strlen(data.buffer);

        YRLOG_DEBUG("put object({}) from buffer after init obs.", objectID);
        obs_put_object_handler handler = { { &OnPutObjectStart, &OnPutObjectComplete }, &OnPutObjectProgress };
        put_object(&options, const_cast<char *>(objectID.c_str()), data.bufferSize, &properties, nullptr, &handler,
                   &data);
        if (OBS_STATUS_OK == data.status) {
            YRLOG_DEBUG("success to put object({}) from buffer", objectID);
            return Status::OK();
        } else {
            YRLOG_ERROR("failed({}) to put object({}) from buffer", obs_get_status_name(data.status), objectID);
            return Status(StatusCode::FUNC_AGENT_OBS_PUT_OBJECT_ERROR,
                          "failed(" + std::string(obs_get_status_name(data.status)) + ") to put object(*) from buffer");
        }
    }
};
}  // namespace functionsystem::test::function_agent
#endif  // FUNCTION_AGENT_S3_PUT_OBJECT_H