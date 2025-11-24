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

#include <async/async.hpp>
#include <async/defer.hpp>
#include <async/future.hpp>

#include "common/logs/logging.h"
#include "meta_store_client/meta_store_client.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/proto/pb/message_pb.h"
#include "kv_service_actor.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;
using namespace testing;
const int MAX_RETRY_TIMES = 3;

class MockKvServiceAccessorActor : public litebus::ActorBase {
public:
    explicit MockKvServiceAccessorActor(const litebus::AID &kvServiceActor)
        : ActorBase("KvServiceAccessorActor"), kvActorAid_(kvServiceActor)
    {
    }

    ~MockKvServiceAccessorActor() override = default;

protected:
    void Init() override
    {
        Receive("Put", &MockKvServiceAccessorActor::AsyncPut);
        Receive("Delete", &MockKvServiceAccessorActor::AsyncDelete);
        Receive("Get", &MockKvServiceAccessorActor::AsyncGet);
        Receive("Txn", &MockKvServiceAccessorActor::AsyncTxn);
        Receive("Watch", &MockKvServiceAccessorActor::AsyncWatch);
        Receive("GetAndWatch", &MockKvServiceAccessorActor::AsyncGetAndWatch);
    }
    void Finalize() override
    {
    }

public:
    void AsyncPut(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockAsyncPut(from, std::move(name), std::move(msg));
    }
    void AsyncDelete(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockAsyncDelete(from, std::move(name), std::move(msg));
    }
    void AsyncGet(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockAsyncGet(from, std::move(name), std::move(msg));
    }
    void AsyncTxn(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockAsyncTxn(from, std::move(name), std::move(msg));
    }
    void AsyncWatch(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockAsyncWatch(from, std::move(name), std::move(msg));
    }
    void AsyncGetAndWatch(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockAsyncGetAndWatch(from, std::move(name), std::move(msg));
    }

    MOCK_METHOD(void, MockAsyncPut, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockAsyncDelete, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockAsyncGet, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockAsyncTxn, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockAsyncWatch, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockAsyncGetAndWatch, (const litebus::AID &, std::string, std::string));

    void RemoveRequest(const std::string &id)
    {
        if (auto iter(requestCount_.find(id)); iter != requestCount_.end()) {
            requestCount_.erase(iter);
        }
    }

    litebus::AID kvActorAid_;
    std::unordered_map<std::string, int> requestCount_;
};

class MetaStoreClientBackOffRetryTest : public ::testing::Test {
public:
    MetaStoreClientBackOffRetryTest() = default;
    ~MetaStoreClientBackOffRetryTest() override = default;
    [[maybe_unused]] static void SetUpTestSuite()
    {
    }
    [[maybe_unused]] static void TearDownTestSuite()
    {
    }

protected:
    void SetUp() override
    {
        kvActor_ = std::make_shared<meta_store::KvServiceActor>();
        kvActorAid_ = kvActor_->GetAID();
        kvAccessorActor_ = std::make_shared<MockKvServiceAccessorActor>(kvActorAid_);
        litebus::Spawn(kvActor_);
        litebus::Spawn(kvAccessorActor_);
    }

    void TearDown() override
    {
        if (kvAccessorActor_ != nullptr) {
            litebus::Terminate(kvAccessorActor_->GetAID());
            litebus::Await(kvAccessorActor_->GetAID());
        }
        if (kvActor_ != nullptr) {
            litebus::Terminate(kvActor_->GetAID());
            litebus::Await(kvActor_->GetAID());
        }
    }

protected:
    std::shared_ptr<meta_store::KvServiceActor> kvActor_;
    litebus::AID kvActorAid_;
    std::shared_ptr<MockKvServiceAccessorActor> kvAccessorActor_;
    MetaStoreTimeoutOption metaStoreTimeoutOpt_ = {
        .operationRetryIntervalLowerBound = 5,
        .operationRetryIntervalUpperBound = 15,
        .operationRetryTimes = MAX_RETRY_TIMES,
        .grpcTimeout = 0,
    };
};

