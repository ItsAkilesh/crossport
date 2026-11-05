#!/usr/bin/env bash
# File: tools/corpus/translate_and_run.sh
# Purpose: Unpack an AppImage, translate its program for Windows and run the app's tests; a failed translation is a recorded result
# Author: Akilesh
# Date: 2026-11-05
#
# Usage: tools/corpus/translate_and_run.sh <app>
# Needs: build/corpus/images/<app>.AppImage, build/shim/crossport_libc.dll and the release builds of crossport and difftest.
# Output: results/apps/<app>-<test>-windows.json, or results/apps/<app>-translate-error.txt when translation fails.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <app>" >&2
  exit 2
fi

app="$1"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
app_dir="$root/tests/corpus/apps/$app"
image="$root/build/corpus/images/$app.AppImage"
unpacked="$root/build/corpus/windows/$app"
bundle="$root/build/bundle/apps/$app"
results="$root/results/apps"
crossport="$root/translator/target/release/crossport.exe"
difftest="$root/tools/difftest/target/release/difftest.exe"
binary="$(python -c 'import json,sys; print(json.load(open(sys.argv[1]))["binary"])' "$app_dir/app.json")"

mkdir -p "$results"
rm -rf "$unpacked" "$bundle" "$results/$app-translate-error.txt"

# An app with "convert": true goes through the one-step command users run, with its flags.
if python -c 'import json,sys; sys.exit(0 if json.load(open(sys.argv[1])).get("convert") else 1)' "$app_dir/app.json"; then
  mapfile -t convert_flags < <(python -c 'import json,sys; print(*json.load(open(sys.argv[1])).get("translate_flags", []), sep="\n")' "$app_dir/app.json" | tr -d '\r' | grep -v '^$' || true)
  if ! "$crossport" convert "$image" --shim "$root/build/shim/crossport_libc.dll" --out "$bundle" "${convert_flags[@]}" \
    > "$results/$app-convert.log" 2> "$results/$app-translate-error.txt"; then
    echo "$app: conversion failed: $(head -n 1 "$results/$app-translate-error.txt")"
    exit 0
  fi
  rm -f "$results/$app-translate-error.txt"
  python "$root/tools/corpus/run_app_tests.py" --app-dir "$app_dir" --exe "$bundle/$binary.exe" --label windows \
    --work "$root/build/corpus/work-windows" --results "$results" --difftest "$difftest"
  exit 0
fi

"$crossport" unpack "$image" --out "$unpacked"

# Flags the app needs, for example --stub-missing for host libraries such as ALSA that AppImages never bundle.
mapfile -t flags < <(python -c 'import json,sys; print(*json.load(open(sys.argv[1])).get("translate_flags", []), sep="\n")' "$app_dir/app.json" | tr -d '\r' | grep -v '^$' || true)

if ! "$crossport" translate "$unpacked/usr/bin/$binary" --appdir "$unpacked" "${flags[@]}" \
  --shim "$root/build/shim/crossport_libc.dll" --out "$bundle" 2> "$results/$app-translate-error.txt"; then
  # The libc is still growing: record why, so the summary shows the blocker instead of failing the job.
  echo "$app: translation failed: $(head -n 1 "$results/$app-translate-error.txt")"
  exit 0
fi
rm -f "$results/$app-translate-error.txt"

python "$root/tools/corpus/run_app_tests.py" \
  --app-dir "$app_dir" \
  --exe "$bundle/$binary.exe" \
  --label windows \
  --work "$root/build/corpus/work-windows" \
  --results "$results" \
  --difftest "$difftest"
