#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output="${TMPDIR:-/tmp}/aer_virtual_driveboard_model_test"

cc -std=c11 -Wall -Wextra -Werror -pedantic \
    "$repo_root/tests/aer_virtual_driveboard_model_test.c" \
    -o "$output"
"$output"
