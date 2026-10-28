#!/usr/bin/env bash
# File: tools/run-case.sh
# Purpose: Build one corpus case natively and record its result for differential testing
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: tools/run-case.sh <case> <label>
#   <case>  directory name under tests/corpus/ (e.g. hello)
#   <label> platform label used in the output name (e.g. linux, windows)
# A case is either <case>.c (built with optional cflags.txt flags) or a build.sh
# that takes an output directory and produces <case> inside it.
# Output: build/<label>/<case>/ and results/<case>-<label>.json

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <case> <label>" >&2
  exit 2
fi

case_name="$1"
label="$2"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case_dir="$root/tests/corpus/$case_name"
build_dir="$root/build/$label/$case_name"
results_dir="$root/results"

exe_suffix=""
if [[ "${OS:-}" == "Windows_NT" ]]; then
  exe_suffix=".exe"
fi

rm -rf "$build_dir"
mkdir -p "$build_dir" "$results_dir"

cargo build --release --locked --manifest-path "$root/tools/difftest/Cargo.toml"
difftest="$root/tools/difftest/target/release/difftest$exe_suffix"

if [[ -f "$case_dir/build.sh" ]]; then
  bash "$case_dir/build.sh" "$build_dir"
else
  extra_flags=()
  if [[ -f "$case_dir/cflags.txt" ]]; then
    read -r -a extra_flags < "$case_dir/cflags.txt"
  fi
  gcc -O2 -Wall -Wextra -Werror "${extra_flags[@]}" \
    -o "$build_dir/$case_name$exe_suffix" "$case_dir/$case_name.c"
fi

"$difftest" run --out "$results_dir/$case_name-$label.json" -- "$build_dir/$case_name$exe_suffix"
echo "recorded $results_dir/$case_name-$label.json"
