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

if (BUILD_LLT)
    add_definitions(-DJSON_HAS_CPP_14)
    message(STATUS "LLT is enabled")
    enable_testing()
    get_property(META_STORE_CLIENT_INCLUDE_DIR GLOBAL PROPERTY "META_STORE_CLIENT_INCLUDE_DIR")
    include_directories(${META_STORE_CLIENT_INCLUDE_DIR})
    add_subdirectory(unit)
    add_subdirectory(integration)
endif ()
