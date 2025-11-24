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
#include <gtest/gtest.h>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "function_master/global_scheduler/global_sched.h"
#include "common/scheduler_topology/sched_node.h"
#include "domain_activator.h"
#include "function_master/common/flags/flags.cpp"
#include "global_sched_driver.h"
#include "mocks/mock_domain_sched_mgr.h"
#include "mocks/mock_domain_scheduler_launcher.h"
#include "mocks/mock_local_sched_mgr.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_sched_tree.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

using namespace global_scheduler;
using namespace domain_scheduler;
using namespace ::testing;

const std::string TEST_DOMAIN_ACTIVATOR_ACTOR_NAME = "TestDomainActivator";
const std::string TEST_GLOBAL_SCHEDULER_ACTOR_NAME = "TestGlobalSchedActor";

class GlobalSchedTest : public ::testing::Test {
public:
    [[maybe_unused]] static void SetUpTestSuite()
    {
        metaStoreAddress_ = "127.0.0.1:" + std::to_string(FindAvailablePort());
    }

    void SetUp() override
    {
        auto domainSchedMgr = std::make_unique<MockDomainSchedMgr>();
        mockDomainSchedMgr_ = domainSchedMgr.get();
        EXPECT_CALL(*mockDomainSchedMgr_, Start).WillOnce(Return());
        EXPECT_CALL(*mockDomainSchedMgr_, Stop).WillOnce(Return());
        EXPECT_CALL(*mockDomainSchedMgr_, AddDomainSchedCallback).WillOnce(Return(Status::OK()));
        EXPECT_CALL(*mockDomainSchedMgr_, DelDomainSchedCallback).WillOnce(Return(Status::OK()));
        EXPECT_CALL(*mockDomainSchedMgr_, DelLocalSchedCallback).WillOnce(Return(Status::OK()));
        EXPECT_CALL(*mockDomainSchedMgr_, NotifyWorkerStatusCallback).WillOnce(Return(Status::OK()));
        auto localSchedMgr = std::make_unique<MockLocalSchedMgr>();
        mockLocalSchedMgr_ = localSchedMgr.get();
        EXPECT_CALL(*mockLocalSchedMgr_, Start).WillOnce(Return());
        EXPECT_CALL(*mockLocalSchedMgr_, Stop).WillOnce(Return());
        EXPECT_CALL(*mockLocalSchedMgr_, AddLocalSchedCallback).WillOnce(Return(Status::OK()));
        EXPECT_CALL(*mockLocalSchedMgr_, DelLocalSchedCallback).WillOnce(Return(Status::OK()));
        globalSched_.InitManager(std::move(domainSchedMgr), std::move(localSchedMgr));

        mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>(metaStoreAddress_);

        uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
        explorer::Explorer::NewStandAloneExplorerActorForMaster(explorer::ElectionInfo{},
            GetLeaderInfo(litebus::AID("function_master", "127.0.0.1:" + std::to_string(port))));

        auto getResponse = std::make_shared<GetResponse>();
        EXPECT_CALL(*mockMetaStoreClient_, Get).WillOnce(Return(getResponse));
        EXPECT_CALL(*mockMetaStoreClient_, Watch).WillOnce(Return(nullptr));

        auto topologyTree = std::make_unique<MockSchedTree>(2, 2);
        mockSchedTree_ = topologyTree.get();
        litebus::Promise<bool> promise;
        EXPECT_CALL(*mockSchedTree_, GetRootNode).WillOnce(DoAll(Invoke([promise]() -> Node::TreeNode {
            promise.SetValue(true);
            return nullptr;
        })));

        mockDomainSchedulerLauncher_ = std::make_shared<MockDomainSchedulerLauncher>();
        auto domainActivator = std::make_shared<DomainActivator>(mockDomainSchedulerLauncher_);
        globalSchedActor_ = std::make_shared<GlobalSchedActor>(TEST_GLOBAL_SCHEDULER_ACTOR_NAME, mockMetaStoreClient_,
                                                               domainActivator, std::move(topologyTree));
        globalSchedActor_->BindCheckLocalAbnormalCallback(
            [](const std::string &nodeID) -> litebus::Future<bool> { return false; });

        ASSERT_TRUE(globalSched_.Start(globalSchedActor_).IsOk());
        ASSERT_AWAIT_READY(promise.GetFuture());
    }

