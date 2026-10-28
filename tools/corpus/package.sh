#!/usr/bin/env bash
# File: tools/corpus/package.sh
# Purpose: Build one corpus app from its pinned source and pack it into a real type 2 AppImage
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: tools/corpus/package.sh <app>
# Output: build/corpus/appdirs/<app>/ (the AppDir) and build/corpus/images/<app>.AppImage
# Needs: python3, gcc, make, mksquashfs (squashfs-tools).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <app>" >&2
  exit 2
fi

app="$1"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
app_dir="$root/tests/corpus/apps/$app"
work="$root/build/corpus"
appdir="$work/appdirs/$app"
image="$work/images/$app.AppImage"

[[ -f "$app_dir/app.json" ]] || { echo "unknown app: $app" >&2; exit 2; }

source_dir="$(python3 "$root/tools/corpus/fetch.py" source "$app_dir" "$work")"
runtime="$(python3 "$root/tools/corpus/fetch.py" file "$root/tests/corpus/apps/runtime.json" "$work/downloads/runtime-x86_64")"
binary="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["binary"])' "$app_dir/app.json")"

rm -rf "$appdir"
mkdir -p "$appdir" "$work/images"
bash "$app_dir/build.sh" "$source_dir" "$appdir"
[[ -x "$appdir/usr/bin/$binary" ]] || { echo "the build did not produce usr/bin/$binary" >&2; exit 1; }
ln -s "usr/bin/$binary" "$appdir/AppRun"

# An AppImage is the runtime followed by a SquashFS image of the AppDir, which is what appimagetool writes.
squashfs="$work/images/$app.squashfs"
rm -f "$squashfs"
mksquashfs "$appdir" "$squashfs" -root-owned -noappend -comp zstd -quiet
cat "$runtime" "$squashfs" > "$image"
chmod +x "$image"
rm -f "$squashfs"
echo "$image"
