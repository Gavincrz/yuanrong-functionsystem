if("${BUILD_CONFIG_DIR}" STREQUAL "")
  message(STATUS "use default thirdparty config dir")
  set(BUILD_CONFIG_DIR ${ROOT_DIR}/build)
endif()

message(STATUS "BUILD_CONFIG_DIR is ${BUILD_CONFIG_DIR}")

list(APPEND CMAKE_MODULE_PATH ${BUILD_CONFIG_DIR}/thirdparty/cmake)
list(APPEND CMAKE_MODULE_PATH ${BUILD_CONFIG_DIR}/yuanrong/cmake)

include(litebus)
include(logs)
include(third_utils)
include(zlib)
include(spdlog)
include(securec)
include(openssl)
include(cjson)
include(curl)

if(BUILD_LLT)
  include(gtest_1_12_1)
endif()

set(litebus_ALL_LIB ${securec_LIB} ${yrlogs_LIB} ${litebus_LIB} pthread)
