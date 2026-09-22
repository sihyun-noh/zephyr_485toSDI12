# Dajilog Zephyr Device Firmware

Zephyr RTOS 기반 Dajilog MCU 장비의 firmware, custom board 정의, 공통 component,
KiCad hardware source를 함께 관리하는 repository입니다.

현재 STM32G431 기반 `dajilog_tech`, `agmo_tic` 보드와 RS-485, SDI-12, CAN,
USB CDC, I2C/OLED, ADS1232, SBUS, digital I/O bring-up 결과를 포함합니다.

## Repository map

```text
app/                         Product/application firmware
  dajilog_scale/             Load-cell scale, ADS1232, OLED, CAN calibration
  dajilog_485_to_sdi12/      RS-485 Modbus sensor to SDI-12 slave bridge
  agmo_tic/                  AGMO tractor ignition-control FSM
  dajilog_tech_test/         dajilog_tech hardware bring-up test
boards/                      Dajilog custom Zephyr board definitions
components/                  Reusable peripheral, protocol, and service modules
hardware/kicad/              KiCad schematic, PCB, production source
scripts/                     OpenOCD option-byte and flash helper scripts
tools/dajilog_provisioner/   macOS USB CDC provisioning GUI
```

`build*` directories are local Zephyr/CMake output and intentionally ignored by Git.

## Quick start

```bash
cd /path/to/example-application
source ~/zephyrproject/.venv/bin/activate

# dajilog_scale
west build -p always -b dajilog_tech app/dajilog_scale -d build_dajilog_scale \
  -- -DEXTRA_CONF_FILE=profiles/scale_default.conf
west flash -d build_dajilog_scale

# RS-485 to SDI-12 bridge, default sensor profile
west build -p always -b dajilog_tech app/dajilog_485_to_sdi12 \
  -d build_dajilog_485_to_sdi12_default \
  -- -DEXTRA_CONF_FILE=profiles/sensor_default.conf
west flash -d build_dajilog_485_to_sdi12_default

# AGMO TIC
west build -p always -b agmo_tic app/agmo_tic -d build_agmo_tic
west flash -d build_agmo_tic

# dajilog_tech bring-up test
west build -p always -b dajilog_tech app/dajilog_tech_test -d build_dajilog_tech_test
west flash -d build_dajilog_tech_test
```

The application CMake files pin board-specific apps to their safe target board where
needed. Do not reuse a build directory for a different board or app.

### Profile build helpers

The same build commands are available as scripts. Source the Zephyr virtual environment first.

```bash
./scripts/build_dajilog_scale.sh
./scripts/build_dajilog_scale.sh flash

./scripts/build_dajilog_485_to_sdi12.sh
./scripts/build_dajilog_485_to_sdi12.sh flash
```

Select a product variant without changing source code by overriding `PROFILE` and,
when needed, use a separate `BUILD_DIR`.

```bash
PROFILE=profiles/scale_default.conf BUILD_DIR=build_scale_v01 \
  ./scripts/build_dajilog_scale.sh
```

## 개발 기준

```text
RTOS: Zephyr
Zephyr version: v3.7.0 LTS
Workspace topology: T2, application manifest repository
Application path: example-application
```

## Workspace

현재 workspace 구조:

```text
my-workspace/
├── .west/
├── example-application/   # this project
├── zephyr/                # Zephyr v3.7.0
└── modules/
```

현재 application 경로:

```text
/Users/sihyun.noh/project/zephyr_workspace/my-workspace/example-application
```

## Python / west 환경

Zephyr 개발용 Python 가상환경:

```bash
source ~/zephyrproject/.venv/bin/activate
```

확인:

```bash
which west
west --version
west topdir
west list zephyr
```

현재 확인된 환경:

```text
west: /Users/sihyun.noh/zephyrproject/.venv/bin/west
west version: v1.4.0
zephyr revision: v3.7.0
```

## Hardware

KiCad 회로도는 firmware repository 안에서 함께 관리합니다.

```text
hardware/kicad/485toSDI12/
├── 485toSDI12.kicad_pro
├── 485toSDI12.kicad_sch
└── 485toSDI12.kicad_pcb
```

회로도와 Zephyr 설정은 함께 관리합니다.

```text
KiCad MCU pin
  -> Zephyr devicetree overlay
  -> driver / peripheral config
  -> board bring-up
```

## Reference board: dajilog_tech

초기 Zephyr bring-up은 dajilog_tech 회로도를 참조해서 진행합니다.

```text
Board definition:
boards/dajilog_tech/dajilog_tech/

Test application:
app/dajilog_tech_test/
```

현재 매핑:

