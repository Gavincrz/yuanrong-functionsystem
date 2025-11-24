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

#include "function_proxy/common/state_handler/state_handler.h"

#include "common/hex/hex.h"
#include "common/utils/files.h"
#include "gtest/gtest.h"
#include "mocks/mock_distributed_cache_client.h"
#include "state_handler_helper.h"
#include "utils/future_test_helper.h"

namespace functionsystem::function_proxy::test {

using namespace functionsystem::test;
using namespace testing;

class StateHandlerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        distributedCacheClient_ = std::make_shared<MockDistributedCacheClient>();
        EXPECT_CALL(*distributedCacheClient_, Init).WillOnce(Return(Status::OK()));

        stateClient_ = std::make_shared<StateClient>(distributedCacheClient_);
        stateActor_ = std::make_shared<StateActor>(stateClient_);
        litebus::Spawn(stateActor_);
        StateHandler::BindStateActor(stateActor_);
    }

    void TearDown() override
    {
        stateClient_ = nullptr;
        litebus::Terminate(stateActor_->GetAID());
        litebus::Await(stateActor_->GetAID());
        stateActor_ = nullptr;
        distributedCacheClient_ = nullptr;
    }

protected:
    std::shared_ptr<MockDistributedCacheClient> distributedCacheClient_;
    std::shared_ptr<StateActor> stateActor_;
    std::shared_ptr<StateClient> stateClient_;
};

/**
 * Feature: RetryInitTest
 * Description: retry test
 * Steps:
 * 1. init failed
 * 2. retry init
 *
 * Expectation:
 * 1. finished
 */
TEST_F(StateHandlerTest, RetryInitTest)
{
    litebus::Terminate(stateActor_->GetAID());
    litebus::Await(stateActor_->GetAID());

    distributedCacheClient_ = std::make_shared<MockDistributedCacheClient>();
    bool isFinished = false;
    EXPECT_CALL(*distributedCacheClient_, Init)
        .WillOnce(Return(Status(StatusCode::FAILED)))
        .WillOnce(DoAll(Assign(&isFinished, true), Return(Status::OK())));
    stateClient_ = std::make_shared<StateClient>(distributedCacheClient_);
    stateActor_ = std::make_shared<StateActor>(stateClient_);
    litebus::Spawn(stateActor_);
    StateHandler::BindStateActor(stateActor_);
    EXPECT_AWAIT_TRUE([&isFinished]() -> bool { return isFinished; });

    litebus::Terminate(stateActor_->GetAID());
    litebus::Await(stateActor_->GetAID());
}

/**
 * Feature: SaveStateFailed
 * Description: save state failed
 * Steps:
 * 1. save state with empty instance id
 * 2. save state set to cache failed
 *
 * Expectation:
 * 1. StatusCode::ERR_PARAM_INVALID
 * 2. StatusCode::ERR_INNER_SYSTEM_ERROR
 */
TEST_F(StateHandlerTest, SaveStateFailed)
{
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    auto response = StateHandler::SaveState("", request);
    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->saversp().code(), common::ErrorCode::ERR_PARAM_INVALID);
    EXPECT_EQ(response.Get()->saversp().message(), "save state failed: empty instance id");

    EXPECT_CALL(*distributedCacheClient_, Set).WillOnce(Return(Status(StatusCode::FAILED)));
    response = StateHandler::SaveState("instance_id", request);
    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->saversp().code(), common::ErrorCode::ERR_INNER_SYSTEM_ERROR);
    EXPECT_THAT(response.Get()->saversp().message(), testing::HasSubstr("save state failed: [code: -1"));
}

/**
* Feature:
* Description: DeleteStateSuccess
* Steps:
* 1. Delete state

* Expectation:
* 1. StatusCode::ERR_NONE
*/
TEST_F(StateHandlerTest, DeleteState)
{
    auto distributedCacheClient1_ = std::make_shared<MockDistributedCacheClient>();
    EXPECT_CALL(*distributedCacheClient1_, Init).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*distributedCacheClient1_, Del(Matcher<const std::string &>("instanceID"))).WillOnce(Return(Status::OK()));

    auto stateClient1_ = std::make_shared<StateClient>(distributedCacheClient1_);
    stateClient1_->Init();
    auto status = stateClient1_->Del("instanceID");
    EXPECT_TRUE(status.IsOk());
}

/**
 * Feature: SaveStateSuccess
 * Description: save state success
 * Steps:
 * 1. save state
 *
 * Expectation:
 * 1. StatusCode::ERR_NONE
 */
TEST_F(StateHandlerTest, SaveStateSuccess)
{
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    EXPECT_CALL(*distributedCacheClient_, Set).WillOnce(Return(Status::OK()));
    auto response = StateHandler::SaveState("instance_id", request);
    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->saversp().code(), common::ErrorCode::ERR_NONE);
    EXPECT_EQ(response.Get()->saversp().checkpointid(), "instance_id");
}

/**
 * Feature: LoadStateFailed
 * Description: load state failed
 * Steps:
 * 1. load state with empty instance id
 * 2. load state with empty checkpoint id
 * 3. load state get from cache failed
 *
 * Expectation:
 * 1. StatusCode::ERR_PARAM_INVALID
 * 2. StatusCode::ERR_PARAM_INVALID
 * 3. StatusCode::ERR_INNER_SYSTEM_ERROR
 */
