#!/usr/bin/env python3
"""File: tools/corpus/fetch.py
Purpose: Download a pinned file over HTTPS, verify its SHA-256, and extract source tarballs without trusting them
Author: Akilesh
Date: 2026-10-28

Usage:
  fetch.py source <app-dir> <dest-root>   download and extract the app's source; prints the source directory
  fetch.py file <pin.json> <dest-file>    download one pinned file (for example the AppImage runtime)

A pin is {"url": "https://...", "sha256": "<64 hex digits>"}. Nothing is used before its hash matches.
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
import tarfile
import urllib.request
from pathlib import Path

MAX_DOWNLOAD_BYTES = 256 * 1024 * 1024
CHUNK_BYTES = 1024 * 1024
TIMEOUT_SECONDS = 120
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


class PinError(ValueError):
    """A pin is malformed, or the bytes did not match it."""


def parse_pin(raw: object) -> tuple[str, str]:
    """Validates a pin object and returns (url, sha256)."""
    if not isinstance(raw, dict):
        raise PinError("a pin must be an object")
    url, digest = raw.get("url"), raw.get("sha256")
    if not isinstance(url, str) or not url.startswith("https://"):
        raise PinError(f"pin url must be an https:// URL, got {url!r}")
    if not isinstance(digest, str) or not SHA256_PATTERN.match(digest):
        raise PinError("pin sha256 must be 64 lowercase hex digits")
    return url, digest


def sha256_of(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(CHUNK_BYTES):
            hasher.update(chunk)
    return hasher.hexdigest()


def download(url: str, destination: Path, expected_sha256: str) -> None:
    """Streams `url` to `destination`, refusing oversized downloads and wrong hashes."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".partial")
    hasher = hashlib.sha256()
    received = 0
    with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as response, partial.open("wb") as out:
        while chunk := response.read(CHUNK_BYTES):
            received += len(chunk)
            if received > MAX_DOWNLOAD_BYTES:
                partial.unlink(missing_ok=True)
                raise PinError(f"{url} is larger than {MAX_DOWNLOAD_BYTES} bytes")
            hasher.update(chunk)
            out.write(chunk)
    if hasher.hexdigest() != expected_sha256:
        partial.unlink(missing_ok=True)
        raise PinError(f"{url}: sha256 is {hasher.hexdigest()}, the pin says {expected_sha256}")
    partial.replace(destination)


def ensure_pinned(url: str, digest: str, destination: Path) -> Path:
    """Reuses a cached file only if it still matches the pin; otherwise downloads it again."""
    if destination.exists() and sha256_of(destination) == digest:
        return destination
    download(url, destination, digest)
    return destination


def extract_source(archive: Path, destination: Path) -> Path:
    """Extracts a tarball with the standard library's 'data' filter, which refuses absolute paths,
    parent-directory escapes, links outside the destination and special files."""
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        names = tar.getnames()
        if not names:
            raise PinError(f"{archive} is empty")
        tar.extractall(destination, filter="data")
    roots = {name.split("/", 1)[0] for name in names}
    if len(roots) != 1:
        raise PinError(f"{archive} should have exactly one top-level directory, has {sorted(roots)}")
    return destination / roots.pop()


def fetch_source(app_dir: Path, dest_root: Path) -> Path:
    config = json.loads((app_dir / "app.json").read_text(encoding="utf-8"))
    url, digest = parse_pin(config.get("source"))
    archive = ensure_pinned(url, digest, dest_root / "downloads" / url.rsplit("/", 1)[-1])
    return extract_source(archive, dest_root / "src" / config["name"])


def main(argv: list[str]) -> int:
    if len(argv) == 4 and argv[1] == "source":
        print(fetch_source(Path(argv[2]), Path(argv[3])))
        return 0
    if len(argv) == 4 and argv[1] == "file":
        url, digest = parse_pin(json.loads(Path(argv[2]).read_text(encoding="utf-8")))
        print(ensure_pinned(url, digest, Path(argv[3])))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (PinError, OSError, json.JSONDecodeError, tarfile.TarError) as error:
        print(f"fetch: {error}", file=sys.stderr)
        sys.exit(1)
