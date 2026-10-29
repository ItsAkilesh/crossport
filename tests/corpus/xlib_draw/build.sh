#!/usr/bin/env bash
# File: tests/corpus/xlib_draw/build.sh
# Purpose: Build the xlib_draw executable against libX11
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: build.sh <output-dir>
# Needs: the X11 headers and library (libx11-dev).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
common=(-O2 -Wall -Wextra -Werror -fstack-protector-all -mno-red-zone -fcf-protection=none
  -fno-builtin -fno-tree-loop-distribute-patterns)

mkdir -p "$out"
gcc "${common[@]}" -fPIE -pie -Wl,-z,now -o "$out/xlib_draw" "$src/main.c" -lX11