    void TearDown() override
    {
        explorer::Explorer::GetInstance().Clear();
    }

protected:
    GlobalSched globalSched_;
    inline static std::string metaStoreAddress_;
    std::shared_ptr<GlobalSchedActor> globalSchedActor_;
    std::shared_ptr<MockMetaStoreClient> mockMetaStoreClient_;
    std::shared_ptr<MockDomainSchedulerLauncher> mockDomainSchedulerLauncher_;
    MockSchedTree *mockSchedTree_ = nullptr;
    MockDomainSchedMgr *mockDomainSchedMgr_ = nullptr;
    MockLocalSchedMgr *mockLocalSchedMgr_ = nullptr;
    Node::TreeNode nullNode_ = nullptr;
    Node::TreeNode rootNode_ = std::make_shared<SchedNode>(NodeInfo{ "root", "root" }, 1);
    Node::TreeNode localSched_ = std::make_shared<SchedNode>(NodeInfo{ "local", "local" }, 0);
};

/*case
 * @title: GlobalScheduler启动恢复Scheduler拓扑关系
 * @type: Function test
 * @precondition:
 * @step:  1.初始化GlobalSched类和及其依赖、初始化相关Mock类
 * @step:  2.调用GlobalSched类的Start方法，会走到GlobalSchedulerActor的Init方法
 * @expect:  1.当从MetaStore恢复拓扑关系超时时，GlobalSched启动失败
 * @expect:  2.当从MetaStore恢复拓扑关系反序列失败时，GlobalSched启动失败
 * @expect:  3.当从MetaStore恢复拓扑关系成功时并为空时，GlobalSched启动成功
 * @expect:  4.当从MetaStore恢复拓扑关系成功时不为空时，GlobalSched启动成功，调用DomainSchedMgr的Connect方法连接顶层Domain
 */
