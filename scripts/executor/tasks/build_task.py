# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os
import json
import utils
import tasks
import compile

log = utils.stream_logger()


def run_build(root_dir, cmd_args):
    args = {
        "root_dir": root_dir,
        "job_num": cmd_args.job_num,
        "version": cmd_args.version
    }
    if args['job_num'] > (os.cpu_count() or 1) * 2:
        log.warning(f"The -j {args['job_num']} is over the max logical cpu count({os.cpu_count()}) * 2")
    log.info(f"Start to build function-system with args: {json.dumps(args)}")

    compile_vendor(args)
    compile_logs(args)
    compile_litebus(args)
    compile_metrics(args)
    compile_functionsystem(root_dir, args)


def compile_vendor(args):
    print(args['root_dir'], args['job_num'])

    # 根据下载清单下载第三方依赖
    tasks.download_vendor(
        config_path=os.path.join(args['root_dir'], "vendor", "VendorList.csv"),
        download_path=os.path.join(args['root_dir'], "vendor", "src")
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


def compile_functionsystem(root_dir, args):
    cwd = os.path.join(args['root_dir'], "functionsystem")
    log.info("Start to compile functionsystem")
    compile.compile_binary(root_dir, args["job_num"], args["version"])
    utils.sync_command(
        ["bash", "build.sh", "-y", "-j", str(args['job_num']), "-v", args['version']],
        cwd=cwd
    )
