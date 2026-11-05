#!/usr/bin/env bash
# File: tests/corpus/red_zone/build.sh
# Purpose: Build the red_zone executable against libX11 and libpthread, WITH the red zone (no -mno-red-zone)
# Author: Akilesh
# Date: 2026-11-05
#
# Usage: build.sh <output-dir>
# Needs: the X11 headers and library (libx11-dev) and objdump.
#
# Every other corpus case is built with -mno-red-zone. This one is not, as real AppImages are not, and it
# fails to build if the compiler stops using the red zone, because the test would then prove nothing. The stack
# protector is off because a canary gives the function a real frame, and then it does not use the red zone.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
common=(-O2 -Wall -Wextra -Werror -fno-stack-protector -fcf-protection=none
  -fno-builtin -fno-tree-loop-distribute-patterns)

mkdir -p "$out"
gcc "${common[@]}" -fPIE -pie -Wl,-z,now -pthread -o "$out/red_zone" "$src/main.c" -lX11

# Disassemble to a file first: under pipefail, grep -q ending the pipe early would make objdump look like a failure.
objdump -d "$out/red_zone" > "$out/red_zone.dis"
if ! grep -Eq -- '-0x[0-9a-f]+\(%rsp\)' "$out/red_zone.dis"; then
  echo "red_zone: the compiler did not use the red zone, so this test would prove nothing" >&2
  exit 1
fi
rm -f "$out/red_zone.dis"
