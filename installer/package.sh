#!/usr/bin/env bash
# File: installer/package.sh
# Purpose: Stage Crossport's files and pack them into one Crossport-Setup.exe
# Author: Akilesh
# Date: 2026-11-05
#
# Usage: installer/package.sh <output-dir> [--runtime <runtime-pack-dir>]
# Writes <output-dir>/Crossport-Setup.exe and <output-dir>/SHA256SUMS.txt (the checksum a release publishes).
# Inputs (override with environment variables):
#   CROSSPORT_EXE   translator, default translator/target/release/crossport.exe
#   CROSSPORT_SHIM  shim DLL, default build/shim/crossport_libc.dll
#   CROSSPORT_RELEASE_VERSION  version to stamp into the programs (a release tag such as v0.8.1); default: the crate's own
# Signing (optional): set CROSSPORT_SIGN to a command that signs the files it is given, for example
#   CROSSPORT_SIGN="pwsh -File tools/ci/sign-windows.ps1"
# and the staged programs, the uninstaller and the finished installer are signed. Unset, nothing is signed.
# The runtime pack is the output of tools/runtime/fetch_pack.py. Without it the installer still works, but
# AppImages that rely on host libraries (libstdc++ and similar) need --stub-missing.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:?usage: package.sh <output-dir> [--runtime <dir>]}"
runtime=""
if [ "${2:-}" = "--runtime" ]; then
  runtime="${3:?--runtime needs a folder}"
  [ -d "$runtime" ] || { echo "runtime pack folder $runtime does not exist" >&2; exit 1; }
  [ -n "$(find "$runtime" -type f -print -quit)" ] || { echo "runtime pack folder $runtime has no files" >&2; exit 1; }
fi
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
sign() { if [ -n "${CROSSPORT_SIGN:-}" ]; then read -r -a sign_cmd <<< "$CROSSPORT_SIGN"; "${sign_cmd[@]}" "$@"; fi; }
uninstaller="$out/uninstall.exe"
cp "$bin/crossport-setup.exe" "$uninstaller"
sign "$stage/crossport.exe" "$stage/crossport_libc.dll" "$stage/crossport-open.exe" "$uninstaller"
"$bin/crossport-setup.exe" --make-installer "$out/Crossport-Setup.exe" --from "$stage" --uninstaller "$uninstaller"
rm -f "$uninstaller"
sign "$out/Crossport-Setup.exe"
(cd "$out" && sha256sum Crossport-Setup.exe > SHA256SUMS.txt)
echo "wrote $out/Crossport-Setup.exe and $out/SHA256SUMS.txt"
