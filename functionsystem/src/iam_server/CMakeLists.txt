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

project(iam_server_lib)

message(STATUS "Begin to configure iam_server")

set_property(GLOBAL PROPERTY SRC_IAM_SERVER_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} IAM_SERVER_SRCS)
list(REMOVE_ITEM IAM_SERVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp") # exclude main

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_library(iam_server_lib STATIC ${IAM_SERVER_SRCS})
set_target_properties(iam_server_lib PROPERTIES UNITY_BUILD ON)
target_compile_options(iam_server_lib PRIVATE -fPIE)
target_link_libraries(iam_server_lib
        ${etcdapi_LIB_A} kube_client
        rpc sts leader explorer
        meta_store_client
        http aksk
        common_flags utils
        posix_pb metrics_adapter)

IF ("${STS_BUILD_ENABLE}" STREQUAL "ON")
    target_link_libraries(iam_server_lib ${stssdk_LIB})
endif ()

add_subdirectory(driver)
add_subdirectory(flags)
add_subdirectory(iam)

add_executable(iam_server main.cpp)
target_compile_options(iam_server PRIVATE -fPIE)

target_link_libraries(iam_server PRIVATE iam_server_lib)

INSTALL_TARGET(iam_server)