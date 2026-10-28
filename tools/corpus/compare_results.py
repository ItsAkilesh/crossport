#!/usr/bin/env python3
"""File: tools/corpus/compare_results.py
Purpose: Compare Linux and translated Windows results per app test, summarise the pass rate and enforce the ratchet
Author: Akilesh
Date: 2026-10-28

Usage: compare_results.py --results DIR --apps NAME... --difftest PATH [--baseline FILE] [--summary FILE]

A test passes when its Linux and Windows results match. A translation that failed leaves
<app>-translate-error.txt, which marks all of that app's tests failed with the reason.
The ratchet: every test listed in the baseline must still pass; new passes are reported so they
can be added to the baseline.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from run_app_tests import load_app

REASON_LINES = 4


@dataclass(frozen=True)
class Outcome:
    name: str
    is_pass: bool
    reason: str = ""


def translate_error(results: Path, app: str) -> str | None:
    marker = results / f"{app}-translate-error.txt"
    return marker.read_text(encoding="utf-8").strip().splitlines()[0] if marker.exists() and marker.stat().st_size else None


def compare_pair(difftest: Path, linux: Path, windows: Path) -> Outcome | tuple[bool, str]:
    completed = subprocess.run([str(difftest), "compare", str(linux), str(windows)], capture_output=True, text=True)
    detail = " ".join(completed.stderr.strip().splitlines()[:REASON_LINES])
    return completed.returncode == 0, detail


def evaluate(app_dir: Path, results: Path, difftest: Path) -> list[Outcome]:
    app, tests = load_app(app_dir)
    blocked = translate_error(results, app)
    outcomes = []
    for test in tests:
        name = f"{app}/{test.id}"
        linux = results / f"{app}-{test.id}-linux.json"
        windows = results / f"{app}-{test.id}-windows.json"
        if not linux.exists():
            outcomes.append(Outcome(name, False, "no Linux reference result"))
        elif blocked is not None:
            outcomes.append(Outcome(name, False, f"translation failed: {blocked}"))
        elif not windows.exists():
            outcomes.append(Outcome(name, False, "no Windows result"))
        else:
            is_match, detail = compare_pair(difftest, linux, windows)
            outcomes.append(Outcome(name, is_match, "" if is_match else detail))
    return outcomes


def ratchet(outcomes: list[Outcome], baseline: set[str]) -> tuple[list[str], list[str]]:
    """Returns (regressions: baseline tests that fail now, improvements: passes not yet in the baseline)."""
    passing = {o.name for o in outcomes if o.is_pass}
    known = {o.name for o in outcomes}
    regressions = sorted(name for name in baseline if name not in passing or name not in known)
    improvements = sorted(passing - baseline)
    return regressions, improvements


def pass_rate(outcomes: list[Outcome]) -> float:
    return sum(o.is_pass for o in outcomes) / len(outcomes) if outcomes else 0.0


def summarise(outcomes: list[Outcome]) -> str:
    lines = [f"{sum(o.is_pass for o in outcomes)} of {len(outcomes)} tests pass ({pass_rate(outcomes):.1%})"]
    apps = sorted({o.name.split("/")[0] for o in outcomes})
    for app in apps:
        mine = [o for o in outcomes if o.name.startswith(app + "/")]
        lines.append(f"  {app}: {sum(o.is_pass for o in mine)}/{len(mine)}")
    for outcome in outcomes:
        if not outcome.is_pass:
            lines.append(f"  FAIL {outcome.name}: {outcome.reason[:300]}")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--apps", nargs="+", required=True)
    parser.add_argument("--difftest", type=Path, required=True)
    parser.add_argument("--apps-root", type=Path, default=Path("tests/corpus/apps"))
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--summary", type=Path)
    arguments = parser.parse_args(argv[1:])
    outcomes = [o for app in arguments.apps for o in evaluate(arguments.apps_root / app, arguments.results, arguments.difftest)]
    print(summarise(outcomes))
    if arguments.summary:
        arguments.summary.write_text(json.dumps([o.__dict__ for o in outcomes], indent=2) + "\n", encoding="utf-8")
    if arguments.baseline is None:
        return 0
    baseline = set(json.loads(arguments.baseline.read_text(encoding="utf-8"))["passing"])
    regressions, improvements = ratchet(outcomes, baseline)
    if improvements:
        print(f"new passes to add to the baseline: {improvements}")
    if regressions:
        print(f"REGRESSIONS against the baseline: {regressions}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
