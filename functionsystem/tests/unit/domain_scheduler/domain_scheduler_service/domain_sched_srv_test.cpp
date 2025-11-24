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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <litebus.hpp>

#include "common/constants/constants.h"
#include "common/constants/metastore_keys.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "common/explorer/etcd_explorer_actor.h"
#include "common/explorer/explorer.h"
#include "common/logs/logging.h"
#include "meta_store_client/meta_store_client.h"
#include "common/resource_view/view_utils.h"
#include "mocks/mock_domain_group_ctrl.h"
#include "mocks/mock_resource_view.h"
#include "uplayer_stub.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/port_helper.h"

namespace functionsystem::domain_scheduler::test {
using namespace functionsystem::test;
using namespace functionsystem::explorer;
using ::testing::_;
using ::testing::Return;

class DomainSchedSrvActorRegisterHelper : public domain_scheduler::DomainSchedSrvActor {
public:
    DomainSchedSrvActorRegisterHelper(const std::string &name, const std::string &metaStoreAddr,
                                      uint32_t maxRegisterTimes = 0, uint32_t registerIntervalMs = 0,
                                      uint32_t putReadyResCycleMs = 0)
        : domain_scheduler::DomainSchedSrvActor(name, MetaStoreClient::Create({ .etcdAddress = metaStoreAddr }), 0,
                                                maxRegisterTimes, registerIntervalMs, putReadyResCycleMs)
    {
    }

    ~DomainSchedSrvActorRegisterHelper() override = default;

    litebus::Future<Status> GetGlobalRegistered()
    {
        return globalRegistered_.GetFuture();
    }

    litebus::Future<Status> GetDomainRegistered()
    {
        return domainRegistered_.GetFuture();
    }

protected:
    void Registered(const messages::Registered &message, RegisterUp &registry) override
    {
        YRLOG_INFO("enter Registered, aid: {}", std::string(registry.aid));
        domain_scheduler::DomainSchedSrvActor::Registered(message, registry);
        if (registry.aid.Name() == DOMAIN_SCHED_MGR_ACTOR_NAME) {
            globalRegistered_.SetValue(Status::OK());
        } else if (registry.aid.Name().find(DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX) != std::string::npos) {
            domainRegistered_.SetValue(Status::OK());
        }
    }

private:
    litebus::Promise<Status> globalRegistered_;
    litebus::Promise<Status> domainRegistered_;
};

class DomainSchedSrvTest : public ::testing::Test {
protected:
    inline static std::string metaStoreServerHost_;
    [[maybe_unused]] static void SetUpTestSuite()
    {
        const auto &address = litebus::GetLitebusAddress();
        address_ = address.ip + ":" + std::to_string(address.port);

        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
    }

    [[maybe_unused]] static void TearDownTestSuite()
    {
        etcdSrvDriver_->StopServer();
    }

    void InitCase(const std::string &name, uint32_t maxRegisterTimes, uint32_t registerIntervalMs,
                  uint32_t putReadyResCycleMs = 5000)
    {
        domainSchedSrvActor_ = std::make_shared<DomainSchedSrvActorRegisterHelper>(
            name, metaStoreServerHost_, maxRegisterTimes, registerIntervalMs, putReadyResCycleMs);

        mockUnderlayerSchedMgr_ = std::make_shared<MockDomainUnderlayerSchedMgr>();
        auto resourceViewMgr = std::make_shared<resource_view::ResourceViewMgr>();
        primary_ = MockResourceView::CreateMockResourceView();
        virtual_ = MockResourceView::CreateMockResourceView();
        resourceViewMgr->primary_ = primary_;
        resourceViewMgr->virtual_ = virtual_;
        mockInstanceCtrl_ = std::make_shared<MockDomainInstanceCtrl>();
        mockGroupCtrl_ = std::make_shared<MockDomainGroupCtrl>();
        domainSchedSrvActor_->BindUnderlayerMgr(mockUnderlayerSchedMgr_);
        domainSchedSrvActor_->BindResourceView(resourceViewMgr);
        domainSchedSrvActor_->BindInstanceCtrl(mockInstanceCtrl_);
        domainSchedSrvActor_->BindDomainGroupCtrl(mockGroupCtrl_);
        litebus::Spawn(domainSchedSrvActor_);
    }

    void Stop()
    {
        litebus::Terminate(domainSchedSrvActor_->GetAID());
        litebus::Await(domainSchedSrvActor_);
        domainSchedSrvActor_ = nullptr;
        mockUnderlayerSchedMgr_ = nullptr;
        primary_ = nullptr;
        virtual_ = nullptr;
        mockInstanceCtrl_ = nullptr;
    }