TEST_F(GlobalSchedTest, StartGlobalScheduler)
{
    globalSched_.Stop();
    globalSched_.Await();
    auto globalSched = GlobalSched();
    auto domainSchedMgr = std::make_unique<MockDomainSchedMgr>("MockDomainSchedMgr1");
    auto localSchedMgr = std::make_unique<MockLocalSchedMgr>("MockLocalSchedMgr1");
    EXPECT_CALL(*domainSchedMgr, Connect).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*domainSchedMgr, AddDomainSchedCallback).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*domainSchedMgr, DelDomainSchedCallback).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*domainSchedMgr, DelLocalSchedCallback).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*domainSchedMgr, NotifyWorkerStatusCallback).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*domainSchedMgr, Schedule).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*localSchedMgr, AddLocalSchedCallback).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*localSchedMgr, DelLocalSchedCallback).WillRepeatedly(Return(Status::OK()));
    globalSched.InitManager(std::move(domainSchedMgr), std::move(localSchedMgr));

    auto mockMetaStoreClient = std::make_shared<MockMetaStoreClient>(metaStoreAddress_);

    auto topologyTree = std::make_unique<MockSchedTree>(2, 2);
    auto mockDomainSchedulerLauncher = std::make_shared<MockDomainSchedulerLauncher>();
    auto domainActivator = std::make_shared<DomainActivator>(mockDomainSchedulerLauncher);
    auto globalSchedActor = std::make_shared<GlobalSchedActor>("TEST_GLOBAL_SCHEDULER_ACTOR_NAME", mockMetaStoreClient,
                                                               domainActivator, std::move(topologyTree));
    globalSchedActor->BindCheckLocalAbnormalCallback(
        [](const std::string &nodeID) -> litebus::Future<bool> { return false; });

    // Failed to get topology info.
    auto getResponse = std::make_shared<GetResponse>();
    getResponse->status = Status(StatusCode::FAILED);
    getResponse->kvs = {};
    bool isFinished = false;
    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(DoAll(Assign(&isFinished, true), Return(getResponse)));
    EXPECT_TRUE(globalSched.Start(globalSchedActor).IsOk());
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });
    globalSched.Stop();
    globalSched.Await();

    // Failed to recover topology tree.
    auto getResponse1 = std::make_shared<GetResponse>();
    getResponse1->kvs = { {} };
    isFinished = false;
    topologyTree = std::make_unique<MockSchedTree>(2, 2);
    EXPECT_CALL(*topologyTree, RecoverFromString).WillOnce(Return(Status(StatusCode::FAILED)));
    globalSchedActor = std::make_shared<GlobalSchedActor>("TEST_GLOBAL_SCHEDULER_ACTOR_NAME1", mockMetaStoreClient,
                                                          domainActivator, std::move(topologyTree));
    globalSchedActor->BindCheckLocalAbnormalCallback(
        [](const std::string &nodeID) -> litebus::Future<bool> { return false; });
    EXPECT_CALL(*mockMetaStoreClient, Get).WillRepeatedly(DoAll(Assign(&isFinished, true), Return(getResponse1)));
    EXPECT_TRUE(globalSched.Start(globalSchedActor).IsOk());
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });
    globalSched.Stop();
    globalSched.Await();

    // GetRootNode return nullNode.
    isFinished = false;
    topologyTree = std::make_unique<MockSchedTree>(2, 2);
    EXPECT_CALL(*topologyTree, RecoverFromString).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*topologyTree, GetRootNode).WillRepeatedly(Return(rootNode_));
    globalSchedActor = std::make_shared<GlobalSchedActor>("TEST_GLOBAL_SCHEDULER_ACTOR_NAME2", mockMetaStoreClient,
                                                          domainActivator, std::move(topologyTree));
    globalSchedActor->BindCheckLocalAbnormalCallback(
        [](const std::string &nodeID) -> litebus::Future<bool> { return false; });
    EXPECT_TRUE(globalSched.Start(globalSchedActor).IsOk());
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });
    globalSched.Stop();
    globalSched.Await();

    // GetRootNode return rootNode.
    isFinished = false;
    auto topologyTree1 = std::make_unique<MockSchedTree>(2, 2);
    EXPECT_CALL(*topologyTree1, RecoverFromString).WillRepeatedly(Return(Status::OK()));
    EXPECT_CALL(*topologyTree1, GetRootNode).WillRepeatedly(Return(rootNode_));
    globalSchedActor = std::make_shared<GlobalSchedActor>("TEST_GLOBAL_SCHEDULER_ACTOR_NAME3", mockMetaStoreClient,
                                                          domainActivator, std::move(topologyTree1));
    globalSchedActor->BindCheckLocalAbnormalCallback(
        [](const std::string &nodeID) -> litebus::Future<bool> { return false; });
    EXPECT_TRUE(globalSched.Start(globalSchedActor).IsOk());
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("req-123");
    req->mutable_instance()->set_instanceid("instance-123");
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });
    auto scheduleRet = globalSched.Schedule(req);
    EXPECT_TRUE(scheduleRet.Get().IsOk());
    globalSched.Stop();
    globalSched.Await();
}

/*case
 * @title: GlobalScheduler添加LocalScheduler节点并激活DomainScheduler成功
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1.调用GlobalSchedActor的AddLocalSchedHandler方法，并传入LocalScheduler信息
 * @expect:  1.LocalScheduler的信息被缓存，并成功激活新的DomainScheduler
 */
TEST_F(GlobalSchedTest, AddLocalSchedAndActivateDomainSuccess)
{
    EXPECT_CALL(*mockSchedTree_, AddLeafNode).WillOnce(Return(nullptr));
    bool isFinished = false;
    EXPECT_CALL(*mockDomainSchedulerLauncher_, Start).WillOnce(DoAll(Assign(&isFinished, true), Return(Status::OK())));
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::AddLocalSchedHandler, "LocalSched-AID", "local",
                   "127.0.0.1:1");
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });

    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: GlobalScheduler添加LocalScheduler节点并激活DomainScheduler失败
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1.调用GlobalSchedActor的AddLocalSchedHandler方法，并传入LocalScheduler信息
 * @expect:  1.激活新的DomainScheduler失败后，LocalScheduler的信息没有被缓存，并向LocalScheduler返回注册失败消息
 */
