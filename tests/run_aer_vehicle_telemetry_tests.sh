#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/aer-vehicle-telemetry.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pedantic -DAER_VEHICLE_TELEMETRY_TESTING \
  "$root/tests/aer_vehicle_telemetry_test.c" "$root/src/loader/research/aerVehicleTelemetry.c" -o "$tmp/test"
"$tmp/test" "$tmp/vehicle.csv"
