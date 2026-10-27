#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OPENOCD_CFG="${PROJECT_DIR}/boards/agmo_ejection/agmo_ejection/support/openocd.cfg"
OPENOCD_BIN="${OPENOCD_BIN:-openocd}"

echo "Provision STM32G473 BOOT0 option bytes"
echo "Project: ${PROJECT_DIR}"
echo "OpenOCD config: ${OPENOCD_CFG}"
echo
echo "Target option:"
echo "  nSWBOOT0 = 0  -> ignore BOOT0/PB8 pin and use option byte"
echo "  nBOOT0   = 1  -> boot from Main Flash"
echo "  expected OPTR after write: 0xfbeff8aa on the current board"
echo

"${OPENOCD_BIN}" \
  -f "${OPENOCD_CFG}" \
  -c "init" \
  -c "stm32l4x option_read 0 0x20" \
  -c "stm32l4x option_write 0 0x20 0x08000000 0x0c000000" \
  -c "stm32l4x option_read 0 0x20" \
  -c "shutdown"

echo
echo "Provision command finished."
echo "Power-cycle or reset the board, then flash firmware with:"
echo "  west flash -d build_ejection_controller"