TEST_F(GlobalSchedTest, AddLocalSchedAndActivateDomainFail)
{
    const litebus::AID from = "LocalSched-AID";
    EXPECT_CALL(*mockSchedTree_, AddLeafNode).WillOnce(Return(nullptr));
    EXPECT_CALL(*mockDomainSchedulerLauncher_, Start).WillOnce(Return(Status(StatusCode::FAILED)));

    bool isFinished = false;
    EXPECT_CALL(*mockLocalSchedMgr_, Registered(from, _)).WillOnce(DoAll(Assign(&isFinished, true), Return()));
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::AddLocalSchedHandler, from, "local", "127.0.0.1:1");
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });

    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 已存在DomainScheduler节点，添加LocalScheduler节点成功
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1.调用GlobalSchedActor的AddLocalSchedHandler方法，并传入LocalScheduler信息
 * @expect:  1.将LocalScheduler添加到已存在DomainScheduler节点下层，并更新MetaStore拓扑图信息，
 *             并通知DomainScheduler更新拓扑关系，并向LocalScheduler返回注册成功消息
 */
TEST_F(GlobalSchedTest, AddLocalSchedSuccess)
{
    const std::string domainAddress = "127.0.0.1:1";
    const litebus::AID from = "LocalSched-AID";
    auto parent = std::make_shared<SchedNode>(NodeInfo{ "domain", domainAddress }, 1);
    auto child = std::make_shared<SchedNode>(NodeInfo{ "local", "127.0.0.1:2" }, 0);
    parent->AddChild(child);
    EXPECT_CALL(*mockSchedTree_, AddLeafNode).WillOnce(Return(child));
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillOnce(Return("fake topology info"));
    EXPECT_CALL(*mockDomainSchedMgr_, UpdateSchedTopoView("domain", domainAddress, _)).Times(1);
    EXPECT_CALL(*mockLocalSchedMgr_, Registered(from, _)).Times(1);
    auto putResponse = std::make_shared<PutResponse>();
    bool isFinished = false;
    auto promise = std::make_shared<litebus::Promise<std::string>>();
    globalSchedActor_->BindLocalAddCallback([promise](const std::string &node){
        promise->SetValue(node);
    });
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(DoAll(Assign(&isFinished, true), Return(putResponse)));
    globalSchedActor_->AddLocalSchedHandler(from, "local", "127.0.0.1:1");
    ASSERT_AWAIT_TRUE([&]() { return isFinished; });
    ASSERT_AWAIT_READY(promise->GetFuture());
    EXPECT_EQ(promise->GetFuture().Get(), "local");
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 删除一个不存在的LocalScheduler节点
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1.调用GlobalSchedActor的DelLocalSchedHandler方法，并传入LocalScheduler信息
 * @expect:  1. 删除LocalScheduler节点失败，并且不会更新DomainScheduler的拓扑关系
 */
TEST_F(GlobalSchedTest, DeleteLocalSchedNotExist)
{
    EXPECT_CALL(*mockSchedTree_, RemoveLeafNode).WillOnce(Return(nullptr));
    EXPECT_CALL(*mockDomainSchedMgr_, UpdateSchedTopoView(_, _, _)).Times(0);
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::DelLocalSchedHandler, "local", LocalExitType::ABNORMAL);
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 删除LocalScheduler节点成功
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1.调用GlobalSchedActor的DelLocalSchedHandler方法，并传入LocalScheduler信息
 * @expect:  1. 删除LocalScheduler节点成功，并更新MetaStore中拓扑图信息，并且通知DomainScheduler更新拓扑关系
 */
TEST_F(GlobalSchedTest, DeleteAbnormalLocalSchedSuccess)
{
    const std::string domainAddress = "127.0.0.1:1";
    auto parent = std::make_shared<SchedNode>(NodeInfo{ "domain", domainAddress }, 1);
    EXPECT_CALL(*mockSchedTree_, RemoveLeafNode).WillOnce(Return(parent));
    EXPECT_CALL(*mockDomainSchedMgr_, UpdateSchedTopoView("domain", domainAddress, _)).Times(1);
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillOnce(Return("fake topology info"));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::DelLocalSchedHandler, "local", LocalExitType::ABNORMAL);
    globalSched_.Stop();
    globalSched_.Await();
}

