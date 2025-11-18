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

#include <async/future.hpp>
#include <cstdint>
#include <exception>
#include <iostream>
#include <litebus.hpp>
#include <memory>
#include <string>

#include "async/async.hpp"
#include "common/aksk/aksk_util.h"
#include "common/constants/constants.h"
#include "common/explorer/explorer.h"
#include "common/explorer/explorer_actor.h"
#include "common/kube_client/kube_client.h"
#include "common/leader/etcd_leader_actor.h"
#include "common/leader/k8s_leader_actor.h"
#include "common/leader/leader_actor.h"
#include "common/leader/txn_leader_actor.h"
#include "common/logs/logging.h"
#include "common/meta_store_adapter/meta_store_monitor_factory.h"
#include "common/meta_store_client/meta_store_client.h"
#include "common/meta_store_client/meta_store_struct.h"
#include "common/rpc/client/grpc_client.h"
#include "common/utils/module_switcher.h"
#include "common/utils/ssl_config.h"
#include "common/utils/version.h"
#include "iam_server/driver/iam_driver.h"
#include "iam_server/flags/flags.h"


using namespace functionsystem;
using namespace functionsystem::iamserver;

namespace {
const std::string COMPONENT_NAME = COMPONENT_NAME_IAM_SERVER;

std::shared_ptr<litebus::Promise<bool>> stopSignal{ nullptr };
std::shared_ptr<functionsystem::ModuleSwitcher> g_iamServerSwitcher{ nullptr };
std::shared_ptr<IAMDriver> g_iamDriver{ nullptr };
std::shared_ptr<functionsystem::leader::LeaderActor> g_leader{ nullptr };
std::shared_ptr<KubeClient> g_kubeClient = nullptr;

bool CheckFlags(const Flags &flags)
{
    const auto &nodeID = flags.GetNodeID();
    if (nodeID.empty()) {
        std::cerr << "null nodeID" << std::endl;
        return false;
    }
    return true;
}

void OnStopHandler(int signum)
{
    YRLOG_INFO("receive signal: {}", signum);
    stopSignal->SetValue(true);
}

void CreateKubeClient(const Flags &flags)
{
    if (flags.GetElectionMode() != K8S_ELECTION_MODE) {
        return;
    }
    g_kubeClient = KubeClient::CreateKubeClient(flags.GetK8sBasePath(), KubeClient::ClusterSslConfig("", "", false));
    g_kubeClient->InitOwnerReference(flags.GetK8sNamespace(), "iam-server");
}

bool CreateExplorer(const Flags &flags, const std::shared_ptr<MetaStoreClient> &metaClient)
{
    auto leaderName = flags.GetElectionMode() == K8S_ELECTION_MODE ? explorer::IAM_SERVER_K8S_LEASE_NAME
                                                                   : explorer::IAM_SERVER_MASTER_ELECTION_KEY;
    explorer::LeaderInfo leaderInfo{ .name = leaderName, .address = flags.GetIP() + ":" + flags.GetHTTPListenPort() };
    explorer::ElectionInfo electionInfo{ .identity = flags.GetIP(),
                                         .mode = flags.GetElectionMode(),
                                         .electKeepAliveInterval = flags.GetElectKeepAliveInterval() };
    return explorer::Explorer::CreateExplorer(electionInfo, leaderInfo, metaClient, g_kubeClient,
                                              flags.GetK8sNamespace());
}

void CreateLeader(const Flags &flags, const std::shared_ptr<MetaStoreClient> &metaClient)
{
    if (flags.GetElectionMode() == STANDALONE_MODE) {
        return;
    }
    auto renewInterval = flags.GetElectLeaseTTL() / 3;
    explorer::ElectionInfo electionInfo{ .identity = flags.GetIP() + ":" + flags.GetHTTPListenPort(),
                                         .mode = flags.GetElectionMode(),
                                         .electKeepAliveInterval = flags.GetElectKeepAliveInterval(),
                                         .electLeaseTTL = flags.GetElectLeaseTTL(),
                                         .electRenewInterval = renewInterval };
    if (flags.GetElectionMode() == K8S_ELECTION_MODE) {
        g_leader = std::make_shared<leader::K8sLeaderActor>(explorer::IAM_SERVER_K8S_LEASE_NAME, electionInfo,
                                                            g_kubeClient, flags.GetK8sNamespace());
        (void)litebus::Spawn(g_leader);
        litebus::Async(g_leader->GetAID(), &leader::LeaderActor::Elect);
    } else if (flags.GetElectionMode() == TXN_ELECTION_MODE) {
        g_leader = std::make_shared<leader::TxnLeaderActor>(explorer::IAM_SERVER_MASTER_ELECTION_KEY, electionInfo,
                                                            metaClient);
        (void)litebus::Spawn(g_leader);
    } else {
        g_leader = std::make_shared<leader::EtcdLeaderActor>(explorer::IAM_SERVER_MASTER_ELECTION_KEY, electionInfo,
                                                             metaClient);
        auto explorerActor = explorer::Explorer::GetInstance().GetExplorer(explorer::IAM_SERVER_MASTER_ELECTION_KEY);
        if (explorerActor != nullptr) {
            g_leader->RegisterPublishLeaderCallBack([aid(explorerActor->GetAID())](const explorer::LeaderInfo &leader) {
                litebus::Async(aid, &explorer::ExplorerActor::FastPublish, leader);
            });
        }
        (void)litebus::Spawn(g_leader);
        litebus::Async(g_leader->GetAID(), &leader::LeaderActor::Elect);
    }
}

std::shared_ptr<MetaStoreClient> GetMetaStoreClient(const Flags &flags)
{
    MetaStoreTimeoutOption option;
    // retries must take longer than health check
    option.operationRetryTimes = static_cast<int64_t>((flags.GetMaxTolerateMetaStoreFailedTimes() + 1) *
                                 (flags.GetMetaStoreCheckInterval() + flags.GetMetaStoreCheckTimeout()) /
                                 KV_OPERATE_RETRY_INTERVAL_LOWER_BOUND);
    MetaStoreMonitorParam param{
        .maxTolerateFailedTimes = flags.GetMaxTolerateMetaStoreFailedTimes(),
        .checkIntervalMs = flags.GetMetaStoreCheckInterval(),
        .timeoutMs = flags.GetMetaStoreCheckTimeout(),
    };
    MetaStoreConfig metaStoreConfig;
    metaStoreConfig.etcdAddress = flags.GetMetaStoreAddress();
    metaStoreConfig.etcdTablePrefix = flags.GetETCDTablePrefix();
    return MetaStoreClient::Create(metaStoreConfig, GetGrpcSSLConfig(flags), option, true, param);
}

void OnCreate(const Flags &flags)
{
    YRLOG_INFO("{} is starting", COMPONENT_NAME);
    YRLOG_INFO("version:{} branch:{} commit_id:{}", BUILD_VERSION, GIT_BRANCH_NAME, GIT_HASH);
    CreateKubeClient(flags);
    auto address = flags.GetIP() + ":" + flags.GetHTTPListenPort();
    if (flags.GetSslEnable()) {
        EnableSSLWhiteList({ flags.GetIP() });
        EnableSSLDownGrade();
        InitLitebusSSLEnv(GetSSLCertConfig(flags));
    }
    g_iamServerSwitcher->InitMetrics(flags.GetEnableMetrics(), flags.GetMetricsConfig(),
                                     flags.GetMetricsConfigFile(), GetSSLCertConfig(flags));
    if (!InitLitebusAKSKEnv(flags).IsOk()) {
        YRLOG_ERROR("failed to get aksk config");
        g_iamServerSwitcher->SetStop();
        return;
    }
    if (!g_iamServerSwitcher->InitLiteBus(address, flags.GetLitebusThreadNum(), false)) {
        g_iamServerSwitcher->SetStop();
        return;
    }

    auto metastoreClient = GetMetaStoreClient(flags);
    auto metaStoreMonitor = MetaStoreMonitorFactory::GetInstance().GetMonitor(flags.GetMetaStoreAddress());
    if (metastoreClient == nullptr || metaStoreMonitor == nullptr
        || metaStoreMonitor->CheckMetaStoreConnected().IsError()) {
        g_iamServerSwitcher->SetStop();
        return;
    }
    if (!CreateExplorer(flags, metastoreClient)) {
        g_iamServerSwitcher->SetStop();
        return;
    }
    CreateLeader(flags, metastoreClient);
    // iam never enable metastore
    InternalIAM::IAMCredType iamCredType{ InternalIAM::IAMCredType::TOKEN };
    if (flags.GetIamCredentialType() == IAM_CREDENTIAL_TYPE_TOKEN) {
        iamCredType = InternalIAM::IAMCredType::TOKEN;
    } else if (flags.GetIamCredentialType() == IAM_CREDENTIAL_TYPE_AK_SK) {
        iamCredType = InternalIAM::IAMCredType::AK_SK;
    }
    IAMStartParam iamStartParam{ .internalIAMParam = { .tokenExpiredTimeSpan = flags.GetTokenExpiredTimeSpan(),
                                                       .isEnableIAM = flags.GetIsEnableIAM(),
                                                       .clusterID = flags.GetClusterID(),
                                                       .credType = iamCredType,
                                                       .permanentCredentialConfigPath =
                                                           flags.GetPermanentCredentialConfigPath(),
                                                       .credentialHostAddress = flags.GetCredentialHostAddress() },
                                 .nodeID = flags.GetNodeID(),
                                 .ip = flags.GetIP(),
                                 .metaStoreAddress = flags.GetMetaStoreAddress() };
    g_iamDriver = std::make_shared<IAMDriver>(iamStartParam, metastoreClient);
    if (auto status = g_iamDriver->Start(); status.IsError()) {
        YRLOG_ERROR("failed to start {}, errMsg: {}", COMPONENT_NAME, status.ToString());
        g_iamServerSwitcher->SetStop();
        return;
    }
    YRLOG_INFO("{} is started", COMPONENT_NAME);
}

void OnDestroy()
{
    YRLOG_INFO("{} is stopping", COMPONENT_NAME);
    MetaStoreMonitorFactory::GetInstance().Clear();
    explorer::Explorer::GetInstance().Clear();
    YRLOG_INFO("start to stop leader");
    if (g_leader != nullptr) {
        litebus::Terminate(g_leader->GetAID());
        litebus::Await(g_leader->GetAID());
        YRLOG_INFO("success to stop leader actor");
    }
    if (g_iamDriver != nullptr && g_iamDriver->Stop().IsOk()) {
        g_iamDriver->Await();
        g_iamDriver = nullptr;
        YRLOG_INFO("success to stop {}", COMPONENT_NAME);
    } else {
        YRLOG_ERROR("failed to stop {}", COMPONENT_NAME);
    }
    g_iamServerSwitcher->StopLogger();
    g_iamServerSwitcher->FinalizeLiteBus();
}
}  // namespace

int main(int argc, char **argv)
{
    Flags flags;
    auto parse = flags.ParseFlags(argc, argv);
    if (parse.IsSome()) {
        std::cerr << COMPONENT_NAME << " parse flag error, flags: " << parse.Get() << std::endl
                  << flags.Usage() << std::endl;
        return -1;
    }

    if (!CheckFlags(flags)) {
        return -1;
    }

    g_iamServerSwitcher = std::make_shared<functionsystem::ModuleSwitcher>(COMPONENT_NAME, flags.GetNodeID());
    if (!g_iamServerSwitcher->InitLogger(flags)) {
        return -1;
    }
    if (!g_iamServerSwitcher->RegisterHandler(OnStopHandler, stopSignal)) {
        return -1;
    }

    OnCreate(flags);

    g_iamServerSwitcher->WaitStop();

    OnDestroy();

    return 0;
}