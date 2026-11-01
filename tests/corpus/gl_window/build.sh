#!/usr/bin/env bash
# File: tests/corpus/gl_window/build.sh
# Purpose: Build the gl_window executable against libX11 and libGL
# Author: Akilesh
# Date: 2026-11-01
#
# Usage: build.sh <output-dir>
# Needs: the X11 and OpenGL development packages (libx11-dev, libgl-dev). The program runs against Mesa
# under Xvfb on Linux; libGL and libX11 are provided by the shim on Windows, so no library is copied.

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
gcc "${common[@]}" -fPIE -pie -Wl,-z,now -o "$out/gl_window" "$src/main.c" -lGL -lX11
