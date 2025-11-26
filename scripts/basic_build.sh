#!/bin/bash
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

set -e

BASE_DIR=$(
    cd "$(dirname "$0")"
    pwd
)
YR_ROOT_DIR="${BASE_DIR}/../"
YUANRONG_DIR="${YR_ROOT_DIR}/output/function_system"
BUILD_DIR="$YR_ROOT_DIR"/functionsystem/build

[[ ! -f "${YR_ROOT_DIR}/tools/utils.sh" ]] && echo "${YR_ROOT_DIR}/tools/utils.sh is not exist" && exit 1
. ${YR_ROOT_DIR}/tools/utils.sh

function build_etcd()
{
    # Check if etcd and etcdctl already exist
    if [[ -f "${YUANRONG_DIR}/third_party/etcd/etcd" && -f "${YUANRONG_DIR}/third_party/etcd/etcdctl" ]]; then
        log_info "etcd and etcdctl already built, skipping build."
        return
    fi
    
    log_info "---- build etcd ----"
    export GO111MODULE=on
    export GONOSUMDB=*
    
    [ -d "$BUILD_DIR"/etcd ] && rm -rf "$BUILD_DIR"/etcd
    mkdir -p "$BUILD_DIR"
    cp -ar "${YR_ROOT_DIR}"/vendor/src/etcd "$BUILD_DIR"
    cd "$BUILD_DIR"/etcd
    go get github.com/myitcv/gobin
    go install github.com/myitcv/gobin
    export GO_LDFLAGS='-linkmode=external -extldflags "-fstack-protector-strong -Wl,-z,now,-z,relro,-z,noexecstack,-s -Wall -Werror"'
    export GO_BUILD_FLAGS='-buildmode=pie'
    git apply "${BASE_DIR}"/deploy/third_party/etcd.patch
    go mod edit -replace=go.uber.org/zap=go.uber.org/zap@v1.24.0
    go mod tidy
    cd "$BUILD_DIR"/etcd/server
    go mod edit -replace=go.uber.org/zap=go.uber.org/zap@v1.24.0
    go mod tidy
    cd "$BUILD_DIR"/etcd/etcdctl
    go mod edit -replace=go.uber.org/zap=go.uber.org/zap@v1.24.0
    go mod tidy
    cd "$BUILD_DIR"/etcd/etcdutl
    go mod edit -replace=go.uber.org/zap=go.uber.org/zap@v1.24.0
    go mod tidy
    cd "$BUILD_DIR"/etcd/
    bash build.sh

    log_info  "---- build etcd success ----"
}

build_etcd
