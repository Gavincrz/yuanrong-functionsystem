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

#include "function_proxy/local_scheduler/subscription_manager/subscription_mgr_actor.h"
#include "common/constants/actor_name.h"
#include "common/constants/signal.h"
#include "utils/future_test_helper.h"

#include "mocks/mock_instance_control_view.h"
#include "mocks/mock_instance_state_machine.h"
#include "mocks/mock_local_sched_srv.h"
#include "mocks/mock_instance_ctrl.h"

namespace functionsystem::test {
using namespace testing;
using namespace local_scheduler;
const std::string LOCAL_NODE_ID = "local";
const std::string REMOTE_NODE_ID = "remote";
// sub-pub
const std::string SUBSCRIBER_ID = "subscriber";
const std::string PUBLISHER_ID = "publisher";

class SubscriptionManagerActorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        SubscriptionMgrConfig config{};
        subscriptionMgrActor_ = std::make_shared<SubscriptionMgrActor>(LOCAL_NODE_ID, config);
        litebus::Spawn(subscriptionMgrActor_);
        mockInstanceCtrlView_ = std::make_shared<MockInstanceControlView>(LOCAL_NODE_ID);
        subscriptionMgrActor_->BindInstanceControlView(mockInstanceCtrlView_);
        mockInstanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        subscriptionMgrActor_->BindInstanceCtrl(mockInstanceCtrl_);
        mockLocalSchedSrv_ = std::make_shared<MockLocalSchedSrv>();
        subscriptionMgrActor_->BindLocalSchedSrv(mockLocalSchedSrv_);
    }

    void TearDown() override
    {
        litebus::Terminate(subscriptionMgrActor_->GetAID());
        litebus::Await(subscriptionMgrActor_->GetAID());
        mockInstanceCtrlView_ = nullptr;
        mockInstanceCtrl_ = nullptr;
        mockLocalSchedSrv_ = nullptr;
    }
    
protected:
    std::shared_ptr<SubscriptionMgrActor> subscriptionMgrActor_;
    std::shared_ptr<MockInstanceControlView> mockInstanceCtrlView_;
    std::shared_ptr<MockInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockLocalSchedSrv> mockLocalSchedSrv_;
};

std::shared_ptr<InstanceStateMachine> GetInstanceMachine(std::string instanceID, InstanceState state,
                                                         std::string functionProxyID = LOCAL_NODE_ID)
{
    const std::string function = "12345678901234561234567890123456/0-test-helloWorld/$latest";
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(state));
    scheduleReq->mutable_instance()->set_functionproxyid(functionProxyID);
    scheduleReq->mutable_instance()->set_instanceid(instanceID);
    scheduleReq->mutable_instance()->set_function(function);
    scheduleReq->set_requestid("requestId");
    auto context = std::make_shared<InstanceContext>(scheduleReq);
    auto instanceStateMachine = std::make_shared<InstanceStateMachine>(LOCAL_NODE_ID, context, false);
    return instanceStateMachine;
}

/*
 * Positive case - Normal subscription flow
 * 1. Subscriber and publisher both in RUNNING state
 * 2. Subscriber successfully subscribes to the instance termination event
 * 3. Duplicate subscription returns same success(idempotency)
 */
TEST_F(SubscriptionManagerActorTest, SubscriptionSuccessWhenBothRunning) {
    auto publisher = GetInstanceMachine(PUBLISHER_ID, InstanceState::RUNNING);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(publisher));

    // 1. Prepare subscription request
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    common::SubscriptionPayload payload;
    payload.mutable_instancetermination()->set_instanceid(PUBLISHER_ID);
    killReq->set_payload(payload.SerializeAsString());

    // 2. Execute subscription
    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
    EXPECT_TRUE(publisher->HasStateChangeCallback("subscribe_instance_termination_" + SUBSCRIBER_ID));

    // 3. Repeat subscription
    result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
}

/*
 * Positive case - Normal remote subscription flow
 * 1. Both subscriber and publisher are in the RUNNING state, with the publisher located on a remote node.
 * 2. The subscriber successfully subscribes to the instance termination event,
 *    which triggers a ForwardSubscriptionEvent.
 */
