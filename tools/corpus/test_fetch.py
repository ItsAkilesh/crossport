"""File: tools/corpus/test_fetch.py
Purpose: Tests for fetch.py: pin validation, hash enforcement and refusal of hostile tarballs
Author: Akilesh
Date: 2026-10-28
"""

from __future__ import annotations

import hashlib
import io
import tarfile
import tempfile
import unittest
from pathlib import Path

import fetch

DIGEST = "a" * 64


def make_tar(path: Path, members: dict[str, bytes], links: dict[str, str] | None = None) -> None:
    with tarfile.open(path, "w:gz") as tar:
        for name, data in members.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        for name, target in (links or {}).items():
            info = tarfile.TarInfo(name)
            info.type = tarfile.SYMTYPE
            info.linkname = target
            tar.addfile(info)


class PinTest(unittest.TestCase):
    def test_a_valid_pin_is_accepted(self) -> None:
        self.assertEqual(fetch.parse_pin({"url": "https://x.org/a.tgz", "sha256": DIGEST}), ("https://x.org/a.tgz", DIGEST))

    def test_malformed_pins_are_rejected(self) -> None:
        for bad in [
            None,
            {},
            {"url": "http://x.org/a", "sha256": DIGEST},
            {"url": "ftp://x.org/a", "sha256": DIGEST},
            {"url": "https://x.org/a", "sha256": "A" * 64},
            {"url": "https://x.org/a", "sha256": "abc"},
            {"url": "https://x.org/a"},
        ]:
            with self.assertRaises(fetch.PinError, msg=str(bad)):
                fetch.parse_pin(bad)


class ExtractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = Path(tempfile.mkdtemp(prefix="crossport-fetch-"))

    def test_a_normal_tarball_extracts_and_reports_its_root(self) -> None:
        archive = self.directory / "ok.tar.gz"
        make_tar(archive, {"lua-1/Makefile": b"all:\n", "lua-1/src/a.c": b"int x;"})
        root = fetch.extract_source(archive, self.directory / "out")
        self.assertEqual((root / "src" / "a.c").read_bytes(), b"int x;")

    def test_path_escapes_are_refused(self) -> None:
        for name in ["../evil", "/abs/evil", "pkg/../../evil"]:
            archive = self.directory / "bad.tar.gz"
            make_tar(archive, {name: b"x", "pkg/ok": b"y"})
            with self.assertRaises(Exception, msg=name):
                fetch.extract_source(archive, self.directory / "escape-out")
            self.assertFalse((self.directory / "evil").exists())

    def test_links_that_leave_the_destination_are_refused(self) -> None:
        archive = self.directory / "link.tar.gz"
        make_tar(archive, {"pkg/ok": b"y"}, links={"pkg/escape": "../../outside"})
        with self.assertRaises(Exception):
            fetch.extract_source(archive, self.directory / "link-out")

    def test_several_top_level_directories_are_refused(self) -> None:
        archive = self.directory / "multi.tar.gz"
        make_tar(archive, {"one/a": b"1", "two/b": b"2"})
        with self.assertRaises(fetch.PinError):
            fetch.extract_source(archive, self.directory / "multi-out")


class CacheTest(unittest.TestCase):
    def test_a_cached_file_with_the_right_hash_is_reused_without_downloading(self) -> None:
        directory = Path(tempfile.mkdtemp(prefix="crossport-cache-"))
        cached = directory / "file.bin"
        cached.write_bytes(b"payload")
        digest = hashlib.sha256(b"payload").hexdigest()
        self.assertEqual(fetch.ensure_pinned("https://invalid.example/never-fetched", digest, cached), cached)


if __name__ == "__main__":
    unittest.main()
