#!/usr/bin/env bash
# File: tools/run-case.sh
# Purpose: Build one corpus case natively and record its result for differential testing
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: tools/run-case.sh <case> <label>
#   <case>  directory name under tests/corpus/ (e.g. hello)
#   <label> platform label used in the output name (e.g. linux, windows)
# Output: results/<case>-<label>.json

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <case> <label>" >&2
  exit 2
fi

case_name="$1"
label="$2"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$root/build/$label"
results_dir="$root/results"

exe_suffix=""
if [[ "${OS:-}" == "Windows_NT" ]]; then
  exe_suffix=".exe"
fi

mkdir -p "$build_dir" "$results_dir"

cargo build --release --locked --manifest-path "$root/tools/difftest/Cargo.toml"
difftest="$root/tools/difftest/target/release/difftest$exe_suffix"

binary="$build_dir/$case_name$exe_suffix"
extra_flags=()
flags_file="$root/tests/corpus/$case_name/cflags.txt"
if [[ -f "$flags_file" ]]; then
  read -r -a extra_flags < "$flags_file"
fi
gcc -O2 -Wall -Wextra -Werror "${extra_flags[@]}" -o "$binary" "$root/tests/corpus/$case_name/$case_name.c"

"$difftest" run --out "$results_dir/$case_name-$label.json" -- "$binary"
echo "recorded $results_dir/$case_name-$label.json"
