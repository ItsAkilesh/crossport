#!/usr/bin/env bash
# File: installer/package.sh
# Purpose: Stage Crossport's files and pack them into one Crossport-Setup.exe
# Author: Akilesh
# Date: 2026-11-05
#
# Usage: installer/package.sh <output-dir> [--runtime <runtime-pack-dir>]
# Inputs (override with environment variables):
#   CROSSPORT_EXE   translator, default translator/target/release/crossport.exe
#   CROSSPORT_SHIM  shim DLL, default build/shim/crossport_libc.dll
# The runtime pack is the output of tools/runtime/fetch_pack.py. Without it the installer still works, but
# AppImages that rely on host libraries (libstdc++ and similar) need --stub-missing.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:?usage: package.sh <output-dir> [--runtime <dir>]}"
runtime=""
if [ "${2:-}" = "--runtime" ]; then runtime="${3:?--runtime needs a folder}"; fi
translator="${CROSSPORT_EXE:-$root/translator/target/release/crossport.exe}"
shim="${CROSSPORT_SHIM:-$root/build/shim/crossport_libc.dll}"
for f in "$translator" "$shim"; do [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }; done

cargo build --release --locked --manifest-path "$root/installer/Cargo.toml"
bin="$root/installer/target/release"

stage="$out/stage"
rm -rf "$stage"
mkdir -p "$stage"
cp "$translator" "$stage/crossport.exe"
cp "$shim" "$stage/crossport_libc.dll"
cp "$bin/crossport-open.exe" "$stage/crossport-open.exe"
cp "$root/LICENSE" "$stage/LICENSE"
if [ -n "$runtime" ]; then
  mkdir -p "$stage/runtime"
  cp -r "$runtime"/. "$stage/runtime/"
fi
"$bin/crossport-setup.exe" --make-installer "$out/Crossport-Setup.exe" --from "$stage"
echo "wrote $out/Crossport-Setup.exe"
