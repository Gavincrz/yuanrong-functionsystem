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

#ifndef FUNCTIONSYSTEM_FUNCTION_AGENT_COMMON_S3_OBJECT_DEL_H
#define FUNCTIONSYSTEM_FUNCTION_AGENT_COMMON_S3_OBJECT_DEL_H

#include <eSDKOBS.h>
#include <securec.h>

#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::test::function_agent {
class S3ObjectDel {
public:
    static void OnHandleComplete(obs_status status, const obs_error_details *error, void *data)
    {
        YRLOG_DEBUG("complete({}) to delete object.", obs_get_status_name(status));
        auto *retStatus = (obs_status *)data;
        *retStatus = status;
    }

    static void DeleteObject(const std::string &bucketID, const std::string &objectID, const S3Config &s3Config)
    {
        obs_object_info objectInfo;
        memset_s(&objectInfo, sizeof(objectInfo), 0, sizeof(obs_object_info));
        objectInfo.key = const_cast<char *>(objectID.c_str());
        objectInfo.version_id = nullptr;

        obs_options options;
        InitObsOptions(&options, s3Config);
        options.bucket_options.bucket_name = const_cast<char *>(bucketID.c_str());

        obs_status retStatus = OBS_STATUS_BUTT;
        obs_response_handler handler = { nullptr, &OnHandleComplete };
        delete_object(&options, &objectInfo, &handler, &retStatus);
        if (OBS_STATUS_OK == retStatus) {
            YRLOG_DEBUG("success to delete object({}).", objectID);
        } else {
            YRLOG_DEBUG("failed({}) to delete object({}).", obs_get_status_name(retStatus), objectID);
        }
    }

private:
    static void InitObsOptions(obs_options *options, const S3Config &s3Config)
    {
        init_obs_options(options);

        options->bucket_options.host_name = const_cast<char *>(s3Config.endpoint.c_str());
        options->bucket_options.access_key = const_cast<char *>(s3Config.accessKey.c_str());
        options->bucket_options.secret_access_key = const_cast<char *>(s3Config.secretKey.GetData());

        options->bucket_options.protocol = obs_protocol::OBS_PROTOCOL_HTTP;
        options->bucket_options.uri_style = obs_uri_style::OBS_URI_STYLE_PATH;
    }
};
}  // namespace functionsystem::test::function_agent

#endif  // FUNCTIONSYSTEM_FUNCTION_AGENT_COMMON_S3_OBJECT_DEL_H
