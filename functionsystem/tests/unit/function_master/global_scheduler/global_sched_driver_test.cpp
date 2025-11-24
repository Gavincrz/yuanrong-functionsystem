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

#include "global_sched_driver.h"

#include <gtest/gtest.h>

#include "common/constants/metastore_keys.h"
#include "common/explorer/explorer.h"
#include "common/resource_view/view_utils.h"
#include "global_sched.h"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "mocks/mock_global_schd.h"
#include "mocks/mock_meta_store_client.h"
#include "utils/generate_info.h"

namespace functionsystem::test {
const std::string HEALTHY_URL = "/healthy";
const std::string GLOBAL_SCHEDULER = "global-scheduler";
const std::string QUERY_AGENTS_URL = "/queryagents";
const std::string EVICT_AGENT_URL = "/evictagent";
const std::string QUERY_AGENT_COUNT_URL = "/queryagentcount";
const std::string QUERY_RESOURCES_URL = "/resources";
const std::string GET_SCHEDULING_QUEUE_URL = "/scheduling_queue";

using namespace ::testing;

class GlobalSchedDriverTest : public ::testing::Test {
public:
    void SetUp() override
    {
        mockGlobalSched_ = std::make_shared<MockGlobalSched>();
        mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>("");

        const char *argv[] = { "./function_master",
                           "--log_config={\"filepath\": \"/home/yr/log\",\"level\": \"DEBUG\",\"rolling\": "
                           "{\"maxsize\": 100, \"maxfiles\": 1}}",
                           "--node_id=aaa",
                           "--ip=127.0.0.1:8080",
                           "--meta_store_address=127.0.0.1:32209",
                           "--d1=2",
                           "--d2=2",
                           "--election_mode=standalone" };
        flags_.ParseFlags(8, argv);

        explorer::Explorer::NewStandAloneExplorerActorForMaster(explorer::ElectionInfo{},
            GetLeaderInfo(litebus::AID("function_master", "127.0.0.1:8080")));
    }

