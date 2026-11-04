#!/usr/bin/env python3
"""File: tools/gen_logo.py
Purpose: Generate the Crossport logo files: the abstract mark, the wordmarks for dark and light backgrounds, and the site favicon
Author: Akilesh
Date: 2026-11-04

Usage: python tools/gen_logo.py <inter-semibold.woff>
Needs: fontTools (pip install fonttools) and a copy of Inter SemiBold (SIL Open Font License 1.1, for example the
latin-600-normal file of the @fontsource/inter package). The letters of the wordmark are drawn as outlines from it,
so the wordmark looks the same on every machine. The mark itself needs no font.

The mark is three bars that cross a gate. Amber is Linux, blue is Windows, and the fuchsia line between them is the
shim. The bars on the right are not the bars on the left: what crosses comes out translated.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

MARK_SIZE = 64
BAR_HEIGHT = 9.0
BAR_RADIUS = BAR_HEIGHT / 2
# The right-hand bars sit half a row lower than the left-hand ones, so the mark reads as blocks in flight that change as
# they cross the gate, not as a menu icon. Both columns are centred as a pair on the canvas.
LEFT_ROW_TOPS = (6.75, 25.25, 43.75)
RIGHT_ROW_TOPS = (11.25, 29.75, 48.25)
# (x, width) of each bar. The left bars end at x = 28 and the right bars start at x = 36, either side of the gate.
LEFT_BARS = ((4.0, 24.0), (12.0, 16.0), (8.0, 20.0))
RIGHT_BARS = ((36.0, 20.0), (36.0, 24.0), (36.0, 16.0))
GATE = (30.5, 3.0, 3.0, 58.0)  # x, y, width, height
GATE_RADIUS = 1.5

WORDMARK_GAP = 20.0
WORDMARK_PADDING = 4.0
WORDMARK_SIZE = 40.0
TRACKING_EM = -0.01
WORDMARK_TEXT = "Crossport"

ROOT = Path(__file__).resolve().parent.parent
HEADER = "File: {name} | Purpose: {purpose} | Author: Akilesh | Date: 2026-11-04"


@dataclass(frozen=True)
class Palette:
    linux: str
    windows: str
    shim: str
    ink: str


# Mid tones that read on both a light and a dark page: the mark on its own, and the favicon.
NEUTRAL = Palette(linux="#F59E0B", windows="#0EA5E9", shim="#D946EF", ink="#0F172A")
FOR_DARK = Palette(linux="#F5A524", windows="#38BDF8", shim="#E879F9", ink="#F1F5F9")
FOR_LIGHT = Palette(linux="#D97706", windows="#0284C7", shim="#C026D3", ink="#0F172A")


def _number(value: float) -> str:
    return f"{value:g}"


def _rect(x: float, y: float, width: float, height: float, radius: float, fill: str) -> str:
    return (
        f'<rect x="{_number(x)}" y="{_number(y)}" width="{_number(width)}" height="{_number(height)}" '
        f'rx="{_number(radius)}" fill="{fill}"/>'
    )


def mark_elements(palette: Palette) -> list[str]:
    """The seven shapes of the mark, in drawing order: three left bars, three right bars, the gate."""
    elements = [_rect(x, top, width, BAR_HEIGHT, BAR_RADIUS, palette.linux) for (x, width), top in zip(LEFT_BARS, LEFT_ROW_TOPS)]
    elements += [_rect(x, top, width, BAR_HEIGHT, BAR_RADIUS, palette.windows) for (x, width), top in zip(RIGHT_BARS, RIGHT_ROW_TOPS)]
    gate_x, gate_y, gate_width, gate_height = GATE
    elements.append(_rect(gate_x, gate_y, gate_width, gate_height, GATE_RADIUS, palette.shim))
    return elements


def mark_svg(palette: Palette) -> str:
    header = HEADER.format(name="mark.svg", purpose="Crossport logo mark: bars crossing a gate, amber (Linux) to blue (Windows)")
    body = "\n".join(f"  {element}" for element in mark_elements(palette))
    return (
        f"<!-- {header} -->\n"
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {MARK_SIZE} {MARK_SIZE}" role="img" aria-label="Crossport">\n'
        f"{body}\n</svg>\n"
    )


def wordmark_svg(name: str, purpose: str, palette: Palette, text_path: str, text_width: float, baseline: float) -> str:
    """The mark and the outlined text side by side. `text_path` is drawn from x = 0, so it is shifted past the mark."""
    offset = MARK_SIZE + WORDMARK_GAP
    width = offset + text_width + WORDMARK_PADDING
    body = "\n".join(f"  {element}" for element in mark_elements(palette))
    return (
        f"<!-- {HEADER.format(name=name, purpose=purpose)} -->\n"
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {_number(round(width, 2))} {MARK_SIZE}" role="img" '
        f'aria-label="Crossport">\n{body}\n'
        f'  <path transform="translate({_number(offset)} {_number(round(baseline, 2))})" fill="{palette.ink}" d="{text_path}"/>\n'
        "</svg>\n"
    )


def text_outline(font_path: Path, text: str, size: float, tracking_em: float) -> tuple[str, float, float]:
    """Outlines of `text` from the font, as one SVG path starting at x = 0 on a baseline at y = 0.

    Returns the path, its width in the same units, and the font's cap height scaled to `size`.
    """
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont

    font = TTFont(font_path)
    glyphs = font.getGlyphSet()
    cmap = font.getBestCmap()
    scale = size / font["head"].unitsPerEm
    cursor = 0.0
    paths: list[str] = []
    for character in text:
        glyph = glyphs[cmap[ord(character)]]
        pen = SVGPathPen(glyphs, ntos=lambda value: f"{value:.2f}".rstrip("0").rstrip("."))
        glyph.draw(TransformPen(pen, (scale, 0, 0, -scale, cursor, 0)))
        paths.append(pen.getCommands())
        cursor += glyph.width * scale + tracking_em * size
    return "".join(paths), cursor - tracking_em * size, font["OS/2"].sCapHeight * scale


def write(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8", newline="\n")
    print(f"wrote {path.relative_to(ROOT)}")


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    path, width, cap_height = text_outline(Path(argv[1]), WORDMARK_TEXT, WORDMARK_SIZE, TRACKING_EM)
    baseline = MARK_SIZE / 2 + cap_height / 2
    write(ROOT / "docs/logo/mark.svg", mark_svg(NEUTRAL))
    write(ROOT / "site/public/favicon.svg", mark_svg(NEUTRAL).replace("mark.svg", "favicon.svg", 1))
    write(
        ROOT / "docs/logo/wordmark-dark.svg",
        wordmark_svg("wordmark-dark.svg", "Crossport wordmark for dark backgrounds", FOR_DARK, path, width, baseline),
    )
    write(
        ROOT / "docs/logo/wordmark-light.svg",
        wordmark_svg("wordmark-light.svg", "Crossport wordmark for light backgrounds", FOR_LIGHT, path, width, baseline),
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
