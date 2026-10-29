#!/usr/bin/env bash
# File: tools/test-shim.sh
# Purpose: Build and run the shim's C library tests against build/shim/crossport_libc.dll
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: tools/test-shim.sh   (run tools/build-shim.sh first)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/shim-test"
mkdir -p "$out"
cp "$root/build/shim/crossport_libc.dll" "$out/"
gcc -O1 -Wall -Wextra -Werror -fno-builtin -o "$out/libc_test.exe" "$root/tests/shim/libc_test.c"
"$out/libc_test.exe"