```text
MCU: STM32G431C8
LED0: PA15, active low
LED1: PA0, active low
USB: PA11 DM, PA12 DP
CAN: PB8 RX, PB9 TX
Debug: SWD through J-Link/OpenOCD
```

빌드:

```bash
source ~/zephyrproject/.venv/bin/activate
west build -b dajilog_tech app/dajilog_tech_test -p always
```

새 보드 최초 1회 BOOT0 option byte 설정:

```bash
./scripts/provision_stm32g431_boot0.sh
```

이 보드는 `PB8`을 `CANFD_RX`로 사용하지만, STM32G431에서 `PB8`은 `BOOT0` 기능도 가진다.
새 MCU의 option byte가 BOOT0 pin을 따르는 상태이면 reset 순간 PB8 상태에 따라 system bootloader로 들어갈 수 있다.
따라서 새 보드는 firmware flash 전에 최초 1회 아래 설정을 적용한다.

```text
nSWBOOT0 = 0  -> BOOT0 pin 무시, option byte 사용
nBOOT0   = 1  -> Main Flash boot
OPTR     = 0xfbeff8aa
```

주의:

```text
이 설정은 일반 application firmware가 아니라 MCU 내부 option byte 설정이다.
전원 off 후에도 유지되므로 매번 west flash 전에 실행할 필요는 없다.
새 보드 / 새 MCU / option byte 초기화 후에만 다시 실행한다.
```

Flash:

```bash
west flash
```

Flash 후 사용률까지 확인:

```bash
./scripts/flash_with_usage.sh
```

출력 예:

```text
** Programming Finished **
verified 53936 bytes in 0.411011s (128.152 KiB/s)
FLASH usage: 53936 B / 131072 B (41.15%)
```

현재 확인된 빌드 결과:

```text
Zephyr: v3.7.0
SDK: 0.16.8
FLASH: 53356 B / 128 KB
RAM: 14464 B / 32 KB
```

## Debug / Flash

현재 J-Link와 OpenOCD를 사용합니다.

OpenOCD 설치 확인:

```bash
which openocd
openocd --version
```

J-Link + STM32G4 연결 확인 예:

```bash
openocd \
  -f interface/jlink.cfg \
  -c "adapter serial 000788529183" \
  -c "transport select swd" \
  -f target/stm32g4x.cfg \
  -c "init" \
  -c "targets" \
  -c "shutdown"
```

칩 정보 확인 예:

```bash
openocd \
  -f interface/jlink.cfg \
  -c "adapter serial 000788529183" \
  -c "transport select swd" \
  -f target/stm32g4x.cfg \
  -c "init" \
  -c "reset halt" \
  -c "flash probe 0" \
  -c "shutdown"
```

현재 `west flash`는 보드 전용 OpenOCD 설정을 사용한다.

```text
boards/dajilog_tech/dajilog_tech/support/openocd.cfg
```

현재 reset 방식:

```text
cortex_m reset_config sysresetreq
```

이 방식은 현재 보드에서 `program` / `verify` / reset 후 실행이 안정적으로 동작했다.

## USB CDC

USB CDC는 MCU를 Mac에서 가상 시리얼 포트처럼 보이게 하는 기능입니다.

Mac에서 확인:

```bash
ls /dev/cu.*
```

접속 예:

```bash
screen /dev/cu.usbmodemXXXX 115200
```

### Device provisioning

`dajilog_scale` provides a common USB CDC shell for installing a CAN node ID in
the EEPROM Device Profile. The Zephyr built-in `device` shell is disabled in this
app so the following Dajilog commands own the `device` namespace.

```text
device info
device profile get
device node set <1..127>
device reboot
```

After `device node set 13` and reboot, Device Info uses request ID `0x60D` and
response ID `0x58D`. The STM32 96-bit chip UID is read directly from hardware and
is shown by `device info`; it is not stored in EEPROM.

For a direct USB provisioning GUI on macOS:

```bash
cd tools/dajilog_provisioner
./install.sh
./run.sh
```

## Common CAN Device Info

All Dajilog applications can use the common node-addressed Device Info protocol.

```text
request  = 0x600 + node_id
response = 0x580 + node_id
```

Supported queries include basic identity, firmware, product serial, runtime health,
and the three parts of the STM32 96-bit UID. The detailed frame definition lives in
`components/dajilog_device_info/` and the project CAN design document.

## 다음 작업

```text
1. dajilog_tech_V0.1 회로도 기준 MCU pin 정리
2. Zephyr board 또는 devicetree overlay 작성
3. 기본 blinky / hello_world 빌드
4. OpenOCD + J-Link flash 확인
5. USB CDC console 확인
6. RS-485 / SDI-12 driver bring-up
```
