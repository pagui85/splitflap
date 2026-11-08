#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
bringup_tests=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bringup_temp=$(mktemp -d "${TMPDIR:-/tmp}/splitflap-bringup.XXXXXX")
trap 'rm -rf -- "$bringup_temp"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror \
    -I "$bringup_tests/../main" \
    -I "$bringup_tests/../../esp-idf/components/chainlink_bus/include" \
    "$bringup_tests/diagnostic_core_test.cpp" "$bringup_tests/../main/diagnostic_core.cpp" \
    -o "$bringup_temp/diagnostic_core_test"
"$bringup_temp/diagnostic_core_test"
python3 "$bringup_tests/test_metadata.py"