TEST_F(SubscriptionManagerActorTest, SubscriptionSucceedsWhenRemotePublisherRunning) {
    auto publisher = GetInstanceMachine(PUBLISHER_ID, InstanceState::RUNNING, REMOTE_NODE_ID);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(publisher));

    // 1. Prepare subscription request
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    common::SubscriptionPayload payload;
    payload.mutable_instancetermination()->set_instanceid(PUBLISHER_ID);
    killReq->set_payload(payload.SerializeAsString());

    // 2. Execute subscription --> Verify if ForwardSubscriptionEvent is triggered
    KillResponse rsp;
    rsp.set_code(common::ErrorCode::ERR_NONE);
    EXPECT_CALL(*mockInstanceCtrl_, ForwardSubscriptionEvent(_)).WillOnce(Return(rsp));
    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
}

/*
 * Negative case - Publisher in terminal state
 * 1. Publisher starts in EXITING state (terminal)
 * 2. Subscription attempt fails with ERR_SUB_STATE_INVALID
 */
TEST_F(SubscriptionManagerActorTest, SubscriptionFailWhenPublisherTerminated) {
    auto publisher = GetInstanceMachine(PUBLISHER_ID, InstanceState::EXITING);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(publisher));

    // 1. Prepare subscription request
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    common::SubscriptionPayload payload;
    payload.mutable_instancetermination()->set_instanceid(PUBLISHER_ID);
    killReq->set_payload(payload.SerializeAsString());

    // 2. Execute subscription
    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_SUB_STATE_INVALID);
}

/*
 * Negative case - Handling orphaned subscriptions
 * 1. Subscription established normally
 * 2. subscriber state change to EXITED
 * 3. Verify CleanupOrphanedSubscription triggering upon subscriber's termination state transition
 */
TEST_F(SubscriptionManagerActorTest, CleanupOrphanedSubscription) {
    auto subscriber = GetInstanceMachine(SUBSCRIBER_ID, InstanceState::RUNNING);
    auto publisher = GetInstanceMachine(PUBLISHER_ID, InstanceState::RUNNING);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance)
        .WillOnce(Return(publisher))
        .WillOnce(Return(subscriber))
        .WillOnce(Return(publisher));

    // 1. Prepare subscription request
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    common::SubscriptionPayload payload;
    payload.mutable_instancetermination()->set_instanceid(PUBLISHER_ID);
    killReq->set_payload(payload.SerializeAsString());

    // 2. Execute subscription
    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
    EXPECT_TRUE(publisher->HasStateChangeCallback("subscribe_instance_termination_" + SUBSCRIBER_ID));

    // 3. Transition subscriber instance state from RUNNING to EXITED
    //    --> Verify if CleanupOrphanedSubscription is triggered
    auto start = std::chrono::steady_clock::now();
    while (!subscriber->HasStateChangeCallback("cleanup_Orphaned_Subscription_" + PUBLISHER_ID)) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(3)) {
            EXPECT_TRUE(false) << "Timeout after 3 seconds";
        }
        std::this_thread::yield();
    }
    InstanceInfo newSubscriber;
    newSubscriber.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::EXITED));
    newSubscriber.set_instanceid(SUBSCRIBER_ID);
    subscriber->UpdateInstanceInfo(newSubscriber);
    start = std::chrono::steady_clock::now();
    while (publisher->HasStateChangeCallback("subscribe_instance_termination_" + SUBSCRIBER_ID)) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(3)) {
            EXPECT_TRUE(false) << "Timeout after 3 seconds";
        }
        std::this_thread::yield();
    }
}

/*
 * Positive case - Normal unsubscription flow
 * 1. Subscription established normally
 * 2. Subscriber successfully unsubscribes from instance termination event
 * 3. Duplicate unsubscription returns same success(idempotency)
 */
