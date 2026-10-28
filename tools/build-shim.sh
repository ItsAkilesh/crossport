#!/usr/bin/env bash
# File: tools/build-shim.sh
# Purpose: Build the Windows shim DLL from shim/*.c; the single place the shim's compiler flags live
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: tools/build-shim.sh
# Output: build/shim/crossport_libc.dll
# Needs: a mingw-w64 gcc on a Windows host.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$root/build/shim"
gcc -O2 -Wall -Wextra -Werror -fno-builtin -shared -o "$root/build/shim/crossport_libc.dll" \
  "$root"/shim/*.c -lbcrypt -lsynchronization
echo "$root/build/shim/crossport_libc.dll"
