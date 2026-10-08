# Experience runtime integration (F02)

This development branch is independent of `research/aer-01c-driveboard-recorder`. It starts at upstream/fork main `9aa6e3e45ccfbbc60eb0f975aa9d3d158a13706c`. Do not merge AER instrumentation or treat this branch as an FFB reconstruction. No original game or framework license is supplied here; upstream's declared CC BY-NC-SA 4.0 and dependency notices remain applicable.

## Corrections

- `initSdlInput` captures the effective absolute controls load path. `saveGuidsToIni` loads/saves that same file, preserves bindings, and clears dirty GUID state only after success. Default no-`-o` behavior remains the legacy local `controls.ini` path.
- `iniSave` already returns 1 for success; the old `== 0` caller was inverted. The new helper checks positive success and parser write/close errors.
- Shared startup no longer rejects existing config/controls merely for spaces.
- `eepromSettingsInit` uses the configured stream opened by `initEeprom` rather than reopening relative `eeprom.bin`. The helper never closes the borrowed stream.
- Windows startup reports argument failure as nonzero while retaining successful help/version exits.
- Correct const-return CRT overloads are selected in the symbol resolver for the pinned GCC baseline; addresses/guest semantics are unchanged.

## Narrow Windows contract

`--experience-capabilities` is an asset-free query. It reports contract 1, original base SHA, full clean Git build SHA, architecture x86, effective controls/config-spaces/save-root capabilities, and explicitly no gameplay-readiness or complete-isolation claim.

Opt-in launch adds `--experience-data DIR --experience-session DIR` to existing `-c FILE -o FILE -L DIR <game>`. `--experience-preflight` checks the same inputs without loading/executing a game. Explicit missing/duplicate arguments, missing configuration/dependency files or unsafe EEPROM/SRAM destinations fail rather than silently falling back. The roots must exist and be disjoint; config belongs in session, controls/EEPROM/SRAM in durable data. The external framework additionally keeps both roots outside installations. Paths are currently ASCII and conservatively under the existing Windows path budget; spaces are accepted.

After `initMain` succeeds and before ELF execution, Windows startup emits `EXPERIENCE_RUNTIME_INITIALIZED=1`. This is a loader-stage observation, never gameplay readiness. Process exit, window appearance or this marker cannot establish a functioning original game.

Opt-in roots map `/home/disk1/rankingdata` and relative ranking paths to durable data, and `/tmp`, `/var/tmp`, warning/segaboot paths to temporary session data. Windows metadata/enumeration/rename paths share the mapper. Known bootstrap shell commands use native file operations; unknown guest shell commands and unowned guest writable filesystem operations fail closed. Mapped traversal/symlink escapes are rejected. Without these flags, legacy mappings/default behavior remain available.

## Isolation boundaries

These bridges are not a complete filesystem sandbox. Original ELF dynamic filenames, native guest-library writes and complete package dependencies require real-runtime validation. Direct host shader diagnostic/disk output is outside guest bridge enforcement: SDX patch cases use in-memory shader adaptation and do not invoke the other-game disk-patching cache path; debug shader dumps require DEBUG_MSGS. F02 framework profiles retain the default false debug policy. Do not claim other games/debug modes are isolated.

The runtime never deletes durable saves. The host owns session cleanup, persistent data/profile paths and retained diagnostic logs. No runtime binaries/dependencies/game assets/profiles are uploaded or released by CI.

## Build and validation

Full Windows i686 MinGW build uses C++17 and upstream bundled archives. CI pins checkout and MSYS2 setup action SHAs and fails unless selected packages match GCC `16.2.0-4`, CMake `4.4.3-3`, Ninja `1.13.2-1`. The action/host/package repository are serviced; exact guards prevent silent toolchain drift, but this is not a hermetic archive. [Dependency digests](DEPENDENCY_SHA256.txt) pin the existing vendored Windows archive/ll-deps bytes. Header versions do not prove their linked binary versions/provenance.

```sh
cmake -S . -B build-runtime -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-runtime --parallel 2
./build-runtime/linuxloader.exe --experience-capabilities
cmake -S tests/experience -B build-experience -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-experience --parallel 2
ctest --test-dir build-experience --output-on-failure
```

Use separate directories for Release. Build from a clean checkout; modified tracked inputs report an unversioned identity and cannot satisfy the framework gate. Keep the complete external `ll-deps` package plus the matching compiler runtime DLL. Preflight checks a baseline subset (`libstdc++.so.5`, `.so.6`, `libgcc_s_dw2-1.dll`), not every possible game DT_NEEDED import or runtime ABI.

Standalone tests execute production effective-path/GUID and EEPROM functions with synthetic files, including custom paths with spaces, repeat writeback, unchanged game sentinels, default compatibility, missing dependencies, strict preflight, root mapping/traversal rejection and save retention after session deletion. Only the unrelated logging frontend is stubbed. Full native runtime compilation and the real capability query are independent CI steps. No proprietary assets or existing tests are modified.

Real SDX launch, native-library filesystem writes, ranking/save semantics, GPU/audio behavior and actual shutdown remain a controlled smoke-validation boundary. See the separate private Experience Framework F02 guide for one focused procedure; do not ask for repeated physical wheel tests as an intermediate build check.
