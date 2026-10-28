#!/usr/bin/env bash
# File: tests/corpus/apps/lua/build.sh
# Purpose: Build Lua 5.4 (no readline, no dlopen) with the compiler's default hardening and install it into an AppDir
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: build.sh <source-dir> <appdir>

set -euo pipefail
src="$1"
appdir="$2"
make -C "$src" posix -j"$(nproc)" > /dev/null
install -D "$src/src/lua" "$appdir/usr/bin/lua"
