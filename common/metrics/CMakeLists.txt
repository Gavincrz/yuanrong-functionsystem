# Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.

cmake_minimum_required(VERSION 3.16.1)
project(observability-metrics LANGUAGES CXX C)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "compile tools: ccache")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")

include(GNUInstallDirs)
set(CMAKE_INSTALL_LIBDIR
    "lib"
    CACHE PATH "Installation directory name for libraries" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(INSTALL_LIBDIR
    ${CMAKE_INSTALL_LIBDIR}
    CACHE PATH "Installation directory for libraries")
set(INSTALL_INCLUDEDIR
    ${CMAKE_INSTALL_INCLUDEDIR}
    CACHE PATH "Installation directory for header files")

foreach(dir LIBDIR INCLUDEDIR)
  file(TO_NATIVE_PATH ${CMAKE_INSTALL_PREFIX}/${INSTALL_${dir}} _install_dir)
  message(STATUS "Installing ${dir} components to ${_install_dir}")
  unset(_install_dir)
endforeach()

# build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Set logs build type: " ${CMAKE_BUILD_TYPE})
message(STATUS "Set logs build config path: " ${BUILD_CONFIG_DIR})

# 引入三方件与配置
# 设置 vendor 文件夹路径 | VENDOR_DIR
set(VENDOR_DIR ${CMAKE_CURRENT_LIST_DIR}/../../vendor)
message(STATUS "VENDOR_DIR: ${VENDOR_DIR}")
# 设置三方件编译配置，导入公共参数
include(${VENDOR_DIR}/vendor_utils.cmake)
# 引入三方件编译的 cmake 文件
list(APPEND CMAKE_MODULE_PATH ${VENDOR_CMAKE_DIR})
include(zlib)
include(spdlog)
include(securec)
include(openssl)
include(cjson)
include(curl)
if (LOGS_BUILD_TEST)
    include(gtest_1_12_1)
endif()
# 引入二方件编译的 cmake 文件
include(${CMAKE_CURRENT_LIST_DIR}/../logs/logs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../litebus/litebus.cmake)
set(litebus_ALL_LIB ${securec_LIB} ${yrlogs_LIB} ${litebus_LIB} pthread)

message(STATUS "Config vendor cmake files done")
# ---------------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)

set(WARN_FLAGS
    "-Wall -Werror -Wextra -Wconversion -Wno-float-equal -Wno-stringop-overflow -Wtrampolines -Wformat=2 -Wdate-time -Wcast-qual -Wcast-align -Wunused"
)
set(CXX_WANR_FLAGS
    "${WARN_FLAGS} -Woverloaded-virtual -Wnon-virtual-dtor -Wdelete-non-virtual-dtor"
)
set(C_WANR_FLAGS "${WARN_FLAGS} -Wstrict-prototypes")
set(LANGUAGE_FLAGS "-fsigned-char -funsigned-char")
set(CODE_GENERATE_FLAGS "-fno-common -freg-struct-return -fstrong-eval-order")
set(OPTIMIZE_FLAGS "-fno-omit-frame-pointer")
set(COMPILE_SAFE_FLAGS "-fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(CMAKE_CXX_FLAGS
    "-fuse-ld=gold -pipe ${CXX_WANR_FLAGS} ${LANGUAGE_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS}"
)
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g3 -ftrapv -fstack-check")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS
    "-fuse-ld=gold -pipe ${C_WANR_FLAGS} ${LANGUAGE_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS}"
)
set(CMAKE_C_FLAGS_DEBUG "-O2 -g3 -ftrapv -fstack-check")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

set(LINK_SAFE_FLAGS "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -s")
set(LINK_COMMON_FLASG "-rdynamic -Wl,--no-as-needed ${LINK_SAFE_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${LINK_COMMON_FLASG}")
set(CMAKE_EXE_LINKER_FLAGS "-pie ${LINK_COMMON_FLASG}")

if(BUILD_GCOV)
  message(STATUS "GCOV is enabled")
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
  set(CMAKE_C_FLAGS
      "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
endif()

add_library(observability-metrics-headers INTERFACE)
target_include_directories(
  observability-metrics-headers
  INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<INSTALL_INTERFACE:include>")

install(
  TARGETS observability-metrics-headers
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(
  DIRECTORY include/metrics
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
  PATTERN "*.h")

add_subdirectory(src)

if(BUILD_LLT)
  add_subdirectory(test)
endif()
