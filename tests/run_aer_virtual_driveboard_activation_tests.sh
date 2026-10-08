#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/aer-vdb-activation.XXXXXX")
trap 'rm -rf "$out"' EXIT

cc -std=c11 -Wall -Wextra -Werror -I"$root/src" \
  "$root/tests/aer_virtual_driveboard_activation_test.c" \
  "$root/src/loader/research/aerVirtualDriveboard.c" \
  -o "$out/test"
"$out/test"

if nm -u "$out/test" | grep -E 'sdlFfbDriveboard|EVIOCSFF|motionBoard|passthrough|shared(Open|Read|Write|Select|Ioctl)'; then
  echo "DEV 5 activation transport acquired a prohibited output dependency" >&2
  exit 1
fi
