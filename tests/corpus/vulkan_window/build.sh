#!/usr/bin/env bash
# File: tests/corpus/vulkan_window/build.sh
# Purpose: Build the vulkan_window executable against the Vulkan loader and libxcb
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: build.sh <output-dir>
# Needs: the Vulkan and xcb headers and libraries (libvulkan-dev, libxcb1-dev).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
common=(-O2 -Wall -Wextra -Werror -fstack-protector-all -mno-red-zone -fcf-protection=none
  -fno-builtin -fno-tree-loop-distribute-patterns -I "$src")

mkdir -p "$out"
gcc "${common[@]}" -fPIE -pie -Wl,-z,now \
  -o "$out/vulkan_window" "$src/main.c" "$src/window.c" "$src/presenter.c" -lvulkan -lxcb
