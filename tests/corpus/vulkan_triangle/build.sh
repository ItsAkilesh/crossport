#!/usr/bin/env bash
# File: tests/corpus/vulkan_triangle/build.sh
# Purpose: Compile the shaders to SPIR-V headers and build the vulkan_triangle executable against the Vulkan loader
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: build.sh <output-dir>
# Needs: glslangValidator (glslang-tools) and the Vulkan headers and loader (libvulkan-dev).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
generated="$out/generated"
common=(-O2 -Wall -Wextra -Werror -fstack-protector-all -mno-red-zone -fcf-protection=none
  -fno-builtin -fno-tree-loop-distribute-patterns -I "$generated" -I "$src")

mkdir -p "$generated"
glslangValidator -V --vn triangle_vert_spv -o "$generated/vert.h" "$src/shaders/triangle.vert"
glslangValidator -V --vn triangle_frag_spv -o "$generated/frag.h" "$src/shaders/triangle.frag"
{
  echo '#include <stdint.h>'
  echo '#include "vert.h"'
  echo '#include "frag.h"'
} > "$generated/shaders.h"
gcc "${common[@]}" -fPIE -pie -Wl,-z,now \
  -o "$out/vulkan_triangle" "$src/main.c" "$src/gpu.c" "$src/pipeline.c" "$src/render.c" -lvulkan
