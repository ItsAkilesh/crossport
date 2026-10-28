"""File: tools/corpus/test_corpus_tools.py
Purpose: Tests for run_app_tests.py and compare_results.py: input validation, command building and the ratchet
Author: Akilesh
Date: 2026-10-28
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import compare_results
import run_app_tests

APPS_ROOT = Path(__file__).resolve().parents[2] / "tests" / "corpus" / "apps"


class ParseTestCase(unittest.TestCase):
    def test_a_complete_test_is_parsed(self) -> None:
        test = run_app_tests.parse_test(
            {"id": "a-1", "args": ["-e", "x"], "stdin": "inputs/in.txt", "files": {"s.lua": "inputs/s.lua"}, "timeout": 5}
        )
        self.assertEqual((test.id, test.args, test.stdin, test.timeout), ("a-1", ("-e", "x"), "inputs/in.txt", 5))

    def test_unsafe_or_malformed_tests_are_rejected(self) -> None:
        bad_tests = [
            {"id": "Upper"},
            {"id": "x", "args": "not-a-list"},
            {"id": "x", "args": [1]},
            {"id": "x", "stdin": "../outside"},
            {"id": "x", "stdin": "/abs/path"},
            {"id": "x", "stdin": "C:\\evil"},
            {"id": "x", "files": {"dir/name": "inputs/a"}},
            {"id": "x", "files": {"..": "inputs/a"}},
            {"id": "x", "files": {"ok": "../escape"}},
            {"id": "x", "timeout": 0},
            {"id": "x", "timeout": "soon"},
            "not an object",
        ]
        for bad in bad_tests:
            with self.assertRaises(ValueError, msg=str(bad)):
                run_app_tests.parse_test(bad)

    def test_every_checked_in_app_definition_is_valid_and_its_inputs_exist(self) -> None:
        for app_dir in sorted(p for p in APPS_ROOT.iterdir() if (p / "app.json").exists()):
            name, tests = run_app_tests.load_app(app_dir)
            self.assertTrue(tests, name)
            for test in tests:
                for source in [*test.files.values(), *([test.stdin] if test.stdin else [])]:
                    self.assertTrue((app_dir / source).is_file(), f"{name}/{test.id}: missing {source}")

    def test_the_difftest_command_carries_every_option_before_the_separator(self) -> None:
        test = run_app_tests.AppTest("t", ("a", "--timeout", "9"), "inputs/in", {}, 7)
        command = run_app_tests.difftest_command(Path("dt"), Path("app"), test, Path("apps/x"), Path("sb"), Path("out.json"))
        separator = command.index("--")
        self.assertEqual(command[separator + 1 :], ["app", "a", "--timeout", "9"])
        self.assertIn("--stdin", command[:separator])
        self.assertEqual(command[command.index("--timeout") + 1], "7", "the program's own --timeout is not confused with ours")


class RatchetTestCase(unittest.TestCase):
    def outcomes(self, passing: set[str], failing: set[str]) -> list[compare_results.Outcome]:
        return [compare_results.Outcome(n, True) for n in sorted(passing)] + [compare_results.Outcome(n, False, "x") for n in sorted(failing)]

    def test_a_test_that_stops_passing_is_a_regression(self) -> None:
        regressions, improvements = compare_results.ratchet(self.outcomes({"a/1"}, {"a/2"}), {"a/1", "a/2"})
        self.assertEqual((regressions, improvements), (["a/2"], []))

    def test_new_passes_are_reported_but_never_fail_the_run(self) -> None:
        regressions, improvements = compare_results.ratchet(self.outcomes({"a/1", "a/2"}, set()), {"a/1"})
        self.assertEqual((regressions, improvements), ([], ["a/2"]))

    def test_a_baseline_test_that_no_longer_exists_counts_as_a_regression(self) -> None:
        regressions, _ = compare_results.ratchet(self.outcomes({"a/1"}, set()), {"a/1", "a/gone"})
        self.assertEqual(regressions, ["a/gone"])

    def test_the_pass_rate_handles_the_empty_case(self) -> None:
        self.assertEqual(compare_results.pass_rate([]), 0.0)
        self.assertAlmostEqual(compare_results.pass_rate(self.outcomes({"a/1"}, {"a/2", "a/3", "a/4"})), 0.25)

    def test_the_summary_lists_failures_with_reasons(self) -> None:
        text = compare_results.summarise(self.outcomes({"lua/ok"}, {"lua/bad"}))
        self.assertIn("1 of 2 tests pass (50.0%)", text)
        self.assertIn("FAIL lua/bad: x", text)


class TranslateErrorTestCase(unittest.TestCase):
    def test_a_marker_file_blocks_the_whole_app_with_its_first_line(self) -> None:
        directory = Path(tempfile.mkdtemp(prefix="crossport-results-"))
        (directory / "lua-translate-error.txt").write_text("symbol printf is not provided\nmore detail\n", encoding="utf-8")
        self.assertEqual(compare_results.translate_error(directory, "lua"), "symbol printf is not provided")
        self.assertIsNone(compare_results.translate_error(directory, "jq"))


if __name__ == "__main__":
    unittest.main()
