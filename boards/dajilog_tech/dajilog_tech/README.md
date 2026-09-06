# dajilog_tech

Out-of-tree Zephyr 3.7 LTS board definition for dajilog_tech.

This board is used as a reference board for early firmware bring-up before the
custom 485toSDI12 hardware is finalized.

## Current mapping

```text
MCU: STM32G431C8
LED0: PA15, active low
LED1: PA0, active low
USB: PA11 DM, PA12 DP
CAN: PB8 RX, PB9 TX
Debug: SWD through J-Link/OpenOCD
```

## Build

```bash
source ~/zephyrproject/.venv/bin/activate
west build -b dajilog_tech app/dajilog_tech_test
```

## Flash

```bash
west flash
```
