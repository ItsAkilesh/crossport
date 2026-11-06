"""File: tools/corpus/test_window_check.py
Purpose: Tests for window_check.py (PPM parsing, the blank-or-drawn decision, PNG output) and its use by run_app_tests.py
Author: Akilesh
Date: 2026-11-06
"""

from __future__ import annotations

import tempfile
import unittest
import zlib
from pathlib import Path

import run_app_tests
import window_check

WIDTH = 240
HEIGHT = 160


def ppm(width: int, height: int, pixel_at) -> bytes:
    body = bytearray()
    for y in range(height):
        for x in range(width):
            body += bytes(pixel_at(x, y))
    return f"P6\n{width} {height}\n255\n".encode() + bytes(body)


def flat(_x: int, _y: int) -> tuple[int, int, int]:
    return (236, 236, 236)


def text_like(x: int, y: int) -> tuple[int, int, int]:
    """A grey background with a block of dark, anti-aliased looking strokes."""
    if 20 <= y < 60 and 20 <= x < 200:
        shade = 40 + (x * 7 + y * 3) % 150
        return (shade, shade, shade)
    return (236, 236, 236)


class ParsePpmTests(unittest.TestCase):
    def test_reads_what_the_shim_writes(self) -> None:
        image = window_check.parse_ppm("w.ppm", ppm(3, 2, lambda x, y: (x, y, 9)))
        self.assertEqual((image.width, image.height), (3, 2))
        self.assertEqual(image.rgb[:3], bytes([0, 0, 9]))

    def test_pixel_bytes_that_look_like_white_space_are_data(self) -> None:
        image = window_check.parse_ppm("w.ppm", ppm(2, 1, lambda x, y: (10, 10, 10)))
        self.assertEqual(image.rgb, bytes([10] * 6))

    def test_rejects_a_wrong_length_a_wrong_magic_and_a_cut_header(self) -> None:
        good = ppm(2, 2, flat)
        for bad in (good[:-1], b"P3" + good[2:], b"P6\n2"):
            with self.assertRaises(ValueError):
                window_check.parse_ppm("w.ppm", bad)

    def test_rejects_an_absurd_size(self) -> None:
        with self.assertRaises(ValueError):
            window_check.parse_ppm("w.ppm", b"P6\n100000 100000\n255\n")


class AnalyzeTests(unittest.TestCase):
    def test_a_flat_window_is_blank(self) -> None:
        report = window_check.analyze(window_check.parse_ppm("flat", ppm(WIDTH, HEIGHT, flat)))
        self.assertEqual(report.colors, 1)
        self.assertFalse(report.is_drawn)

    def test_a_window_with_strokes_is_drawn(self) -> None:
        report = window_check.analyze(window_check.parse_ppm("text", ppm(WIDTH, HEIGHT, text_like)))
        self.assertTrue(report.is_drawn)
        self.assertGreater(report.ink_share, window_check.MIN_INK_SHARE)

    def test_a_few_stray_pixels_are_not_a_drawn_window(self) -> None:
        def speckle(x: int, y: int) -> tuple[int, int, int]:
            return (0, 0, 0) if (x, y) in {(1, 1), (2, 2), (3, 3)} else (255, 255, 255)

        report = window_check.analyze(window_check.parse_ppm("speckle", ppm(WIDTH, HEIGHT, speckle)))
        self.assertFalse(report.is_drawn)


class PngTests(unittest.TestCase):
    def test_the_png_has_a_header_and_decodes_to_the_same_pixels(self) -> None:
        image = window_check.parse_ppm("t", ppm(4, 3, lambda x, y: (x * 40, y * 60, 7)))
        png = window_check.encode_png(image)
        self.assertTrue(png.startswith(window_check.PNG_SIGNATURE))
        data_start = png.index(b"IDAT") + 4
        data_length = int.from_bytes(png[data_start - 8:data_start - 4], "big")
        raw = zlib.decompress(png[data_start:data_start + data_length])
        stride = 4 * window_check.CHANNELS
        rows = [raw[i * (stride + 1) + 1:(i + 1) * (stride + 1)] for i in range(3)]
        self.assertEqual(b"".join(rows), image.rgb)


class CheckDirectoryTests(unittest.TestCase):
    def check(self, files: dict[str, bytes]) -> tuple[bool, list[str], Path]:
        directory = Path(tempfile.mkdtemp(prefix="crossport-windows-"))
        for name, data in files.items():
            (directory / name).write_bytes(data)
        is_ok, lines = window_check.check_directory(directory, 200, 150)
        return is_ok, lines, directory

    def test_a_drawn_window_passes_and_gets_a_png(self) -> None:
        is_ok, _lines, directory = self.check({"window-1.ppm": ppm(WIDTH, HEIGHT, text_like)})
        self.assertTrue(is_ok)
        self.assertTrue((directory / "window-1.png").is_file())

    def test_a_blank_window_fails(self) -> None:
        is_ok, lines, _ = self.check({"window-1.ppm": ppm(WIDTH, HEIGHT, flat)})
        self.assertFalse(is_ok)
        self.assertIn("no window of at least 200x150 has drawn contents", lines[-1])

    def test_a_small_drawn_window_does_not_count(self) -> None:
        is_ok, lines, _ = self.check({"window-1.ppm": ppm(60, 40, lambda x, y: (x * 4, y * 5, 1))})
        self.assertFalse(is_ok)
        self.assertIn("too small to count", lines[0])

    def test_one_drawn_window_among_blank_ones_is_enough(self) -> None:
        is_ok, _, _ = self.check({"window-1.ppm": ppm(WIDTH, HEIGHT, flat), "window-2.ppm": ppm(WIDTH, HEIGHT, text_like)})
        self.assertTrue(is_ok)

    def test_no_dump_files_and_no_directory_fail(self) -> None:
        is_ok, lines, directory = self.check({})
        self.assertFalse(is_ok)
        self.assertIn("mapped no window", lines[0])
        self.assertFalse(window_check.check_directory(directory / "missing", 1, 1)[0])


class RunAppTestsIntegrationTests(unittest.TestCase):
    def test_window_check_is_parsed_with_defaults_and_validated(self) -> None:
        test = run_app_tests.parse_test({"id": "gui", "window_check": {}})
        self.assertEqual(test.window_size, (window_check.DEFAULT_MIN_WIDTH, window_check.DEFAULT_MIN_HEIGHT))
        self.assertIsNone(run_app_tests.parse_test({"id": "plain"}).window_size)
        for bad in ({"min_width": 0}, {"min_height": "tall"}, "yes"):
            with self.assertRaises(ValueError):
                run_app_tests.parse_test({"id": "gui", "window_check": bad})

    def test_only_the_windows_run_is_checked_and_it_gets_the_dump_directory(self) -> None:
        test = run_app_tests.parse_test({"id": "gui", "window_check": {"min_width": 300}})
        self.assertTrue(run_app_tests.wants_window_check(test, "windows"))
        self.assertFalse(run_app_tests.wants_window_check(test, "linux"))
        dumps = Path(tempfile.mkdtemp(prefix="crossport-dump-"))
        command = run_app_tests.difftest_command(Path("difftest"), Path("app.exe"), test, Path("app"), Path("sb"), Path("out.json"), dumps)
        self.assertIn(f"CROSSPORT_DUMP_WINDOWS={dumps}", command)
        plain = run_app_tests.difftest_command(Path("difftest"), Path("app.exe"), test, Path("app"), Path("sb"), Path("out.json"))
        self.assertFalse(any("CROSSPORT_DUMP_WINDOWS" in part for part in plain))


if __name__ == "__main__":
    unittest.main()
