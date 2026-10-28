# Phase 4 — CAN queued ejection

Current implementation; hardware waveform and button validation are pending.

## Board operation

In IDLE, configure common timing (milliseconds):

```text
settings set conveyor_delay_ms 500
settings set ejection_hold_ms 100
settings save
controller ready
controller start
```

- 0x101, standard DLC 8, D0 + low nibble D1: 12-channel event mask.
- Nonzero mask: one event, including identical consecutive masks.
- Zero mask: cancel all pending/active events and turn all outputs off.
- Echo means accepted, not mechanically fired.
- T0: MCU RX callback. ON target = T0 + MOVE; OFF target = ON target + HOLD.
- 32 total outstanding slots (pending + active); a multichannel event uses one slot.
- No time-based OUTPUT_TIMEOUT fault. OFF at the requested HOLD deadline is normal execution.
- If an entire interval has already expired before dispatch, record MISSED and do not fire late.
- A late start does not move the OFF deadline. Actual hold can therefore be shorter.
- Same-channel overlapping events merge into continuous ON; they are not separate mechanical strokes.
- STOP/FAULT/ALL OFF invalidate buffered old-epoch CAN requests. Wait for cancel Echo before submitting new events.
- ALL OFF has a separate control mailbox; repeated pending cancels may coalesce.
- A successful cancellation cannot revive old events on restart.
- Manual shell output is refused while reservations exist. Reservations are refused while a manual output is active.

## Shell diagnostics

```text
scheduler status
scheduler list
scheduler history
scheduler clear
canproto status
controller status
```

History keeps the last 32 finished/cancelled/missed events. `rx_gap` is the actual
interval from the previous CAN reception (`-1` for the first displayed entry).
Completed entries show `delay=actual/planned`, `hold=actual/planned`, and
`late(on/off)=actual dispatch-due` in milliseconds. `scheduler list` retains the
raw T0/on_due/off_due/T1/T2/cancel_at
timestamps for detailed debugging. These are software dispatch times near GPIO
application, not measured physical edges. `-1` means an event did not reach that
stage. For overlapping events no new physical edge may occur.
The scheduler scans all 32 slots and applies the union of active masks once per pass.

## OLED and encoder

PA8 push button + PC6/PC7 rotary encoder:
- Top row shows current state and selected action, e.g. >READY>START.
- In IDLE: short press cycles STATE/MOVE/HOLD.
- On STATE: rotation selects START/STOP in READY; other states have one action.
- Hold STATE for 1 second to execute: IDLE→READY, READY→START or STOP,
  RUNNING→STOP, FAULT→CLEAR. Existing FSM prerequisites apply.
- On MOVE/HOLD: one decoded encoder step changes 10ms (clamped to bounds);
  hold for 1 second to save EEPROM.
- READY/RUNNING/FAULT lock selection to STATE; time edits and save are blocked.
- One long press executes once. Release and press again for the next state transition.
  Rotation while pressed is ignored; an external state change disarms a held action.
- OLED: state/action, MOVE, HOLD with RAM/SAV, pending Q and active A.
  Rejected actions show ACTION ERR; faults show their reason.
- MOVE: 0..10000ms; HOLD: 1..1000ms. These are initial software ranges, not certified valve limits.
- Existing pulse_ms remains the independent manual shell pulse default.
- EEPROM v1 settings migrate in RAM; new MOVE/HOLD defaults are 500/100ms.
  Explicit save writes v2 to the opposite slot; existing records are retained until then.
- CAN bitrate accepts 125/250/500/1000k. Save and reboot to apply a bitrate change.

## Simulator

Restart tools/ejection_can_simulator with ./run.
Select matching CAN speed, connect, then use:
- Selected channels: one reservation.
- Repeated selected channels: configured count and PC send interval.
- CH1→CH12 sequence: wraps until the count is reached.
- Cancel all: stops the local repeat sequence and sends zero mask.

No automatic OFF is sent on connection, disconnection, or window close.
Closing the simulator does not cancel reservations already accepted by the board.
First accepted event starts status publication; use canproto status_send for initial observation.
The old PC-timed 100ms Pulse and selected-OFF controls do not apply to Phase 4.
PC log timestamps and repeat intervals are not precision timing measurements.

For the 32/33 capacity test, use MOVE=5000ms and a 20ms send interval, so all requests
arrive before execution starts. 32 should be accepted; the 33rd should increase overflow/reject.
Inspect shell counters rather than inferring acceptance from TX rows.

## Implementation

scheduler_core is pure C time/state logic. ejection_scheduler serializes submit/cancel/manual
operations and the deadline thread with one mutex. Submit returns only after slot allocation.
The deadline thread uses k_sem_take with the nearest deadline; submit/cancel wakes it.
No command mailbox is needed inside the scheduler; the CAN RX queue remains separate.
A generation counter rejects stale CAN requests across cancellation and state transitions.
The common settings snapshot is copied per accepted event. GPIO, OLED, EEPROM and CAN roles
remain separated; EEPROM/display never execute in CAN ISR.

## Host checks

```sh
sh app/ejection_controller/tests/run_host.sh
cd tools/ejection_can_simulator
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=src .venv/bin/pytest -q -p no:cacheprovider
.venv/bin/ruff check --no-cache src tests
```

The C tests run with AddressSanitizer/UndefinedBehaviorSanitizer:
- timeline, overlap, adjacent boundaries, capacity, cancellation, lateness and randomized reference;
- public scheduler registration/state/epoch/manual ownership (single-thread kernel doubles);
- EEPROM v1 migration, A/B verification, torn write recovery and IDLE gate.

These host checks do not validate Zephyr interrupt scheduling, physical GPIO timing, EEPROM
hardware, or encoder electrical behavior. Verify those on the board after flashing.
