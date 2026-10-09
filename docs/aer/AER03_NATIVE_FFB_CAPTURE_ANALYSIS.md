# AER-03 — Native FFB Capture Analysis

The successful DEV 5 session `20261008-172718-8c375f9c` is the first live recording in which Jennifer completed native initialization, reached driver state 12 and cabinet-check state 2, ran its original steering callback, and produced sustained gameplay requests.

The reusable analyzer and exact generated reports are preserved under:

```text
research/aer/tools/ffb_analyzer/
research/aer/reports/AER03_20261008_172718/
```

## Main findings

- All 10,176 binary records parse structurally and all complete four/seven-byte frames pass XOR validation.
- The native callback boundary separates startup/configuration traffic from 16,210 logical gameplay-slot observations.
- Channel 0 contains all 1,617 non-idle gameplay requests; SDX channel 1 remains idle in this capture.
- There are 283 bounded continuous-magnitude observations. Every observed magnitude is the game-side minimum `4`; encoded direction changes 95 times.
- There are 55 pattern requests: six unique Pattern-10 translations, ten unique Pattern-13 translations, two Pattern-0 translations, and 37 zero translations that cannot be mapped to a unique internal index.
- Median active serial-request spacing is about 15.92 ms. Long gaps demonstrate that serial intervals are event/request timing, not a fixed force-update clock.

## Surface and collision boundary

This session has no synchronized position, collision state, or road-classification telemetry. It cannot prove that any request occurred on cobblestone, a kerb, a wall, or another vehicle.

Pattern 10 remains a classification-family transition—not a universal cobblestone effect. Pattern 13 can be identified from its unique `0x02` translation. Zero translations cannot distinguish Patterns 12, 14, and 15 from the other internal indices that share the same translated value. Pattern 0 remains strongly associated with the statically traced wall-rebound family, but the live capture alone cannot establish the exact event for either occurrence.

## Integrity

The recorder reports 28 dropped records and marks the raw capture incomplete. Those losses do not undo the independent native-activation proof, but they prevent an exact event-for-event timeline claim. Original write-call results, virtual accepted frames, callback executions, and decoded logical slots are retained as separate measurements.

The capture contains loader-synthesized inbound responses. They are used to operate the isolated virtual board and are not treated as authentic Sega firmware behavior. Command magnitudes are game requests, not torque measurements.