    void TearDown() override
    {
        explorer::Explorer::GetInstance().Clear();
        mockGlobalSched_ = nullptr;
        globalSchedDriver_ = nullptr;
    }

protected:
    std::shared_ptr<MockGlobalSched> mockGlobalSched_;
    std::shared_ptr<MockMetaStoreClient> mockMetaStoreClient_;
    std::shared_ptr<global_scheduler::GlobalSchedDriver> globalSchedDriver_;
    functionsystem::functionmaster::Flags flags_;
};

TEST_F(GlobalSchedDriverTest, StartAndStopGlobalSchedulerDriver)
{
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    auto status = globalSchedDriver_->Start();
    ASSERT_TRUE(status == Status::OK());
    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}

TEST_F(GlobalSchedDriverTest, QueryHealthyRouter)
{
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto status = globalSchedDriver_->Start();
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    litebus::http::URL urlHealthy("http", "127.0.0.1", port, GLOBAL_SCHEDULER + HEALTHY_URL);
    std::unordered_map<std::string, std::string> headers = {
        {"Node-ID", "aaa"},
        {"PID", std::to_string(getpid())}
    };
    litebus::Future<HttpResponse> response = litebus::http::Get(urlHealthy, headers);
    response.Wait();
    ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}

// test query agent info
// case1: invalid method
// case2: query successful (not empty)
TEST_F(GlobalSchedDriverTest, QueryAgentsRouter)
{
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto status = globalSchedDriver_->Start();
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::http::URL urlQueryAgents("http", "127.0.0.1", port, GLOBAL_SCHEDULER + QUERY_AGENTS_URL);
    // case1: invalid method
    {
        auto response = litebus::http::Post(urlQueryAgents, litebus::None(), litebus::None(), litebus::None());
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
    }

    // case2: query successful (empty)
    {
        auto resp = messages::QueryAgentInfoResponse();
        auto info = resp.add_agentinfos();
        info->set_agentid("agentID");
        info->set_alias("alias");
        info->set_localid("localID");
        EXPECT_CALL(*mockGlobalSched_, QueryAgentInfo(_)).WillOnce(Return(resp));
        auto response = litebus::http::Get(urlQueryAgents, litebus::None());
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        auto infos = messages::ExternalQueryAgentInfoResponse();
        EXPECT_EQ(google::protobuf::util::JsonStringToMessage(body, &infos).ok(), true);
        ASSERT_EQ(infos.data().size(), 1);
        EXPECT_EQ(infos.data().Get(0).id(), "localID/agentID");
        EXPECT_EQ(infos.data().Get(0).alias(), "alias");
    }

    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}

messages::FunctionSystemStatus ParseResponse(const std::string &body)
{
    messages::FunctionSystemStatus status;
    YRLOG_INFO("body: {}", body);
    (void)google::protobuf::util::JsonStringToMessage(body, &status) ;
    return status;
}

// test evcit agent info
// case1: invalid method
// case2: invalid body
// case3: invalid timemout
// case4: invalid agentID
// case5: query return failed
// case6: query return successful
// case7: default timeout
// case8: empty agentID
// case9: serialize success but without agentID
// case10: serialize fail but with agentID
// case11: corner case
TEST_F(GlobalSchedDriverTest, EvictAgentRouter)
{
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto status = globalSchedDriver_->Start();
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::http::URL urlEvictAgent("http", "127.0.0.1", port, GLOBAL_SCHEDULER + EVICT_AGENT_URL);
    // case1: invalid method
    {
        auto response = litebus::http::Get(urlEvictAgent, litebus::None());
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
    }
    // case2: invalid body
    {
        std::string reqData =
            "{\"agentid\": \"EuerOS-220-41-65280/function_agent_10.30.220.41-29847\","
            "\"timeoutsec\": \"10\"}";
        std::string contentType = "application/json";
        auto response =
            litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_PARAM_INVALID);
    }
    // case3: invalid timemout
    {
        std::string reqData = "{\"agentID\": \"EuerOS-220-41-65280/function_agent_10.30.220.41-29847\","
            "\"timeoutSec\": \"6001\"}";
        std::string contentType = "application/json";
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_PARAM_INVALID);
    }
    // case4: invalid agentID
    {
        std::string reqData =
            "{\"agentID\": \"xxxxxxx\","
            "\"timeoutSec\": \"10\"}";
        std::string contentType = "application/json";
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_PARAM_INVALID);
    }
    // case5: query return failed
    {
        std::string reqData =
            "{\"agentID\": \"localID/agentID\","
            "\"timeoutSec\": \"10\"}";
        std::string contentType = "application/json";
        EXPECT_CALL(*mockGlobalSched_, EvictAgent(Eq("localID"), _))
            .WillOnce(Return(Status(StatusCode::PARAMETER_ERROR)));
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_PARAM_INVALID);
    }
    // case6: query return successful
    {
        std::string reqData =
            "{\"agentID\": \"localID/agentID\","
            "\"timeoutSec\": \"10\"}";
        std::string contentType = "application/json";
        EXPECT_CALL(*mockGlobalSched_, EvictAgent(Eq("localID"), _))
            .WillOnce(Return(Status::OK()));
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_NONE);
    }

    // case7: default timeout
    {
        std::string reqData =
            "{\"agentID\": \"localID/agentID\","
            "\"timeoutSec\": \"0\"}";
        std::string contentType = "application/json";
        EXPECT_CALL(*mockGlobalSched_, EvictAgent(Eq("localID"), _))
            .WillOnce(
                DoAll(Invoke([](const std::string &localID,
                                const std::shared_ptr<messages::EvictAgentRequest> &req) -> litebus::Future<Status> {
                    EXPECT_EQ(req->timeoutsec(), uint32_t(30));
                    EXPECT_EQ(req->agentid(), "agentID");
                    return Status::OK();
                })));
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_NONE);
    }

    // case8: empty agentID
    {
        std::string reqData = "{}";
        std::string contentType = "application/json";
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_PARAM_INVALID);
        EXPECT_PRED_FORMAT2(testing::IsSubstring, "Empty", status.message());
    }
    // case9: serialize success but without agentID
    {
        std::string reqData = "{\"timeoutSec\": 10}";
        std::string contentType = "application/json";
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_PARAM_INVALID);
        EXPECT_PRED_FORMAT2(testing::IsSubstring, "Empty", status.message());
    }
    // case10: serialize fail but with agentID
    {
        std::string reqData = "{\"agentID\": \"localID/agentID\","
                              "\"timeoutsec\": \"0\"}";
        std::string contentType = "application/json";
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_PARAM_INVALID);
        EXPECT_PRED_FORMAT2(testing::IsNotSubstring, "Empty", status.message());
    }
    // case11: corner case
    {
        std::string reqData = "{\"agentID\": \"localID/\","
                              "\"timeoutSec\": \"10\"}";
        std::string contentType = "application/json";
        auto response = litebus::http::Post(urlEvictAgent, litebus::None(), reqData, contentType);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto status = ParseResponse(response.Get().body);
        EXPECT_EQ(status.code(), common::ERR_PARAM_INVALID);
        EXPECT_PRED_FORMAT2(testing::IsSubstring, "Invalid", status.message());
    }
    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}

