# 485toSDI12

Zephyr RTOS 기반의 485 to SDI-12 디바이스 펌웨어 프로젝트입니다.

이 프로젝트는 RS-485, SDI-12, USB CDC, UART, SWD/J-Link 디버깅을 포함하는 MCU 기반 edge I/O 장치를 개발하기 위한 코드베이스입니다.

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
  -c "adapter serial 000069658999" \
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
  -c "adapter serial 000069658999" \
  -c "transport select swd" \
  -f target/stm32g4x.cfg \
  -c "init" \
  -c "reset halt" \
  -c "flash probe 0" \
  -c "shutdown"
```

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

## 다음 작업

```text
1. 485toSDI12 회로도 기준 MCU pin 정리
2. Zephyr board 또는 devicetree overlay 작성
3. 기본 blinky / hello_world 빌드
4. OpenOCD + J-Link flash 확인
5. USB CDC console 확인
6. RS-485 / SDI-12 driver bring-up
```
