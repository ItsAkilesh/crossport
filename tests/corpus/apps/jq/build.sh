#!/usr/bin/env bash
# File: tests/corpus/apps/jq/build.sh
# Purpose: Build jq with its bundled oniguruma and a static libjq, with default flags, and install it into an AppDir
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: build.sh <source-dir> <appdir>

set -euo pipefail
src="$1"
appdir="$2"
cd "$src"
./configure --prefix=/usr --with-oniguruma=builtin --disable-docs --disable-valgrind \
  --disable-maintainer-mode --disable-shared --enable-static > /dev/null
make -j"$(nproc)" > /dev/null
install -D jq "$appdir/usr/bin/jq"
