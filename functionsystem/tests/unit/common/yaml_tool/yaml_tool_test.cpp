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

#include "common/yaml_tool/yaml_tool.h"

#include <gtest/gtest.h>

#include "common/service_json/service_json.h"

namespace functionsystem::test {

const std::string yamlStr1 =
    "- service: oxx\n"
    "  kind: yrlib\n"
    "  description: this is oxx demo\n"
    "  functions:\n"
    "    oxx:\n"
    "      timeout: 86400\n"
    "      cpu: 1500\n"
    "      memory: 3000\n"
    "      runtime: cpp11\n"
    "      concurrentNum: 20\n"
    "      environment:\n"
    "        key: value\n"
    "      storageType: local\n"
    "      codePath: /home/sn/";

const std::string yamlStr2 =
    "- service: wm\n"
    "  kind: yrlib\n"
    "  description: this is wm demo\n"
    "  functions:\n"
    "    wm:\n"
    "      timeout: 86400\n"
    "      cpu: 1500\n"
    "      memory: 3000\n"
    "      runtime: cpp11\n"
    "      concurrentNum: 20\n"
    "      environment:\n"
    "        WEIMING_HOME: /data/build/centos7.x86_64.release\n"
    "      storageType: local\n"
    "      codePath: /data/build/centos7.x86_64.release/lib/pkg";

class YamlToolTest : public ::testing::Test {};

std::string PrintHookHandler(std::map<std::string, std::string> hookHandler)
{
    std::string str = "{";
    for (auto &handler : hookHandler) {
        str += "[" + handler.first + " : " + handler.second + "]";
    }
    str += "}";
    return str;
}

TEST_F(YamlToolTest, TranslateSuccess)
{
    std::string expectJsonStr =
        "[{\"service\": \"oxx\", \"kind\": \"yrlib\", \"description\": \"this is oxx demo\", \"functions\": {\"oxx\": "
        "{\"timeout\": \"86400\", \"cpu\": \"1500\", \"memory\": \"3000\", \"runtime\": \"cpp11\", \"concurrentNum\": "
        "\"20\", \"environment\": {\"key\": \"value\"}, \"storageType\": \"local\", \"codePath\": \"/home/sn/\"}}}]";

    auto jsonStr = std::string(YamlToJson(yamlStr1));

    EXPECT_STREQ(jsonStr.c_str(), expectJsonStr.c_str());
}

TEST_F(YamlToolTest, GetFunctionMetaSuccess)
{
    auto jsonStr = std::string(YamlToJson(yamlStr2.c_str()));

    auto serviceInfosOpt = service_json::GetServiceInfosFromJson(jsonStr);
    ASSERT_TRUE(serviceInfosOpt.IsSome());
    EXPECT_EQ(serviceInfosOpt.Get().size(), 1u);

    auto functionMetas = service_json::ConvertFunctionMeta(serviceInfosOpt.Get(), "/home/sn");

    ASSERT_TRUE(functionMetas.IsSome());
    EXPECT_EQ(functionMetas.Get().size(), 1u);

    for (auto functionMeta : functionMetas.Get()) {
        YRLOG_INFO("name: {}", functionMeta.funcMetaData.name);
        YRLOG_INFO("urn: {}", functionMeta.funcMetaData.urn);
        YRLOG_INFO("runtime: {}", functionMeta.funcMetaData.runtime);
        YRLOG_INFO("handler: {}", functionMeta.funcMetaData.handler);
        YRLOG_INFO("codeSha256: {}", functionMeta.funcMetaData.codeSha256);
        YRLOG_INFO("entryFile: {}", functionMeta.funcMetaData.entryFile);
        YRLOG_INFO("hookHandler: {}", PrintHookHandler(functionMeta.funcMetaData.hookHandler));
        YRLOG_INFO("version: {}", functionMeta.funcMetaData.version);
        YRLOG_INFO("tenantID: {}", functionMeta.funcMetaData.tenantId);
        YRLOG_INFO("storage: {}", functionMeta.codeMetaData.storageType);
        YRLOG_INFO("bucketID: {}", functionMeta.codeMetaData.bucketID);
        YRLOG_INFO("objectID: {}", functionMeta.codeMetaData.objectID);
        YRLOG_INFO("deployDir: {}", functionMeta.codeMetaData.deployDir);
        YRLOG_INFO("envKey: {}", functionMeta.envMetaData.envKey);
        YRLOG_INFO("envInfo: {}", functionMeta.envMetaData.envInfo);
        YRLOG_INFO("encryptedUserData: {}", functionMeta.envMetaData.encryptedUserData);
        YRLOG_INFO("resources: {}", functionMeta.resources.ShortDebugString());
        YRLOG_INFO("maxInstance: {}", functionMeta.extendedMetaData.instanceMetaData.maxInstance);
        YRLOG_INFO("minInstance: {}", functionMeta.extendedMetaData.instanceMetaData.minInstance);
        YRLOG_INFO("concurrentNum: {}", functionMeta.extendedMetaData.instanceMetaData.concurrentNum);
        YRLOG_INFO("cacheInstance: {}", functionMeta.extendedMetaData.instanceMetaData.cacheInstance);
    }

    sleep(1);
}

}  // namespace functionsystem::test
