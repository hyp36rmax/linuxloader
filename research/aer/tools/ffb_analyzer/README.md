# AER native FFB analyzer

This command-line tool decodes `AER_DRIVEBOARD_RAW_V1` without modifying the capture. It separates initialization/menu traffic from gameplay using the first observed `DrCtrlDataSet` callback in `native_activation.json`.

```text
python3 research/aer/tools/ffb_analyzer/analyze.py CAPTURE_DIRECTORY --output OUTPUT_DIRECTORY
```

It generates a CSV timeline and pattern matrix, machine-readable analysis and integrity JSON, plus human-readable overview, surface/kerb, and collision reports.

## Evidence rules

- Four-byte frames contain one logical command and checksum; seven-byte SDX frames contain two.
- Bit 7 of the first command is transport framing and is cleared for classification.
- `0x0B` becomes a continuous gameplay request only after the native gameplay callback begins. Its bounded third byte is a game-side magnitude, not torque.
- `0x7B` is a pattern request. Translation and direction are preserved, but translations are not always reversible to one internal index.
- Reads are loader-synthesized virtual-board responses, never original firmware evidence.
- Callback counts, original write calls, accepted virtual frames, and decoded logical commands remain separate measurements.
