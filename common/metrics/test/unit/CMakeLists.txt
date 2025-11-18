# Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.

set(UNIT_TEST_MODULE observability-metrics_unit_test)

add_executable(${UNIT_TEST_MODULE} main.cpp)
target_compile_options(${UNIT_TEST_MODULE} PRIVATE -Wno-error)
add_dependencies(${UNIT_TEST_MODULE} gtest_1_12_1 cjson)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/metric METRICS_UNIT_TEST_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/exporter METRICS_UNIT_TEST_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/common METRICS_UNIT_TEST_SRCS)

target_sources(${UNIT_TEST_MODULE} PRIVATE ${METRICS_UNIT_TEST_SRCS})

target_link_libraries(
  ${UNIT_TEST_MODULE}
  PRIVATE pthread
          dl
          ${litebus_ALL_LIB}
          ${gtest_LIB_A}
          ${gmock_LIB_A}
          observability-metrics
          observability-metrics-sdk
          observability-metrics-exporter-ostream
          observability-metrics-file-exporter
          observability-prometheus-push-exporter)

target_include_directories(
  ${UNIT_TEST_MODULE}
  PRIVATE "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/api/include>")

add_test(NAME ${UNIT_TEST_MODULE} COMMAND ${UNIT_TEST_MODULE}
                                          --gtest_filter=*.*)

install(TARGETS ${UNIT_TEST_MODULE} DESTINATION ${PROJECT_BINARY_DIR}/bin)
