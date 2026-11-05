#!/usr/bin/env python3
"""File: tools/ci/check_alertable.py
Purpose: Fail when the shim calls a Windows function that lets the system run user-mode code on a translated thread
Author: Akilesh
Date: 2026-11-05

Usage: check_alertable.py <directory>...

Translated x86-64 Linux code may keep data in the 128 bytes below its stack pointer (the red zone). Windows
runs asynchronous user-mode code (APCs, I/O completion routines) on a thread's stack, below the stack pointer,
only while the thread is in an alertable wait or has asked for such a callback. The shim therefore never enters
an alertable wait and never queues an APC; this check keeps it that way (docs/DESIGN.md, section 2.12). Comments
and string literals are ignored.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

FORBIDDEN = (
    "SleepEx",
    "WaitForSingleObjectEx",
    "WaitForMultipleObjectsEx",
    "MsgWaitForMultipleObjectsEx",
    "SignalObjectAndWait",
    "QueueUserAPC",
    "QueueUserAPC2",
    "NtQueueApcThread",
    "NtDelayExecution",
    "NtTestAlert",
    "ReadFileEx",
    "WriteFileEx",
    "GetOverlappedResultEx",
    "GetQueuedCompletionStatusEx",
    "SuspendThread",
    "SetThreadContext",
    "WSARecvFrom",
    "WSASend",
    "WSASendTo",
    "WSARecv",
    "WSAWaitForMultipleEvents",
)
SOURCE_SUFFIXES = {".c", ".h"}
COMMENTS_AND_STRINGS = re.compile(
    r"""//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'""", re.DOTALL
)
CALL = re.compile(r"\b(" + "|".join(FORBIDDEN) + r")\b")


def strip(text: str) -> str:
    """The source with comments and literals blanked out, keeping line numbers."""
    return COMMENTS_AND_STRINGS.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def violations(path: Path) -> list[str]:
    text = strip(path.read_text(encoding="utf-8", errors="replace"))
    found = []
    for number, line in enumerate(text.splitlines(), start=1):
        for match in CALL.finditer(line):
            found.append(f"{path}:{number}: {match.group(1)} can run an APC on the calling thread")
    return found


def scan(directories: list[Path]) -> list[str]:
    found: list[str] = []
    for directory in directories:
        for path in sorted(directory.rglob("*")):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                found += violations(path)
    return found


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    found = scan([Path(arg) for arg in argv[1:]])
    for line in found:
        print(line, file=sys.stderr)
    if found:
        print("the shim must not enter alertable waits (the red zone, docs/DESIGN.md 2.12)", file=sys.stderr)
        return 1
    print("no alertable waits or APC calls")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
