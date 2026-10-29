#!/usr/bin/env bash
# File: tests/corpus/cxx_exceptions/build.sh
# Purpose: Build the cxx_exceptions executable with the C++ runtime and unwinder linked in statically
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: build.sh <output-dir>
# The runtime is static because bundled AppImage libraries do not include libstdc++; the host one is not
# available on Windows (see docs/DESIGN.md, host libraries).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$out"
g++ -O2 -Wall -Wextra -Werror -std=c++17 -fstack-protector-all -mno-red-zone -fcf-protection=none \
  -fPIE -pie -Wl,-z,now -static-libstdc++ -static-libgcc \
  -o "$out/cxx_exceptions" "$src/main.cpp"
