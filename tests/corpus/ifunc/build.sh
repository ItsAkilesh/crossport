#!/usr/bin/env bash
# File: tests/corpus/ifunc/build.sh
# Purpose: Build the ifunc executable and the library with indirect functions it calls
# Author: Akilesh
# Date: 2026-11-06
#
# Usage: build.sh <output-dir>

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
common=(-O2 -Wall -Wextra -Werror -fno-stack-protector -mno-red-zone -fcf-protection=none)
link=(-Wl,-z,now -Wl,-rpath,'$ORIGIN')

mkdir -p "$out"
gcc "${common[@]}" -fPIC -shared "${link[@]}" -Wl,-soname,libpick.so.1 \
  -o "$out/libpick.so.1" "$src/libpick.c"
gcc "${common[@]}" -fPIE -pie "${link[@]}" -o "$out/ifunc" "$src/main.c" "$out/libpick.so.1"