    void RegisterUplayer(const std::string &upDomainName, const std::string &selfName,
                         const std::shared_ptr<UplayerActor> &globalStub, const std::shared_ptr<UplayerActor> &leadStub,
                         DomainSchedSrv &domainSchedSrv)
    {
        globalStub->SetResponseLeader(upDomainName, address_);
        EXPECT_CALL(*primary_, GetFullResourceView())
            .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));
        EXPECT_CALL(*virtual_, GetFullResourceView())
            .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));
        LeaderResponse response{ .status = Status::OK(),
                                 .header = {},
                                 .kv = std::make_pair(DEFAULT_MASTER_ELECTION_KEY, address_) };
        litebus::Async(explorer_->GetAID(), &EtcdExplorerActor::OnObserveEvent, response);
        auto isGlobalRegistered =
            litebus::Async(domainSchedSrvActor_->GetAID(), &DomainSchedSrvActorRegisterHelper::GetGlobalRegistered);
        ASSERT_AWAIT_READY_FOR(isGlobalRegistered, 1000);
        EXPECT_EQ(globalStub->GetRegisteredName(), selfName);
        EXPECT_EQ(globalStub->GetRegisteredAddress(), address_);

        // check leader register succeed
        auto isDomainRegistered =
            litebus::Async(domainSchedSrvActor_->GetAID(), &DomainSchedSrvActorRegisterHelper::GetDomainRegistered);
        ASSERT_AWAIT_READY_FOR(isDomainRegistered, 1000);
        EXPECT_EQ(leadStub->GetRegisteredName(), selfName);
        EXPECT_EQ(leadStub->GetRegisteredAddress(), address_);
    }

    void SetUp() override
    {
        explorer::LeaderInfo leader = GetLeaderInfo(litebus::AID("function_master"));
        explorer_ = explorer::Explorer::NewStandAloneExplorerActorForMaster(explorer::ElectionInfo{}, leader);
    }

    void TearDown() override
    {
        explorer::Explorer::GetInstance().Clear();
    }

protected:
    inline static std::string address_;
    std::shared_ptr<MockDomainInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockDomainGroupCtrl> mockGroupCtrl_;
    std::shared_ptr<MockDomainUnderlayerSchedMgr> mockUnderlayerSchedMgr_;
    std::shared_ptr<MockResourceView> primary_;
    std::shared_ptr<MockResourceView> virtual_;
    std::shared_ptr<DomainSchedSrvActorRegisterHelper> domainSchedSrvActor_;
    std::shared_ptr<ExplorerActor> explorer_;
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
};

TEST_F(DomainSchedSrvTest, RegisterToGlobalTimeout)
{
    InitCase("RegisterToGlobalTimeout", 5, 2);
    auto unit = std::make_shared<resource_view::ResourceUnit>();
    EXPECT_CALL(*primary_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));
    EXPECT_CALL(*virtual_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));
    auto domainSchedSrv = DomainSchedSrv(domainSchedSrvActor_->GetAID());
    // test global not start
    LeaderResponse response{ .status = Status::OK(),
                             .header = {},
                             .kv = std::make_pair(DEFAULT_MASTER_ELECTION_KEY, address_) };
    litebus::Async(explorer_->GetAID(), &EtcdExplorerActor::OnObserveEvent, response);
    auto isGlobalRegistered =
        litebus::Async(domainSchedSrvActor_->GetAID(), &DomainSchedSrvActorRegisterHelper::GetGlobalRegistered);
    ASSERT_AWAIT_NO_SET_FOR(isGlobalRegistered, 1000);
    Stop();
}

TEST_F(DomainSchedSrvTest, RegisterToGlobalNormalAndNotifyAbnormal)
{
    InitCase("RegisterToGlobalNormalAndNotifyAbnormal", 5, 1000);
    auto domainSchedSrv = DomainSchedSrv(domainSchedSrvActor_->GetAID());
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UpdateUnderlayerTopo).WillOnce(Return());
    auto unit = std::make_shared<resource_view::ResourceUnit>();
    EXPECT_CALL(*primary_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));
    EXPECT_CALL(*virtual_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>())));

    // test global start
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    LeaderResponse response{ .status = Status::OK(),
                             .header = {},
                             .kv = std::make_pair(DEFAULT_MASTER_ELECTION_KEY, address_) };
    litebus::Async(explorer_->GetAID(), &EtcdExplorerActor::OnObserveEvent, response);
    auto isGlobalRegistered =
        litebus::Async(domainSchedSrvActor_->GetAID(), &DomainSchedSrvActorRegisterHelper::GetGlobalRegistered);
    ASSERT_AWAIT_READY_FOR(isGlobalRegistered, 1000);
    EXPECT_EQ(globalStub->GetRegisteredName(), "RegisterToGlobalNormalAndNotifyAbnormal");
    EXPECT_EQ(globalStub->GetRegisteredAddress(), address_);

    // test notify abnormal to global
    const std::string abnormalSched = "AbnormalSched";
    messages::NotifySchedAbnormalRequest req;
    req.set_schedname(abnormalSched);
    auto notify = domainSchedSrv.NotifySchedAbnormal(req);
    ASSERT_AWAIT_READY_FOR(notify, 1000);
    EXPECT_EQ(globalStub->GetAbnormalName(), abnormalSched);
    // notify worker status
    messages::NotifyWorkerStatusRequest workerReq;
    workerReq.set_healthy(false);
    workerReq.set_workerip("10.0.0.0");
    auto workerNotify = domainSchedSrv.NotifyWorkerStatus(workerReq);
    ASSERT_AWAIT_READY_FOR(workerNotify, 1000);
    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

class DomainSchedSrvActorHelper : public domain_scheduler::DomainSchedSrvActor {
public:
    explicit DomainSchedSrvActorHelper(const std::string &name, const std::string &etcdAddress)
        : domain_scheduler::DomainSchedSrvActor(name, MetaStoreClient::Create({ .etcdAddress = etcdAddress }))
    {
    }

