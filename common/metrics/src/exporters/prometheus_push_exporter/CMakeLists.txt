# Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} PROMETHEUS_PUSH_EXPORTER)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/../common/ COMMON_SOURCES)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/../http_exporter/ HTTP_EXPORTER)

set(TARGET_NAME observability-prometheus-push-exporter)

add_library(${TARGET_NAME} SHARED ${COMMON_SOURCES} ${HTTP_EXPORTER} ${PROMETHEUS_PUSH_EXPORTER})

add_dependencies(${TARGET_NAME} curl)

target_include_directories(${TARGET_NAME} PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>")

target_link_libraries(${TARGET_NAME} PUBLIC observability-metrics-headers ${curl_LIB} ${securec_LIB} ${litebus_ALL_LIB})

install(
    TARGETS ${TARGET_NAME}
    LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)