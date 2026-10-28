#!/usr/bin/env bash
# File: tests/corpus/apps/sqlite3/build.sh
# Purpose: Build the sqlite3 shell from the amalgamation with default flags (no readline, no extensions) into an AppDir
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: build.sh <source-dir> <appdir>

set -euo pipefail
src="$1"
appdir="$2"
mkdir -p "$appdir/usr/bin"
gcc -O2 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1 \
  "$src/shell.c" "$src/sqlite3.c" -lm -lpthread -o "$appdir/usr/bin/sqlite3"