// test query agent count
// case1: invalid method
// case2: query successful (not empty)
// case3: query fail (not ok)
// case4: query fail (multiple results)
TEST_F(GlobalSchedDriverTest, QueryAgentCountRouter)
{
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto status = globalSchedDriver_->Start();
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::http::URL urlQueryAgentCount("http", "127.0.0.1", port, GLOBAL_SCHEDULER + QUERY_AGENT_COUNT_URL);
    // case1: invalid method
    {
        auto response = litebus::http::Post(urlQueryAgentCount, litebus::None(), litebus::None(), litebus::None());
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
    }

    // case2: query successful
    {
        auto resp = std::make_shared<GetResponse>();
        KeyValue kv;
        kv.set_key(READY_AGENT_CNT_KEY);
        kv.set_value("100");
        resp->kvs.emplace_back(kv);
        EXPECT_CALL(*mockMetaStoreClient_, Get(_, _)).WillOnce(Return(resp));
        auto response = litebus::http::Get(urlQueryAgentCount, litebus::None());
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        EXPECT_TRUE(body == "100");
    }

    // case3: query fail (not ok)
    {
        auto resp = std::make_shared<GetResponse>();
        KeyValue kv;
        kv.set_key(READY_AGENT_CNT_KEY);
        kv.set_value("100");
        resp->kvs.emplace_back(kv);
        resp->kvs.emplace_back(kv);
        EXPECT_CALL(*mockMetaStoreClient_, Get(_, _)).WillOnce(Return(resp));
        auto response = litebus::http::Get(urlQueryAgentCount, litebus::None());
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        EXPECT_TRUE(body == "-1");
    }

    // case4: query fail (multiple results)
    {
        auto resp = std::make_shared<GetResponse>();
        resp->status = Status(StatusCode::FAILED, "get failed");
        EXPECT_CALL(*mockMetaStoreClient_, Get(_, _)).WillOnce(Return(resp));
        auto response = litebus::http::Get(urlQueryAgentCount, litebus::None());
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        EXPECT_TRUE(body == "-1");
    }

    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}

resource_view::InstanceInfo GetInstanceInfo(std::string instanceId)
{
    Resources resources;
    Resource resource_cpu = view_utils::GetCpuResource();
    (*resources.mutable_resources())["CPU"] = resource_cpu;
    Resource resource_memory = view_utils::GetMemResource();
    (*resources.mutable_resources())["Memory"] = resource_memory;

    InstanceInfo instanceInfo;
    instanceInfo.set_instanceid(instanceId);
    instanceInfo.set_requestid("requestIdIdId");
    instanceInfo.set_parentid("parentidIdId");
    instanceInfo.mutable_resources()->CopyFrom(resources);

    return instanceInfo;
}

