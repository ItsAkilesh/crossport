#!/usr/bin/env python3
# File: tools/gen-shim-def.py
# Purpose: Generate the shim DLL's .def file: every LIBC_FN function is exported as its real name, every LIBC_DATA symbol too
# Author: Akilesh
# Date: 2026-10-28
#
# Usage: gen-shim-def.py <shim-dir> <output.def>
#
# A function written `LIBC_FN type sysv_name(...)` is exported as `name`; data declared
# `LIBC_DATA(type, name)` is exported as `name` (DATA). math.c defines many functions through the
# MATH1(name) and MATH2(name) macros, which are listed here by scanning their invocations.

import re
import sys
from pathlib import Path

FUNCTION = re.compile(r"^LIBC_FN\b[^;{(]*?\bsysv_(\w+)\s*\(", re.MULTILINE)
DATA = re.compile(r"^LIBC_DATA\([^,]+,\s*(\w+)\)", re.MULTILINE)
MACRO_FUNCTION = re.compile(r"\bMATH[12]\((\w+)\)")
MACRO_PARAMETER = "name"
# Data symbols the C library also exports under other names.
DATA_ALIASES = {"environ": ["__environ", "_environ"]}


def scan(source: str) -> tuple[set[str], set[str]]:
    """Returns the exported function names and data names found in one source file."""
    functions = set(FUNCTION.findall(source))
    functions |= {name for name in MACRO_FUNCTION.findall(source) if name != MACRO_PARAMETER}
    return functions, set(DATA.findall(source))


def render(functions: set[str], data: set[str]) -> str:
    lines = ["EXPORTS"]
    lines += [f"    {name}=sysv_{name}" for name in sorted(functions)]
    for name in sorted(data):
        for exported in [name, *DATA_ALIASES.get(name, [])]:
            lines.append(f"    {exported}=crossport_data_{name} DATA")
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: gen-shim-def.py <shim-dir> <output.def>", file=sys.stderr)
        return 2
    functions: set[str] = set()
    data: set[str] = set()
    for path in sorted(Path(argv[1]).glob("*.c")):
        found_functions, found_data = scan(path.read_text(encoding="utf-8"))
        functions |= found_functions
        data |= found_data
    Path(argv[2]).write_text(render(functions, data), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
