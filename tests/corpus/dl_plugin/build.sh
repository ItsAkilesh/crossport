#!/usr/bin/env bash
# File: tests/corpus/dl_plugin/build.sh
# Purpose: Build the dl_plugin host program and its plugins (two plugins and a library only they need)
# Author: Akilesh
# Date: 2026-11-05
#
# Usage: build.sh <output-dir>
# The plugins are not linked into the host: it opens them by name with dlopen, and finds them next to itself
# through an $ORIGIN rpath. The host exports host_value and host_log to them (-rdynamic).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

out="$1"
src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
common=(-O2 -Wall -Wextra -Werror -fstack-protector-all -mno-red-zone -fcf-protection=none
  -fno-builtin -fno-tree-loop-distribute-patterns)
link=(-Wl,-z,now -Wl,-rpath,'$ORIGIN')

mkdir -p "$out"
gcc "${common[@]}" -fPIC -shared "${link[@]}" -Wl,-soname,libplugin_dep.so \
  -o "$out/libplugin_dep.so" "$src/libplugin_dep.c"
for plugin in a b; do
  gcc "${common[@]}" -fPIC -shared "${link[@]}" -Wl,-soname,libplugin_$plugin.so \
    -o "$out/libplugin_$plugin.so" "$src/libplugin_$plugin.c" "$out/libplugin_dep.so"
done
gcc "${common[@]}" -fPIE -pie -rdynamic "${link[@]}" -o "$out/dl_plugin" "$src/main.c" -ldl
