# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ejection_controller)

target_sources(app PRIVATE
    src/main.c
    src/ejection_io.c
    src/ejection_shell.c
    src/phase3_devices.c
    src/phase3_shell.c
    src/ejection_settings.c
    src/settings_shell.c
    src/controller_state.c
    src/controller_shell.c
    src/can_protocol.c
    src/can_protocol_shell.c
)
