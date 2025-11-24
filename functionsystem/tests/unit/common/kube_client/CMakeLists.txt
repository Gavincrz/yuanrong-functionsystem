# Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.

aux_source_directory(../../common/kube_client KUBE_CLIENT_TEST_SRCS)

target_sources(${UNIT_TEST_MODULE} PRIVATE ${KUBE_CLIENT_TEST_SRCS})