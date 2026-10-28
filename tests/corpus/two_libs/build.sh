#!/usr/bin/env bash
# File: tests/corpus/two_libs/build.sh
# Purpose: Build the two_libs case: libmath.so.1, libgreet.so.1 (needs libmath) and the two_libs executable
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: build.sh <output-dir>

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# TLS (stack protector) and the red zone are out of scope until Milestone 3 and the red-zone decision.
common=(-O2 -Wall -Wextra -Werror -fno-stack-protector -mno-red-zone -fcf-protection=none)
link=(-Wl,-z,now -Wl,-rpath,'$ORIGIN')

mkdir -p "$out"
gcc "${common[@]}" -fPIC -shared "${link[@]}" -Wl,-soname,libmath.so.1 \
  -o "$out/libmath.so.1" "$src/libmath.c"
gcc "${common[@]}" -fPIC -shared "${link[@]}" -Wl,-soname,libgreet.so.1 \
  -o "$out/libgreet.so.1" "$src/libgreet.c" "$out/libmath.so.1"
gcc "${common[@]}" -fPIE -pie "${link[@]}" \
  -o "$out/two_libs" "$src/main.c" "$out/libgreet.so.1" "$out/libmath.so.1"
