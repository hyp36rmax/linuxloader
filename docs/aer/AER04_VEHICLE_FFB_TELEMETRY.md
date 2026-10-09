# AER-04 — Native FFB and Vehicle Telemetry

## Purpose

AER-04 adds a passive, separately versioned observation stream beside the recovered native steering pipeline. It does not calculate modern FFB, call original game functions, or interpret virtual-board responses as Sega firmware behavior.

Schema: `AER_VEHICLE_FFB_V1`

## Observation points

```text
CabinetCtrl_Main
    → DrCtrlDataSet (original executes)
        → passive vehicle snapshot
    → DrCtrlMoveSend (original executes)
        → passive scheduling marker
    → steerReqSendOut
        → passive logical-command snapshot
    → hardcomSend / raw recorder
```

All timestamps come from the same monotonic clock used by `AER_DRIVEBOARD_RAW_V1`. The vehicle snapshot is taken immediately after the original `DrCtrlDataSet`; the command row uses the most recent snapshot. This preserves ordering but does not claim zero delay between calculation, scheduling, and transport.

## Verified fields

| CSV field | Original source | Representation | Validity | Evidence |
|---|---|---|---|---|
| `front_tire_direction_s16` | `CalcTireDirection()` → `EVWORK_CAR+0x054` | Signed-16 angle, π/32768 | Valid with `carWork` and flag `0x2` | Confirmed average front-tire direction |
| `road_aggregate_mask` | `EVWORK_CAR+0x3FC` | 32-bit one-hot/contact-family mask | Flag `0x4` | Confirmed transition input |
| `front_left_road_mask` | `EVWORK_CAR+0x404` | 32-bit one-hot classification mask | Flag `0x4` | Confirmed front contact |
| `front_right_road_mask` | `EVWORK_CAR+0x408` | 32-bit one-hot classification mask | Flag `0x4` | Confirmed front contact |
| `threshold_state_f32` | `EVWORK_CAR+0x3AC` | Native float; semantics unresolved | Flag `0x4` | Confirmed calculation/selection input, meaning unknown |
| `logical_channel` and command bytes | `steerReqSendOut()` input | Original three-byte logical request per channel | Flag `0x8` | Confirmed game-side request |

`carWork` is the preserved `DrCtrlDataSet()` vehicle-work argument. Reads happen only in the verified DVP-0015A observer hook.

## Explicitly unavailable fields

Vehicle identity, speed, raw player steering, vehicle orientation, track position, wall-contact state, and vehicle-contact state are written as `-1`. Existing evidence does not yet establish safe source offsets and representations for this executable. AER-04 does not guess them.

## Correlation capability

The synchronized stream can directly compare front-tire direction and all three verified road masks with the selected logical request. It can identify exact classification transitions around Pattern 10 and the road-mask conditions surrounding translated Pattern 13 or ambiguous zero-translated patterns.

It cannot yet call an event a kerb, bump, cobblestone, wall impact, or vehicle impact unless the observed classification is joined to independently established course evidence. Tulip Garden's ordinal-20 bridge-road mapping remains the strongest course-specific reference, but track position is not yet captured.

## Capture integrity corrections

The successful AER-03 recording reached virtual lifecycle `FAULT` only after the ready transport stopped receiving traffic long enough to exceed its general 900-tick watchdog. The watchdog is now restricted to initialization/configuration/calibration; a ready but idle board remains ready. Physical disconnect and malformed traffic still fail closed.

The 28 AER-03 recorder losses came from production `pthread_mutex_trylock()` contention, not malformed frames. Production capture now takes the recorder's short queue mutex normally. Queue-full behavior and overflow accounting remain intact and tested.

These corrections do not change Jennifer's state ownership, responses, steering calculations, serial routing, or physical-output isolation.
