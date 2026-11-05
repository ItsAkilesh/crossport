#!/usr/bin/env bash
# File: tests/corpus/apps/sdl2_tests/build.sh
# Purpose: Build SDL 2.30.9 with its own test harness (testautomation) and install both into an AppDir
# Author: Akilesh
# Date: 2026-11-04
#
# Usage: build.sh <source-dir> <appdir>
# SDL is built as a shared library with only the X11 video driver, so the AppImage needs libX11 and nothing else
# from the host. The harness finds the library through an $ORIGIN rpath, like a real AppImage does.
# Needs: cmake, libx11-dev, libxext-dev.

set -euo pipefail
src="$1"
appdir="$2"
build="$src/build-crossport"

cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DSDL_TESTS=ON -DSDL_INSTALL_TESTS=OFF -DSDL_STATIC=OFF \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib' \
  -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF \
  -DSDL_SNDIO=OFF -DSDL_OSS=OFF -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_HIDAPI=OFF \
  -DSDL_VULKAN=OFF -DSDL_X11_XCURSOR=OFF -DSDL_X11_XINPUT=OFF -DSDL_X11_XFIXES=OFF -DSDL_X11_XRANDR=OFF \
  -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XSHAPE=OFF -DSDL_X11_XDBE=OFF > /dev/null
cmake --build "$build" --target SDL2 testautomation -j"$(nproc)" > /dev/null

install -D "$build/test/testautomation" "$appdir/usr/bin/testautomation"
lib="$(find "$build" -name 'libSDL2-2.0.so*' -type f | head -n 1)"
[[ -n "$lib" ]] || { echo "the SDL build produced no library" >&2; find "$build" -name '*.so*' >&2; exit 1; }
install -D "$lib" "$appdir/usr/lib/libSDL2-2.0.so.0"