TEST_F(SubscriptionManagerActorTest, UnsubscriptionSuccess) {
    auto publisher = GetInstanceMachine(PUBLISHER_ID, InstanceState::RUNNING);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(publisher));

    // 1. Prepare subscription request
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    common::SubscriptionPayload subscribePayload;
    subscribePayload.mutable_instancetermination()->set_instanceid(PUBLISHER_ID);
    killReq->set_payload(subscribePayload.SerializeAsString());

    // 2. Execute subscription
    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
    EXPECT_TRUE(publisher->HasStateChangeCallback("subscribe_instance_termination_" + SUBSCRIBER_ID));

    // 3. Prepare unsubscription request
    killReq = std::make_shared<KillRequest>();
    killReq->set_signal(UNSUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    common::UnsubscriptionPayload unsubscribePayload;
    unsubscribePayload.mutable_instancetermination()->set_instanceid(PUBLISHER_ID);
    killReq->set_payload(unsubscribePayload.SerializeAsString());

    // 4. Execute unsubscription
    result = subscriptionMgrActor_->Unsubscribe(SUBSCRIBER_ID, killReq).Get();

    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
    EXPECT_FALSE(publisher->HasStateChangeCallback("subscribe_instance_termination_" + SUBSCRIBER_ID));

    // 5. Repeat unsubscription
    result = subscriptionMgrActor_->Unsubscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
}

/*
 * State transition test - Notification triggering
 * 1. Subscription established normally
 * 2. Publisher state change to EXITING
 * 3. Verify notification signal triggering upon publisher's termination state transition
 *    --> Verify if notification signal is triggered
 */
TEST_F(SubscriptionManagerActorTest, NotificationTriggerOnStateTransition) {
    auto publisher = GetInstanceMachine(PUBLISHER_ID, InstanceState::RUNNING);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(publisher));

    // 1. Prepare subscription request
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    common::SubscriptionPayload payload;
    payload.mutable_instancetermination()->set_instanceid(PUBLISHER_ID);
    killReq->set_payload(payload.SerializeAsString());

    // 2. Execute subscription
    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
    EXPECT_TRUE(publisher->HasStateChangeCallback("subscribe_instance_termination_" + SUBSCRIBER_ID));

    // 3. Transition publisher instance state from RUNNING to EXITING --> Verify if notification signal is triggered
    EXPECT_CALL(*mockInstanceCtrl_, Kill(_, _)).WillOnce(Return(KillResponse()));
    InstanceInfo newPublisher;
    newPublisher.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::EXITING));
    publisher->UpdateInstanceInfo(newPublisher);
}

/*
 * Instance termination subscription-related parameter validation test -- invalid dst instance
 * 1. Empty instance ID rejection
 * 2. Non-existent instance handling
 */
TEST_F(SubscriptionManagerActorTest, InvalidDstInstance) {
    auto killCtx = std::make_shared<KillContext>();

    // 1. Empty instance ID
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    common::SubscriptionPayload payload;
    payload.mutable_instancetermination()->set_instanceid("");
    killReq->set_payload(payload.SerializeAsString());

    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_PARAM_INVALID);

    // 2. Non-existent instance
    payload.mutable_instancetermination()->set_instanceid(PUBLISHER_ID);
    killReq->set_payload(payload.SerializeAsString());

    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillOnce(Return(nullptr));
    result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_INSTANCE_NOT_FOUND);
}

/*
 * Instance termination subscription-related parameter validation test -- invalid payload
 * 1. empty subscription payload
 * 2. empty unsubscription payload
 */
TEST_F(SubscriptionManagerActorTest, InvalidInstanceTerminationPayload) {
    auto killCtx = std::make_shared<KillContext>();

    // 1. empty subscription payload
    auto subscriber = GetInstanceMachine(SUBSCRIBER_ID, InstanceState::RUNNING);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(subscriber));

    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_PARAM_INVALID);

    // 2. empty unsubscription payload
    killReq = std::make_shared<KillRequest>();
    killReq->set_signal(UNSUBSCRIBE_SIGNAL);
    killReq->set_instanceid(PUBLISHER_ID);

    result = subscriptionMgrActor_->Unsubscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_PARAM_INVALID);
}

/*
 * test for Subscribe Function Master
 * 1. mock query Master IP successfully
 * 2. Kill signal successfully
 */
