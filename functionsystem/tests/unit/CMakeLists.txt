# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

include_directories(${CMAKE_CURRENT_LIST_DIR})

set(UNIT_TEST_MODULE functionsystem_unit_test)

add_executable(${UNIT_TEST_MODULE} main.cpp)
target_compile_options(${UNIT_TEST_MODULE} PRIVATE -Wno-error -fno-access-control -Wno-missing-field-initializers) # -fno-access-control for test

add_dependencies(${UNIT_TEST_MODULE} gtest_1_10_0)

add_subdirectory(utils)
add_subdirectory(common)
add_subdirectory(domain_scheduler)
add_subdirectory(function_proxy)
add_subdirectory(function_master)
add_subdirectory(runtime_manager)
add_subdirectory(function_agent) # function_agent test

get_property(SRC_INCLUDE_DIR GLOBAL PROPERTY "SRC_INCLUDE_DIR")
get_property(SRC_BUSPROXY_INCLUDE_DIR GLOBAL PROPERTY "SRC_BUSPROXY_INCLUDE_DIR")
get_property(SRC_LOCAL_SCHEDULER_INCLUDE_DIR GLOBAL PROPERTY "SRC_LOCAL_SCHEDULER_INCLUDE_DIR")
get_property(SRC_FUNCTION_PROXY_INCLUDE_DIR GLOBAL PROPERTY "SRC_FUNCTION_PROXY_INCLUDE_DIR")
get_property(INSTANCE_MANAGER_DIR GLOBAL PROPERTY "INSTANCE_MANAGER_DIR") # instance_manager src
get_property(INSTANCE_MANAGER_LIB GLOBAL PROPERTY "INSTANCE_MANAGER_LIB") # instance_manager lib
get_property(SYSTEM_FUNCTION_LOADER_DIR GLOBAL PROPERTY "SYSTEM_FUNCTION_LOADER_DIR") # system_function_loader src
get_property(SYSTEM_FUNCTION_LOADER_LIB GLOBAL PROPERTY "SYSTEM_FUNCTION_LOADER_LIB") # system_function_loader lib
get_property(MASTER_COMMON_DIR GLOBAL PROPERTY "MASTER_COMMON_DIR")
get_property(MASTER_COMMON_LIB GLOBAL PROPERTY "MASTER_COMMON_LIB")
get_property(SCALER_DIR GLOBAL PROPERTY "SCALER_DIR") # scaler src
get_property(SCALER_LIB GLOBAL PROPERTY "SCALER_LIB") # scaler lib
get_property(GLOBAL_SCHEDULER_DIR GLOBAL PROPERTY "GLOBAL_SCHEDULER_DIR")
get_property(GLOBAL_SCHEDULER_LIB GLOBAL PROPERTY "GLOBAL_SCHEDULER_LIB")
get_property(RESOURCE_GROUP_MANAGER_DIR GLOBAL PROPERTY "RESOURCE_GROUP_MANAGER_DIR")
get_property(RESOURCE_GROUP_MANAGER_LIB GLOBAL PROPERTY "RESOURCE_GROUP_MANAGER_LIB")
get_property(META_STORE_LIB GLOBAL PROPERTY "META_STORE_LIB")
get_property(SRC_RUNTIME_MANAGER_INCLUDE_DIR GLOBAL PROPERTY "SRC_RUNTIME_MANAGER_INCLUDE_DIR")
get_property(SRC_DOMAIN_SCHEDULER_INCLUDE_DIR GLOBAL PROPERTY "SRC_DOMAIN_SCHEDULER_INCLUDE_DIR")
get_property(FUNCTION_AGENT_DIR GLOBAL PROPERTY "FUNCTION_AGENT_DIR") # function_agent src
get_property(FUNCTION_AGENT_LIB GLOBAL PROPERTY "FUNCTION_AGENT_LIB") # function_agent lib
get_property(SRC_RUNTIME_MANAGER_INCLUDE_DIR GLOBAL PROPERTY "SRC_RUNTIME_MANAGER_INCLUDE_DIR")
get_property(SRC_IAM_SERVER_INCLUDE_DIR GLOBAL PROPERTY "SRC_IAM_SERVER_INCLUDE_DIR") # iam_server src
get_property(META_STORE_DIR GLOBAL PROPERTY "META_STORE_DIR")
target_include_directories(${UNIT_TEST_MODULE} PRIVATE
        ${CLIENT_CPP_INCLUDE}
        ${gtest_INCLUDE_DIR}
        ${SRC_INCLUDE_DIR}
        ${META_STORE_DIR}
        ${SRC_BUSPROXY_INCLUDE_DIR}
        ${SRC_LOCAL_SCHEDULER_INCLUDE_DIR}
        ${SRC_FUNCTION_PROXY_INCLUDE_DIR}
        ${MASTER_COMMON_DIR} # function master common src
        ${INSTANCE_MANAGER_DIR} # instance_manager src
        ${SYSTEM_FUNCTION_LOADER_DIR} # system_function_loader src
        ${RESOURCE_GROUP_MANAGER_DIR}
        ${FUNCTION_AGENT_DIR} # function_agent src
        ${SCALER_DIR} # scaler src
        ${GLOBAL_SCHEDULER_DIR}
        ${SRC_RUNTIME_MANAGER_INCLUDE_DIR}
        ${SRC_DOMAIN_SCHEDULER_INCLUDE_DIR}
)
target_compile_options(${UNIT_TEST_MODULE} PRIVATE "-fvisibility=hidden")
get_property(PLUGIN_LIB GLOBAL PROPERTY "PLUGIN_LIB")
target_link_libraries(${UNIT_TEST_MODULE} PRIVATE
        ${crypto_LIB} ${ssl_LIB} ${protobuf_LIB}
        ${grpcpp_LIB} ${gpr_LIB} ${etcdapi_LIB_A}
        ${securec_LIB}
        ${gtest_LIB_A}
        ${gmock_LIB_A}
        ${litebus_ALL_LIB}
        ${GLOBAL_SCHEDULER_LIB}
        ${MASTER_COMMON_LIB} # function master common lib
        ${INSTANCE_MANAGER_LIB} # instance_manager lib
        ${SYSTEM_FUNCTION_LOADER_LIB} # system_function_loader lib
        ${SCALER_LIB} # scaler lib
        ${RESOURCE_GROUP_MANAGER_LIB}
        ${FUNCTION_AGENT_LIB} # function_agent lib
        ${META_STORE_LIB}
        scheduler_topology
        busproxy_lib
        global_scheduler_lib
        local_scheduler_lib
        runtime_manager_lib
        function_proxy_common_lib
        leader
        explorer
        profile
        yaml_tool
        metrics_adapter
        ${PLUGIN_LIB}
        pthread
)

add_test(NAME ${UNIT_TEST_MODULE} COMMAND ${UNIT_TEST_MODULE} --gtest_filter=*.*)
