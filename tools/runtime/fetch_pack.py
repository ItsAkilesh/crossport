#!/usr/bin/env python3
"""File: tools/runtime/fetch_pack.py
Purpose: Download the pinned runtime pack packages, verify them, and unpack their shared libraries into one directory
Author: Akilesh
Date: 2026-11-04

Usage: fetch_pack.py <pack.json> <cache-dir> <output-dir>
Every package is checked against the SHA-256 in pack.json before it is opened. The output directory is what
`crossport translate --runtime <dir>` searches after the AppDir; the libraries in it are translated like any bundled
library, and an AppImage's own copy of a library always wins over the pack.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent / "corpus")]

import debs  # noqa: E402
import fetch  # noqa: E402


def fetch_pack(pack: object, cache: Path, output: Path) -> list[str]:
    if not isinstance(pack, dict) or not isinstance(pack.get("packages"), list):
        raise fetch.PinError("pack.json needs a list of packages")
    written: list[str] = []
    for entry in pack["packages"]:
        url, digest = fetch.parse_pin(entry)
        deb = fetch.ensure_pinned(url, digest, cache / url.rsplit("/", 1)[-1])
        written += debs.extract_libraries(deb.read_bytes(), output)
    return written


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    pack = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
    names = fetch_pack(pack, Path(argv[2]), Path(argv[3]))
    print(f"{len(names)} libraries in {argv[3]}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (fetch.PinError, debs.DebError, OSError, json.JSONDecodeError) as error:
        print(f"fetch_pack: {error}", file=sys.stderr)
        sys.exit(1)
