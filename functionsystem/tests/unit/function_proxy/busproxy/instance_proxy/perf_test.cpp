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

#include <string>

#include "busproxy/instance_proxy/perf.h"
#include "common/status/status.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace busproxy;

class PerfTest : public ::testing::Test {};

TEST_F(PerfTest, PerfRecorderTest)
{
    Perf perf;
    perf.Enable(true);
    std::string requestID = "perf-requestID";
    std::string instanceID = "perf-instanceID";
    std::string traceID = "perf-traceID";
    runtime::CallRequest callreq;
    callreq.set_requestid(requestID);
    callreq.set_traceid(traceID);
    perf.Record(callreq, instanceID, nullptr);
    perf.RecordSendCall(requestID);
    perf.RecordReceivedCallRsp(requestID);
    perf.RecordCallResult(requestID, nullptr);
    perf.RecordSendCallResult(requestID);
    auto context = perf.GetPerfContext(requestID);
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->traceID, traceID);
    EXPECT_EQ(context->dstInstance, instanceID);
    EXPECT_EQ(context->requestID, requestID);
    EXPECT_NE(context->proxyReceivedTime, nullptr);
    EXPECT_NE(context->proxySendCallTime, nullptr);
    EXPECT_NE(context->proxyReceivedCallRspTime, nullptr);
    EXPECT_NE(context->proxyReceivedCallResultTime, nullptr);
    EXPECT_NE(context->proxySendCallResultTime, nullptr);
    perf.EndRecord(requestID);
    context = perf.GetPerfContext(requestID);
    ASSERT_EQ(context, nullptr);

    perf.Enable(false);
    perf.Record(callreq, instanceID, nullptr);
    perf.RecordSendCall(requestID);
    perf.RecordReceivedCallRsp(requestID);
    perf.RecordCallResult(requestID, nullptr);
    perf.RecordSendCallResult(requestID);
    context = perf.GetPerfContext(requestID);
    ASSERT_EQ(context, nullptr);
}

}