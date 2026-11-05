"""File: tools/ci/test_check_alertable.py
Purpose: Tests for the alertable-wait check that protects translated code's red zone
Author: Akilesh
Date: 2026-11-05
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_alertable  # noqa: E402


class Scan(unittest.TestCase):
    def found(self, source: str) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "x.c").write_text(source, encoding="utf-8")
            return check_alertable.scan([Path(directory)])

    def test_an_alertable_wait_is_reported_with_its_line(self) -> None:
        found = self.found("void f(void)\n{\n    SleepEx(10, TRUE);\n}\n")
        self.assertEqual(len(found), 1)
        self.assertIn("x.c:3: SleepEx", found[0])

    def test_a_plain_wait_is_fine(self) -> None:
        self.assertEqual(self.found("void f(void) { Sleep(10); WaitForSingleObject(h, 5); }"), [])

    def test_comments_and_strings_are_ignored(self) -> None:
        source = '/* SleepEx is forbidden */\n// QueueUserAPC too\nconst char *s = "WSASend";\n'
        self.assertEqual(self.found(source), [])

    def test_a_longer_name_is_not_mistaken_for_a_forbidden_one(self) -> None:
        self.assertEqual(self.found("void f(void) { MySleepExample(); WSASendDisconnect(s); }"), [])

    def test_every_forbidden_name_is_caught(self) -> None:
        for name in check_alertable.FORBIDDEN:
            self.assertEqual(len(self.found(f"void f(void) {{ {name}(0); }}")), 1, name)

    def test_only_c_sources_and_headers_are_scanned(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "notes.txt").write_text("SleepEx", encoding="utf-8")
            Path(directory, "sub").mkdir()
            Path(directory, "sub", "y.h").write_text("#define W SuspendThread", encoding="utf-8")
            self.assertEqual(len(check_alertable.scan([Path(directory)])), 1)


if __name__ == "__main__":
    unittest.main()
