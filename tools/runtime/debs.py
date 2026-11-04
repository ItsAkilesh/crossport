#!/usr/bin/env python3
"""File: tools/runtime/debs.py
Purpose: Read the shared libraries out of a Debian package without trusting its contents
Author: Akilesh
Date: 2026-11-04

A .deb is an ar archive whose data.tar.* member holds the files. Only shared objects directly under the multiarch
library directories are taken, regular files and symlinks alike; a symlink becomes a copy of its target under the
link name, so the result needs no symlink support (Windows hosts have none by default). Names are reduced to a base
name, so nothing can be written outside the destination.
"""

from __future__ import annotations

import io
import posixpath
import subprocess
import tarfile
import tempfile
from pathlib import Path

AR_MAGIC = b"!<arch>\n"
AR_HEADER_BYTES = 60
LIBRARY_DIRECTORIES = ("usr/lib/x86_64-linux-gnu", "lib/x86_64-linux-gnu")
MAX_LINK_DEPTH = 8
SEVEN_ZIP = "7z"
MAX_LIBRARY_BYTES = 256 * 1024 * 1024


class DebError(ValueError):
    """A package is malformed."""


def ar_members(data: bytes) -> dict[str, bytes]:
    """The members of an ar archive by name."""
    if not data.startswith(AR_MAGIC):
        raise DebError("not an ar archive")
    members: dict[str, bytes] = {}
    offset = len(AR_MAGIC)
    while offset + AR_HEADER_BYTES <= len(data):
        header = data[offset : offset + AR_HEADER_BYTES]
        name = header[:16].decode("ascii").strip().rstrip("/")
        try:
            size = int(header[48:58].decode("ascii").strip())
        except ValueError as error:
            raise DebError("bad ar member size") from error
        offset += AR_HEADER_BYTES
        if offset + size > len(data):
            raise DebError("truncated ar member")
        members[name] = data[offset : offset + size]
        offset += size + (size & 1)
    return members


def decompress_data(name: str, payload: bytes) -> bytes:
    """The tar bytes of a data.tar.* member; tarfile reads plain, gz, xz and bz2, zst needs the zstd module or tool."""
    if not name.endswith(".zst"):
        return payload
    try:
        from compression import zstd  # Python 3.14 and later

        return zstd.decompress(payload)
    except ImportError:
        pass
    try:
        result = subprocess.run(["zstd", "-dc"], input=payload, capture_output=True, check=False)
        if result.returncode == 0 and result.stdout:
            return result.stdout
    except OSError:
        pass
    with tempfile.TemporaryDirectory() as directory:
        archive = Path(directory) / "data.tar.zst"
        archive.write_bytes(payload)
        try:
            result = subprocess.run([SEVEN_ZIP, "x", "-so", str(archive)], capture_output=True, check=False)
        except OSError:
            result = None
    if result is not None and result.returncode == 0 and result.stdout:
        return result.stdout
    raise DebError("a .zst package needs Python 3.14, the zstd tool or 7-Zip 25 or later")


def data_tar(members: dict[str, bytes]) -> tarfile.TarFile:
    for name, payload in members.items():
        if name.startswith("data.tar"):
            return tarfile.open(fileobj=io.BytesIO(decompress_data(name, payload)))
    raise DebError("no data.tar member")


def normalized(name: str) -> str:
    return posixpath.normpath(name[2:] if name.startswith("./") else name)


def is_library(path: str) -> bool:
    directory, base = posixpath.split(path)
    return directory in LIBRARY_DIRECTORIES and ".so" in base


def resolve(path: str, links: dict[str, str]) -> str:
    for _ in range(MAX_LINK_DEPTH):
        if path not in links:
            return path
        path = posixpath.normpath(posixpath.join(posixpath.dirname(path), links[path]))
    raise DebError(f"too many symlinks resolving {path}")


def extract_libraries(deb: bytes, destination: Path) -> list[str]:
    """Writes every shared library of the package into `destination`; returns the file names written."""
    destination.mkdir(parents=True, exist_ok=True)
    files: dict[str, bytes] = {}
    links: dict[str, str] = {}
    with data_tar(ar_members(deb)) as tar:
        for member in tar:
            path = normalized(member.name)
            if not is_library(path):
                continue
            if member.issym():
                links[path] = member.linkname
            elif member.isreg():
                if member.size > MAX_LIBRARY_BYTES:
                    raise DebError(f"{path} is too large")
                handle = tar.extractfile(member)
                files[path] = handle.read() if handle is not None else b""
    written = []
    for path in sorted(set(files) | set(links)):
        target = resolve(path, links)
        if target in files:
            name = posixpath.basename(path)
            (destination / name).write_bytes(files[target])
            written.append(name)
    return written
