#!/usr/bin/env bash
# File: tools/stress.sh
# Purpose: Run a translated bundle repeatedly and require identical output every time, to expose races
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: tools/stress.sh <case> <runs>
# Needs: build/bundle/<case>/<case>.exe (from tools/run-translated.sh) and the difftest build.

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <case> <runs>" >&2
  exit 2
fi

case_name="$1"
runs="$2"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
difftest="$root/tools/difftest/target/release/difftest.exe"
exe="$root/build/bundle/$case_name/$case_name.exe"
scratch="$root/build/stress"

if [[ ! -f "$exe" || ! -f "$difftest" ]]; then
  echo "missing $exe or $difftest; run tools/run-translated.sh $case_name first" >&2
  exit 2
fi

mkdir -p "$scratch"
"$difftest" run --out "$scratch/baseline.json" -- "$exe"
for ((run = 1; run <= runs; run++)); do
  "$difftest" run --out "$scratch/run.json" -- "$exe"
  if ! "$difftest" compare "$scratch/baseline.json" "$scratch/run.json"; then
    echo "run $run of $runs differed from the first run" >&2
    exit 1
  fi
done
echo "$case_name: $runs runs, all identical"
