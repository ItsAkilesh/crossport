#!/usr/bin/env bash
# File: tests/corpus/dbus_session/build.sh
# Purpose: Build the dbus_session executable against libdbus
# Author: Akilesh
# Date: 2026-11-09
#
# Usage: build.sh <output-dir>
# Needs: the libdbus headers and library (libdbus-1-dev) and pkg-config.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
common=(-O2 -Wall -Wextra -Werror -fstack-protector-all -mno-red-zone -fcf-protection=none
  -fno-builtin -fno-tree-loop-distribute-patterns)
read -r -a dbus_flags <<< "$(pkg-config --cflags --libs dbus-1)"

mkdir -p "$out"
gcc "${common[@]}" -fPIE -pie -Wl,-z,now -o "$out/dbus_session" "$src/main.c" "${dbus_flags[@]}"
