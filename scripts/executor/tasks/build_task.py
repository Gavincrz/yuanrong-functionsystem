#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
import utils
import tasks

log = utils.init_logger()


def run_build(ROOT_DIR, args):
    build_args = {
        "root_dir": ROOT_DIR,
        "job_num": args.jobs if args.jobs else (os.cpu_count() or 1),
        "version": args.version if args.version else "0.0.1"
    }
    if build_args['job_num'] > (os.cpu_count() or 1) * 2:
        log.warning(f"The -j {build_args['job_num']} is over the max logical cpu count({os.cpu_count()}) * 2")
    log.info(f"Start to build function system with args: {build_args}")

    compile_thirdparty(build_args)
    compile_logs(build_args)
    compile_litebus(build_args)
    compile_metrics(build_args)
    compile_functionsystem(build_args)

def compile_thirdparty(args):
    print(args['root_dir'],args['job_num'])

    # 根据下载清单下载第三方依赖
    tasks.download_thirdparty(
        config_path = os.path.join(args['root_dir'], "vendor/ThirdPartyList.csv"),
        download_path = os.path.join(args['root_dir'], "vendor/src")
    )

    utils.sync_command(
        ["cmake", "-B", "build"], 
        cwd=os.path.join(args['root_dir'], "vendor")
    )
    utils.sync_command(
        ["cmake", "--build", "build", "--parallel", str(args['job_num'])],
         cwd=os.path.join(args['root_dir'], "vendor")
    )
    utils.sync_command(
        ["bash", "basic_build.sh"],
         cwd=os.path.join(args['root_dir'], "scripts")
    )

def compile_logs(args):
    log.info("Start to compile common/logs")
    utils.sync_command(
        ["bash", "build.sh", "-j", str(args['job_num'])], 
        cwd=os.path.join(args['root_dir'], "common", "logs")
    )

def compile_litebus(args):
    log.info("Start to compile common/litebus")
    utils.sync_command(
        ["bash", "build.sh", "-t", "off", "-j", str(args['job_num'])], 
        cwd=os.path.join(args['root_dir'], "common", "litebus")
    )

def compile_metrics(args):
    log.info("Start to compile common/metrics")
    utils.sync_command(
        ["bash", "build.sh", "-j", str(args['job_num'])], 
        cwd=os.path.join(args['root_dir'], "common", "metrics")
    )

def compile_functionsystem(args):
    cwd = os.path.join(args['root_dir'], "functionsystem")
    log.info("Start to compile functionsystem")
    utils.sync_command(
        ["bash", "build.sh", "-r", "-j", str(args['job_num']), "-v", args['version']], 
        cwd=cwd
    )
    utils.sync_command(
        ["bash", "build.sh", "-y", "-j", str(args['job_num']), "-v", args['version']], 
        cwd=cwd
    )   