TEST_F(GlobalSchedDriverTest, GetSchedulingQueue)
{
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto status = globalSchedDriver_->Start();
    uint16_t port = GetPortEnv("LITEBUS_PORT", 0);
    litebus::http::URL urlGetSchedulingQueue("http", "127.0.0.1", port, GLOBAL_SCHEDULER + GET_SCHEDULING_QUEUE_URL);

    // case1: invalid method
    {
        auto response = litebus::http::Post(urlGetSchedulingQueue, litebus::None(), litebus::None(), litebus::None());
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
    }

    // case2: query successful
    {
        auto resp = messages::QueryInstancesInfoResponse();
        resp.set_requestid("requestIdIdId");
        google::protobuf::RepeatedPtrField<resource_view::InstanceInfo> &instanceinfos = *resp.mutable_instanceinfos();
        instanceinfos.Add(std::move(GetInstanceInfo("app-script-1-instanceid")));
        instanceinfos.Add(std::move(GetInstanceInfo("app-script-2-instanceid")));
        EXPECT_CALL(*mockGlobalSched_, GetSchedulingQueue(_)).WillOnce(Return(resp));

        auto response = litebus::http::Get(urlGetSchedulingQueue, litebus::None());
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        auto infos = messages::QueryInstancesInfoResponse();

        EXPECT_EQ(google::protobuf::util::JsonStringToMessage(body, &infos).ok(), true);
        EXPECT_EQ(infos.instanceinfos_size(), 2);
    }

    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}

// test query resource info
// case1: invalid method
// case2: query successful (not empty)
TEST_F(GlobalSchedDriverTest, QueryResourcesRouter)
{
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto status = globalSchedDriver_->Start();
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    litebus::http::URL urlQueryResource("http", "127.0.0.1", port, GLOBAL_SCHEDULER + QUERY_RESOURCES_URL);
    std::string resourceId = "id1";

    // query resource info case1: invalid method
    {
        auto response = litebus::http::Post(urlQueryResource, litebus::None(), litebus::None(), litebus::None());
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
    }

    // query resource info case2: query successful (empty header)
    {
        auto resp = messages::QueryResourcesInfoResponse();
        (*resp.mutable_resource()) = std::move(view_utils::Get1DResourceUnit(resourceId));
        EXPECT_CALL(*mockGlobalSched_, QueryResourcesInfo(_)).WillOnce(Return(resp));
        auto response = litebus::http::Get(urlQueryResource, litebus::None());
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        auto infos = messages::QueryResourcesInfoResponse();
        EXPECT_EQ(google::protobuf::util::JsonStringToMessage(body, &infos).ok(), true);
        EXPECT_EQ(infos.resource().id(), resourceId);
    }

    // query resource info case3: query successful (header: type is json)
    {
        auto resp = messages::QueryResourcesInfoResponse();
        (*resp.mutable_resource()) = std::move(view_utils::Get1DResourceUnit(resourceId));
        EXPECT_CALL(*mockGlobalSched_, QueryResourcesInfo(_)).WillOnce(Return(resp));

        std::unordered_map<std::string, std::string> headers = {
            {"Type", "json"},
        };

        auto response = litebus::http::Get(urlQueryResource, headers);
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        auto infos = messages::QueryResourcesInfoResponse();
        EXPECT_EQ(google::protobuf::util::JsonStringToMessage(body, &infos).ok(), true);
        EXPECT_EQ(infos.resource().id(), resourceId);
    }

    // query resource info case4: query successful (header: type is protobuf)
    {
        auto resp = messages::QueryResourcesInfoResponse();
        (*resp.mutable_resource()) = std::move(view_utils::Get1DResourceUnit("id1"));
        EXPECT_CALL(*mockGlobalSched_, QueryResourcesInfo(_)).WillOnce(Return(resp));
        std::unordered_map<std::string, std::string> headers = {
            {"Type", "protobuf"},
        };

        auto response = litebus::http::Get(urlQueryResource, headers);
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        auto infos = messages::QueryResourcesInfoResponse();
        EXPECT_EQ(infos.ParseFromString(body), true);
        EXPECT_EQ(infos.resource().id(), resourceId);
    }

    // query resource info case3: query successful (header: invalid type)
    {
        std::unordered_map<std::string, std::string> headers = {
                {"Type", "invalidType"},
        };

        auto response = litebus::http::Get(urlQueryResource, headers);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::BAD_REQUEST);
    }

    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}
}  // namespace functionsystem::test
