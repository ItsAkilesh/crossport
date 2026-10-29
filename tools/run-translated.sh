#!/usr/bin/env bash
# File: tools/run-translated.sh
# Purpose: Translate a Linux ELF corpus binary to a Windows bundle, run it and record its result
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: tools/run-translated.sh <case>
#   <case> corpus case name; the ELF and its libraries are read from build/elf/<case>/
# Output: results/<case>-windows.json
# Extra translate flags: tests/corpus/<case>/translate-flags.txt, one flag per line (for example --stub-missing).
# Needs: cargo and a mingw-w64 gcc on a Windows host.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <case>" >&2
  exit 2
fi

case_name="$1"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
elf_dir="$root/build/elf/$case_name"
elf="$elf_dir/$case_name"
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
[[ -f "$shim_dir/crossport_libc.dll" ]] || bash "$root/tools/build-shim.sh" > /dev/null

extra_flags=()
flags_file="$root/tests/corpus/$case_name/translate-flags.txt"
if [[ -f "$flags_file" ]]; then
  mapfile -t extra_flags < <(tr -d '\r' < "$flags_file" | grep -v '^[[:space:]]*$')
fi

"$root/translator/target/release/crossport.exe" translate "$elf" \
  --shim "$shim_dir/crossport_libc.dll" --lib-dir "$elf_dir" --out "$bundle_dir" "${extra_flags[@]}"

"$root/tools/difftest/target/release/difftest.exe" run \
  --out "$results_dir/$case_name-windows.json" -- "$bundle_dir/$case_name.exe"
echo "recorded $results_dir/$case_name-windows.json"
