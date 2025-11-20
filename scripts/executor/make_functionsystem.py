#!/usr/bin/env python3
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

import os
import re
import sys
import tasks
import utils
import argparse
import subprocess

log = utils.init_logger()
# 计算根目录，期望的目录结构为 yuanrong-functionsystem/scripts/executor/make_functionsystem.py
ROOT_DIR = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", ".."))


def parser_args():
    parser = argparse.ArgumentParser(description="Build script for function system")
    subparsers = parser.add_subparsers(dest="subcommand", required=False)

    build_parser = subparsers.add_parser("build", help="Build all components of function system")
    build_parser.add_argument("-j", "--jobs", type=int, help="Set the number of jobs run in parallel for compiling")
    build_parser.add_argument("-v", "--version", type=str, help="Set the version for function system build")
    build_parser.set_defaults(func=lambda args: tasks.run_build(ROOT_DIR, args))

    clean_parser = subparsers.add_parser("clean", help="Clean all build artifacts and caches")
    clean_parser.set_defaults(func=lambda args: tasks.run_clean(ROOT_DIR, args))

    test_parsor = subparsers.add_parser("test", help="Run tests for function system")
    test_parsor.set_defaults(func=lambda args: tasks.run_test(ROOT_DIR, args))

    args = parser.parse_args()
    return parser, args

if __name__ == "__main__":
    _parser, _args = parser_args()
    # 执行对应的函数
    if hasattr(_args, 'func'):
        _args.func(_args)
    else:
        _parser.print_help()
