# AGMO TIC FSM Firmware

STM32G431C8 및 Zephyr RTOS 3.7 기반 트랙터 수동/자동 시동 제어 펌웨어이다.

## 주요 동작

- AUTO 명령 전: R1/R2/R3 OFF, 기존 수동 Key/Crank 경로 유지
- AUTO 진입: R1 ON → 100ms → R3 Manual Lockout ON
- START: R2를 1~3초 ON한 뒤 자동 OFF
- Key OFF, AUTO OFF, AUTO 운전 중 CAN timeout: R2 → R3 → R1 OFF
- R2/R3는 소프트웨어와 하드웨어 모두 R1 종속 인터록 적용
- CAN 250 kbit/s, Classical CAN, 29-bit Extended ID, 200ms 주기

## Build

```bash
source ~/zephyrproject/.venv/bin/activate
west build -b agmo_tic app/agmo_tic
```

## Console

USB CDC 연결 후 `help`로 전체 명령을 확인한다.

```text
tic status
tic auto on
tic start 1000
tic auto off
tic all-off

input status
can status

led status
led key on
led all off
led auto
```

릴레이를 직접 조작하는 Shell 명령은 제공하지 않는다. `tic auto`, `tic start`,
`tic all-off` 명령은 모두 제어 FSM을 거치며, `tic start`는 Key ON 및 AUTO READY
조건에서만 허용된다. LED 개별 명령은 하드웨어 bring-up 진단용이다.

## CAN

| 방향 | Extended ID | 주기 |
| --- | --- | --- |
| 상위 제어기 → TIC | `0x18FF0001` | 200ms |
| TIC → 상위 제어기 | `0x18FF0100` | 200ms |

Application CRC는 사용하지 않으며 Alive Counter와 명령 유효성 검사를 사용한다.