TEST_F(GlobalSchedTest, DeleteExitedLocalSchedSuccess)
{
    const std::string domainAddress = "127.0.0.1:1";
    auto parent = std::make_shared<SchedNode>(NodeInfo{ "domain", domainAddress }, 1);
    EXPECT_CALL(*mockSchedTree_, RemoveLeafNode).WillOnce(Return(parent));
    EXPECT_CALL(*mockDomainSchedMgr_, UpdateSchedTopoView("domain", domainAddress, _)).Times(1);
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillOnce(Return("fake topology info"));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));
    auto promise = std::make_shared<litebus::Promise<std::string>>();
    globalSchedActor_->BindLocalDeleteCallback([promise](const std::string &node){
        promise->SetValue(node);
    });
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::DelLocalSchedHandler, "local", LocalExitType::UNREGISTER);
    ASSERT_AWAIT_READY(promise->GetFuture());
    EXPECT_EQ(promise->GetFuture().Get(), "local");
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 添加Root DomainScheduler
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1.调用GlobalSchedActor的AddDomainSchedHandler方法，并传入DomainScheduler信息
 * @expect:  1. 添加DomainScheduler节点成功，并更新MetaStore中拓扑图信息，并且通知DomainScheduler注册成功，
 *           并与DomainScheduler建立心跳连接
 */
TEST_F(GlobalSchedTest, AddRootDomainScheduler)
{
    const litebus::AID from = "DomainSched-AID";
    const std::string domainName = "domain";
    const std::string domainAddress = "127.0.0.1:1";
    auto domain = std::make_shared<SchedNode>(NodeInfo{ domainName, domainAddress }, 1);
    EXPECT_CALL(*mockSchedTree_, AddNonLeafNode).WillOnce(Return(domain)).WillOnce(Return(domain));
    EXPECT_CALL(*mockDomainSchedMgr_, Disconnect).Times(1);
    EXPECT_CALL(*mockDomainSchedMgr_, Connect(domainName, domainAddress)).WillOnce(Return(Status::OK()));
    litebus::Future<litebus::Option<messages::ScheduleTopology>> future;
    EXPECT_CALL(*mockDomainSchedMgr_, Registered(from, _)).WillOnce(Return()).WillOnce(FutureArg<1>(&future));
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillOnce(Return("fake topology info"));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::AddDomainSchedHandler, from, domainName,
                   domainAddress);
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::AddDomainSchedHandler, from, domainName,
                   "127.0.0.1:2");
    ASSERT_AWAIT_READY(future);
    EXPECT_TRUE(future.Get().IsNone());
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 添加Child DomainScheduler
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1. 设定DomainScheduler2的父节点是DomainScheduler1
 * @step:  2.调用GlobalSchedActor的AddDomainSchedHandler方法，并传入DomainScheduler2信息
 * @expect:  1. 添加DomainScheduler2节点成功，并更新MetaStore中拓扑图信息，并且通知DomainScheduler2注册成功，
 *           并通知DomainScheduler1更新拓扑关系
 */
TEST_F(GlobalSchedTest, AddDomainSchedulerBecomeChild)
{
    const std::string domainName1 = "domain1";
    const std::string domainAddress1 = "127.0.0.1:1";
    const std::string domainName2 = "domain2";
    const std::string domainAddress2 = "127.0.0.1:2";
    const litebus::AID from = "DomainSched-AID";
    auto domain1 = std::make_shared<SchedNode>(NodeInfo{ domainName1, domainAddress1 }, 1);
    auto domain2 = std::make_shared<SchedNode>(NodeInfo{ domainName2, domainAddress2 }, 1);

    domain1->AddChild(domain2);
    EXPECT_CALL(*mockSchedTree_, AddNonLeafNode).WillOnce(Return(domain2));
    EXPECT_CALL(*mockDomainSchedMgr_, UpdateSchedTopoView(domainName1, domainAddress1, _)).Times(1);
    EXPECT_CALL(*mockDomainSchedMgr_, Registered(from, _)).Times(1);
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillOnce(Return("fake topology info"));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));

    globalSchedActor_->AddDomainSchedHandler(from, domainName2, domainAddress2);
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 添加DomainScheduler成为新的Root DomainScheduler节点
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1. 设定DomainScheduler2的子节点是DomainScheduler1
 * @step:  2.调用GlobalSchedActor的AddDomainSchedHandler方法，并传入DomainScheduler2信息
 * @expect:  1. 添加DomainScheduler2节点成功，并更新MetaStore中拓扑图信息，
             断开与DomainScheduler1的心跳，与DomainScheduler2建立心跳，并且通知DomainScheduler2注册成功，
 *           并通知DomainScheduler1更新拓扑关系
 */
