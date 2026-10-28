#!/usr/bin/env bash
# File: tools/corpus/linux_baseline.sh
# Purpose: Package one corpus app as an AppImage and record the Linux reference result of every test
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: tools/corpus/linux_baseline.sh <app>
# Output: build/corpus/images/<app>.AppImage and results/apps/<app>-<test>-linux.json

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <app>" >&2
  exit 2
fi

app="$1"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

bash "$root/tools/corpus/package.sh" "$app" > /dev/null
python3 "$root/tools/corpus/run_app_tests.py" \
  --app-dir "$root/tests/corpus/apps/$app" \
  --exe "$root/build/corpus/appdirs/$app/AppRun" \
  --label linux \
  --work "$root/build/corpus/work-linux" \
  --results "$root/results/apps" \
  --difftest "$root/tools/difftest/target/release/difftest"
