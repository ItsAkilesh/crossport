#!/usr/bin/env bash
# File: tests/corpus/xkb_requests/build.sh
# Purpose: Build the xkb_requests executable against libxcb
# Author: Akilesh
# Date: 2026-11-06
#
# Usage: build.sh <output-dir>
# Needs: the xcb headers and library (libxcb1-dev).

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
gcc "${common[@]}" -fPIE -pie -Wl,-z,now -o "$out/xkb_requests" "$src/main.c" -lxcb
