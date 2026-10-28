#!/usr/bin/env bash
# File: tools/run-translated.sh
# Purpose: Translate a Linux ELF corpus binary to a Windows bundle, run it and record its result
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: tools/run-translated.sh <case>
#   <case> corpus case name; the ELF is read from build/elf/<case>
# Output: results/<case>-windows.json
# Needs: cargo and a mingw-w64 gcc on a Windows host.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <case>" >&2
  exit 2
fi

case_name="$1"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
elf="$root/build/elf/$case_name"
shim_dir="$root/build/shim"
bundle_dir="$root/build/bundle/$case_name"
results_dir="$root/results"

if [[ ! -f "$elf" ]]; then
  echo "missing ELF: $elf" >&2
  exit 2
fi

mkdir -p "$shim_dir" "$results_dir"
rm -rf "$bundle_dir"

cargo build --release --locked --manifest-path "$root/tools/difftest/Cargo.toml"
cargo build --release --locked --manifest-path "$root/translator/Cargo.toml"
gcc -O2 -Wall -Wextra -Werror -fno-builtin -shared -o "$shim_dir/crossport_libc.dll" "$root/shim/crossport_libc.c"

"$root/translator/target/release/crossport.exe" translate "$elf" \
  --shim "$shim_dir/crossport_libc.dll" --out "$bundle_dir"

"$root/tools/difftest/target/release/difftest.exe" run \
  --out "$results_dir/$case_name-windows.json" -- "$bundle_dir/$case_name.exe"
echo "recorded $results_dir/$case_name-windows.json"
