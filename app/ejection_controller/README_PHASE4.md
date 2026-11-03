# Phase 4 — CAN queued ejection

Current implementation. The 2026-10-29 field test verified 12-channel output,
AUTO/MANUAL operation and CAN communication. The 0..200ms timing refinement still
requires OLED, EEPROM and physical output timing validation on the board.

## Board operation

In IDLE, configure local/manual timing defaults (milliseconds):

```text
ejection config set move_ms 100
ejection config set hold_ms 100
ejection config save
ejection ready
ejection start
```

- 0x100, standard DLC 8: D0 version 1, D1 IDLE(0)/RUN(1), D2 sequence.
  RUN is accepted in AUTO mode; IDLE cancels reservations and turns all outputs off.
- 0x101, standard DLC 8: D0 + low nibble D1 event mask, D2..D3 MOVE ms,
  D4..D5 HOLD ms (uint16 little-endian), D6..D7 zero.
- MOVE/HOLD are 0..200ms and are captured per accepted event. CAN timing does not
  overwrite the EEPROM/OLED local settings.
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
ejection queue status
ejection queue list
ejection queue history
ejection queue cancel
ejection can status
ejection status
```

History keeps the last 32 finished/cancelled/missed events. `rx_gap` is the actual
interval from the previous CAN reception (`-1` for the first displayed entry).
Completed entries show `delay=actual/planned`, `hold=actual/planned`, and
`late(on/off)=actual dispatch-due` in milliseconds. `ejection queue list` retains the
raw T0/on_due/off_due/T1/T2/cancel_at
timestamps for detailed debugging. These are software dispatch times near GPIO
application, not measured physical edges. `-1` means an event did not reach that
stage. For overlapping events no new physical edge may occur.
The scheduler scans all 32 slots and applies the union of active masks once per pass.

## OLED and encoder

PA8 push button + PC6/PC7 rotary encoder:
- AUTO top row is `>AUTO IDLE` or `>AUTO RUN`; MANUAL top row is `MANUAL`.
- AUTO short press cycles FSM/MOVE/HOLD. MANUAL short press cycles MOVE/HOLD.
- Hold FSM for 1 second to toggle AUTO IDLE/RUN. Existing FSM prerequisites apply.
- On MOVE/HOLD: one decoded encoder step changes 2ms (clamped to bounds);
  hold for 1 second to save EEPROM.
- MOVE/HOLD can be viewed while RUNNING, but edits and save are allowed only in IDLE.
- One long press executes once. Release and press again for the next state transition.
  Rotation while pressed is ignored; an external state change disarms a held action.
- OLED: state/action, MOVE, HOLD with RAM/SAV, pending Q and active A.
  Rejected actions show ACTION ERR; faults show their reason.
- MOVE: 0..200ms; HOLD: 0..200ms. MOVE=0 dispatches immediately. HOLD=0 completes without energizing an output.
- Existing pulse_ms remains the independent manual shell pulse default.
- EEPROM v1 settings migrate in RAM; new MOVE/HOLD defaults are 100/100ms.
  Explicit save writes v2 to the opposite slot; existing records are retained until then.
- CAN bitrate accepts 125/250/500/1000k. Save and reboot to apply a bitrate change.

## Simulator

Restart tools/ejection_can_simulator with ./run.
Select matching CAN speed, connect, then use:
- AUTO RUN / IDLE: send 0x100 operating-state control.
- MOVE/HOLD: enter the per-reservation values (0..200ms).
- Selected channels: one reservation.
- Repeated selected channels: configured count and PC send interval.
- CH1→CH12 sequence: wraps until the count is reached.
- Cancel all: stops the local repeat sequence and sends zero mask.

No automatic OFF is sent on connection, disconnection, or window close.
Closing the simulator does not cancel reservations already accepted by the board.
First accepted event starts status publication; use `ejection diag status_send` for initial observation in a development build.
The old PC-timed 100ms Pulse and selected-OFF controls do not apply to Phase 4.
PC log timestamps and repeat intervals are not precision timing measurements.

The host test deterministically verifies the 32/33 capacity boundary. A hardware capacity
test must use MOVE=200ms, HOLD=200ms and a burst sender fast enough to register all 33
frames before slots retire. The simulator accepts 1ms minimum intervals, but USB serial
transport timing is not deterministic. Inspect Echo and shell counters rather than inferring
acceptance from TX rows.

## Implementation

scheduler_core is pure C time/state logic. ejection_scheduler serializes submit/cancel/manual
operations and the deadline thread with one mutex. Submit returns only after slot allocation.
The deadline thread uses k_sem_take with the nearest deadline; submit/cancel wakes it.
No command mailbox is needed inside the scheduler; the CAN RX queue remains separate.
A generation counter rejects stale CAN requests across cancellation and state transitions.
The MOVE/HOLD values carried by 0x101 are copied per accepted event. GPIO, OLED, EEPROM and CAN roles
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
