# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dajilog_485_to_sdi12)

if(NOT "${BOARD}" STREQUAL "dajilog_tech")
  message(FATAL_ERROR
    "dajilog_485_to_sdi12 is pinned to -b dajilog_tech. "
    "This prevents SDI-12 pins PB10/PB11/PB12 from driving relay pins on other boards.")
endif()

target_include_directories(app PRIVATE
  include
)

target_sources(app PRIVATE
  src/main.c
  src/stm32g4_ucpd_fix.c
  src/app_msg.c
  src/app_context.c
  src/app_fsm.c
  src/bridge_state.c
  src/rs485_task.c
  src/sdi12_task.c
  src/sdi12_response.c
  src/monitor_task.c
  src/bridge_can_service.c
  src/bridge_shell.c
)
