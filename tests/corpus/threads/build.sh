#!/usr/bin/env bash
# File: tests/corpus/threads/build.sh
# Purpose: Build the threads case: libcount.so.1 and the threads executable, with stack protectors on
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
common=(-O2 -Wall -Wextra -Werror -pthread -fstack-protector-all -mno-red-zone -fcf-protection=none
  -fno-builtin -fno-tree-loop-distribute-patterns -I "$src/../common")
link=(-Wl,-z,now -Wl,-rpath,'$ORIGIN')

mkdir -p "$out"
gcc "${common[@]}" -fPIC -shared "${link[@]}" -Wl,-soname,libcount.so.1 \
  -o "$out/libcount.so.1" "$src/libcount.c"
gcc "${common[@]}" -fPIE -pie "${link[@]}" \
  -o "$out/threads" "$src/main.c" "$out/libcount.so.1"
