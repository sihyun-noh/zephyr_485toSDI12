# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd
  "--config=${BOARD_DIR}/support/openocd.cfg"
  "--cmd-reset-halt=agmo_tic_reset_halt"
  "--verify"
)

set(OPENOCD_FLASH "program")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
