# Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.

set(METRICS_INTEGRATION_TEST_MODULE observability-metrics_integration_test)

aux_source_directory(. INTEGRATION_TEST_SRCS)

add_executable(${METRICS_INTEGRATION_TEST_MODULE} ${INTEGRATION_TEST_SRCS})
target_compile_options(${METRICS_INTEGRATION_TEST_MODULE} PRIVATE -Wno-error)

add_dependencies(${METRICS_INTEGRATION_TEST_MODULE} gtest_1_12_1 cjson curl)

target_sources(${METRICS_INTEGRATION_TEST_MODULE} PRIVATE ${INTEGRATION_TEST_SRCS})

target_link_libraries(${METRICS_INTEGRATION_TEST_MODULE} PRIVATE
        pthread
        dl
        ${litebus_ALL_LIB}
        ${gtest_LIB_A}
        ${gmock_LIB_A}
        observability-metrics
        observability-metrics-sdk
        observability-metrics-file-exporter
        observability-prometheus-push-exporter)

add_test(NAME ${METRICS_INTEGRATION_TEST_MODULE} COMMAND ${METRICS_INTEGRATION_TEST_MODULE} --gtest_filter=*.*)

install(
        TARGETS ${METRICS_INTEGRATION_TEST_MODULE}
        DESTINATION ${PROJECT_BINARY_DIR}/bin
)
