# 12CH Ejection CAN Simulator

Waveshare USB-CAN-A를 사용하여 12채널 Ejection Controller를 시험하는 macOS용 PySide6 GUI입니다.

## Features

- Standard CAN 11-bit, 125/250/500/1000 kbit/s selectable before connection
- `0x101` 기존 payload를 사용하는 12채널 예약 등록 (Phase 4 의미)
- 선택 채널 1회/반복 예약, CH1→CH12 순차 예약, 전체 취소
- 쉼표 또는 공백으로 입력하는 가변 전송 간격 시퀀스와 반복 적용
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

정상 운전은 heartbeat 없이 동작합니다. IDLE에서 보드의 공통 시간을 설정합니다.

```text
ejection config set move_ms 500
ejection config set hold_ms 100
ejection config save
ejection ready
ejection start
```

PC는 자동 OFF를 보내지 않습니다. 유지시간이 지나면 보드가 출력을 끕니다.
전체 0 마스크는 예약 전체 취소입니다. 같은 nonzero 마스크도 매번 새 이벤트입니다.
반복 횟수 기본값은 32회입니다. 전송 간격에는 `20, 10, 50, 30, 100`처럼 쉼표 또는 공백으로 여러 값을 입력할 수 있습니다. 첫 프레임은 즉시 전송하고, 이후 프레임 사이에 입력 간격을 순서대로 적용하며 끝까지 사용하면 처음부터 반복합니다. 각 값의 범위는 1~10000ms입니다.
PC GUI/USB 스케줄링을 사용하므로 정밀 시간은 보장하지 않으며, 실제 간격은 송신 로그의 시간으로 확인하세요.
32개가 모두 쌓이는 시험은 보드 MOVE를 5000ms로 설정하고 `ejection queue status/history`로 확인하세요.
Echo는 Queue 접수 확인이며 실제 타격 완료가 아닙니다.
연결/연결 해제/창 닫기 시 자동 OFF를 보내지 않으므로 보드 예약이 유지됩니다.
초기 상태 표시가 필요하면 개발 빌드에서 `ejection diag status_send`를 실행하세요.
사용법과 검증 범위는 프로젝트 `app/ejection_controller/README_PHASE4.md`를 참고하세요.

Enable heartbeat gating only when deliberately testing it:

```text
ejection can heartbeat on
```

Disable it again:

```text
ejection can heartbeat off
```
