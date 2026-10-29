#!/usr/bin/env bash
# File: tools/build-shim.sh
# Purpose: Build the Windows shim DLL from shim/**/*.c plus the generated Vulkan thunks; the single place the shim's compiler flags live
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: tools/build-shim.sh
# Output: build/shim/crossport_libc.dll
# Needs: a mingw-w64 gcc and python3 on a Windows host.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/shim"
generated="$out/generated"
mkdir -p "$generated"
py=python3
"$py" --version >/dev/null 2>&1 || py=python
"$py" "$root/tools/gen-vulkan-thunks.py" "$root/third_party/vulkan/vk.xml" "$generated/vulkan_thunks.c"
"$py" "$root/tools/gen-shim-def.py" "$out/crossport_libc.def" "$root/shim" "$generated"
sources=$(find "$root/shim" "$generated" -name '*.c' | sort)
# shellcheck disable=SC2086
gcc -O2 -Wall -Wextra -Werror -fno-builtin -fno-tree-loop-distribute-patterns -shared \
  -I "$root/shim" -I "$root/shim/vulkan" \
  -o "$out/crossport_libc.dll" $sources "$out/crossport_libc.def" -lbcrypt -lsynchronization -lshell32
echo "$out/crossport_libc.dll"