    ~DomainSchedSrvActorHelper() override = default;

    void SetUplayerHelper(const litebus::AID &aid)
    {
        RegisterUp uplayer;
        uplayer.aid = aid;
        SetUplayer(uplayer);
    }

    void SetGlobalHelper(const litebus::AID &aid)
    {
        RegisterUp global;
        global.aid = aid;
        SetGlobal(global);
    }

    void PingPongLostHelper(const litebus::AID &lostDst)
    {
        PingPongLost(lostDst);
    }

    void UpdateLeaderHelper(const std::string &name, const std::string &address)
    {
        UpdateLeader(name, address);
    }

    MOCK_METHOD(litebus::Future<Status>, RegisterToGlobal, (), (override));
    MOCK_METHOD(void, RegisterToLeader, (), (override));
};

TEST_F(DomainSchedSrvTest, NotifyAbnormalFailWithNoGlobalAndUplayer)
{
    DomainSchedSrvActorHelper domainSchedSrvActorHelper("test", metaStoreServerHost_);
    litebus::AID connDst("conn", "127.0.0.1:12345"); // ���ᷢ����ʵ������
    domainSchedSrvActorHelper.SetUplayerHelper(connDst);
    domainSchedSrvActorHelper.SetGlobalHelper(connDst);

    messages::NotifySchedAbnormalRequest req;
    auto status = domainSchedSrvActorHelper.NotifySchedAbnormal(req);

    ASSERT_AWAIT_READY_FOR(status, 1000);
    EXPECT_EQ(status.Get().StatusCode(), StatusCode::DOMAIN_SCHEDULER_REGISTER_ERR);
}

TEST_F(DomainSchedSrvTest, NotifyWorkerFailWithNoGlobalAndUplayer)
{
    DomainSchedSrvActorHelper domainSchedSrvActorHelper("test", metaStoreServerHost_);
    litebus::AID connDst("conn", "127.0.0.1:12345"); // ���ᷢ����ʵ������
    domainSchedSrvActorHelper.SetUplayerHelper(connDst);
    domainSchedSrvActorHelper.SetGlobalHelper(connDst);

    messages::NotifyWorkerStatusRequest req;
    auto status = domainSchedSrvActorHelper.NotifyWorkerStatus(req);
    ASSERT_AWAIT_READY_FOR(status, 1000);
    EXPECT_EQ(status.Get().StatusCode(), StatusCode::DOMAIN_SCHEDULER_REGISTER_ERR);
}

TEST_F(DomainSchedSrvTest, PingPongLostFail)
{
    DomainSchedSrvActorHelper domainSchedSrvActorHelper("test", metaStoreServerHost_);
    litebus::AID connDst("conn", "127.0.0.1:12345"); // ������ò�����Mock����
    litebus::AID lostDst("lost", "127.0.0.1:12345"); // ������ò�����Mock����
    domainSchedSrvActorHelper.SetUplayerHelper(lostDst);

    EXPECT_CALL(domainSchedSrvActorHelper, RegisterToLeader()).WillRepeatedly(Return());
    domainSchedSrvActorHelper.PingPongLostHelper(lostDst);

    domainSchedSrvActorHelper.SetUplayerHelper(connDst);
    domainSchedSrvActorHelper.SetGlobalHelper(lostDst);
    EXPECT_CALL(domainSchedSrvActorHelper, RegisterToGlobal()).WillOnce(Return(Status::OK()));

    domainSchedSrvActorHelper.PingPongLostHelper(lostDst);
}

TEST_F(DomainSchedSrvTest, UpdateLeaderFail)
{
    DomainSchedSrvActorHelper domainSchedSrvActorHelper("test", metaStoreServerHost_);
    litebus::AID connDst("conn", "127.0.0.1:12345");
    domainSchedSrvActorHelper.SetUplayerHelper(connDst);

    EXPECT_CALL(domainSchedSrvActorHelper, RegisterToLeader()).WillRepeatedly(Return());
    domainSchedSrvActorHelper.UpdateLeaderHelper("conn", "127.0.0.1:12345");
}

resource_view::ResourceUnit GenAgentUnit(const std::string &name, double value, const std::string &ownerId,
                                         const std::string &alias = "")
{
    resource_view::ResourceUnit agentUnit;
    agentUnit.set_id(name);
    agentUnit.set_ownerid(ownerId);
    agentUnit.set_alias(alias);
    auto agentCapacity = agentUnit.mutable_capacity();
    auto capResources = agentCapacity->mutable_resources();
    resource_view::Resource cpuRes;
    cpuRes.mutable_scalar()->set_value(value);
    capResources->insert({ resource_view::CPU_RESOURCE_NAME, cpuRes });
    return agentUnit;
}

resource_view::ResourceUnit GenUnitByFragment(
    const std::vector<std::pair<std::string, resource_view::ResourceUnit>> &fragment, const std::string &name = "")
{
    resource_view::ResourceUnit unit;
    unit.set_id(name);
    auto unitFragment = unit.mutable_fragment();
    for (const auto &fragmentIter : fragment) {
        unitFragment->insert({ fragmentIter.first, fragmentIter.second });
    }

    return unit;
}

