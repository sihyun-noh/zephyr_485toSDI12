# 12CH Ejection CAN Simulator

Waveshare USB-CAN-A를 사용하여 12채널 Ejection Controller를 시험하는 macOS용 PySide6 GUI입니다.

## Features

- Standard CAN 11-bit, 125/250/500/1000 kbit/s selectable before connection
- `0x101` 업체 호환 12채널 절대 출력 명령
- 선택 채널 ON/OFF, 100 ms pulse 및 ALL OFF
- 선택 기능인 `0x102` heartbeat
- `0x201` FSM, fault, 14입력, 12출력과 health 표시
- 전체 CAN 송수신 로그

Heartbeat는 Vision PC 업체와 협의되지 않았으므로 GUI와 펌웨어에서 모두 기본 비활성입니다.

## Setup

```sh
cd /Users/sihyun.noh/project/zephyr_workspace/my-workspace/example-application/tools/ejection_can_simulator
./init
./run
```

`./init`은 `install.sh`을 호출해 Python 3.12 `.venv`를 생성하고 PySide6/pyserial 설치 및 pytest를 실행합니다. 이후에는 `./run`만 실행하면 됩니다. `./install.sh`, `./run.sh`도 직접 사용할 수 있습니다.

장치 포트는 일반적으로 `/dev/cu.usbserial-*` 형태입니다. 다른 CAN 프로그램이 같은 USB-CAN 포트를 열고 있으면 연결할 수 없습니다.

## Firmware operation

정상 운전은 heartbeat 없이 동작합니다.

```text
controller ready
controller start
```

Enable heartbeat gating only when deliberately testing it:

```text
canproto heartbeat on
```

Disable it again:

```text
canproto heartbeat off
```
