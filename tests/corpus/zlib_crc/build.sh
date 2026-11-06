#!/usr/bin/env bash
# File: tests/corpus/zlib_crc/build.sh
# Purpose: Build the zlib_crc executable and stage the system libz.so.1 beside it, so the translated run uses a real zlib
# Author: Akilesh
# Date: 2026-11-06
#
# Usage: build.sh <output-dir>
# Needs: libz.so.1 (zlib1g); no development package, the program declares the two functions it calls.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
common=(-O2 -Wall -Wextra -Werror -fno-stack-protector -mno-red-zone -fcf-protection=none)

mkdir -p "$out"
libz="$(readlink -f "$(gcc -print-file-name=libz.so.1)")"
cp "$libz" "$out/libz.so.1"
gcc "${common[@]}" -fPIE -pie -Wl,-z,now -Wl,-rpath,'$ORIGIN' -o "$out/zlib_crc" "$src/main.c" "$out/libz.so.1"
