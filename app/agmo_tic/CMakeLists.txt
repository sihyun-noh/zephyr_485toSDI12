# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(AGMO_TIC)

target_sources(app PRIVATE
    src/main.c
    src/tic_output.c
    src/tic_input.c
    src/tic_fsm.c
    src/tic_can.c
    src/tic_shell.c
)
