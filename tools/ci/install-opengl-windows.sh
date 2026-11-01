#!/usr/bin/env bash
# File: tools/ci/install-opengl-windows.sh
# Purpose: Give a hosted Windows runner a software OpenGL driver (Mesa llvmpipe) by staging its opengl32.dll for translated programs
# Author: Akilesh
# Date: 2026-11-01
#
# Usage: tools/ci/install-opengl-windows.sh
# Output: build/gl-runtime/ with opengl32.dll and libgallium_wgl.dll from the pinned, checksum-verified Mesa
# package (the one install-vulkan-windows.sh uses, reused when it is already downloaded).
# tools/run-translated.sh copies these next to a translated program that has a uses-opengl marker file, so
# the shim's LoadLibrary("opengl32.dll") finds them before the system's. Hosted runners have no GPU, and
# CPU rendering is deterministic.

set -euo pipefail

MESA_URL="https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z"
MESA_SHA256="351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695"
SEVEN_ZIP="/c/Program Files/7-Zip/7z.exe"
RUNTIME_FILES=(opengl32.dll libgallium_wgl.dll)

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work="${RUNNER_TEMP:-/tmp}/opengl"
archive="${RUNNER_TEMP:-/tmp}/vulkan/mesa.7z"
runtime="$root/build/gl-runtime"

# verified <file>: true when the file exists and has the pinned checksum.
verified() {
  [[ -f "$1" ]] && echo "$MESA_SHA256 *$1" | sha256sum --check --status
}

if ! verified "$archive"; then
  mkdir -p "$work"
  archive="$work/mesa.7z"
  curl --fail --silent --show-error --location --output "$archive" "$MESA_URL"
  verified "$archive" || { echo "checksum mismatch for $MESA_URL" >&2; exit 1; }
fi

rm -rf "$work/extracted" "$runtime"
mkdir -p "$work" "$runtime"
"$SEVEN_ZIP" x -y "-o$work/extracted" "$archive" "${RUNTIME_FILES[@]/#/x64/}" > /dev/null
for file in "${RUNTIME_FILES[@]}"; do
  test -f "$work/extracted/x64/$file" || { echo "$file not found in the Mesa package" >&2; exit 1; }
  cp "$work/extracted/x64/$file" "$runtime/"
done
echo "OpenGL runtime: $(cygpath -w "$runtime")"
