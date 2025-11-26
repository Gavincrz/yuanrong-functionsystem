# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import json
import shutil
import os.path

import utils

log = utils.stream_logger()


def compile_gtest(root_dir, job_num):
    compile_functionsystem(root_dir, job_num, build_type="Debug", gtest=True)


def compile_binary(root_dir, job_num, version):
    compile_functionsystem(root_dir, job_num, build_type="Release", version=version)


def compile_functionsystem(root_dir, job_num, version="0.0.0", build_type="Debug",
                           time_trace=False, coverage=False, jemalloc=False, sanitizers=False, gtest=False):
    print("Build cpp code in functionsystem")

    # 拷贝 proto 文件
    log.info("Auto copy all proto file to cpp common folder")
    inner_proto = os.path.join(root_dir, "proto", "inner")
    posix_proto = os.path.join(root_dir, "proto", "posix")
    cpp_proto_dir = os.path.join(root_dir, "functionsystem", "src", "common", "proto", "posix")
    os.makedirs(cpp_proto_dir, exist_ok=True)
    copy_proto_folder(inner_proto, cpp_proto_dir)
    copy_proto_folder(posix_proto, cpp_proto_dir)

    # 使用 CMake 创建 Ninja 构建清单
    root_dir = os.path.abspath(root_dir)  # Git根目录
    code_path = os.path.join(root_dir, "functionsystem")
    output_dir = os.path.join(code_path, "output")
    build_dir = os.path.join(code_path, "build")
    cmake_args = {
        "BUILD_VERSION": version_name(version),
        "CMAKE_INSTALL_PREFIX": output_dir,
        "CMAKE_BUILD_TYPE": build_type,
        "SANITIZERS": bool2switch(sanitizers),
        "BUILD_LLT": bool2switch(gtest),
        "BUILD_GCOV": bool2switch(coverage),
        "BUILD_THREAD_NUM": job_num,
        "ROOT_DIR": root_dir,  # 为了数据系统路径
        "JEMALLOC_PROF_ENABLE": bool2switch(jemalloc),
        "FUNCTION_SYSTEM_BUILD_TIME_TRACE": bool2switch(time_trace),
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
    }
    cmake_generate(code_path, build_dir, cmake_args)

    # 使用 Ninja 编译程序
    ninja_make(build_dir, str(job_num))

    # 使用 CMake 完成产物复制
    cmake_install(build_dir)


def cmake_generate(source_dir, build_dir, cmake_args: dict[str, str]):
    log.info(f"CMAKE generate Ninja make list with args: {json.dumps(cmake_args)}")
    log.info(f"Run cmake with source code[{source_dir}] to build[{build_dir}]")
    args = []
    for key, val in cmake_args.items():
        k = "-D" + key.upper()
        v = val if val is not None else ""
        args.append(f"{k}={v}")
    utils.sync_command(["cmake", "-G", "Ninja", "-S", source_dir, "-B", build_dir, *args])


def ninja_make(build_dir: str, job_num: str):
    log.info(f"Run Ninja build in dir[{build_dir}] using {job_num} cores.")
    utils.sync_command(["ninja", "-C", build_dir, "-j", job_num])


def cmake_install(build_dir: str):
    log.info(f"Run cmake install in dir[{build_dir}]")
    utils.sync_command(["cmake", "--build", build_dir, "--target", "install"])


def version_name(version):
    return f"yr-functionsystem-v{version}"


def bool2switch(b: bool):
    return "ON" if b else "OFF"

def copy_proto_folder(src, dst):
    for proto_file in os.listdir(src):
        if proto_file.endswith(".proto"):
            shutil.copy(os.path.join(src, proto_file), dst)