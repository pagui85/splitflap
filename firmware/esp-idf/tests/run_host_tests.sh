#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
splitflap_tests=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
splitflap_component="$splitflap_tests/../components/chainlink_bus"
splitflap_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/splitflap-tests.XXXXXX")
trap 'rm -rf -- "$splitflap_test_dir"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -I "$splitflap_component/include" \
    "$splitflap_tests/chainlink_protocol_test.cpp" -o "$splitflap_test_dir/protocol"
"$splitflap_test_dir/protocol"
for splitflap_s3 in 0 1; do
    g++ -std=c++17 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32S3="$splitflap_s3" \
        -I "$splitflap_tests/fakes" -I "$splitflap_component/include" \
        "$splitflap_tests/chainlink_bus_test.cpp" "$splitflap_component/chainlink_bus.cpp" \
        -o "$splitflap_test_dir/hal-$splitflap_s3"
    "$splitflap_test_dir/hal-$splitflap_s3"
done
