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

message(STATUS "litebus src dir: ${CMAKE_CURRENT_LIST_DIR}")
if (NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/output)
    if (EXISTS ${CMAKE_CURRENT_LIST_DIR})
        message(STATUS "begin build litebus on ${CMAKE_CURRENT_LIST_DIR}")
        execute_process(COMMAND bash build/build.sh -X 1.1.1 -W off -t off WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
    endif()
endif()


set(litebus_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/output/include)
set(litebus_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/output/lib)
set(litebus_LIB ${litebus_LIB_DIR}/liblitebus.so)

include_directories(${litebus_INCLUDE_DIR})

install(FILES ${litebus_LIB_DIR}/liblitebus.so DESTINATION lib)
install(FILES ${litebus_LIB_DIR}/liblitebus.so.0.0.1 DESTINATION lib)