#!/usr/bin/env bash
# File: tools/ci/install-vulkan-windows.sh
# Purpose: Give a hosted Windows runner a Vulkan loader (built from pinned Khronos sources) and a software driver (Mesa lavapipe)
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: tools/ci/install-vulkan-windows.sh
# Builds vulkan-1.dll from the Khronos Vulkan-Loader at a pinned commit, downloads the pinned,
# checksum-verified Mesa package for lavapipe, registers lavapipe where the loader looks for drivers, and
# adds the loader to the workflow PATH. Hosted runners have no GPU, and CPU rendering is deterministic.
# The vendor installer is not used: it is a GUI installer and hangs on a headless runner.

set -euo pipefail

HEADERS_COMMIT="29f979ee5aa58b7b005f805ea8df7a855c39ff37" # Vulkan-Headers v1.3.296
LOADER_COMMIT="4eaa6df4250693b4a2b654ca2464b25a299d94fe"  # Vulkan-Loader v1.3.296
MESA_URL="https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z"
MESA_SHA256="351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695"
SEVEN_ZIP="/c/Program Files/7-Zip/7z.exe"

work="${RUNNER_TEMP:-/tmp}/vulkan"
mkdir -p "$work"

# checkout <repository> <commit> <directory>: fetches exactly one commit and refuses anything else.
checkout() {
  git init --quiet "$3"
  git -C "$3" fetch --quiet --depth 1 "https://github.com/KhronosGroup/$1.git" "$2"
  git -C "$3" checkout --quiet FETCH_HEAD
  test "$(git -C "$3" rev-parse HEAD)" = "$2" || { echo "unexpected commit for $1" >&2; exit 1; }
}

# download <url> <sha256> <output>: fetches a file and refuses it unless the checksum matches.
download() {
  curl --fail --silent --show-error --location --output "$3" "$1"
  echo "$2 *$3" | sha256sum --check --status || {
    echo "checksum mismatch for $1" >&2
    exit 1
  }
}

checkout Vulkan-Headers "$HEADERS_COMMIT" "$work/headers"
checkout Vulkan-Loader "$LOADER_COMMIT" "$work/loader"
cmake -S "$work/headers" -B "$work/headers-build" -DCMAKE_INSTALL_PREFIX="$work/headers-install" > /dev/null
cmake --build "$work/headers-build" --target install --config Release > /dev/null
cmake -S "$work/loader" -B "$work/loader-build" -G "Visual Studio 17 2022" -A x64 -DUPDATE_DEPS=OFF \
  -DBUILD_TESTS=OFF -DVULKAN_HEADERS_INSTALL_DIR="$work/headers-install" > /dev/null
cmake --build "$work/loader-build" --config Release > /dev/null
loader="$(find "$work/loader-build" -name vulkan-1.dll | head -n 1)"
test -n "$loader" || { echo "the Vulkan loader did not build" >&2; exit 1; }

download "$MESA_URL" "$MESA_SHA256" "$work/mesa.7z"
"$SEVEN_ZIP" x -y "-o$work/mesa" "$work/mesa.7z" > /dev/null
icd="$work/mesa/x64/lvp_icd.x86_64.json"
test -f "$icd" || { echo "lavapipe not found in the Mesa package" >&2; exit 1; }

windows_icd="$(cygpath -w "$icd")"
echo "loader: $(cygpath -w "$loader")"
echo "driver: $windows_icd"
# Hosted runners run elevated, and the Vulkan loader then ignores VK_DRIVER_FILES (it only trusts the
# registry for high-integrity processes), so the driver is registered where the loader looks for it.
MSYS_NO_PATHCONV=1 reg add 'HKLM\SOFTWARE\Khronos\Vulkan\Drivers' /v "$windows_icd" /t REG_DWORD /d 0 /f > /dev/null
if [[ -n "${GITHUB_PATH:-}" ]]; then
  cygpath -w "$(dirname "$loader")" >> "$GITHUB_PATH"
fi
