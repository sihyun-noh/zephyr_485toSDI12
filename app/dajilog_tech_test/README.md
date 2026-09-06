# dajilog_tech_V0.1 Test Firmware

This application is the first bring-up firmware for dajilog_tech_V0.1 using
the dajilog_tech reference board.

## Goals

```text
1. Boot on STM32G431C8
2. Blink both status LEDs
3. Enable USB CDC console
4. Probe/start the FDCAN device
5. Validate J-Link/OpenOCD flash flow
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

## Serial console

After flashing, reconnect USB if needed and check:

```bash
ls /dev/cu.usbmodem*
screen /dev/cu.usbmodemXXXX 115200
```
