#!/usr/bin/env python3
"""File: tools/runtime/pin_pack.py
Purpose: Resolve the runtime pack packages against a dated Ubuntu snapshot and write tools/runtime/pack.json
Author: Akilesh
Date: 2026-11-04

Usage: pin_pack.py <spec.json> <pack.json>
The spec names a snapshot (a UTC timestamp on snapshot.ubuntu.com) and, for each package, the suite it comes from.
The Packages index of each suite is checked against the SHA-256 that the suite's InRelease file lists (both fetched
over HTTPS from the same dated snapshot, which never changes); then each package's file name, size and SHA-256 are
taken from it. The pack file is what fetch_pack.py consumes, so a build never trusts anything newer than the pin.
"""

from __future__ import annotations

import hashlib
import json
import lzma
import re
import sys
import urllib.request
from pathlib import Path

SNAPSHOT_HOST = "https://snapshot.ubuntu.com/ubuntu"
ARCHITECTURE = "amd64"
TIMEOUT_SECONDS = 120
SNAPSHOT_PATTERN = re.compile(r"^\d{8}T\d{6}Z$")
NAME_PATTERN = re.compile(r"^[a-z0-9][a-z0-9+.-]*$")
SUITE_PATTERN = re.compile(r"^[a-z]+(-updates|-security)?$")


class SpecError(ValueError):
    """The spec or an index is not what it should be."""


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as response:
        return response.read()


def release_hashes(release: str) -> dict[str, str]:
    """The SHA-256 block of an InRelease file: relative path to digest."""
    hashes: dict[str, str] = {}
    in_block = False
    for line in release.splitlines():
        if line.startswith("SHA256:"):
            in_block = True
        elif in_block and line.startswith(" "):
            digest, _size, path = line.split()
            hashes[path] = digest
        elif in_block:
            break
    return hashes


def parse_packages(text: str) -> dict[str, dict[str, str]]:
    """Package stanzas by name; a later stanza of the same package replaces an earlier one."""
    packages: dict[str, dict[str, str]] = {}
    for stanza in text.split("\n\n"):
        fields: dict[str, str] = {}
        for line in stanza.splitlines():
            if ": " in line and not line.startswith(" "):
                key, value = line.split(": ", 1)
                fields[key] = value
        if "Package" in fields:
            packages[fields["Package"]] = fields
    return packages


def suite_packages(snapshot: str, suite: str) -> dict[str, dict[str, str]]:
    base = f"{SNAPSHOT_HOST}/{snapshot}"
    hashes = release_hashes(fetch(f"{base}/dists/{suite}/InRelease").decode("utf-8", "replace"))
    path = f"main/binary-{ARCHITECTURE}/Packages.xz"
    raw = fetch(f"{base}/dists/{suite}/{path}")
    if hashes.get(path) != hashlib.sha256(raw).hexdigest():
        raise SpecError(f"{suite}/{path} does not match its InRelease hash")
    return parse_packages(lzma.decompress(raw).decode("utf-8"))


def validate_spec(spec: object) -> tuple[str, list[tuple[str, list[str]]]]:
    if not isinstance(spec, dict) or not isinstance(spec.get("snapshot"), str) or not isinstance(spec.get("packages"), list):
        raise SpecError("the spec needs a snapshot and a list of packages")
    if not SNAPSHOT_PATTERN.match(spec["snapshot"]):
        raise SpecError("the snapshot must look like 20260101T000000Z")
    entries = []
    for entry in spec["packages"]:
        name = entry.get("name") if isinstance(entry, dict) else None
        suites = entry.get("suites") if isinstance(entry, dict) else None
        is_valid = isinstance(name, str) and NAME_PATTERN.match(name) and isinstance(suites, list) and len(suites) > 0
        if not is_valid or not all(isinstance(suite, str) and SUITE_PATTERN.match(suite) for suite in suites):
            raise SpecError(f"bad package entry: {entry!r}")
        entries.append((name, suites))
    return spec["snapshot"], entries


def pin(snapshot: str, entries: list[tuple[str, list[str]]]) -> dict[str, object]:
    """Each package comes from the first of its suites that has it (list the updates pocket before the release one)."""
    indexes: dict[str, dict[str, dict[str, str]]] = {}
    pinned = []
    for name, suites in entries:
        found = None
        for suite in suites:
            if suite not in indexes:
                indexes[suite] = suite_packages(snapshot, suite)
            if name in indexes[suite]:
                found = (suite, indexes[suite][name])
                break
        if found is None:
            raise SpecError(f"{name} is not in {', '.join(suites)}")
        suite, fields = found
        pinned.append({"name": name, "version": fields["Version"], "suite": suite,
                       "url": f"{SNAPSHOT_HOST}/{snapshot}/{fields['Filename']}",
                       "sha256": fields["SHA256"], "size": int(fields["Size"])})
    return {"snapshot": snapshot, "packages": pinned}


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    snapshot, entries = validate_spec(json.loads(Path(argv[1]).read_text(encoding="utf-8")))
    Path(argv[2]).write_text(json.dumps(pin(snapshot, entries), indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (SpecError, OSError, json.JSONDecodeError, lzma.LZMAError) as error:
        print(f"pin_pack: {error}", file=sys.stderr)
        sys.exit(1)