TEST_F(DomainSchedSrvTest, PutReadyAgent)
{
    InitCase("RegisterToGlobalNormalAndNotifyAbnormal", 5, 1000, 100);
    auto domainSchedSrv = DomainSchedSrv(domainSchedSrvActor_->GetAID());
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UpdateUnderlayerTopo).WillOnce(Return());

    std::vector<std::pair<std::string, resource_view::ResourceUnit>> agentUnits1 = {
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(1),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(1), 1, "local1") },
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(2),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(2), 500, "local1") },
        { "custom-" + FUNCTION_AGENT_ID_PREFIX + std::to_string(3),
          GenAgentUnit("custom-" + FUNCTION_AGENT_ID_PREFIX + std::to_string(3), 500, "local1") },
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(11),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(11), 1, "local2") },
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(12),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(12), 500, "local2") },
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(13),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(13), 500, "local2") },
    };

    auto domainUnit1 = GenUnitByFragment(agentUnits1);

    std::vector<std::pair<std::string, resource_view::ResourceUnit>> agentUnits2(agentUnits1.end() - 3,
                                                                                 agentUnits1.end());
    auto domainUnit2 = GenUnitByFragment(agentUnits2);

    EXPECT_CALL(*primary_, GetFullResourceView())
        .WillOnce(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(domainUnit1))))
        .WillOnce(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(domainUnit1))))
        .WillOnce(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(domainUnit2))))
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(domainUnit1))));

    EXPECT_CALL(*virtual_, GetFullResourceView())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(domainUnit1))));

    uint32_t putCnt = 0;
    uint32_t readyResCnt = 0;
    auto observer = [&](const std::vector<WatchEvent> &events, bool) -> bool {
        for (const WatchEvent &event : events) {
            switch (event.eventType) {
                case EVENT_TYPE_PUT: {
                    putCnt++;
                    readyResCnt = std::stoul(event.kv.value());
                    break;
                }
                default: {
                    break;
                }
            }
        }
        return true;
    };
    MetaStoreClient client(MetaStoreConfig{ .etcdAddress = metaStoreServerHost_ });
    client.Init();
    WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
    auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    auto watcher = client.Watch(READY_AGENT_CNT_KEY, option, observer, syncer).Get();
    ASSERT_AWAIT_TRUE([&watcher]() -> bool { return watcher->GetWatchId() == 0; });

    // test global start
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    LeaderResponse response{ .status = Status::OK(),
                             .header = {},
                             .kv = std::make_pair(DEFAULT_MASTER_ELECTION_KEY, address_) };
    litebus::Async(explorer_->GetAID(), &EtcdExplorerActor::OnObserveEvent, response);
    auto isGlobalRegistered =
        litebus::Async(domainSchedSrvActor_->GetAID(), &DomainSchedSrvActorRegisterHelper::GetGlobalRegistered);
    ASSERT_AWAIT_READY_FOR(isGlobalRegistered, 1000);
    EXPECT_EQ(globalStub->GetRegisteredName(), "RegisterToGlobalNormalAndNotifyAbnormal");
    EXPECT_EQ(globalStub->GetRegisteredAddress(), address_);

    ASSERT_AWAIT_TRUE([&putCnt]() -> bool { return putCnt == 3; });
    EXPECT_EQ(readyResCnt, 4u);
    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, ReceiveLeaderTopoFromGlobalToRegister)
{
    InitCase("ReceiveLeaderTopoFromGlobalToRegister", 5, 1000);
    auto domainSchedSrv = DomainSchedSrv(domainSchedSrvActor_->GetAID());
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UpdateUnderlayerTopo).WillOnce(Return());

    // test global start
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);
    auto leadStub = std::make_shared<UplayerActor>("d1" + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    litebus::Spawn(leadStub);
    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*leadStub, MockUpdateResource(_, _, _))
        .WillRepeatedly(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));
    resource_view::ResourceUnit successRet;
    successRet.set_id("ReceiveLeaderTopoFromGlobalToRegister");
    EXPECT_CALL(*primary_, GetSerializedResourceView()).WillOnce(Return(AsyncReturn(successRet.SerializeAsString())));
    RegisterUplayer("d1", "ReceiveLeaderTopoFromGlobalToRegister", globalStub, leadStub, domainSchedSrv);
    ASSERT_AWAIT_READY_FOR(name, 5000);
    EXPECT_EQ(name.Get(), "UpdateResources");
    resource_view::ResourceUnit rsp;
    EXPECT_EQ(rsp.ParseFromString(msg.Get()), true);
    EXPECT_EQ(rsp.id(), "ReceiveLeaderTopoFromGlobalToRegister");

    // test notify abnormal to leader
    const std::string abnormalSched = "AbnormalSched";
    messages::NotifySchedAbnormalRequest req;
    req.set_schedname(abnormalSched);
    auto notify = domainSchedSrv.NotifySchedAbnormal(req);
    EXPECT_AWAIT_READY_FOR(notify, 1000);
    EXPECT_EQ(leadStub->GetAbnormalName(), abnormalSched);
    litebus::Terminate(globalStub->GetAID());
    litebus::Terminate(leadStub->GetAID());
    litebus::Await(globalStub);
    litebus::Await(leadStub);
    Stop();
}

