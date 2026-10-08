#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/aer-activation-test.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT

cc -std=c11 -D_POSIX_C_SOURCE=200809L -DAER_ACTIVATION_DIAGNOSTICS_TESTING \
    -I"$root/src" \
    "$root/tests/aer_activation_diagnostics_test.c" \
    "$root/src/loader/research/aerActivationDiagnostics.c" \
    -o "$test_dir/aer_activation_diagnostics_test"

"$test_dir/aer_activation_diagnostics_test" disabled "$test_dir/disabled.json"
test ! -e "$test_dir/disabled.json"
"$test_dir/aer_activation_diagnostics_test" capture "$test_dir/capture.json"
echo "AER activation diagnostic synthetic tests passed"
