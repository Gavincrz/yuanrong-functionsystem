# Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} FILE_EXPORTER_SRCS)

set(TARGET_NAME observability-metrics-file-exporter)

add_library(${TARGET_NAME} SHARED ${FILE_EXPORTER_SRCS})

add_dependencies(${TARGET_NAME} spdlog cjson zlib)

target_include_directories(
  ${TARGET_NAME} PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>")

target_link_libraries(
  ${TARGET_NAME} PUBLIC observability-metrics-headers ${yrlogs_LIB} ${zlib_LIB}
                        ${spdlog_LIB} metrics-common)

install(TARGETS ${TARGET_NAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/exporters/file_exporter/include
  FILES_MATCHING
  PATTERN "*.h")
