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

#ifndef IAM_SERVER_INTERNAL_IAM_IAM_TOKEN_CONTENT_H
#define IAM_SERVER_INTERNAL_IAM_IAM_TOKEN_CONTENT_H

#include "common/status/status.h"
#include "securec.h"
#include "common/logs/logging.h"
#include "common/utils/token_transfer.h"

namespace functionsystem::iamserver {

const int INTERNAL_IAM_TOKEN_MAX_SIZE = 4096;

struct TokenContent {
    std::string tenantID;
    uint64_t expiredTimeStamp;
    std::string salt;
    // need to set it to sensitive value
    std::string encryptToken;

    ~TokenContent()
    {
        CleanSensitiveStrMemory(encryptToken, "deconstructing token, tenantID: " + tenantID);
    }

    enum class SerializeType {
        TENANT_ID = 0,
        EXPIRED_TIME_STAMP = 1,
        SERIALIZE_TYPE_MAX = 2,
    };

    Status IsValid(const uint32_t offset = 0) const
    {
        if (tenantID.empty()) {
            return Status(StatusCode::FAILED, "token tenantID is empty");
        }
        if (salt.empty()) {
            return Status(StatusCode::FAILED, "token salt is empty");
        }
        if (encryptToken.empty()) {
            return Status(StatusCode::FAILED, "token value is empty");
        }
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (expiredTimeStamp < now + offset) {
            return Status(StatusCode::FAILED, "token expired time stamp is earlier than now, expiredTimeStamp: "
                                                  + std::to_string(expiredTimeStamp));
        }
        return Status::OK();
    }

    Status Serialize(char *token, size_t &size) const
    {
        std::string splitSymbol = "__";
        auto timeStampStr = std::to_string(expiredTimeStamp);
        size = tenantID.size() + splitSymbol.size() + timeStampStr.size();
        if (size >= static_cast<size_t>(INTERNAL_IAM_TOKEN_MAX_SIZE)) {
            return Status(StatusCode::FAILED, "token size too long");
        }
        int len = sprintf_s(token, INTERNAL_IAM_TOKEN_MAX_SIZE, "%s__%s", tenantID.c_str(), timeStampStr.c_str());
        if (size != static_cast<size_t>(len)) {
            return Status(StatusCode::FAILED, "sprintf_s failed");
        }
        return Status::OK();
    }

    Status Parse(const char *token)
    {
        tenantID.clear();
        std::string timeStampStr;
        int index = 0;
        int type = static_cast<int>(SerializeType::TENANT_ID);
        while (index <= INTERNAL_IAM_TOKEN_MAX_SIZE && *(token + index) != '\0') {
            char curChar = *(token + index);
            char nextChar = *(token + index + 1);
            if (curChar == '_' && nextChar == '_') {
                // token format: xxx__xxx, has double underscore to split
                index++;
                index++;
                type++;
                continue;
            }
            if (static_cast<int>(type) >= static_cast<int>(SerializeType::SERIALIZE_TYPE_MAX)) {
                return Status(StatusCode::FAILED, "token format error");
            }
            switch (type) {
                case static_cast<int>(SerializeType::TENANT_ID):
                    tenantID += curChar;
                    break;
                case static_cast<int>(SerializeType::EXPIRED_TIME_STAMP):
                    timeStampStr += curChar;
                    break;
                default:
                    break;
            }
            index++;
        }
        if (index > INTERNAL_IAM_TOKEN_MAX_SIZE) {
            return Status(StatusCode::FAILED, "token length error");
        }
        try {
            expiredTimeStamp = std::stoull(timeStampStr);
        } catch (std::exception &e) {
            return Status(StatusCode::FAILED, "transform time stamp type failed, err:" + std::string(e.what()));
        }
        return Status::OK();
    }

    bool operator==(const TokenContent &targetToken) const
    {
        return targetToken.tenantID == tenantID && targetToken.expiredTimeStamp == expiredTimeStamp;
    }

    bool operator!=(const TokenContent &targetToken) const
    {
        return targetToken.tenantID != tenantID || targetToken.expiredTimeStamp != expiredTimeStamp;
    }

    std::shared_ptr<TokenContent> Copy()
    {
        auto tokenContent = std::make_shared<TokenContent>();
        tokenContent->salt = salt;
        tokenContent->tenantID = tenantID;
        tokenContent->expiredTimeStamp = expiredTimeStamp;
        tokenContent->encryptToken = encryptToken;
        return tokenContent;
    }
};
}  // namespace functionsystem::iamserver
#endif // IAM_SERVER_INTERNAL_IAM_IAM_TOKEN_CONTENT_H