TEST_F(GlobalSchedTest, AddDomainSchedulerBecomeRoot)
{
    const std::string domainName1 = "domain1";
    const std::string domainAddress1 = "127.0.0.1:1";
    const std::string domainName2 = "domain2";
    const std::string domainAddress2 = "127.0.0.1:2";
    const litebus::AID from = "DomainSched-AID";
    auto domain1 = std::make_shared<SchedNode>(NodeInfo{ domainName1, domainAddress1 }, 1);
    auto domain2 = std::make_shared<SchedNode>(NodeInfo{ domainName2, domainAddress2 }, 1);
    domain2->AddChild(domain1);
    EXPECT_CALL(*mockSchedTree_, AddNonLeafNode).WillOnce(Return(domain2));
    EXPECT_CALL(*mockDomainSchedMgr_, Disconnect).Times(1);
    EXPECT_CALL(*mockDomainSchedMgr_, Connect(domainName2, domainAddress2)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockDomainSchedMgr_, Registered(from, _)).Times(1);
    EXPECT_CALL(*mockDomainSchedMgr_, UpdateSchedTopoView(domainName1, domainAddress1, _)).Times(1);
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillOnce(Return("fake topology info"));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::AddDomainSchedHandler, from, domainName2,
                   domainAddress2);
    EXPECT_CALL(*mockSchedTree_, GetRootNode).WillOnce(Return(domain2));
    auto future = globalSched_.GetRootDomainInfo();
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsSome(), true);
    EXPECT_EQ(future.Get().Get().name, domainName2);
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 删除一个不存在的DomainScheduler节点
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1. 调用GlobalSchedActor的DelDomainSchedHandler方法，删除DomainScheduler信息
 * @expect:  1. 删除DomainScheduler失败，并且不会更新DomainScheduler的拓扑关系
 */
TEST_F(GlobalSchedTest, DeleteDomainSchedulerNotExist)
{
    const std::string domainName = "domain";
    EXPECT_CALL(*mockSchedTree_, FindNonLeafNode).WillOnce(Return(nullptr));
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).Times(0);

    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::DelDomainSchedHandler, domainName);
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 删除DomainScheduler节点
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1. 调用GlobalSchedActor的DelDomainSchedHandler方法，删除DomainScheduler信息
 * @expect:  1. DomainScheduler节点状态被设置为BROKEN，并且激活新的DomainScheduler节点
 */
TEST_F(GlobalSchedTest, DeleteDomainSchedulerSuccess)
{
    const std::string domainName = "domain";
    const std::string domainAddress = "127.0.0.1:1";
    auto domain = std::make_shared<SchedNode>(NodeInfo{ domainName, domainAddress }, 1);
    EXPECT_CALL(*mockSchedTree_, FindNonLeafNode).WillOnce(Return(domain));
    EXPECT_CALL(*mockSchedTree_, SetState(_, NodeState::BROKEN)).Times(1);
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).Times(0);
    EXPECT_CALL(*mockDomainSchedulerLauncher_, Start).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillOnce(Return("fake topology info"));
    auto putResponse = std::make_shared<PutResponse>();
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(Return(putResponse));

    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::DelDomainSchedHandler, domainName);
    EXPECT_CALL(*mockSchedTree_, GetRootNode).WillOnce(Return(nullptr));
    auto future = globalSched_.GetRootDomainInfo();
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsSome(), false);
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 添加LocalScheduler节点异常回调函数
 * @type: Function test
 * @precondition: GlobalSched启动成功，并初始化相关依赖和Mock类
 * @step:  1. 调用GlobalSchedActor的LocalSchedAbnormalCallback方法，注册回调函数
 * @expect:  1. LocalScheduler节点被删除时，回调函数会被调用
 */