static bool operator==(const messages::ScheduleTopology &lhs, const messages::ScheduleTopology &rhs)
{
    if (lhs.has_leader() != rhs.has_leader() || lhs.members_size() != rhs.members_size()) {
        return false;
    }
    if (lhs.leader().address() != rhs.leader().address() || lhs.leader().name() != rhs.leader().name()) {
        return false;
    }
    for (auto i = 0; i < lhs.members_size(); i++) {
        if (lhs.members(i).address() != rhs.members(i).address() || lhs.members(i).name() != rhs.members(i).name()) {
            return false;
        }
    }
    return true;
}

TEST_F(DomainSchedSrvTest, UpdateSchedTopoView)
{
    InitCase("UpdateSchedTopoView", 5, 1000);

    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);
    auto leadStub = std::make_shared<UplayerActor>("d2" + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    litebus::Spawn(leadStub);

    messages::ScheduleTopology topo;
    topo.mutable_leader()->set_address(address_);
    topo.mutable_leader()->set_name("d2");
    auto member = topo.add_members();
    member->set_address(address_);
    member->set_name("member");

    messages::ScheduleTopology topo2;
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UpdateUnderlayerTopo(_))
        .WillOnce(::testing::Invoke([topo](const messages::ScheduleTopology &topoIn) {
            if (topo == topoIn) {
                EXPECT_EQ(1, 1);
                return;
            }
            EXPECT_EQ(1, 0);
        }));
    auto unit = std::make_shared<resource_view::ResourceUnit>();
    EXPECT_CALL(*primary_, GetFullResourceView()).WillRepeatedly(Return(AsyncReturn(unit)));
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(),
                   "UpdateSchedTopoView", topo.SerializeAsString());
    litebus::Terminate(globalStub->GetAID());
    litebus::Terminate(leadStub->GetAID());
    litebus::Await(globalStub);
    litebus::Await(leadStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, UpdateSchedTopoViewWithNoHeader)
{
    InitCase("UpdateSchedTopoView", 5, 1000);

    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);
    auto leadStub = std::make_shared<UplayerActor>("d2" + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    litebus::Spawn(leadStub);

    messages::ScheduleTopology topo;
    auto member = topo.add_members();
    member->set_address(address_);
    member->set_name("member");

    EXPECT_CALL(*mockUnderlayerSchedMgr_, UpdateUnderlayerTopo(_))
        .WillOnce(::testing::Invoke([topo](const messages::ScheduleTopology &topoIn) {
            if (topo == topoIn) {
                EXPECT_EQ(1, 1);
                return;
            }
            EXPECT_EQ(1, 0);
        }));
    auto unit = std::make_shared<resource_view::ResourceUnit>();
    EXPECT_CALL(*primary_, GetFullResourceView()).WillRepeatedly(Return(AsyncReturn(unit)));
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(),
                   "UpdateSchedTopoView", topo.SerializeAsString());
    litebus::Terminate(globalStub->GetAID());
    litebus::Terminate(leadStub->GetAID());
    litebus::Await(globalStub);
    litebus::Await(leadStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, UpdateSchedTopoViewWithPareseFail)
{
    InitCase("UpdateSchedTopoView", 5, 1000);

    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);
    auto leadStub = std::make_shared<UplayerActor>("d2" + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    litebus::Spawn(leadStub);

    litebus::Future<messages::ScheduleTopology> topo;
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UpdateUnderlayerTopo(_)).WillRepeatedly(testing::DoAll(FutureArg<0>(&topo)));
    auto unit = std::make_shared<resource_view::ResourceUnit>();
    EXPECT_CALL(*primary_, GetFullResourceView()).WillRepeatedly(Return(AsyncReturn(unit)));
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(),
                   "UpdateSchedTopoView", "test");

    ASSERT_AWAIT_NO_SET_FOR(topo, 1000);
    EXPECT_FALSE(topo.IsOK());
    litebus::Terminate(globalStub->GetAID());
    litebus::Terminate(leadStub->GetAID());
    litebus::Await(globalStub);
    litebus::Await(leadStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, PullResources)
{
    InitCase("PullResources", 5, 1000);
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    resource_view::ResourceUnit successRet;
    successRet.set_id("PullResources");
    auto failed = litebus::Future<std::string>();
    failed.SetFailed(StatusCode::FAILED);
    EXPECT_CALL(*primary_, GetSerializedResourceView())
        .WillOnce(Return(AsyncReturn(successRet.SerializeAsString())))
        .WillOnce(Return(AsyncReturn(failed)));

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*globalStub, MockUpdateResource(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(), "PullResources",
                   "");

    ASSERT_AWAIT_READY_FOR(msg, 1000);
    resource_view::ResourceUnit updatedResource;
    EXPECT_TRUE(updatedResource.ParseFromString(msg.Get()));
    EXPECT_EQ(updatedResource.id(), successRet.id());
    EXPECT_CALL(*globalStub, MockUpdateResource(_, _, _)).Times(0);
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(), "PullResources",
                   "");
    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, ForwardScheduleSuccessful)
{
    InitCase("ForwardSchedule", 5, 1000);
    auto domainSchedSrv = DomainSchedSrv(domainSchedSrvActor_->GetAID());
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UpdateUnderlayerTopo).WillOnce(Return());
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);
    auto leadStub = std::make_shared<UplayerActor>("d3" + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    litebus::Spawn(leadStub);
    EXPECT_CALL(*leadStub, MockUpdateResource(_, _, _)).WillRepeatedly(Return());
    RegisterUplayer("d3", "ForwardSchedule", globalStub, leadStub, domainSchedSrv);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    messages::ScheduleResponse rsp;
    rsp.set_code(0);
    rsp.set_requestid("request");
    EXPECT_CALL(*leadStub, MockForwardSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg), Return(rsp.SerializeAsString())));

    auto future = domainSchedSrv.ForwardSchedule(req);

    ASSERT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleRequest recv;
    EXPECT_TRUE(recv.ParseFromString(msg.Get()));
    EXPECT_EQ(recv.requestid(), "request");

    ASSERT_AWAIT_READY_FOR(future, 1000);
    EXPECT_EQ(future.Get()->requestid(), "request");
    litebus::Terminate(globalStub->GetAID());
    litebus::Terminate(leadStub->GetAID());
    litebus::Await(globalStub);
    litebus::Await(leadStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, ForwardScheduleWithParseFail)
{
    InitCase("ForwardSchedule", 5, 1000);
    auto domainSchedSrv = DomainSchedSrv(domainSchedSrvActor_->GetAID());
    EXPECT_CALL(*mockUnderlayerSchedMgr_, UpdateUnderlayerTopo).WillOnce(Return());
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);
    auto leadStub = std::make_shared<UplayerActor>("d3" + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    litebus::Spawn(leadStub);
    EXPECT_CALL(*leadStub, MockUpdateResource(_, _, _)).WillRepeatedly(Return());
    RegisterUplayer("d3", "ForwardSchedule", globalStub, leadStub, domainSchedSrv);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    EXPECT_CALL(*leadStub, MockForwardSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg), Return("test")));

    auto future = domainSchedSrv.ForwardSchedule(req);

    ASSERT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleRequest recv;
    EXPECT_TRUE(recv.ParseFromString(msg.Get()));
    EXPECT_EQ(recv.requestid(), "request");

    ASSERT_AWAIT_NO_SET_FOR(future, 1000);
    EXPECT_FALSE(future.IsOK());
    litebus::Terminate(globalStub->GetAID());
    litebus::Terminate(leadStub->GetAID());
    litebus::Await(globalStub);
    litebus::Await(leadStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, ForwardScheduleWithNoUplayer)
{
    InitCase("ForwardSchedule", 5, 1000);
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    auto future = litebus::Async(domainSchedSrvActor_->GetAID(), &DomainSchedSrvActor::ForwardSchedule, req);
    ASSERT_AWAIT_READY_FOR(future, 1000);
    const auto &ret = future.Get();
    EXPECT_EQ(ret->requestid(), "request");
    EXPECT_EQ(ret->code(), StatusCode::DOMAIN_SCHEDULER_FORWARD_ERR);
    Stop();
}

