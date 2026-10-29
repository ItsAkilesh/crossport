#!/usr/bin/env bash
# File: tools/build-shim.sh
# Purpose: Build the Windows shim DLL from shim/*.c; the single place the shim's compiler flags live
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: tools/build-shim.sh
# Output: build/shim/crossport_libc.dll
# Needs: a mingw-w64 gcc and python3 on a Windows host.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/shim"
mkdir -p "$out"
python3 "$root/tools/gen-shim-def.py" "$root/shim" "$out/crossport_libc.def" 2>/dev/null \
  || python "$root/tools/gen-shim-def.py" "$root/shim" "$out/crossport_libc.def"
gcc -O2 -Wall -Wextra -Werror -fno-builtin -fno-tree-loop-distribute-patterns -shared \
  -o "$out/crossport_libc.dll" "$root"/shim/*.c "$out/crossport_libc.def" -lbcrypt -lsynchronization -lshell32
echo "$out/crossport_libc.dll"
