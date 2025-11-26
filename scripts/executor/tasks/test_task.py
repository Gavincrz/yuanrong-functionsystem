# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import json
import os
import utils
import tasks
import compile

log = utils.stream_logger()


def run_test(root_dir, cmd_args):
    it_bin_path = os.path.join(root_dir, "functionsystem", "build", "bin", "functionsystem_integration_test")
    ut_bin_path = os.path.join(root_dir, "functionsystem", "build", "bin", "functionsystem_unit_test")

    args = {
        "root_dir": root_dir,
        "action": cmd_args.action,
        "job_num": cmd_args.job_num,
        "test_suite": cmd_args.test_suite,
        "test_case": cmd_args.test_case,
        "it_bin": it_bin_path,
        "ut_bin": ut_bin_path,
        "exec_timeout": cmd_args.exec_timeout,
        "retry_times": cmd_args.retry_times,
        "print_logs": cmd_args.print_logs,
    }
    if args['job_num'] > (os.cpu_count() or 1) * 2:
        log.warning(f"The -j {args['job_num']} is over the max logical cpu count({os.cpu_count()}) * 2")
    log.info(f"Start to run function-system test case with args: {json.dumps(args)}")

    if args['action'] in ["all", "make"]:
        # 编译测试用例
        log.info(f"Step(1/3, action = [make]): Make test case with {args['job_num']} cores")
        compile.compile_gtest(root_dir, args['job_num'])
    else:
        log.info("Step(1/3, action = [make]): Skip to make test case")

    if args['action'] in ["all", "exec"]:
        # 执行测试用例
        log.info(f"Step(2/3, action = [exec]): Exec test case with args: {json.dumps(args)}")
        exit_code = tasks.run_code_gate(args["it_bin"], args["ut_bin"], args["test_suite"], args["test_case"],
                            exec_timeout=args['exec_timeout'], retry_times=args['retry_times'],
                            job_num=args['job_num'], print_logs=args['print_logs'])
        if exit_code != 0:
            log.error(f"Run the test case and exit the code with a non-zero value of {exit_code}. Program termination")
            exit(exit_code)
    else:
        log.info(f"Step(2/3, action = [exec]): Skip to exec test case")

    if args['action'] in ["all", "gcov"]:
        # 计算用例覆盖率
        pass

