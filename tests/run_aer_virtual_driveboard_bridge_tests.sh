#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/aer-vdb-bridge.XXXXXX")
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -DAER_VDB_TESTING -I"$root/src" \
  "$root/tests/aer_virtual_driveboard_bridge_test.c" \
  "$root/src/loader/research/aerVirtualDriveboard.c" \
  "$root/src/loader/research/aerVirtualDriveboardBootstrap.c" \
  "$root/src/loader/research/aerVirtualDriveboardBridge.c" \
  -o "$out/test"
"$out/test" "$out/Jennifer.synthetic"
if nm -u "$out/test" | grep -E 'sdlFfbDriveboard|EVIOCSFF|motionBoard|shared(Open|Read|Write|Select|Ioctl)|CabinetCtrl|DrCtrl|hardcom'; then
  echo "virtual bridge acquired a prohibited runtime/output dependency" >&2; exit 1
fi
