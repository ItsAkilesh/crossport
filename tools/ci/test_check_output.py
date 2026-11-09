"""File: tools/ci/test_check_output.py
Purpose: Tests for the output check that CI uses on Windows-only cases
Author: Akilesh
Date: 2026-11-09
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_output  # noqa: E402


class CheckOutput(unittest.TestCase):
    def run_check(self, result: dict, expected: list[str]) -> int:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, "r.json")
            path.write_text(json.dumps(result), encoding="utf-8")
            return check_output.main(["check_output.py", str(path), *expected])

    def test_every_expected_line_present_passes(self) -> None:
        self.assertEqual(self.run_check({"exit_code": 0, "stdout": "a\nb: 1\ndone\n"}, ["b: 1", "done"]), 0)

    def test_a_missing_line_or_a_partial_line_fails(self) -> None:
        self.assertEqual(self.run_check({"exit_code": 0, "stdout": "a\nb: 12\n"}, ["b: 1"]), 1)
        self.assertEqual(self.run_check({"exit_code": 0, "stdout": "a\n"}, ["z"]), 1)

    def test_a_failed_program_fails_whatever_it_printed(self) -> None:
        self.assertEqual(self.run_check({"exit_code": 3, "stdout": "done\n"}, ["done"]), 1)

    def test_missing_lines_lists_only_the_absent_ones(self) -> None:
        self.assertEqual(check_output.missing_lines("a\nb\n", ["a", "c"]), ["c"])


if __name__ == "__main__":
    unittest.main()
