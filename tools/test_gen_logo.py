"""File: tools/test_gen_logo.py
Purpose: Tests for tools/gen_logo.py: the mark's geometry, its palettes, and the assembly of the wordmark
Author: Akilesh
Date: 2026-11-04
"""

from __future__ import annotations

import os
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import gen_logo

SVG = "{http://www.w3.org/2000/svg}"


def rectangles(svg: str) -> list[dict[str, str]]:
    return [element.attrib for element in ET.fromstring(svg).iter(f"{SVG}rect")]


class MarkTest(unittest.TestCase):
    def test_it_is_three_left_bars_three_right_bars_and_a_gate(self) -> None:
        shapes = rectangles(gen_logo.mark_svg(gen_logo.NEUTRAL))
        colours = [shape["fill"] for shape in shapes]
        self.assertEqual(colours, [gen_logo.NEUTRAL.linux] * 3 + [gen_logo.NEUTRAL.windows] * 3 + [gen_logo.NEUTRAL.shim])

    def test_bars_stay_on_their_side_of_the_gate_and_inside_the_canvas(self) -> None:
        gate_left, _, gate_width, _ = gen_logo.GATE
        for x, width in gen_logo.LEFT_BARS:
            self.assertLess(x + width, gate_left)
            self.assertGreaterEqual(x, 0)
        for x, width in gen_logo.RIGHT_BARS:
            self.assertGreater(x, gate_left + gate_width)
            self.assertLessEqual(x + width, gen_logo.MARK_SIZE)
        for top in (*gen_logo.LEFT_ROW_TOPS, *gen_logo.RIGHT_ROW_TOPS):
            self.assertGreaterEqual(top, 0)
            self.assertLessEqual(top + gen_logo.BAR_HEIGHT, gen_logo.MARK_SIZE)

    def test_the_pair_of_columns_is_centred_vertically(self) -> None:
        top = min(gen_logo.LEFT_ROW_TOPS)
        bottom = max(gen_logo.RIGHT_ROW_TOPS) + gen_logo.BAR_HEIGHT
        self.assertAlmostEqual((top + bottom) / 2, gen_logo.MARK_SIZE / 2)

    def test_what_crosses_the_gate_comes_out_changed(self) -> None:
        left = [width for _, width in gen_logo.LEFT_BARS]
        right = [width for _, width in gen_logo.RIGHT_BARS]
        self.assertNotEqual(left, right)

    def test_the_mark_is_a_valid_square_svg_with_a_name(self) -> None:
        root = ET.fromstring(gen_logo.mark_svg(gen_logo.NEUTRAL))
        self.assertEqual(root.attrib["viewBox"], "0 0 64 64")
        self.assertEqual(root.attrib["aria-label"], "Crossport")

    def test_light_and_dark_palettes_differ_so_each_reads_on_its_background(self) -> None:
        self.assertNotEqual(gen_logo.FOR_DARK, gen_logo.FOR_LIGHT)
        self.assertNotEqual(gen_logo.FOR_DARK.ink, gen_logo.FOR_LIGHT.ink)


class WordmarkTest(unittest.TestCase):
    def build(self, width: float) -> ET.Element:
        return ET.fromstring(gen_logo.wordmark_svg("w.svg", "test", gen_logo.FOR_DARK, "M0 0L1 1Z", width, 46.0))

    def test_it_is_as_wide_as_the_mark_the_gap_the_text_and_the_padding(self) -> None:
        box = self.build(120.0).attrib["viewBox"].split()
        expected = gen_logo.MARK_SIZE + gen_logo.WORDMARK_GAP + 120.0 + gen_logo.WORDMARK_PADDING
        self.assertEqual(float(box[2]), expected)
        self.assertEqual(float(box[3]), gen_logo.MARK_SIZE)

    def test_the_text_is_drawn_past_the_mark_in_the_palette_ink(self) -> None:
        path = next(self.build(120.0).iter(f"{SVG}path"))
        self.assertEqual(path.attrib["fill"], gen_logo.FOR_DARK.ink)
        self.assertTrue(path.attrib["transform"].startswith(f"translate({gen_logo.MARK_SIZE + gen_logo.WORDMARK_GAP:g} "))

    def test_it_repeats_the_marks_seven_shapes(self) -> None:
        self.assertEqual(len(list(self.build(120.0).iter(f"{SVG}rect"))), 7)


@unittest.skipUnless(os.environ.get("CROSSPORT_LOGO_FONT"), "set CROSSPORT_LOGO_FONT to an Inter SemiBold file to test outlining")
class OutlineTest(unittest.TestCase):
    def test_outlining_is_deterministic_and_the_text_has_width(self) -> None:
        font = Path(os.environ["CROSSPORT_LOGO_FONT"])
        first = gen_logo.text_outline(font, "Crossport", 40.0, -0.01)
        self.assertEqual(first, gen_logo.text_outline(font, "Crossport", 40.0, -0.01))
        self.assertGreater(first[1], 100.0)
        self.assertTrue(first[0].startswith("M"))


if __name__ == "__main__":
    unittest.main()
