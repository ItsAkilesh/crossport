"""File: tools/test_check_abort.py
Purpose: Tests for tools/check_abort.py: it must accept a real abort and reject every way a run can fail to abort
Author: Akilesh
Date: 2026-10-28
"""

from __future__ import annotations

import unittest

import check_abort

MESSAGE = "stack smashing detected"


def aborted() -> dict[str, object]:
    return {"exit_code": 134, "stdout": "before the overflow\n", "stderr": f"*** {MESSAGE} ***: terminated\n"}


class CheckAbortTest(unittest.TestCase):
    def test_a_genuine_abort_passes(self) -> None:
        self.assertEqual(check_abort.check(aborted(), MESSAGE), [])

    def test_a_normal_exit_is_rejected(self) -> None:
        result = aborted() | {"exit_code": 0}
        self.assertTrue(any("exit code" in p for p in check_abort.check(result, MESSAGE)))

    def test_a_missing_message_is_rejected(self) -> None:
        result = aborted() | {"stderr": ""}
        self.assertTrue(any("stderr" in p for p in check_abort.check(result, MESSAGE)))

    def test_continuing_past_the_overflow_is_rejected(self) -> None:
        result = aborted() | {"stdout": "after the overflow: the stack protector failed\n"}
        self.assertTrue(any("continued" in p for p in check_abort.check(result, MESSAGE)))

    def test_a_signal_death_with_no_exit_code_is_rejected(self) -> None:
        result = aborted() | {"exit_code": None}
        self.assertTrue(check_abort.check(result, MESSAGE))


if __name__ == "__main__":
    unittest.main()