/**
 * Description: test shedule successful from uplayer or fcaccessor
 * Steps:
 * 1. mock instanceCtrl Schedule successful
 * 2. mock resourceView GetResourceView successful
 * 3. mock uplayer ResponseSchedule to receive result
 * Expectation:
 * return successful
 */
TEST_F(DomainSchedSrvTest, ScheduleSuccessful)
{
    InitCase("ScheduleSuccessful", 5, 1000);
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*globalStub, MockResponseSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));

    auto rsp = std::make_shared<messages::ScheduleResponse>();
    rsp->set_requestid("request");
    rsp->set_code(0);
    EXPECT_CALL(*mockInstanceCtrl_, Schedule(_)).WillOnce(Return(rsp));
    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(), "Schedule",
                   req->SerializeAsString());

    ASSERT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleResponse recv;
    EXPECT_TRUE(recv.ParseFromString(msg.Get()));
    EXPECT_EQ(recv.code(), 0);
    EXPECT_EQ(recv.requestid(), "request");
    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

/**
 * Description: test shedule failed from uplayer or fcaccessor
 * Steps:
 * 1. mock instanceCtrl Schedule failed
 * 3. mock uplayer ResponseSchedule to receive result
 * Expectation:
 * return successful
 */
TEST_F(DomainSchedSrvTest, ScheduleFailed)
{
    InitCase("ScheduleFailed", 5, 1000);
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*globalStub, MockResponseSchedule(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));

    auto rsp = std::make_shared<messages::ScheduleResponse>();
    rsp->set_requestid("request");
    rsp->set_code(StatusCode::RESOURCE_NOT_ENOUGH);
    EXPECT_CALL(*mockInstanceCtrl_, Schedule(_)).WillOnce(Return(AsyncReturn(rsp)));
    EXPECT_CALL(*primary_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));
    EXPECT_CALL(*virtual_, GetResourceViewChanges())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnitChanges>())));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(), "Schedule",
                   req->SerializeAsString());

    ASSERT_AWAIT_READY_FOR(msg, 1000);
    messages::ScheduleResponse recv;
    EXPECT_TRUE(recv.ParseFromString(msg.Get()));
    EXPECT_EQ(recv.code(), StatusCode::RESOURCE_NOT_ENOUGH);
    EXPECT_EQ(recv.requestid(), "request");
    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

