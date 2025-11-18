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

#include "flags.h"
#include "common/constants/constants.h"
#include "common/utils/param_check.h"

namespace functionsystem::iamserver {
namespace {
const uint32_t DEFAULT_TOKEN_EXPIRED_TIME_SPAN = 24 * 60 * 60;  // 24 hours, unit: s
const uint32_t MIN_TOKEN_EXPIRED_TIME_SPAN = 12 * 60;  // unit: s
const uint32_t MAX_TOKEN_EXPIRED_TIME_SPAN = 7 * 24 * 60 * 60;  // unit: s
} // namespace
using namespace litebus::flag;
Flags::Flags()
{
    AddFlag(&Flags::logConfig, "log_config", "json format string. For log initialization.", "");
    AddFlag(&Flags::nodeID, "node_id", "vm id", "");
    AddFlag(&Flags::ip, "ip", "IP address for listening.", true, FlagCheckWrraper(IsIPValid));
    AddFlag(&Flags::httpListenPort, "http_listen_port", "For posix server listening. example: 8080",
            true, FlagCheckWrraper(IsPortValid));
    AddFlag(&Flags::metaStoreAddress, "meta_store_address", "For MetaStorage service discover", "");
    AddFlag(&Flags::enableTrace, "enable_trace", "For trace enable, example: false", false);
    AddFlag(&Flags::enableIAM_, "enable_iam", "enable verify and authorize token of internal request", false);
    AddFlag(&Flags::tokenExpiredTimeSpan_, "token_expired_time_span", "token alive period of internal request",
            DEFAULT_TOKEN_EXPIRED_TIME_SPAN, NumCheck(MIN_TOKEN_EXPIRED_TIME_SPAN, MAX_TOKEN_EXPIRED_TIME_SPAN));
    AddFlag(&Flags::decryptAlgorithm_, "decrypt_algorithm", "decrypt algorithm",
            std::string("NO_CRYPTO"), WhiteListCheck({ "NO_CRYPTO" }));
    AddFlag(&Flags::resourcePath_, "resource_path", "resource path to read secret key files", "/");
    AddFlag(&Flags::k8sBasePath_, "k8s_base_path", "For k8s service discovery.", "");
    AddFlag(&Flags::k8sNamespace_, "k8s_namespace", "k8s cluster namespace", "default");
    AddFlag(&Flags::electionMode_, "election_mode", "selection mode, eg: standalone,etcd,txn,k8s",
            std::string("standalone"), WhiteListCheck({ "etcd", "txn", "k8s", "standalone" }));
    AddFlag(&Flags::electLeaseTTL_, "elect_lease_ttl", "lease ttl of function master election", DEFAULT_ELECT_LEASE_TTL,
            NumCheck(MIN_ELECT_LEASE_TTL, MAX_ELECT_LEASE_TTL));
    AddFlag(&Flags::electKeepAliveInterval_, "elect_keep_alive_interval", "interval of elect's lease keep alive",
            DEFAULT_ELECT_KEEP_ALIVE_INTERVAL,
            NumCheck(MIN_ELECT_KEEP_ALIVE_INTERVAL, MAX_ELECT_KEEP_ALIVE_INTERVAL));
    AddFlag(&Flags::iamCredentialType_, "iam_credential_type", "credential type for iam", IAM_CREDENTIAL_TYPE_TOKEN,
            WhiteListCheck({ IAM_CREDENTIAL_TYPE_TOKEN, IAM_CREDENTIAL_TYPE_AK_SK }));
    AddFlag(&Flags::permanentCredentialConfigPath_, "permanent_cred_conf_path", "permanent credential config path",
            "/home/sn/config/permanent-credential-config.json");
    AddFlag(&Flags::credentialHostAddress_, "credential_host_address", "credential host platform address", "");
}

Flags::~Flags()
{
}
} // functionsystem::iamserver