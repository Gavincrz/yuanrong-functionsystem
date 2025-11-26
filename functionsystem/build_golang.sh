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

BASE_DIR=$(cd "$(dirname "$0")"; pwd)
PROJECT_DIR="${BASE_DIR}/pkg/cli"
OUTPUT_DIR="${BASE_DIR}/../output/function_system/cli"
TARGET_OS=$1
CLI_NAME=${CLI_NAME:-"yr"}
PLATFORM_NAME=${PLATFORM_NAME:-"OpenYuanrong"}

BUILD_DATE=$(date -u "+%Y%m%d%H%M%S")
VERSION=$YR_VERSION
if [ -z "$VERSION" ]; then
    VERSION="DEV"
fi

FLAGS='-X functionsystem/pkg/cli/build.Version='${VERSION}' -X functionsystem/pkg/cli/build.Date='${BUILD_DATE}' -X functionsystem/pkg/cli/constant.CliName='${CLI_NAME}' -X functionsystem/pkg/cli/constant.PlatformName='${PLATFORM_NAME}' -X functionsystem/pkg/cli/build.CustomizeVer='${CLI_NAME}' -linkmode=external -extldflags "-fPIC -fstack-protector-strong -Wl,-z,now,-z,relro,-z,noexecstack,-s -Wall -Werror"'

MODE='-buildmode=pie'

function build_linux() {
    CC='gcc -fstack-protector-strong -D_FORTIFY_SOURCE=2 -O2' go build -buildmode=pie -ldflags "${FLAGS}"  -o \
    build/_output/bin/${CLI_NAME} ../../cmd/cli/main.go
    ls -l build/_output/bin/${CLI_NAME}
}

function build_windows() {
    if ! CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -ldflags "${FLAGS}" \
    -o build/_output/bin/"${CLI_NAME}".exe ../../cmd/cli/main.go; then
        log_error "Failed to build "${CLI_NAME}".exe!!!"
        return 1
    fi
    return 0
}

cd "${PROJECT_DIR}" || die "${PROJECT_DIR} not exist"

# clean
rm -rf build/_output
rm -fr build/cli/usr/local/cli/bin/"${CLI_NAME}"

cd "${PROJECT_DIR}"
mkdir -p build/_output/bin
mkdir -p build/_output/pkg
mkdir -p build/cli/usr/local/cli/bin
if [ "${TARGET_OS}" = "linux" ]; then
    build_linux
elif [ "${TARGET_OS}" = "windows" ]; then
    build_windows
else
    build_linux
    build_windows
fi
rm -fr build/cli/usr/local/cli/bin/"${CLI_NAME}"
cp build/_output/bin/"${CLI_NAME}" build/cli/usr/local/cli/bin/
tar -cf build/_output/pkg/cli.tar -C ../../cmd/cli install.sh -C "${PROJECT_DIR}"/build/cli/usr/local cli

mkdir -p "${OUTPUT_DIR}"
cp -ar build/_output/* "${OUTPUT_DIR}"/

exit 0
