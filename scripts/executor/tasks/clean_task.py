#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
import utils
import tasks

log = utils.init_logger()


def run_clean(ROOT_DIR, args):
    log.info("Start to clean vendor cache and component build cache")
    utils.sync_command(["git", "clean", "-dffx"], cwd=ROOT_DIR)
    utils.sync_command(["git", "submodule", "foreach", "--recursive", "git clean -dffx"], cwd=ROOT_DIR)