TEST_F(StateHandlerTest, LoadStateFailed)
{
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    auto response = StateHandler::LoadState("", request);
    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->loadrsp().code(), common::ErrorCode::ERR_PARAM_INVALID);
    EXPECT_EQ(response.Get()->loadrsp().message(), "load state failed: empty instance id");

    StateLoadRequest loadReq{};
    loadReq.set_checkpointid("");
    *request->mutable_loadreq() = std::move(loadReq);
    response = StateHandler::LoadState("instance_id", request);
    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->loadrsp().code(), common::ErrorCode::ERR_PARAM_INVALID);
    EXPECT_EQ(response.Get()->loadrsp().message(), "load state failed: empty checkpoint id");

    std::string state = "";
    EXPECT_CALL(*distributedCacheClient_,
                Get(Matcher<const std::string &>("checkpoint_id"), Matcher<std::string &>(Eq(""))))
        .WillOnce(DoAll(SetArgReferee<1>(state), Return(Status(StatusCode::FAILED))));

    loadReq.set_checkpointid("checkpoint_id");
    *request->mutable_loadreq() = std::move(loadReq);
    response = StateHandler::LoadState("instance_id", request);
    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->loadrsp().code(), common::ErrorCode::ERR_INNER_SYSTEM_ERROR);
    EXPECT_THAT(response.Get()->loadrsp().message(), testing::HasSubstr("load state failed: [code: -1"));
}

/**
 * Feature: LoadStateSuccess
 * Description: load state success
 * Steps:
 * 1. load state
 *
 * Expectation:
 * 1. StatusCode::ERR_NONE
 */
TEST_F(StateHandlerTest, LoadStateSuccess)
{
    std::string state = "state";
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    EXPECT_CALL(*distributedCacheClient_,
                Get(Matcher<const std::string &>("checkpoint_id"), Matcher<std::string &>(Eq(""))))
        .WillOnce(DoAll(SetArgReferee<1>(state), Return(Status::OK())));

    StateLoadRequest loadReq{};
    loadReq.set_checkpointid("checkpoint_id");
    *request->mutable_loadreq() = std::move(loadReq);
    auto response = StateHandler::LoadState("instance_id", request);
    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->loadrsp().code(), common::ErrorCode::ERR_NONE);
    EXPECT_EQ(response.Get()->loadrsp().state(), state);
}

/**
 * Feature: SaveStateFailed
 * Description: save state failed
 * Steps:
 * 1. clear aid
 * 2. save state
 *
 * Expectation:
 * 1. StatusCode::ERR_INNER_SYSTEM_ERROR
 */
TEST_F(StateHandlerTest, SaveStateFailedWithInValidAID)
{
    StateHandlerHelper::ClearStateActorHelper();
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    auto response = StateHandler::SaveState("instance_id", request);

    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->saversp().code(), common::ErrorCode::ERR_INNER_SYSTEM_ERROR);
    EXPECT_EQ(response.Get()->saversp().message(), "save state failed: don't init state actor");
}

/**
 * Feature: LoadStateFailed
 * Description: load state failed
 * Steps:
 * 1. clear aid
 * 2. save state
 *
 * Expectation:
 * 1. StatusCode::ERR_INNER_SYSTEM_ERROR
 */
TEST_F(StateHandlerTest, LoadStateFailedWithInValidAID)
{
    StateHandlerHelper::ClearStateActorHelper();
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    auto response = StateHandler::LoadState("instance_id", request);

    EXPECT_AWAIT_READY(response);
    EXPECT_EQ(response.Get()->saversp().code(), common::ErrorCode::ERR_INNER_SYSTEM_ERROR);
    EXPECT_EQ(response.Get()->saversp().message(), "save state failed: don't init state actor");
}

/**
 * Feature: SaveStateSuccess
 * Description: save state success
 * Steps:
 * 1. save state
 *
 * Expectation:
 * 1. StatusCode::ERR_NONE
 */
TEST_F(StateHandlerTest, DeleteStateSuccess)
{
    EXPECT_CALL(*distributedCacheClient_, Del(Matcher<const std::string &>("DeleteStateSuccess_instance_id")))
            .WillOnce(Return(Status::OK()));

    auto status = StateHandler::DeleteState("DeleteStateSuccess_instance_id");
    EXPECT_TRUE(status.Get().IsOk());
}

/**
 * Feature: DeleteStateFailed
 * Description: delete state failed
 * Steps:
 * 1. delete state with empty instance id
 * 2. delete state from cache failed
 *
 * Expectation:
 * 1. StatusCode::ERR_INSTANCE_INFO_INVALID
 * 2. StatusCode::FAILED
 */
TEST_F(StateHandlerTest, DeleteStateFailed)
{
    auto status = StateHandler::DeleteState("");
    EXPECT_AWAIT_READY(status);
    EXPECT_FALSE(status.Get());

    EXPECT_CALL(*distributedCacheClient_, Del(Matcher<const std::string &>("DeleteStateFailed_instance_id")))
    .WillOnce(Return(Status(StatusCode::FAILED, "something was wrong")));

    status = StateHandler::DeleteState("DeleteStateFailed_instance_id");
    EXPECT_AWAIT_READY(status);
    EXPECT_EQ(status.Get().StatusCode(), StatusCode::FAILED);

}


/**
 * Feature: DeleteStateFailed
 * Description: delete state failed
 * Steps:
 * 1. clear aid
 * 2. delete state
 *
 * Expectation:
 * 1. StatusCode::ERR_INNER_SYSTEM_ERROR
 */
    TEST_F(StateHandlerTest, DeleteStateFailedWithInValidAID)
    {
        StateHandlerHelper::ClearStateActorHelper();

        auto status = StateHandler::DeleteState("instance_id");

        EXPECT_AWAIT_READY(status);
        EXPECT_EQ(status.Get().StatusCode(), StatusCode::ERR_INNER_SYSTEM_ERROR);
    }

}  // namespace functionsystem::function_proxy::test