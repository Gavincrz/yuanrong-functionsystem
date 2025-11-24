#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
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
set -o nounset
set -o pipefail

readonly USAGE="
Usage: bash build.sh [-h] [-C] [-v <version>] [-j <job_num>] [-o <install dir>]
                     [-u off|build|run] [-M <test suit name>] [-t <test case name>] [-c off/on/html]

Options:
    -v YuanRong version
    -o set the output path of the compilation result, default is './output'.
    -j set the number of jobs run in parallel for compiling source code and compiling open source software,
       default is the number of processor.
    -b enable print build object time trace
    -C clear make history before building
    -h show usage
    -T set path of third party, default is ./third_party
    -V enable print ninja build verbose message

    For LLT:
    -c Build coverage, choose from: off/on/html, default: off.
    -M Specifies the test suit name to run, all suits by default.
    -t Specifies the testcase name to run, all testcases by default.
    -u Compiling or running unit testcases, default off. Choose from: off/build/run.
       Field 'off' indicates that testcases are not compiled and executed.
       Field 'build' indicates that testcases are compiled but not run.
       Field 'run' indicates that testcases are compiled and run.


Environment:
1) YR_OPENSOURCE_DIR: Specifies a directory to cache the opensource compilation result.
    Cache the compilation result to speed up the compilation. Default: readlink -f BASE_DIR

Example:
1) Compile version and export compilation result to the output directory.
  $ bash build.sh -o ./output
"
BASE_DIR=$(
    cd "$(dirname "$0")"
    pwd
)

# test options
BUILD_LLT=OFF
RUN_LLT=OFF
BUILD_GCOV=OFF
GEN_LLT_REPORT=OFF
IT_EXECUTABLE="observability-metrics_integration_test"
UT_EXECUTABLE="observability-metrics_unit_test"
TEST_SUIT="*"
TEST_CASE="*"
# compile options
YR_VERSION="1.0.0.dev"
CLEAR_OUTPUT=OFF
MODULE="observability-metrics"
PROJECT_DIR="${BASE_DIR}"
BUILD_DIR="${BASE_DIR}/build"
OUTPUT_DIR="${BASE_DIR}/output"
YR_ROOT_DIR="${BASE_DIR}/../.."
PACKAGE_OUTPUT_DIR="${YR_ROOT_DIR}/output/function_system/metrics"
CPU_NUM="$(grep -c 'processor' /proc/cpuinfo)"
JOB_NUM="$(($(grep -c 'processor' /proc/cpuinfo) + 1))"
YR_OPENSOURCE_DIR=""
LD_LIBRARY_PATH=""
DOWNLOAD_OPENSRC="OFF"
BUILD_TYPE=Release
VERBOSE=""

BUILD_CONFIG_DIR="$(readlink -f "${YR_ROOT_DIR}/thirdparty/")"
THIRDPARTY_SRC_DIR="$(readlink -f "${YR_ROOT_DIR}/vendor/")"
THIRDPARTY_INSTALL_DIR="${THIRDPARTY_SRC_DIR}/out"

. "${BASE_DIR}"/scripts/utils.sh
usage_cpp() {
    echo -e "$USAGE"
}

function check_number() {
    number_check='^([0-9]+)$'
    if [[ "$1" =~ ${number_check} ]]; then
        return 0
    fi
    log_error "Invalid value $1 for option -j"
    log_warning "${USAGE}"
    return 1
}

download_opensource()
{
    if [ "${DOWNLOAD_OPENSRC^^}" != "ON" ]; then
        echo "don't need download opensource"
        return 0
    fi

    echo "build config dir: ${BUILD_CONFIG_DIR}"
    echo "thirdparty src dir: ${THIRDPARTY_SRC_DIR}"
    echo "thirdparty install dir: ${THIRDPARTY_INSTALL_DIR}"

    if [ ! -d "${BUILD_CONFIG_DIR}" ]; then
        echo "please download build config"
        exit 1
    fi

    ARGS="-T ${THIRDPARTY_SRC_DIR} -Y ${YR_ROOT_DIR} -M metrics"
    if [ "X${BUILD_LLT}" = "XOFF" ]; then
        ARGS="${ARGS} -r"
    fi

    if ! bash "${BUILD_CONFIG_DIR}/download_src.sh" "${ARGS}"; then
        echo "download dependency source of src fail"
        exit 1
    fi
}