/**
 * Description: test shedule failed from uplayer or fcaccessor
 * Steps:
 * 1. mock error request of schedule
 * Expectation:
 * return fail
 */
TEST_F(DomainSchedSrvTest, ScheduleWithParseFail)
{
    InitCase("ScheduleFailed", 5, 1000);
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*globalStub, MockResponseSchedule(_, _, _))
        .WillRepeatedly(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("request");
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(), "Schedule",
                   "test");

    ASSERT_AWAIT_NO_SET_FOR(msg, 1000);
    EXPECT_FALSE(msg.IsOK());
    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

/**
 * Description: test query agent info
 * Steps:
 * 1. mock GetFullResourceView to get resource view
 * 3. mock uplayer ResponseQueryAgentInfo to receive result
 * Expectation:
 * return successful
 */
static bool operator==(const resources::AgentInfo &lhs, const resources::AgentInfo &rhs)
{
    YRLOG_ERROR("lhs: {}, {}, {}", lhs.localid(), lhs.agentid(), lhs.alias());
    YRLOG_ERROR("rhs: {}, {}, {}", rhs.localid(), rhs.agentid(), rhs.alias());
    return (lhs.localid() == rhs.localid() && lhs.agentid() == rhs.agentid() && lhs.alias() == rhs.alias());
}

resources::AgentInfo GenAgentInfo(const std::string &localID, const std::string &agentID, const std::string &alias)
{
    resources::AgentInfo info;
    info.set_localid(localID);
    info.set_agentid(agentID);
    info.set_alias(alias);
    return info;
}

TEST_F(DomainSchedSrvTest, QueryAgentInfo)
{
    InitCase("ScheduleFailed", 5, 1000);
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*globalStub, MockResponseQueryAgentInfo(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));

    // Create 2 agent units
    std::vector<std::pair<std::string, resource_view::ResourceUnit>> agentUnits = {
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(1),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(1), 1, "local1", "alias1") },
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(2),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(2), 500, "local1", "alias2") },
        { "custom-" + FUNCTION_AGENT_ID_PREFIX + std::to_string(3),
          GenAgentUnit("custom-" + FUNCTION_AGENT_ID_PREFIX + std::to_string(3), 500, "local1", "alias3") },
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(11),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(11), 1, "local2", "alias11") },
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(12),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(12), 500, "local2", "alias12") },
        { FUNCTION_AGENT_ID_PREFIX + std::to_string(13),
          GenAgentUnit(FUNCTION_AGENT_ID_PREFIX + std::to_string(13), 500, "local2", "alias13") },
    };

    auto domainUnit1 = GenUnitByFragment(agentUnits, "domain1");
    EXPECT_CALL(*primary_, GetFullResourceView())
        .WillOnce(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(domainUnit1))));

    auto req = std::make_shared<messages::QueryAgentInfoRequest>();
    req->set_requestid("request");
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(), "QueryAgentInfo",
                   req->SerializeAsString());

    ASSERT_AWAIT_READY_FOR(msg, 1000);
    messages::QueryAgentInfoResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.requestid(), "request");

    std::sort(rsp.mutable_agentinfos()->begin(), rsp.mutable_agentinfos()->end(),
              [](const resources::AgentInfo &a, const resources::AgentInfo &b) {
                  if (a.localid() != b.localid()) {
                      return a.localid() < b.localid();
                  }
                  if (a.agentid() != b.agentid()) {
                      return a.agentid() < b.agentid();
                  }
                  return a.alias() < b.alias();
              });
    auto agentInfos = rsp.agentinfos();
    ASSERT_EQ(agentInfos.size(), 4);
    EXPECT_TRUE(agentInfos[0]
                == GenAgentInfo("local1", "custom-" + FUNCTION_AGENT_ID_PREFIX + std::to_string(3), "alias3"));
    EXPECT_TRUE(agentInfos[1] == GenAgentInfo("local1", FUNCTION_AGENT_ID_PREFIX + std::to_string(2), "alias2"));
    EXPECT_TRUE(agentInfos[2] == GenAgentInfo("local2", FUNCTION_AGENT_ID_PREFIX + std::to_string(12), "alias12"));
    EXPECT_TRUE(agentInfos[3] == GenAgentInfo("local2", FUNCTION_AGENT_ID_PREFIX + std::to_string(13), "alias13"));
    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, TryCancelSchedule)
{
    InitCase("ScheduleFailed", 5, 1000);
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*globalStub, MockTryCancelResponse(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));
    auto expectedReq = litebus::Future<std::shared_ptr<messages::CancelSchedule>>();
    EXPECT_CALL(*mockGroupCtrl_, TryCancelSchedule).WillOnce(testing::DoAll(FutureArg<0>(&expectedReq)));

    auto req = std::make_shared<messages::CancelSchedule>();
    req->set_msgid("request");
    req->set_type(messages::CancelType::JOB);
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(),
                   "TryCancelSchedule", req->SerializeAsString());

    ASSERT_AWAIT_READY(msg);
    EXPECT_EQ(msg.IsOK(), true);
    auto rsp = messages::CancelScheduleResponse();
    rsp.ParseFromString(msg.Get());
    EXPECT_EQ(rsp.msgid(), req->msgid());
    ASSERT_AWAIT_READY(expectedReq);
    EXPECT_EQ(expectedReq.Get()->msgid(), req->msgid());
    EXPECT_EQ(expectedReq.Get()->type(), req->type());
    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

