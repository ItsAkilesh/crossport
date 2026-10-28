#!/usr/bin/env bash
# File: tests/corpus/tls_basic/build.sh
# Purpose: Build the tls_basic case: libtls.so.1 and the tls_basic executable, with stack protectors on
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
# The red zone stays off until its design decision; the stack protector is on so %fs:0x28 is exercised.
# The last two flags keep gcc from turning loops into memcpy/memset calls the shim does not provide yet.
common=(-O2 -Wall -Wextra -Werror -fstack-protector-all -mno-red-zone -fcf-protection=none
  -fno-builtin -fno-tree-loop-distribute-patterns -I "$src/../common")
link=(-Wl,-z,now -Wl,-rpath,'$ORIGIN')

mkdir -p "$out"
gcc "${common[@]}" -fPIC -shared "${link[@]}" -Wl,-soname,libtls.so.1 \
  -o "$out/libtls.so.1" "$src/libtls.c"
gcc "${common[@]}" -fPIE -pie "${link[@]}" \
  -o "$out/tls_basic" "$src/main.c" "$out/libtls.so.1"