TEST_F(MetaStoreClientBackOffRetryTest, DropFirstSeveralAttemptsAndSuccess_PutGetDelete)
{
    EXPECT_CALL(*kvAccessorActor_, MockAsyncPut)
        .Times(MAX_RETRY_TIMES)
        .WillRepeatedly([this](const litebus::AID &from, std::string && /* name */, std::string &&msg) {
            auto req = std::make_shared<messages::MetaStore::PutRequest>();
            ASSERT_TRUE(req->ParseFromString(msg));
            if ((++kvAccessorActor_->requestCount_[req->requestid()]) == MAX_RETRY_TIMES) {
                litebus::Async(kvActorAid_, &KvServiceActor::AsyncPut, from, req)
                    .OnComplete(
                        litebus::Defer(kvAccessorActor_, &MockKvServiceAccessorActor::RemoveRequest, req->requestid()));
            } else {
                YRLOG_DEBUG("Dropped Put for {} times", kvAccessorActor_->requestCount_[req->requestid()]);
            }
        });

    EXPECT_CALL(*kvAccessorActor_, MockAsyncDelete)
        .Times(MAX_RETRY_TIMES)
        .WillRepeatedly([this](const litebus::AID &from, std::string && /* name */, std::string &&msg) {
            auto req = std::make_shared<messages::MetaStoreRequest>();
            ASSERT_TRUE(req->ParseFromString(msg));
            if ((++kvAccessorActor_->requestCount_[req->requestid()]) == MAX_RETRY_TIMES) {
                litebus::Async(kvActorAid_, &KvServiceActor::AsyncDelete, from, req)
                    .OnComplete(
                        litebus::Defer(kvAccessorActor_, &MockKvServiceAccessorActor::RemoveRequest, req->requestid()));
            } else {
                YRLOG_DEBUG("Dropped Delete for {} times", kvAccessorActor_->requestCount_[req->requestid()]);
            }
        });

    EXPECT_CALL(*kvAccessorActor_, MockAsyncGet)
        .Times(MAX_RETRY_TIMES)
        .WillRepeatedly([this](const litebus::AID &from, std::string && /* name */, std::string &&msg) {
            auto req = std::make_shared<messages::MetaStoreRequest>();
            ASSERT_TRUE(req->ParseFromString(msg));
            if ((++kvAccessorActor_->requestCount_[req->requestid()]) == MAX_RETRY_TIMES) {
                litebus::Async(kvActorAid_, &KvServiceActor::AsyncGet, from, req)
                    .OnComplete(
                        litebus::Defer(kvAccessorActor_, &MockKvServiceAccessorActor::RemoveRequest, req->requestid()));
            } else {
                YRLOG_DEBUG("Dropped Get for {} times", kvAccessorActor_->requestCount_[req->requestid()]);
            }
        });

    EXPECT_CALL(*kvAccessorActor_, MockAsyncWatch)
        .WillOnce([this](const litebus::AID &from, std::string && /* name */, std::string &&msg) {
            auto req = std::make_shared<messages::MetaStoreRequest>();
            ASSERT_TRUE(req->ParseFromString(msg));
            // do not drop watch request
            litebus::Async(kvActorAid_, &KvServiceActor::AsyncWatch, from, req);
        });

    MetaStoreConfig metaStoreConf;
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    metaStoreConf.metaStoreAddress = "127.0.0.1:" + std::to_string(port);
    metaStoreConf.enableMetaStore = true;
    auto client = MetaStoreClient::Create(metaStoreConf, GrpcSslConfig(), metaStoreTimeoutOpt_);
    litebus::Promise<bool> putPromise, deletePromise;
    {
        auto observer = [putPromise, deletePromise](const std::vector<WatchEvent> &events, bool) -> bool {
            for (auto &event : events) {
                switch (event.eventType) {
                    case EVENT_TYPE_PUT: {
                        putPromise.SetValue(true);
                        break;
                    }
                    case EVENT_TYPE_DELETE: {
                        deletePromise.SetValue(true);
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }
            return true;
        };
        WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
        auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
            return SyncResult{ Status::OK() };
        };
        auto watcher = client->Watch("llt/sn/workers", option, observer, syncer);
        ASSERT_AWAIT_READY(watcher);
    }
    {
        PutOption op = { .leaseId = 0, .prevKv = false };
        client->Put("llt/sn/workers/xxx", "1.0", op).Get();
    }
    {
        GetOption op = { false, false, false, 0, SortOrder::DESCEND, SortTarget::MODIFY };
        auto response = client->Get("llt/sn/workers/xxx", op).Get();
        EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(1));
        EXPECT_EQ(response->kvs[0].key(), "llt/sn/workers/xxx");
        EXPECT_EQ(response->kvs[0].value(), "1.0");
    }
    {
        DeleteOption op = { .prevKv = true, .prefix = false };
        auto response = client->Delete("llt/sn/workers/xxx", op).Get();
        EXPECT_EQ(response->deleted, 1);
        EXPECT_EQ(response->prevKvs.size(), static_cast<uint32_t>(1));
        EXPECT_EQ(response->prevKvs[0].key(), "llt/sn/workers/xxx");
        EXPECT_EQ(response->prevKvs[0].value(), "1.0");
    }
    ASSERT_AWAIT_READY(putPromise.GetFuture());
    ASSERT_AWAIT_READY(deletePromise.GetFuture());
}