messages::ScheduleRequest GetScheduleRequest()
{
    messages::ScheduleRequest scheduleRequest;

    resource_view::InstanceInfo instanceInfo;
    instanceInfo.set_instanceid("app-script-9527-instanceid");
    instanceInfo.set_requestid("requestIdIdId");
    instanceInfo.set_parentid("parentidIdId");

    resource_view::Resources resources;

    resource_view::Resource resource_cpu = view_utils::GetCpuResource();
    (*resources.mutable_resources())["CPU"] = resource_cpu;
    resource_view::Resource resource_memory = view_utils::GetMemResource();
    (*resources.mutable_resources())["Memory"] = resource_memory;

    instanceInfo.mutable_resources()->CopyFrom(resources);
    scheduleRequest.mutable_instance()->CopyFrom(instanceInfo);

    return scheduleRequest;
}

std::vector<std::shared_ptr<messages::ScheduleRequest>> GetInstanceRequest()
{
    messages::ScheduleRequest scheduleRequest = GetScheduleRequest();
    auto scheduleRequestQueue = std::make_shared<messages::ScheduleRequest>(scheduleRequest);

    std::vector<std::shared_ptr<messages::ScheduleRequest>> queue;
    queue.push_back(scheduleRequestQueue);

    return queue;
}

std::vector<std::shared_ptr<messages::ScheduleRequest>> GetGroupRequest()
{
    messages::ScheduleRequest scheduleRequest = GetScheduleRequest();
    auto scheduleRequestQueue = std::make_shared<messages::ScheduleRequest>(scheduleRequest);

    std::vector<std::shared_ptr<messages::ScheduleRequest>> vector;
    vector.push_back(scheduleRequestQueue);

    return vector;
}

TEST_F(DomainSchedSrvTest, GetSchedulingQueue)
{
    InitCase("GetSchedulingQueueSuccess", 5, 1000);
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    Spawn(globalStub);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*globalStub, MockResponseGetSchedulingQueue(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));

    std::vector<std::shared_ptr<messages::ScheduleRequest>> instanceRequest = GetInstanceRequest();
    EXPECT_CALL(*mockInstanceCtrl_, GetSchedulerQueue()).WillOnce(Return(AsyncReturn(instanceRequest)));

    std::vector<std::shared_ptr<messages::ScheduleRequest>> groupRequest = GetGroupRequest();
    EXPECT_CALL(*mockGroupCtrl_, GetRequests()).WillOnce(Return(AsyncReturn(groupRequest)));

    auto req = std::make_shared<messages::QueryInstancesInfoResponse>();
    std::string requestId = "requestIdIdId";
    req->set_requestid(requestId);

    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(),
                   "GetSchedulingQueue", req->SerializeAsString());

    ASSERT_AWAIT_READY_FOR(msg, 1000);
    messages::QueryInstancesInfoResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.requestid(), requestId);

    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}

TEST_F(DomainSchedSrvTest, QueryResourceInfo)
{
    InitCase("ScheduleFailed", 5, 1000);
    auto globalStub = std::make_shared<UplayerActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(globalStub);

    litebus::Future<std::string> name;
    litebus::Future<std::string> msg;
    EXPECT_CALL(*globalStub, MockResponseQueryResourcesInfo(_, _, _))
        .WillOnce(testing::DoAll(FutureArg<1>(&name), FutureArg<2>(&msg)));

    std::string unitId = "test";
    auto unit = view_utils::Get1DResourceUnit(unitId);
    auto invalid = view_utils::Get1DResourceUnit("invalid");
    invalid.set_status(static_cast<uint32_t>(UnitStatus::TO_BE_DELETED));
    (*unit.mutable_fragment())["invalid"] = invalid;
    EXPECT_CALL(*primary_, GetResourceViewCopy())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(unit))));
    EXPECT_CALL(*virtual_, GetResourceViewCopy())
        .WillRepeatedly(Return(AsyncReturn(std::make_shared<resource_view::ResourceUnit>(unit))));

    auto req = std::make_shared<messages::QueryResourcesInfoRequest>();
    req->set_requestid("request");
    litebus::Async(globalStub->GetAID(), &UplayerActor::SendRequest, domainSchedSrvActor_->GetAID(),
                   "QueryResourcesInfo", req->SerializeAsString());

    ASSERT_AWAIT_READY_FOR(msg, 1000);
    messages::QueryResourcesInfoResponse rsp;
    EXPECT_TRUE(rsp.ParseFromString(msg.Get()));
    EXPECT_EQ(rsp.requestid(), "request");
    ASSERT_EQ(rsp.resource().id(), unitId);
    ASSERT_EQ(rsp.resource().fragment_size(), 0);

    litebus::Terminate(globalStub->GetAID());
    litebus::Await(globalStub);
    Stop();
}
}  // namespace functionsystem::domain_scheduler::test
