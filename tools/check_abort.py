#!/usr/bin/env python3
"""File: tools/check_abort.py
Purpose: Assert that a recorded run aborted the way a stack-protector failure must: exit 134 and a message on stderr
Author: Akilesh
Date: 2026-10-28

Usage: python tools/check_abort.py <result.json> <expected stderr substring>
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ABORT_EXIT_CODE = 134  # 128 + SIGABRT, as a shell reports it


def check(result: dict[str, object], expected_message: str) -> list[str]:
    """Returns a list of problems; empty means the run aborted as required."""
    problems = []
    if result.get("exit_code") != ABORT_EXIT_CODE:
        problems.append(f"exit code {result.get('exit_code')!r}, expected {ABORT_EXIT_CODE}")
    stderr = result.get("stderr")
    if not isinstance(stderr, str) or expected_message not in stderr:
        problems.append(f"stderr {stderr!r} does not contain {expected_message!r}")
    stdout = result.get("stdout")
    if isinstance(stdout, str) and "failed" in stdout:
        problems.append("the program continued past the overflow")
    return problems


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    result = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
    problems = check(result, argv[2])
    for problem in problems:
        print(f"check_abort: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