TEST_F(SubscriptionManagerActorTest, SubscribeFunctionMasterSuccessfully) {
    auto killCtx = std::make_shared<KillContext>();

    // 1. mock subscriber is running, mock query ip and  kill to subscriber successfully
    std::string expectIP = "192.167.0.4:19247";
    std::string capturedInstanceID;
    std::shared_ptr<KillRequest> capturedKillReq;
    auto subscriber = GetInstanceMachine(SUBSCRIBER_ID, InstanceState::RUNNING);
    EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(subscriber));
    EXPECT_CALL(*mockLocalSchedSrv_, QueryMasterIP).WillOnce(Return(expectIP));
    EXPECT_CALL(*mockInstanceCtrl_, Kill).WillOnce(DoAll(
        SaveArg<0>(&capturedInstanceID),
        SaveArg<1>(&capturedKillReq),
        Return(KillResponse{})
        ));

    // 2. construct killReq & payload
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);

    common::SubscriptionPayload payload;
    payload.mutable_functionmaster(); // set functionmaster;
    killReq->set_payload(payload.SerializeAsString());

    auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
    EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
    auto eventKey = "subscribe_master_" + SUBSCRIBER_ID;
    EXPECT_TRUE(subscriber->HasStateChangeCallback(eventKey));

    // 3. check subscriber and killReq
    ASSERT_AWAIT_TRUE([&]() { return capturedKillReq != nullptr; });
    EXPECT_EQ(capturedInstanceID, SUBSCRIBER_ID);
    EXPECT_EQ(capturedKillReq->instanceid(), SUBSCRIBER_ID);
    common::NotificationPayload deserializedPayload;
    if (!deserializedPayload.ParseFromString(capturedKillReq->payload())) {
        ASSERT_TRUE(false);
    } else {
        EXPECT_EQ(deserializedPayload.mutable_functionmasterevent()->address(), expectIP);
    }
}

/*
 * test for clean subscriber
 * case 1: Unsubscribe Function Master
 * 1. unsubscribe Function Master
 * 2. clean subscriber successfully

 * case 2: subscriber is exited
 * 1. mock subscriber exited and statemachine execute callback function
 * 2. clean subscriber successfully
 */
TEST_F(SubscriptionManagerActorTest, CleanFunctionMasterSubscriberSuccessfully) {

    // 0. construct killReq & payload
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    common::SubscriptionPayload payload;
    payload.mutable_functionmaster(); // set functionmaster;
    killReq->set_payload(payload.SerializeAsString());

    // case 1 Unsubscribe Function Master
    {
        // 1. mock subscriber is running, mock query ip is empty
        auto subscriber = GetInstanceMachine(SUBSCRIBER_ID, InstanceState::RUNNING);
        EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(subscriber));
        EXPECT_CALL(*mockLocalSchedSrv_, QueryMasterIP).WillOnce(Return(""));
        EXPECT_CALL(*mockInstanceCtrl_, Kill).Times(0);

        auto result = subscriptionMgrActor_->Subscribe(SUBSCRIBER_ID, killReq).Get();
        EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);

        // 2. construct UnsubscriptionPayload
        common::UnsubscriptionPayload unPayload;
        unPayload.mutable_functionmaster(); // set functionmaster;
        killReq->set_payload(unPayload.SerializeAsString());

        // 2. unsubscribe Function Master and clean subscriber successfully
        result = subscriptionMgrActor_->Unsubscribe(SUBSCRIBER_ID, killReq).Get();
        EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
        auto eventKey = "subscribe_master_" + SUBSCRIBER_ID;
        EXPECT_TRUE(subscriber->HasStateChangeCallback(eventKey));
        EXPECT_EQ(subscriptionMgrActor_->masterSubscriberMap_.size(), 0);
    }

    // case 2: subscriber is exited
    {
        // 1. mock subscriber is running, mock query ip is empty
        auto exitSubscriberID = SUBSCRIBER_ID + "1";
        auto subscriber = GetInstanceMachine(exitSubscriberID, InstanceState::RUNNING);
        EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillRepeatedly(Return(subscriber));
        EXPECT_CALL(*mockLocalSchedSrv_, QueryMasterIP).WillOnce(Return(""));
        EXPECT_CALL(*mockInstanceCtrl_, Kill).Times(0);

        killReq->set_payload(payload.SerializeAsString());
        auto result = subscriptionMgrActor_->Subscribe(exitSubscriberID, killReq).Get();
        EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);

        // 2. check subscriber successfully
        auto eventKey = "subscribe_master_" + exitSubscriberID;
        EXPECT_TRUE(subscriber->HasStateChangeCallback(eventKey));
        EXPECT_TRUE(subscriptionMgrActor_->masterSubscriberMap_.find(exitSubscriberID)
                    != subscriptionMgrActor_->masterSubscriberMap_.end());

        // 3. mock subscriber is exited
        subscriber->ExecuteStateChangeCallback("reqId", InstanceState::EXITED);
        ASSERT_AWAIT_TRUE([&]() { return subscriptionMgrActor_->masterSubscriberMap_.size() == 0; });
    }
}

