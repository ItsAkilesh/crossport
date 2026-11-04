#!/usr/bin/env bash
# File: tests/corpus/sdl2_input/build.sh
# Purpose: Build the sdl2_input executable against SDL2 and copy libSDL2 beside it for the translation step
# Author: Akilesh
# Date: 2026-11-01
#
# Usage: build.sh <output-dir>
# Needs: libsdl2-dev, libx11-dev and libxtst-dev. The program runs natively against the system libSDL2; the
# same library file is copied into the output directory so the Windows job translates exactly what Linux ran.

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
# shellcheck disable=SC2046
gcc "${common[@]}" $(sdl2-config --cflags) -fPIE -pie -Wl,-z,now -o "$out/sdl2_input" "$src/main.c" -lSDL2 -lX11 -lXtst
cp -L "$(gcc -print-file-name=libSDL2-2.0.so.0)" "$out/libSDL2-2.0.so.0"
