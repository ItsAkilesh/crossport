#!/usr/bin/env bash
# File: tools/ci/install-vulkan-windows.sh
# Purpose: Give a hosted Windows runner a Vulkan loader and a software driver (Mesa lavapipe), both pinned and checksum-verified
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: tools/ci/install-vulkan-windows.sh
# Installs the Vulkan runtime (vulkan-1.dll) system-wide and exports VK_DRIVER_FILES to $GITHUB_ENV so
# the loader uses only lavapipe. Hosted runners have no GPU, and CPU rendering is deterministic.

set -euo pipefail

RUNTIME_URL="https://sdk.lunarg.com/sdk/download/1.3.296.0/windows/VulkanRT-1.3.296.0-Installer.exe"
RUNTIME_SHA256="93a14d17f70bde4e5e2a4daf0af0e72b0fcb9693d84757ffd08255e5828d28ca"
MESA_URL="https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z"
MESA_SHA256="351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695"
SEVEN_ZIP="/c/Program Files/7-Zip/7z.exe"
LOADER="/c/Windows/System32/vulkan-1.dll"

work="${RUNNER_TEMP:-/tmp}/vulkan"
mkdir -p "$work"

# download <url> <sha256> <output>: fetches a file and refuses it unless the checksum matches.
download() {
  curl --fail --silent --show-error --location --output "$3" "$1"
  echo "$2 *$3" | sha256sum --check --status || {
    echo "checksum mismatch for $1" >&2
    exit 1
  }
}

download "$RUNTIME_URL" "$RUNTIME_SHA256" "$work/runtime.exe"
"$work/runtime.exe" /S
test -f "$LOADER" || { echo "the Vulkan runtime did not install $LOADER" >&2; exit 1; }

download "$MESA_URL" "$MESA_SHA256" "$work/mesa.7z"
"$SEVEN_ZIP" x -y "-o$work/mesa" "$work/mesa.7z" > /dev/null
icd="$work/mesa/x64/lvp_icd.x86_64.json"
test -f "$icd" || { echo "lavapipe not found in the Mesa package" >&2; exit 1; }

windows_icd="$(cygpath -w "$icd")"
echo "using $windows_icd"
if [[ -n "${GITHUB_ENV:-}" ]]; then
  echo "VK_DRIVER_FILES=$windows_icd" >> "$GITHUB_ENV"
fi
