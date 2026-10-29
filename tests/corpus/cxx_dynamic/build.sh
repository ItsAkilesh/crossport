#!/usr/bin/env bash
# File: tests/corpus/cxx_dynamic/build.sh
# Purpose: Build a C++ program against the dynamic libstdc++ and put the C++ runtime libraries beside it, as a translated bundle needs
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: build.sh <output-dir>
# On Windows nothing provides libstdc++ or libgcc_s, so the translator is given the Linux copies as
# "runtime pack" libraries and translates them with the program (see docs/DESIGN.md, host libraries).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$out"
g++ -O2 -Wall -Wextra -Werror -std=c++17 -pthread -fstack-protector-all -mno-red-zone -fcf-protection=none \
  -fPIE -pie -Wl,-z,now -Wl,-rpath,'$ORIGIN' -o "$out/cxx_dynamic" "$src/main.cpp"
for library in libstdc++.so.6 libgcc_s.so.1; do
  cp "$(realpath "$(g++ -print-file-name="$library")")" "$out/$library"
done
