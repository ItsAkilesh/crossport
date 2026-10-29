#!/usr/bin/env python3
"""File: tools/corpus/run_app_tests.py
Purpose: Run an app's test invocations through difftest, each in a fresh sandbox, and record the results
Author: Akilesh
Date: 2026-10-29

Usage: run_app_tests.py --app-dir DIR --exe PATH --label linux|windows --work DIR --results DIR --difftest PATH
Writes <results>/<app>-<test>-<label>.json for every test.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

TEST_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
DEFAULT_TIMEOUT_SECONDS = 120
MAX_TIMEOUT_SECONDS = 3600


@dataclass(frozen=True)
class AppTest:
    id: str
    args: tuple[str, ...]
    stdin: str | None
    files: dict[str, str]
    timeout: int


def _relative(path: object, what: str) -> str:
    """An input path inside the app directory: relative, no parent escapes."""
    if not isinstance(path, str) or not path or path.startswith(("/", "\\")) or ".." in Path(path).parts:
        raise ValueError(f"{what} must be a relative path inside the app directory, got {path!r}")
    if re.match(r"^[A-Za-z]:", path):
        raise ValueError(f"{what} must not have a drive prefix, got {path!r}")
    return path


def _sandbox_name(name: object) -> str:
    """A plain file name for the sandbox: no directories, so inputs cannot be placed elsewhere."""
    if not isinstance(name, str) or not name or "/" in name or "\\" in name or name in {".", ".."}:
        raise ValueError(f"sandbox file name must be a plain name, got {name!r}")
    return name


def parse_test(raw: object) -> AppTest:
    if not isinstance(raw, dict):
        raise ValueError("each test must be an object")
    test_id = raw.get("id")
    if not isinstance(test_id, str) or not TEST_ID_PATTERN.match(test_id):
        raise ValueError(f"test id must match {TEST_ID_PATTERN.pattern}, got {test_id!r}")
    args = raw.get("args", [])
    if not isinstance(args, list) or not all(isinstance(a, str) for a in args):
        raise ValueError(f"{test_id}: args must be a list of strings")
    files = raw.get("files", {})
    if not isinstance(files, dict):
        raise ValueError(f"{test_id}: files must be an object")
    stdin = raw.get("stdin")
    timeout = raw.get("timeout", DEFAULT_TIMEOUT_SECONDS)
    if not isinstance(timeout, int) or not 1 <= timeout <= MAX_TIMEOUT_SECONDS:
        raise ValueError(f"{test_id}: timeout must be 1..{MAX_TIMEOUT_SECONDS}")
    return AppTest(
        id=test_id,
        args=tuple(args),
        stdin=_relative(stdin, f"{test_id}: stdin") if stdin is not None else None,
        files={_sandbox_name(n): _relative(p, f"{test_id}: file") for n, p in files.items()},
        timeout=timeout,
    )


def load_app(app_dir: Path) -> tuple[str, list[AppTest]]:
    config = json.loads((app_dir / "app.json").read_text(encoding="utf-8"))
    tests = [parse_test(raw) for raw in config["tests"]]
    ids = [t.id for t in tests]
    if len(set(ids)) != len(ids):
        raise ValueError("test ids must be unique")
    return config["name"], tests


def prepare_sandbox(app_dir: Path, test: AppTest, sandbox: Path) -> None:
    if sandbox.exists():
        shutil.rmtree(sandbox)
    sandbox.mkdir(parents=True)
    for name, source in test.files.items():
        shutil.copyfile(app_dir / source, sandbox / name)


def difftest_command(difftest: Path, exe: Path, test: AppTest, app_dir: Path, sandbox: Path, out: Path) -> list[str]:
    command = [
        str(difftest), "run", "--out", str(out), "--cwd", str(sandbox), "--capture-dir", str(sandbox),
        "--timeout", str(test.timeout),
    ]
    if test.stdin is not None:
        command += ["--stdin", str(app_dir / test.stdin)]
    return command + ["--", str(exe), *test.args]


def anonymize_program(result_path: Path, exe: Path, name: str) -> None:
    """Replaces the program's path with the app name in its output.

    Programs print argv[0] in messages. It is the AppDir's AppRun on Linux and a path to the .exe on
    Windows, which says nothing about whether the translation is right, so both become the app's name.
    """
    result = json.loads(result_path.read_text(encoding="utf-8"))
    for stream in ("stdout", "stderr"):
        result[stream] = result[stream].replace(str(exe), name)
    result_path.write_text(json.dumps(result), encoding="utf-8")


def run_all(app_dir: Path, exe: Path, label: str, work: Path, results: Path, difftest: Path) -> int:
    name, tests = load_app(app_dir)
    results.mkdir(parents=True, exist_ok=True)
    failures = 0
    for test in tests:
        sandbox = work / name / test.id
        prepare_sandbox(app_dir, test, sandbox)
        out = results / f"{name}-{test.id}-{label}.json"
        completed = subprocess.run(difftest_command(difftest, exe, test, app_dir, sandbox, out), capture_output=True, text=True)
        if completed.returncode != 0:
            failures += 1
            print(f"{name}/{test.id}: the runner failed: {completed.stderr.strip()}", file=sys.stderr)
        else:
            anonymize_program(out, exe, name)
    print(f"{name}: ran {len(tests)} tests ({label}), {failures} runner failures")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-dir", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--difftest", type=Path, required=True)
    arguments = parser.parse_args(argv[1:])
    return run_all(arguments.app_dir, arguments.exe, arguments.label, arguments.work, arguments.results, arguments.difftest)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