TEST_F(GlobalSchedTest, LocalSchedAbnormalCallback)
{
    auto promise = std::make_shared<litebus::Promise<Status>>();
    globalSched_.LocalSchedAbnormalCallback([&promise](const std::string &name) -> litebus::Future<Status> {
        promise->SetValue(Status::OK());
        return Status::OK();
    });
    auto promise1 = std::make_shared<litebus::Promise<Status>>();
    auto promise2 = std::make_shared<litebus::Promise<Status>>();
    globalSched_.AddLocalSchedAbnormalNotifyCallback("callback1", [&promise1](const std::string &name) -> litebus::Future<Status> {
        promise1->SetValue(Status::OK());
        return Status::OK();
    });
    globalSched_.AddLocalSchedAbnormalNotifyCallback("callback2", [&promise2](const std::string &name) -> litebus::Future<Status> {
        promise2->SetValue(Status::OK());
        return Status::OK();
    });

    auto localSchedNode = std::make_shared<SchedNode>(NodeInfo{ "local", "127.0.0.1:2" }, 0);
    EXPECT_CALL(*mockSchedTree_, RemoveLeafNode).WillOnce(Return(localSchedNode));
    EXPECT_CALL(*mockDomainSchedMgr_, UpdateSchedTopoView).Times(1);
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillRepeatedly(Return(""));
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::DelLocalSchedHandler, "local", LocalExitType::ABNORMAL);
    ASSERT_AWAIT_READY(promise->GetFuture());
    EXPECT_TRUE(promise->GetFuture().Get().IsOk());
    ASSERT_AWAIT_READY(promise1->GetFuture());
    ASSERT_AWAIT_READY(promise2->GetFuture());
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 从GlobalSchedDriver获取GlobalSched指针
 * @type: Function test
 * @precondition:
 * @step:  1. 调用GlobalSchedDriver的GetGlobalSched方法
 * @expect:  1. 获取GlobalSched指针成功
 */
TEST_F(GlobalSchedTest, GetGlobalSched)
{
    functionmaster::Flags flags;
    std::shared_ptr<GlobalSched> globalSched = std::make_shared<GlobalSched>();
    std::shared_ptr<global_scheduler::GlobalSchedDriver> globalDriver =
        std::make_shared<global_scheduler::GlobalSchedDriver>(globalSched, flags, mockMetaStoreClient_);
    std::shared_ptr<GlobalSched> globalSched1 = globalDriver->GetGlobalSched();
    EXPECT_EQ(globalSched1, globalSched);
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 获取RootDomain信息发送查询Agent请求
 * @type: Function test
 * @precondition:
 * @step:  1. 调用GlobalSchedActor的QueryAgentInfo方法
 */
TEST_F(GlobalSchedTest, QueryAgentInfo)
{
    {
        auto req = std::make_shared<messages::QueryAgentInfoRequest>();
        EXPECT_CALL(*mockDomainSchedMgr_, QueryAgentInfo(Eq("root"), Eq("root"), _))
            .WillOnce(Return(messages::QueryAgentInfoResponse{}));
        EXPECT_CALL(*mockSchedTree_, GetRootNode).WillOnce(Return(rootNode_));
        auto future = globalSched_.QueryAgentInfo(req);
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }

    {
        auto req = std::make_shared<messages::QueryAgentInfoRequest>();;
        EXPECT_CALL(*mockSchedTree_, GetRootNode).WillOnce(Return(nullNode_));
        auto future = globalSched_.QueryAgentInfo(req);
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }
    globalSched_.Stop();
    globalSched_.Await();
}

/*case
 * @title: 获取对应local信息发送查询驱逐Agent请求
 * @type: Function test
 * @precondition:
 * @step:  1. 调用GlobalSchedActor的GetLocalAddress方法
 * 2. 获取到地址后调用localSchedMgr_的EvictAgentOnLocal方法
 */
TEST_F(GlobalSchedTest, EvictAgent)
{
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        EXPECT_CALL(*mockLocalSchedMgr_, EvictAgentOnLocal(Eq("local"), _)).WillOnce(Return(Status::OK()));
        EXPECT_CALL(*mockSchedTree_, FindLeafNode(Eq("localID")))
            .WillOnce(Return(localSched_))
            .WillOnce(Return(localSched_));
        auto future = globalSched_.EvictAgent("localID", req);
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }

    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        EXPECT_CALL(*mockSchedTree_, FindLeafNode(Eq("localID")))
            .WillOnce(Return(nullNode_))
            .WillOnce(Return(nullNode_));
        auto future = globalSched_.EvictAgent("localID", req);
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().StatusCode(), StatusCode::PARAMETER_ERROR);
    }
    globalSched_.Stop();
    globalSched_.Await();
}

