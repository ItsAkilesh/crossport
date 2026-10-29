"""File: tools/ci/symbolize.py
Purpose: Name the shim functions in a crash report: map "crossport_libc.dll+0xOFFSET" in a recorded run's stderr to symbols of the built DLL
Author: Akilesh
Date: 2026-10-29

Usage: python tools/ci/symbolize.py <result.json> <crossport_libc.dll>
Needs: GNU nm and objdump (mingw-w64 binutils) on the path.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

FRAME = re.compile(r"crossport_libc\.dll\+0x([0-9a-f]+)")
IMAGE_BASE_LINE = re.compile(r"^ImageBase\s+([0-9a-f]+)\s*$", re.MULTILINE)
CODE_SYMBOL_TYPES = {"T", "t"}


def parse_image_base(objdump_output: str) -> int:
    """The preferred load address from `objdump -p` output; symbol addresses in `nm` output include it."""
    match = IMAGE_BASE_LINE.search(objdump_output)
    if match is None:
        raise ValueError("no ImageBase line in the objdump output")
    return int(match.group(1), 16)


def parse_symbols(nm_output: str) -> list[tuple[int, str]]:
    """The code symbols sorted by address, from `nm -n` output; lines without an address are skipped."""
    symbols: list[tuple[int, str]] = []
    for line in nm_output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in CODE_SYMBOL_TYPES:
            symbols.append((int(parts[0], 16), parts[2]))
    return sorted(symbols)


def resolve(image_base: int, symbols: list[tuple[int, str]], offset: int) -> str:
    """The symbol containing the module offset, as name+0xdelta, or the bare offset when none precedes it."""
    address = image_base + offset
    best: tuple[int, str] | None = None
    for start, name in symbols:
        if start > address:
            break
        best = (start, name)
    return f"{best[1]}+{address - best[0]:#x}" if best is not None else f"{offset:#x}"


def symbolize(stderr: str, image_base: int, symbols: list[tuple[int, str]]) -> list[str]:
    """One line per shim frame in the report: its offset and the symbol it falls in."""
    return [f"crossport_libc.dll+{int(match, 16):#x} = {resolve(image_base, symbols, int(match, 16))}"
            for match in FRAME.findall(stderr)]


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    stderr = json.loads(Path(argv[1]).read_text(encoding="utf-8")).get("stderr", "")
    listing = subprocess.run(["nm", "-n", argv[2]], capture_output=True, text=True, check=True).stdout
    header = subprocess.run(["objdump", "-p", argv[2]], capture_output=True, text=True, check=True).stdout
    for line in symbolize(stderr, parse_image_base(header), parse_symbols(listing)):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