TEST_F(MetaStoreClientBackOffRetryTest, DropFirstSeveralAttemptsAndSuccess_Txn)
{
    EXPECT_CALL(*kvAccessorActor_, MockAsyncPut)
        .WillOnce([this](const litebus::AID &from, std::string && /* name */, std::string &&msg) {
            auto req = std::make_shared<messages::MetaStore::PutRequest>();
            ASSERT_TRUE(req->ParseFromString(msg));
            litebus::Async(kvActorAid_, &KvServiceActor::AsyncPut, from, req);
        });

    EXPECT_CALL(*kvAccessorActor_, MockAsyncTxn)
        .Times(MAX_RETRY_TIMES)
        .WillRepeatedly([this](const litebus::AID &from, std::string && /* name */, std::string &&msg) {
            auto req = std::make_shared<messages::MetaStoreRequest>();
            ASSERT_TRUE(req->ParseFromString(msg));
            if ((++kvAccessorActor_->requestCount_[req->requestid()]) == MAX_RETRY_TIMES) {
                litebus::Async(kvActorAid_, &KvServiceActor::AsyncTxn, from, req)
                    .OnComplete(
                        litebus::Defer(kvAccessorActor_, &MockKvServiceAccessorActor::RemoveRequest, req->requestid()));
            } else {
                YRLOG_DEBUG("Dropped Txn for {} times", kvAccessorActor_->requestCount_[req->requestid()]);
            }
        });

    EXPECT_CALL(*kvAccessorActor_, MockAsyncGetAndWatch)
        .WillRepeatedly([this](const litebus::AID &from, std::string && /* name */, std::string &&msg) {
            auto req = std::make_shared<messages::MetaStoreRequest>();
            ASSERT_TRUE(req->ParseFromString(msg));
            litebus::Async(kvActorAid_, &KvServiceActor::AsyncGetAndWatch, from, req);
        });

    MetaStoreConfig metaStoreConf;
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    metaStoreConf.metaStoreAddress = "127.0.0.1:" + std::to_string(port);
    metaStoreConf.enableMetaStore = true;
    auto client = MetaStoreClient::Create(metaStoreConf, GrpcSslConfig(), metaStoreTimeoutOpt_);
    litebus::Promise<bool> putPromise, deletePromise;
    {
        auto observer = [putPromise, deletePromise](const std::vector<WatchEvent> &events, bool) -> bool {
            for (auto &event : events) {
                switch (event.eventType) {
                    case EVENT_TYPE_PUT: {
                        putPromise.SetValue(true);
                        break;
                    }
                    case EVENT_TYPE_DELETE: {
                        deletePromise.SetValue(true);
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }
            return true;
        };
        WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
        auto syncer = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
            return SyncResult{ Status::OK() };
        };
        auto watcher = client->GetAndWatch("llt/sn/workers", option, observer, syncer);
        ASSERT_AWAIT_READY(watcher);

        watcher = client->GetAndWatch("llt/sn/workers", option, observer, syncer);
        ASSERT_AWAIT_READY(watcher);
    }

    {
        PutOption op = { .leaseId = 0, .prevKv = false };
        client->Put("llt/sn/workers/xxx", "1.0", op).Get();
    }
    {
        auto transaction = client->BeginTransaction();
        transaction->If(TxnCompare::OfValue("llt/sn/workers/xxx", CompareOperator::EQUAL, "1.0"));
        DeleteOption delOption = { true, false };
        transaction->Then(TxnOperation::Create("llt/sn/workers/xxx", delOption));
        PutOption putOption = { .leaseId = 0, .prevKv = true };
        transaction->Then(TxnOperation::Create("llt/sn/workers/yyy", "2.0", putOption));
        GetOption getOption = { true, false, false, 0, SortOrder::DESCEND, SortTarget::KEY };
        transaction->Then(TxnOperation::Create("llt/sn/workers/", getOption));
        transaction->Else(TxnOperation::Create("llt/sn/workers/zzz", "2.0", putOption));

        std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();

        EXPECT_TRUE(txnResponse->success);
        EXPECT_EQ(txnResponse->responses.size(), static_cast<uint32_t>(3));

        EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[0].response).prevKvs.size(),
                  static_cast<uint32_t>(1));
        EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[0].response).prevKvs[0].key(), "llt/sn/workers/xxx");
        EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[1].response).prevKv.value(), "");
        EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs.size(), static_cast<uint32_t>(1)); // Txn 中的 Range 查询始终查的缓存 cache_
        EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].key(), "llt/sn/workers/xxx"); // Txn 中的 Range 查询始终查的缓存 cache_
        EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].value(), "1.0"); // Txn 中的 Range 查询始终查的缓存 cache_
    }

    ASSERT_AWAIT_READY(putPromise.GetFuture());
    ASSERT_AWAIT_READY(deletePromise.GetFuture());
}
}  // namespace functionsystem::meta_store::test
