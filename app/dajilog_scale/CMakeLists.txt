# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dajilog_scale)

if(NOT "${BOARD}" STREQUAL "dajilog_tech")
  message(FATAL_ERROR "dajilog_scale is pinned to -b dajilog_tech")
endif()

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
  src/main.c
  src/scale_service.c
  src/calibration_fsm.c
  src/calibration_store.c
  src/scale_can_protocol.c
  src/scale_can_service.c
)
