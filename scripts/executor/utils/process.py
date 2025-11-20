#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
import subprocess

def sync_command(cmd: tuple[str], cwd:str = None) -> tuple:
    """
    执行指定路径的命令，并返回命令的返回码、标准输出和标准错误。
    注意：不能打印cmd，因为cmd可能包含敏感信息或用户输入的参数，打印可能导致信息泄露。

    :param process: 可执行命令路径
    :type process: str
    :param args: 命令的参数
    :type args: tuple
    :return: 返回码、标准输出和标准错误
    :rtype: tuple
    """
    print(f"Executing command[{cmd[0]}] in {cwd if cwd else os.getcwd()}")
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
    )


def exec_command(cmd: tuple[str]):
    """
    启动指定的shell命令，并用该命令替换当前进程，类似于shell中的exec命令。

    :param process: 可执行命令路径
    :type process: str
    :param args: 命令的参数
    :type args: tuple
    """
    # 使用os.execvp来替换当前进程
    os.execvp(cmd[0], cmd[1:])