/*
 * test for NotifyMasterIPToSubscribers
 * 1. mock N runtime subscribe master
 * 2. mock master ip is update, and try to Notify all subscriber
 */
TEST_F(SubscriptionManagerActorTest, NotifyMasterIPToSubscribers) {
    auto killCtx = std::make_shared<KillContext>();

    // 1. mock N runtime subscribe master
    std::string expectIP = "192.167.0.4:19247";
    std::string capturedInstanceID;
    std::shared_ptr<KillRequest> capturedKillReq;
    int subscriberCnt = 5;

    EXPECT_CALL(*mockLocalSchedSrv_, QueryMasterIP).WillRepeatedly(Return(""));
    auto killReq = std::make_shared<KillRequest>();
    killReq->set_signal(SUBSCRIBE_SIGNAL);
    common::SubscriptionPayload payload;
    payload.mutable_functionmaster(); // set functionmaster;
    killReq->set_payload(payload.SerializeAsString());

    for (int i = 0; i < subscriberCnt; i++) {
        auto subscriberID = SUBSCRIBER_ID + std::to_string(i);
        auto subscriber = GetInstanceMachine(subscriberID, InstanceState::RUNNING);
        EXPECT_CALL(*mockInstanceCtrlView_, GetInstance).WillOnce(Return(subscriber));
        auto result = subscriptionMgrActor_->Subscribe(subscriberID, killReq).Get();
        EXPECT_EQ(result.code(), common::ErrorCode::ERR_NONE);
        EXPECT_EQ(subscriptionMgrActor_->masterSubscriberMap_.size(), i+1);
    }

    {
        // 2. mock master ip is update successfully, and try to Notify all subscriber
        std::vector<std::string> capturedInstanceIDs;
        std::vector<std::shared_ptr<KillRequest>> capturedKillReqs;
        EXPECT_CALL(*mockInstanceCtrl_, Kill)
            .WillRepeatedly(testing::DoAll(
                testing::Invoke([&](const std::string &srcInstanceID, const std::shared_ptr<KillRequest> &killReq) {
                    capturedInstanceIDs.push_back(srcInstanceID);
                    capturedKillReqs.push_back(killReq);
                    return KillResponse{};
                })));

        auto result = subscriptionMgrActor_->NotifyMasterIPToSubscribers(expectIP);
        EXPECT_TRUE(result.Get().IsOk());

        // check whether notify all subscriber
        ASSERT_AWAIT_TRUE([&]() { return capturedKillReqs.size() == subscriberCnt; });

        // check whether notify ip whether is right
        common::NotificationPayload deserializedPayload;
        for (int i = 0; i < subscriberCnt; i++) {
            auto subscriberID = SUBSCRIBER_ID + std::to_string(i);
            EXPECT_EQ(capturedInstanceIDs[i], subscriberID);
            EXPECT_EQ(capturedKillReqs[i]->instanceid(), subscriberID);
            if (!deserializedPayload.ParseFromString(capturedKillReqs[i]->payload())) {
                ASSERT_TRUE(false);
            } else {
                EXPECT_EQ(deserializedPayload.mutable_functionmasterevent()->address(), expectIP);
            }
        }
    }

    {
        // 3. mock master ip is update to empty, and failed to Notify all subscriber
        EXPECT_CALL(*mockInstanceCtrl_, Kill).Times(0);
        auto result = subscriptionMgrActor_->NotifyMasterIPToSubscribers("");
        EXPECT_TRUE(result.Get().IsError());
    }

}

}  // namespace functionsystem::test