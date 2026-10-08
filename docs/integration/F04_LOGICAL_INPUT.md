# F04 — Experience logical-input extension

This non-AER branch extends runtime main 3efa9d99f228c4e9d34194cf0e39952608b309ad. Ordinary launches and Experience launches without custom input retain existing controls/default-device handling. No game files, recorder source or reconstructed force models are involved.

Windows capabilities add contract 2 and LOGICAL_INPUT=1. The host supplies paired --experience-input-pipe / --experience-input-owner arguments alongside existing configuration/dependency/save-root arguments. Preflight validates the pair/namespace without connecting or launching a game. Build identity remains the exact clean source HEAD; dependency identity remains intact.

The host owns physical input. A local pipe delivers 32-byte little-endian snapshots: ORI1, version 1, size 32, advancing u32 sequence, u16 steering/gas/brake, u16 buttons and 12 zero reserved bytes. Mask bits: 0 GearUp, 1 GearDown, 2 Start, 3 Coin, 4 ViewChange, 5 reserved, 6 Service, 7 Test, 8 Exit. Unknown bits/versions, partial packets, reserved bytes and replayed sequences fail closed. Steering neutral is 32768; released pedals are zero.

The receiver verifies the server PID, uses overlapped reads, bounds work to eight messages per event update and neutralizes snapshots older than 250 ms. Fresh delivery recovers from staleness. Protocol/ownership failures terminate nonzero. The first packet prints EXPERIENCE_INPUT_RECEIVED=1; this establishes transport decoding, never gameplay readiness.

The custom path initializes existing JVS mappings/game-specific overrides, restricts itself to OutRun gameplay, bypasses legacy bindings/GUID writeback and feeds existing action states/dirty dispatch. fixPlayerForAction retains arcade gear routing. processChangedActions retains JVS quantization, switches and coin behavior. No JVS emulation, calibration, device roster, FFB or game patch is copied or rebuilt here.

Asset-free tests cover the production decoder and native receiver/watchdog. Windows Debug/Release also build the entire x86 runtime, verify build/dependency identity and run prior persistence tests. Framework fixtures cover aggregation and delivery to a child process.

Gameplay response, runtime poll cadence, device compatibility and latency remain **REQUIRES RUNTIME VALIDATION**. R02-W01 assembly stays pinned to its reviewed F02 runtime until a separate package update is reviewed. Do not merge this branch automatically.
