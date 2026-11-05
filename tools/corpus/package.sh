#!/usr/bin/env bash
# File: tools/corpus/package.sh
# Purpose: Build one corpus app from its pinned source and pack it into a real type 2 AppImage
# Author: Akilesh
# Date: 2026-11-05
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

# An app that ships as a prebuilt AppImage ("image" in app.json) is downloaded, checked against its pin and
# unpacked; there is nothing to build. The wrapper sets the environment AppRun would, for the Linux baseline.
if python3 -c 'import json,sys; sys.exit(0 if "image" in json.load(open(sys.argv[1])) else 1)' "$app_dir/app.json"; then
  pin="$work/downloads/$app.pin.json"
  mkdir -p "$work/downloads" "$work/images"
  python3 -c 'import json,sys; print(json.dumps(json.load(open(sys.argv[1]))["image"]))' "$app_dir/app.json" > "$pin"
  python3 "$root/tools/corpus/fetch.py" file "$pin" "$image" > /dev/null
  chmod +x "$image"
  cargo build --release --locked --manifest-path "$root/translator/Cargo.toml"
  rm -rf "$appdir"
  "$root/translator/target/release/crossport" unpack "$image" --out "$appdir" > /dev/null
  binary="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["binary"])' "$app_dir/app.json")"
  cat > "$appdir/crossport-run" <<WRAPPER
#!/usr/bin/env bash
here="\$(cd "\$(dirname "\$0")" && pwd)"
export APPDIR="\$here" LD_LIBRARY_PATH="\$here/usr/lib" QT_PLUGIN_PATH="\$here/usr/plugins"
exec "\$here/usr/bin/$binary" "\$@"
WRAPPER
  chmod +x "$appdir/crossport-run"
  echo "$image"
  exit 0
fi

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