// SlaveBusiness test cases
TEST_F(GlobalSchedTest, SlaveBusinessTest)  // NOLINT
{
    auto domainSchedMgr = std::make_unique<MockDomainSchedMgr>();
    auto mockDomainSchedMgr = domainSchedMgr.get();
    EXPECT_CALL(*mockDomainSchedMgr, Disconnect).Times(1);
    auto member = std::make_shared<GlobalSchedActor::Member>();
    member->domainSchedMgr = std::move(domainSchedMgr);
    auto globalSchedActor = std::make_shared<GlobalSchedActor>("", nullptr, nullptr, nullptr);
    auto slaveBusiness = std::make_shared<GlobalSchedActor::SlaveBusiness>(globalSchedActor, member);
    slaveBusiness->OnChange();
    slaveBusiness->FindRootDomainSched();
    slaveBusiness->ResponseUpdateTaint({}, "", "");
    slaveBusiness->OnHealthyStatus(Status::OK());
    globalSched_.Stop();
    globalSched_.Await();
}

// fallback metastore recover test
TEST_F(GlobalSchedTest, OnHealthyStatusTest)  // NOLINT
{
    globalSchedActor_->topoRecovered = litebus::Promise<bool>();
    Status status(StatusCode::FAILED);
    globalSched_.OnHealthyStatus(status);

    globalSchedActor_->topoRecovered.SetValue(true);
    globalSched_.OnHealthyStatus(status);
    std::string jsonStr = "fake topology info";
    EXPECT_CALL(*mockSchedTree_, SerializeAsString).WillOnce(Return(jsonStr));
    auto putResponse = std::make_shared<PutResponse>();
    litebus::Future<std::string> topoInfo;
    EXPECT_CALL(*mockMetaStoreClient_, Put).WillOnce(DoAll(FutureArg<1>(&topoInfo), Return(putResponse)));
    globalSched_.OnHealthyStatus(Status::OK());
    ASSERT_AWAIT_READY(topoInfo);
    EXPECT_EQ(topoInfo.Get(), jsonStr);
    globalSched_.Stop();
    globalSched_.Await();
}

TEST_F(GlobalSchedTest, QueryResourcesInfo)
{
    {
        auto req = std::make_shared<messages::QueryResourcesInfoRequest>();
        EXPECT_CALL(*mockDomainSchedMgr_, QueryResourcesInfo(Eq("root"), Eq("root"), _))
            .WillOnce(Return(messages::QueryResourcesInfoResponse{}));
        EXPECT_CALL(*mockSchedTree_, GetRootNode).WillOnce(Return(rootNode_));
        auto future = globalSched_.QueryResourcesInfo(req);
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }
    {
        explorer::LeaderInfo leaderInfo{.name = "newMaster", .address = "127.0.0.2:8080"};
        globalSchedActor_->UpdateLeaderInfo(leaderInfo);
        auto req = std::make_shared<messages::QueryResourcesInfoRequest>();
        auto future = globalSched_.QueryResourcesInfo(req);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto resp = messages::QueryResourcesInfoResponse{};
        resp.set_requestid("requestId");
        globalSchedActor_->ResponseResourcesInfo(litebus::AID(), "Test", resp.SerializeAsString());
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }

    globalSched_.Stop();
    globalSched_.Await();
}

TEST_F(GlobalSchedTest, RecoverTopologyTest)
{
    globalSchedActor_->topoRecovered = litebus::Promise<bool>();
    globalSchedActor_->topoRecovered.SetValue(true);
    std::vector<WatchEvent> events;
    auto kv = KeyValue();
    kv.set_key("SCHEDULER_TOPOLOGY");
    std::string topo = "fake";
    kv.set_value(topo);
    events.emplace_back(WatchEvent{EventType::EVENT_TYPE_PUT, kv});

    litebus::Future<std::string> topoInfo;
    EXPECT_CALL(*mockSchedTree_, RecoverFromString).WillOnce(DoAll(FutureArg<0>(&topoInfo), Return(Status::OK())));
    globalSchedActor_->OnTopologyEvent(events);
    EXPECT_EQ(globalSchedActor_->cacheTopo_, topo);
    auto status = globalSchedActor_->RecoverSchedTopology();
    EXPECT_EQ(status.IsOk(), true);
    ASSERT_AWAIT_READY(topoInfo);
    EXPECT_EQ(topoInfo.Get(), topo);
    events.clear();
    events.emplace_back(WatchEvent{EventType::EVENT_TYPE_DELETE, KeyValue()});
    globalSchedActor_->OnTopologyEvent(events);
    EXPECT_EQ(globalSchedActor_->cacheTopo_, "");
    globalSched_.Stop();
    globalSched_.Await();
}

}  // namespace functionsystem::test
