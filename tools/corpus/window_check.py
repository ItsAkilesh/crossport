#!/usr/bin/env python3
"""File: tools/corpus/window_check.py
Purpose: Decide whether the window dumps of a run (PPM files from CROSSPORT_DUMP_WINDOWS) show a drawn window rather than a blank one
Author: Akilesh
Date: 2026-11-06

Usage: window_check.py DIRECTORY [--min-width N] [--min-height N]
Exits 0 when at least one window of the minimum size has drawn contents, 1 otherwise, and writes a PNG beside every
PPM so a person can look at what the test saw.

A window counts as drawn when it uses several colours and a real share of its pixels differ from its most common
colour. A toolkit window that never painted is one flat colour; a window with text, icons or controls is not.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

MIN_COLORS = 8
MIN_INK_SHARE = 0.01
DEFAULT_MIN_WIDTH = 200
DEFAULT_MIN_HEIGHT = 150
MAX_PIXELS = 16 * 1024 * 1024
CHANNELS = 3
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


@dataclass(frozen=True)
class WindowImage:
    name: str
    width: int
    height: int
    rgb: bytes


@dataclass(frozen=True)
class WindowReport:
    name: str
    width: int
    height: int
    colors: int
    ink_share: float

    @property
    def is_drawn(self) -> bool:
        return self.colors >= MIN_COLORS and self.ink_share >= MIN_INK_SHARE

    def describe(self) -> str:
        verdict = "drawn" if self.is_drawn else "blank"
        return f"{self.name}: {self.width}x{self.height}, {self.colors} colors, {self.ink_share:.1%} not background: {verdict}"


def parse_ppm(name: str, data: bytes) -> WindowImage:
    """Reads a binary PPM (P6, 8 bits per channel) as the shim writes it."""
    fields: list[bytes] = []
    position = 0
    while len(fields) < 4:
        while position < len(data) and data[position:position + 1].isspace():
            position += 1
        start = position
        while position < len(data) and not data[position:position + 1].isspace():
            position += 1
        if start == position:
            raise ValueError(f"{name}: the header is cut short")
        fields.append(data[start:position])
    position += 1
    magic, width_text, height_text, depth_text = fields
    if magic != b"P6" or depth_text != b"255":
        raise ValueError(f"{name}: not an 8-bit P6 image")
    width, height = int(width_text), int(height_text)
    if width <= 0 or height <= 0 or width * height > MAX_PIXELS:
        raise ValueError(f"{name}: unreasonable size {width}x{height}")
    rgb = data[position:]
    if len(rgb) != width * height * CHANNELS:
        raise ValueError(f"{name}: {len(rgb)} pixel bytes for {width}x{height}")
    return WindowImage(name=name, width=width, height=height, rgb=rgb)


def analyze(image: WindowImage) -> WindowReport:
    red, green, blue = image.rgb[0::CHANNELS], image.rgb[1::CHANNELS], image.rgb[2::CHANNELS]
    counts = Counter(zip(red, green, blue))
    pixels = image.width * image.height
    most_common = counts.most_common(1)[0][1]
    return WindowReport(
        name=image.name, width=image.width, height=image.height, colors=len(counts), ink_share=(pixels - most_common) / pixels
    )


def encode_png(image: WindowImage) -> bytes:
    """A PNG of the image (no filtering: these pictures are for people to look at, not to be small)."""
    stride = image.width * CHANNELS
    raw = b"".join(b"\x00" + image.rgb[row * stride:(row + 1) * stride] for row in range(image.height))

    def chunk(kind: bytes, body: bytes) -> bytes:
        checksum = zlib.crc32(kind + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", checksum)

    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 2, 0, 0, 0)
    return PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def load_images(directory: Path) -> list[WindowImage]:
    images = []
    for path in sorted(directory.glob("window-*.ppm")):
        images.append(parse_ppm(path.name, path.read_bytes()))
    return images


def check_directory(directory: Path, min_width: int, min_height: int) -> tuple[bool, list[str]]:
    """Returns whether a big enough window is drawn, and one line per window saying why."""
    if not directory.is_dir():
        return False, [f"no window dump directory {directory}"]
    images = load_images(directory)
    lines = []
    has_drawn_window = False
    for image in images:
        (directory / f"{Path(image.name).stem}.png").write_bytes(encode_png(image))
        report = analyze(image)
        is_big = image.width >= min_width and image.height >= min_height
        has_drawn_window = has_drawn_window or (is_big and report.is_drawn)
        lines.append(report.describe() + ("" if is_big else " (too small to count)"))
    if not images:
        lines.append("the program mapped no window that painted")
    elif not has_drawn_window:
        lines.append(f"no window of at least {min_width}x{min_height} has drawn contents")
    return has_drawn_window, lines


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--min-width", type=int, default=DEFAULT_MIN_WIDTH)
    parser.add_argument("--min-height", type=int, default=DEFAULT_MIN_HEIGHT)
    arguments = parser.parse_args(argv[1:])
    is_ok, lines = check_directory(arguments.directory, arguments.min_width, arguments.min_height)
    print("\n".join(lines))
    return 0 if is_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
