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

#ifndef FUNCTION_AGENT_COMMON_S3_BUCKET_OPT_H
#define FUNCTION_AGENT_COMMON_S3_BUCKET_OPT_H

#include <eSDKOBS.h>

#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::test::function_agent {
class S3BucketOpt {
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

    static void OnHandleComplete(obs_status status, const obs_error_details *error, void *data)
    {
        auto *retStatus = (obs_status *)data;
        *retStatus = status;
    }

    static Status CreateBucket(const std::string &bucketName, const S3Config &s3Config)
    {
        obs_options options;
        InitObsOptions(&options, s3Config);
        options.bucket_options.bucket_name = const_cast<char *>(bucketName.c_str());

        obs_response_handler handler = { nullptr, &OnHandleComplete };

        obs_status retStatus = OBS_STATUS_BUTT;
        obs_canned_acl cannedAcl = OBS_CANNED_ACL_BUCKET_OWNER_FULL_CONTROL;
        create_bucket(&options, cannedAcl, nullptr, &handler, &retStatus);
        if (retStatus == OBS_STATUS_OK || retStatus == OBS_STATUS_BucketAlreadyExists ||
            retStatus == OBS_STATUS_BucketAlreadyOwnedByYou) {
            YRLOG_DEBUG("success to create bucket({}).", bucketName);
            return Status::OK();
        } else {
            YRLOG_DEBUG("failed({}) to create bucket({}).", obs_get_status_name(retStatus), bucketName);
            return Status(StatusCode::FUNC_AGENT_OBS_ADD_BUCKET_ERROR,
                          "failed({" + std::string(obs_get_status_name(retStatus)) + "}) to create bucket({" +
                              bucketName + "}).");
        }
    }

    static Status DeleteBucket(const std::string &bucketName, const S3Config &s3Config)
    {
        obs_options options;
        InitObsOptions(&options, s3Config);
        options.bucket_options.bucket_name = const_cast<char *>(bucketName.c_str());

        obs_response_handler handler = { nullptr, &OnHandleComplete };

        obs_status retStatus = OBS_STATUS_BUTT;
        delete_bucket(&options, &handler, &retStatus);

        if (retStatus == OBS_STATUS_OK) {
            YRLOG_DEBUG("success to delete bucket({}).", bucketName);
            return Status::OK();
        } else {
            YRLOG_DEBUG("failed({}) to delete bucket({}).", obs_get_status_name(retStatus), bucketName);
            return Status(
                StatusCode::FUNC_AGENT_OBS_DEL_BUCKET_ERROR,
                "failed(" + std::string(obs_get_status_name(retStatus)) + ") to delete bucket(" + bucketName + ").");
        }
    }
};
}  // namespace functionsystem::test::function_agent

#endif  // FUNCTION_AGENT_COMMON_S3_BUCKET_OPT_H