function strip_symbols() {
    local src_dir="$1"
    local dest_dir="$2"
    if [[ ! -d "${dest_dir}" ]]; then
        mkdir -p "${dest_dir}"
    fi

    for file in ${src_dir}/*; do
        local type
        type="$(file -b --mime-type ${file} | sed 's|/.*||')"
        if [[ ! -L "${file}" ]] && [[ ! -d "${file}" ]] && [[ "x${type}" != "xtext" ]]; then
            strip_file_symbols ${file} ${dest_dir}
        fi
    done
}

function strip_file_symbols() {
    local file="$1"
    local dest_dir="$2"
    echo "---- start to strip ${file}"
    local basename
    basename=$(basename "${file}")
    objcopy --only-keep-debug "${file}" "${dest_dir}/${basename}.sym"
    objcopy --add-gnu-debuglink="${dest_dir}/${basename}.sym" "${file}"
    objcopy --strip-all "${file}"
}

while getopts 'Vx:hv:o:j:Cc:u:t:M:T:' opt; do
    logical_cpu_count=$(cat /proc/cpuinfo | grep "processor" | wc -l)
    case "$opt" in
    v)
        YR_VERSION="${OPTARG}"
        ;;
    o)
        OUTPUT_DIR=$(realpath -m "${OPTARG}")
        ;;
    j)
        if ! check_number "${OPTARG}"; then
            exit 1
        fi
        if [ ${OPTARG} -gt $(($CPU_NUM * 2)) ]; then
            log_warning "The -j $OPTARG is over the max logical cpu count($CPU_NUM) * 2"
        fi
        JOB_NUM="${OPTARG}"
        ;;
    h)
        usage_cpp
        exit 0
        ;;
    C)
        CLEAR_OUTPUT=ON
        ;;
    c)
        if [ "X${OPTARG}" = "Xoff" ]; then
            log_info "Coverage reports is disabled"
        elif [ "X${OPTARG}" = "Xon" ]; then
            BUILD_GCOV=ON
        elif [ "X${OPTARG}" = "Xhtml" ]; then
            BUILD_GCOV=ON
            GEN_LLT_REPORT=ON
        else
            log_error "Invalid value ${OPTARG} for option -c, choose from off/on/html"
            log_info "${USAGE}"
            exit 1
        fi
        ;;
    d)
        BUILD_TYPE=Debug
        ;;
    M)
        TEST_SUIT=${OPTARG}
        ;;
    t)
        TEST_CASE=${OPTARG}
        ;;
    u)
        if [ "X${OPTARG}" = "Xoff" ]; then
            log_info "LLT is disabled"
        elif [ "X${OPTARG}" = "Xbuild" ]; then
            BUILD_LLT=ON
        elif [ "X${OPTARG}" = "Xrun" ]; then
            BUILD_LLT=ON
            RUN_LLT=ON
        else
            log_error "Invalid value ${OPTARG} for option -u, choose from off/build/run"
            log_info "${USAGE}"
            exit 1
        fi
        ;;
    x)
        if [ "${OPTARG^^}" = "ON" ]; then
            DOWNLOAD_OPENSRC="$OPTARG"
        fi
        ;;
    T)
        THIRDPARTY_SRC_DIR=$(readlink -f "${OPTARG}")
        THIRDPARTY_INSTALL_DIR="${THIRDPARTY_SRC_DIR}/out"
        echo "download opensource to ${THIRDPARTY_SRC_DIR}"
        ;;
    V)
        VERBOSE="-v"
        ;;
    *)
        log_error "Invalid command"
        usage_cpp
        exit 1
        ;;
    esac
done

log_info "Begin to build, Enable-LLT:${BUILD_LLT} "

[ -z "$YR_OPENSOURCE_DIR" ] && export YR_OPENSOURCE_DIR="${YR_ROOT_DIR}"/.3rd

if [ "X${CLEAR_OUTPUT}" = "XON" ]; then
    [ -d "${BUILD_DIR}" ] && rm -rf "${BUILD_DIR}"
    [ -d "${OUTPUT_DIR}" ] && rm -rf "${OUTPUT_DIR}"
fi

download_opensource

if command -v ccache &> /dev/null
then
    ccache -p
    ccache -z
fi

# Build and install
mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"/
cmake -G Ninja "${PROJECT_DIR}" -DCMAKE_INSTALL_PREFIX="${OUTPUT_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBUILD_LLT="${BUILD_LLT}" \
    -DBUILD_GCOV="${BUILD_GCOV}" \
    -DBUILD_CONFIG_DIR="${BUILD_CONFIG_DIR}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON # generate compile_commands.json file

# Compatible with EulerOS and Ubuntu
if [ ! -d "${OUTPUT_DIR}"/lib64 ]; then
    mkdir -p "${OUTPUT_DIR}"/lib64
fi

if [ ! -d "${OUTPUT_DIR}"/lib ]; then
    mkdir -p "${OUTPUT_DIR}"/lib
fi

ninja ${MODULE} ${VERBOSE} -j "${JOB_NUM}" || die "Failed to compile ${MODULE}"
ninja ${MODULE} install || die "Failed to install ${MODULE}"

if command -v ccache &> /dev/null
then
    ccache -s
fi

# If LLT is enabled to build, need to set LD_LIBRARY_PATH to link the generated shared library.
if [ "X${BUILD_LLT}" = "XON" ]; then
    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:${OUTPUT_DIR}/lib:${OUTPUT_DIR}/lib64"
    export BIN_PATH="${OUTPUT_DIR}/bin" # Integration test need this path to find test executables.
fi

if [ "$BUILD_TYPE" != "Debug" ]; then
    [ ! -d "${OUTPUT_DIR}/observability_metrics_SYM" ] && mkdir -p "${OUTPUT_DIR}/observability_metrics_SYM"
    rm -rf "${OUTPUT_DIR}/observability_metrics_SYM/*"
    strip_symbols "$OUTPUT_DIR"/lib "${OUTPUT_DIR}/observability_metrics_SYM"
fi

# Tests
if [ "X${RUN_LLT}" = "XON" ]; then
    pushd "bin"
    log_info "Running unit tests"
    "$(pwd)"/${UT_EXECUTABLE} --gtest_filter="${TEST_SUIT}.${TEST_CASE}"

    log_info "Running integration tests"
    "$(pwd)"/${IT_EXECUTABLE} --gtest_filter="${TEST_SUIT}.${TEST_CASE}"
    popd
    pushd "test"
    "$(pwd)"/metrics_test --gtest_filter="${TEST_SUIT}.${TEST_CASE}"
    popd
fi

if [ "X${GEN_LLT_REPORT}" = "XON" ]; then
    log_info "Generate test report"
    lcov --capture --directory "${PROJECT_DIR}" --output-file  coverage.info_tmp
    lcov --extract coverage.info_tmp '*src/*' --output-file coverage.info
    genhtml coverage.info --output-directory coverage_report --ignore-errors source
fi

# copy to output
mkdir -p "${PACKAGE_OUTPUT_DIR}"/lib "${PACKAGE_OUTPUT_DIR}"/include

if [ $(ls -A "${OUTPUT_DIR}"/lib | wc -w) -ne 0 ]; then
    cp -ar "${OUTPUT_DIR}"/lib/* "${PACKAGE_OUTPUT_DIR}"/lib
fi

if [ -d "${OUTPUT_DIR}"/include ]; then
    cp -ar "${OUTPUT_DIR}"/include/* "${PACKAGE_OUTPUT_DIR}"/include
fi

log_info "Build observability-metrics successfully"
