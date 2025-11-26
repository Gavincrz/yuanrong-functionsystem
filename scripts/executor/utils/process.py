# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
import utils
import subprocess

log = utils.stream_logger()


def sync_command(cmd: list[str], cwd: str = None):
    """
    在指定路径同步执行命令，不返回任何信息
    注意：不能打印cmd，因为cmd可能包含敏感信息或用户输入的参数，打印可能导致信息泄露。
    :param cmd: 可执行命令路径
    :param cwd: 命令的参数
    """
    log.info(f"Executing command[{cmd[0]}] in {cwd if cwd else os.getcwd()}")
    subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        check=True
    )


def exec_command(cmd: list[str]):
    """
    启动指定的shell命令，并用该命令替换当前进程，类似于shell中的exec命令。
    :param cmd: 可执行命令路径
    """
    # 使用os.execvp来替换当前进程
    os.execvp(cmd[0], cmd[1:])


def pipe_command(cmd: list[str], cwd: str = None):
    """
    在指定路径同步执行命令，捕获并返回码、标准输出和标准错误。
    注意：不能打印cmd，因为cmd可能包含敏感信息或用户输入的参数，打印可能导致信息泄露。
    :param cmd: 可执行命令路径
    :param cwd: 命令的参数
    """
    log.info(f"Executing command[{cmd[0]}] in {cwd if cwd else os.getcwd()}")
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    return proc.returncode, proc.stdout, proc.stderr
