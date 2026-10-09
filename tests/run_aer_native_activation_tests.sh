#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/aer-native-activation-test.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT

cc -std=c11 -D_POSIX_C_SOURCE=200809L -DAER_NATIVE_ACTIVATION_TESTING \
    -I"$root/src" \
    "$root/tests/aer_native_activation_test.c" \
    "$root/src/loader/research/aerNativeActivation.c" \
    "$root/src/loader/research/aerVehicleTelemetry.c" \
    -o "$test_dir/aer_native_activation_test"

"$test_dir/aer_native_activation_test" disabled "$test_dir/disabled.json"
test ! -e "$test_dir/disabled.json"
"$test_dir/aer_native_activation_test" wrong-revision "$test_dir/wrong.json"
test ! -e "$test_dir/wrong.json"
"$test_dir/aer_native_activation_test" capture "$test_dir/capture.json"
echo "AER native activation synthetic tests passed"
