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

add_subdirectory(data_view)
add_subdirectory(distribute_cache_client)
add_subdirectory(observer)
add_subdirectory(posix_client)
add_subdirectory(posix_service)
add_subdirectory(state_handler)
add_subdirectory(state_machine)
add_subdirectory(rate_limiter)
add_subdirectory(common_driver)
add_subdirectory(iam)

target_include_directories(${UNIT_TEST_MODULE} PRIVATE ${SRC_BUSPROXY_INCLUDE_DIR})
