#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/aer-recorder-test.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT INT TERM

cc -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -DAER_RECORDER_TESTING \
    -pthread \
    "$root/src/loader/research/aerDriveboardRecorder.c" \
    "$root/tests/aer_driveboard_recorder_test.c" \
    -o "$test_dir/aer_driveboard_recorder_test"

"$test_dir/aer_driveboard_recorder_test" disabled "$test_dir/disabled"
"$test_dir/aer_driveboard_recorder_test" capture "$test_dir/capture"
"$test_dir/aer_driveboard_recorder_test" paths "$test_dir/unused"
echo "AER drive-board recorder synthetic tests passed"
