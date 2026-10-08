#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/aer-vdb-bootstrap.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT

cc -std=c11 -Wall -Wextra -Werror -DAER_VDB_TESTING -I"$root/src" \
    "$root/tests/aer_virtual_driveboard_bootstrap_test.c" \
    "$root/src/loader/research/aerVirtualDriveboard.c" \
    "$root/src/loader/research/aerVirtualDriveboardBootstrap.c" \
    -o "$test_dir/aer_virtual_driveboard_bootstrap_test"

"$test_dir/aer_virtual_driveboard_bootstrap_test" \
    "$test_dir/Jennifer.synthetic" "$test_dir/Jennifer.truncated"

if nm -u "$test_dir/aer_virtual_driveboard_bootstrap_test" | \
   grep -E 'sdlFfbDriveboard|EVIOCSFF|emulateMotion|passthrough|shared(Open|Write|Read|Select|Ioctl)' >/dev/null; then
    echo "AER eligibility bootstrap acquired a prohibited runtime dependency" >&2
    exit 1
fi
