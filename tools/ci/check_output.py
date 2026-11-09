#!/usr/bin/env python3
"""File: tools/ci/check_output.py
Purpose: Fail unless a recorded run exited with 0 and its output contains every expected line
Author: Akilesh
Date: 2026-11-09

Usage: check_output.py <result.json> <expected line>...
A result is what `difftest run` writes. Each expected line must appear in standard output as a whole line.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


def missing_lines(stdout: str, expected: list[str]) -> list[str]:
    lines = set(stdout.splitlines())
    return [line for line in expected if line not in lines]


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    result = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
    if result.get("exit_code") != 0:
        print(f"check_output: the program exited with {result.get('exit_code')}: {result.get('stderr', '')[:500]}", file=sys.stderr)
        return 1
    absent = missing_lines(result.get("stdout", ""), argv[2:])
    for line in absent:
        print(f"check_output: missing the line {line!r}; the output was:\n{result.get('stdout', '')}", file=sys.stderr)
    return 1 if absent else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
