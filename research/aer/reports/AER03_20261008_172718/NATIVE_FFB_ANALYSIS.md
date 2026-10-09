# AER-03 Native FFB Capture Analysis

Capture: `20261008-172718-8c375f9c`

## What this establishes

Jennifer reached driver state 12 and cabinet-check state 2. The analyzer parsed 10,176 events and 16,210 gameplay logical-slot commands.

These are original game requests to Sega's motor controller, not physical torque or reconstructed sensation.

## Steering load

Decoded continuous gameplay requests: 283. Magnitude transitions: 0. Encoded-direction changes: 95.

| Request | Count |
|---:|---:|
| 4 | 283 |

## Discrete patterns

Observed requests: 55.

| Translation | Direction | Candidate indices | Count |
|---:|---:|---|---:|
| 0x00 | 0 | 2|3|6|7|8|9|11|12|14|15 | 34 |
| 0x02 | 0 | 13 | 10 |
| 0x04 | 1 | 10 | 4 |
| 0x00 | 1 | 2|3|6|7|8|9|11|12|14|15 | 3 |
| 0x04 | 0 | 10 | 2 |
| 0x0B | 0 | 0 | 2 |

Pattern 10 maps uniquely to `0x04`; Pattern 13 maps uniquely to `0x02`. Zero translations remain ambiguous.

## Surface, kerb, and collision evidence

No synchronized road classification, position, or collision state is present. The capture cannot directly label a request cobblestone, kerb, wall, or vehicle contact. Pattern 10 remains a classification-family transition, not a universal cobblestone label.

## Timing

Active serial-request interval statistics in milliseconds: `{"max": 15183.8954, "median": 15.922350000000002, "min": 0.0032, "p95": 366.27}`. Callback counts, serial writes, accepted virtual frames, and firmware durations remain separate measurements.

## Capture integrity

- Events: 10,176
- Writes / reads: 9,997 / 151
- Dropped / overflow records: 28 / 28
- Checksum failures: 0
- Virtual accepted frames: 544
- Physical isolation: True

The raw recorder marks this capture incomplete because 28 events were dropped. Native activation and sustained command generation are established; exact event-for-event reconstruction is not.

## Evidence limits

- No synchronized vehicle position, road-classification, or collision-event telemetry is present.
- Translated pattern values are not unique pattern indices where translations collide.
- Command magnitudes are original game requests, not physical torque.
- Inbound bytes are loader-synthesized responses, not authentic Sega firmware output.
- Recorder overflow makes the timeline incomplete at 28 documented points.
- Original write results and virtual accepted-frame counts are different observation layers.
