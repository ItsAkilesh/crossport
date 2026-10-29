"""File: tools/ci/test_symbolize.py
Purpose: Tests for tools/ci/symbolize.py: nm parsing, symbol lookup by offset, and frame extraction from a crash report
Author: Akilesh
Date: 2026-10-29
"""

from __future__ import annotations

import unittest

import symbolize

OBJDUMP_OUTPUT = """Characteristics 0x2022
ImageBase		0000000180000000
SectionAlignment	00001000
"""

NM_OUTPUT = """                 U undefined_function
0000000180001000 T first_function
0000000180001100 t second_function
0000000180001200 D some_data
0000000180002000 T third_function
"""


class SymbolizeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.image_base = symbolize.parse_image_base(OBJDUMP_OUTPUT)
        self.symbols = symbolize.parse_symbols(NM_OUTPUT)

    def test_parsing_finds_the_image_base(self) -> None:
        self.assertEqual(self.image_base, 0x180000000)
        with self.assertRaises(ValueError):
            symbolize.parse_image_base("no such line")

    def test_parsing_keeps_only_addressed_code_symbols(self) -> None:
        self.assertEqual([name for _, name in self.symbols], ["first_function", "second_function", "third_function"])

    def test_an_offset_resolves_to_the_symbol_before_it(self) -> None:
        self.assertEqual(symbolize.resolve(self.image_base, self.symbols, 0x1104), "second_function+0x4")
        self.assertEqual(symbolize.resolve(self.image_base, self.symbols, 0x1000), "first_function+0x0")

    def test_an_offset_before_every_symbol_stays_bare(self) -> None:
        self.assertEqual(symbolize.resolve(self.image_base, self.symbols, 0x10), "0x10")

    def test_only_shim_frames_are_reported(self) -> None:
        report = "crossport: access violation at ucrtbase.dll+0x4e2e8, reading 0x0\n  from crossport_libc.dll+0x2010\n  from app.exe+0x2010\n"
        lines = symbolize.symbolize(report, self.image_base, self.symbols)
        self.assertEqual(lines, ["crossport_libc.dll+0x2010 = third_function+0x10"])

    def test_a_report_without_shim_frames_gives_nothing(self) -> None:
        self.assertEqual(symbolize.symbolize("no frames here", self.image_base, self.symbols), [])


if __name__ == "__main__":
    unittest.main()
