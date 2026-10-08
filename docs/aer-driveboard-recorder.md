# AER-01C passive drive-board transport recorder

This research-only recorder observes raw OutRun 2 SP SDX drive-board transport
calls before or after the existing loader boundary, without interpreting force
semantics. It is disabled by default and supports DVP-0015 and DVP-0015A only.

## Enable a capture

Set these environment variables before starting the loader:

```text
AER_DRIVEBOARD_RECORDER=1
AER_DRIVEBOARD_OUTPUT=<capture path without extension>
AER_GAME_EXECUTABLE_SHA256=<verified game executable SHA-256>
AER_CABINET_TYPE=<configured cabinet type, or UNKNOWN>
AER_CABINET_ID=<configured cabinet ID, or UNKNOWN>
AER_DRIVEBOARD_MARKER_FILE=<optional append-only marker text file>
```

The recorder creates:

- `<capture>.aerbin`: `AER_DRIVEBOARD_RAW_V1` binary event stream;
- `<capture>.json`: human-readable provenance and capture diagnostics.

If optional metadata is unavailable, the sidecar records `UNKNOWN`. It does not
guess.

## Operator markers

When `AER_DRIVEBOARD_MARKER_FILE` is set, append one marker per line to that
file. Markers such as `INIT`, `MENU`, `STATIONARY`, `STEERING_LEFT`,
`STEERING_RIGHT`, `DRIVING`, `PAUSE`, and `STOP` describe operator activity
only. They do not assign meaning to drive-board bytes.

The writer checks the append-only marker file in the background. Markers are
stored as distinct event records and are never injected into drive-board
communication.

## Binary format

The stream starts with a packed little-endian header:

| Field | Type |
|---|---|
| magic | 8 bytes (`AERDBR1`) |
| format version | `uint16` |
| header size | `uint16` |
| endian marker | `uint32` |
| schema | 32 bytes |

Each record contains a packed header followed by `payloadLength` raw bytes:

| Field | Purpose |
|---|---|
| record size | Header plus payload bytes |
| event type | Direct write, read, marker, overflow, `writev`, `fwrite`, or descriptor duplication |
| capture status | Complete, truncated, or invalid buffer |
| sequence | Global recorder sequence |
| timestamp | Monotonic nanoseconds |
| process/thread ID | Runtime origin |
| endpoint/file descriptor | Intercepted serial identity |
| requested count | Original requested byte count |
| operation result | Existing loader operation result |
| payload length | Exact bytes present in this record |

Outbound payloads are copied from the game-supplied write buffer. Inbound
payloads are the bytes returned to the game by the existing loader and are
explicitly classified as **LOADER-SYNTHESIZED RESPONSES**.

The recorder does not assume a write is a protocol packet and does not decode
commands.

Transport APIs retain their original call boundaries. Event types 1–4 keep
their original meaning; type 5 is `writev`, type 6 is `fwrite`, and type 7 is
descriptor duplication. A duplication record stores the source descriptor in
`file descriptor` and the returned destination descriptor in `operation
result`. The reserved header field remains zero and has not been reinterpreted.

## Buffering and completeness

The intercepted thread uses a bounded queue and a nonblocking `trylock`. Disk
I/O runs on a background thread. If the queue is full or briefly unavailable,
the recorder increments the dropped-record count, marks the capture incomplete,
and emits an overflow record when the writer can resume. Oversized observations
are explicitly marked truncated; loss is never silent.

## First smoke test

After a platform build succeeds, the first DVP-0015 run should capture only:

1. startup;
2. drive-board initialization;
3. menu;
4. stationary state; and
5. shutdown.

Do not infer force semantics from this capture. Do not begin driving scenarios
until the transport recorder is shown to be stable and noninvasive.
