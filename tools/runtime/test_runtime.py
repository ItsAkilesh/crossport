"""File: tools/runtime/test_runtime.py
Purpose: Tests for the runtime pack tools: reading libraries out of a package, and resolving the pack spec
Author: Akilesh
Date: 2026-11-04
"""

import io
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import debs  # noqa: E402
import pin_pack  # noqa: E402

LIB_DIRECTORY = "./usr/lib/x86_64-linux-gnu/"


def ar_archive(members: dict[str, bytes]) -> bytes:
    out = bytearray(debs.AR_MAGIC)
    for name, payload in members.items():
        out += f"{name + '/':<16}{0:<12}{0:<6}{0:<6}{100644:<8}{len(payload):<10}`\n".encode("ascii")
        out += payload + (b"\n" if len(payload) % 2 else b"")
    return bytes(out)


def make_deb(entries: list[tuple[str, str, bytes | str]]) -> bytes:
    """entries: (path, kind, content) where kind is 'file' or 'link' (content is the target)."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w:xz") as tar:
        for path, kind, content in entries:
            info = tarfile.TarInfo(path)
            if kind == "link":
                info.type = tarfile.SYMTYPE
                info.linkname = str(content)
                tar.addfile(info)
            else:
                data = content if isinstance(content, bytes) else content.encode()
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
    return ar_archive({"debian-binary": b"2.0\n", "data.tar.xz": raw.getvalue()})


class ExtractLibraries(unittest.TestCase):
    def extract(self, entries):
        with tempfile.TemporaryDirectory() as directory:
            written = debs.extract_libraries(make_deb(entries), Path(directory))
            return written, {name: (Path(directory) / name).read_bytes() for name in written}

    def test_a_symlink_becomes_a_copy_of_its_target(self):
        written, files = self.extract([
            (LIB_DIRECTORY + "libz.so.1.2.11", "file", b"ELF"),
            (LIB_DIRECTORY + "libz.so.1", "link", "libz.so.1.2.11"),
        ])
        self.assertEqual(sorted(written), ["libz.so.1", "libz.so.1.2.11"])
        self.assertEqual(files["libz.so.1"], b"ELF")

    def test_only_libraries_directly_in_the_multiarch_directory_are_taken(self):
        written, _ = self.extract([
            ("./usr/share/doc/readme", "file", b"x"),
            (LIB_DIRECTORY + "sub/libhidden.so.1", "file", b"x"),
            ("./usr/lib/python3/libother.so", "file", b"x"),
            (LIB_DIRECTORY + "libok.so.1", "file", b"x"),
        ])
        self.assertEqual(written, ["libok.so.1"])

    def test_a_link_that_leaves_the_package_is_skipped(self):
        written, _ = self.extract([(LIB_DIRECTORY + "libx.so.1", "link", "../../../../etc/passwd")])
        self.assertEqual(written, [])

    def test_a_name_with_parent_components_cannot_escape(self):
        written, _ = self.extract([("./usr/lib/x86_64-linux-gnu/../../../../evil.so", "file", b"x")])
        self.assertEqual(written, [])

    def test_a_circular_link_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            deb = make_deb([(LIB_DIRECTORY + "liba.so.1", "link", "libb.so.1"),
                            (LIB_DIRECTORY + "libb.so.1", "link", "liba.so.1")])
            with self.assertRaises(debs.DebError):
                debs.extract_libraries(deb, Path(directory))

    def test_something_that_is_not_a_package_is_an_error(self):
        with self.assertRaises(debs.DebError):
            debs.ar_members(b"not an archive")

    def test_a_truncated_member_is_an_error(self):
        with self.assertRaises(debs.DebError):
            debs.ar_members(ar_archive({"data.tar.xz": b"abcdef"})[:-3])


class ResolveSpec(unittest.TestCase):
    RELEASE = "Origin: Ubuntu\nSHA256:\n abc123 100 main/binary-amd64/Packages.xz\n def456 5 main/Contents-amd64\nSHA512:\n x 1 y\n"
    PACKAGES = "Package: libz\nVersion: 1\nFilename: pool/z.deb\nSHA256: aa\nSize: 3\n\nPackage: libq\nVersion: 2\nFilename: pool/q.deb\nSHA256: bb\nSize: 4\n"

    def test_the_release_hash_block_is_read(self):
        hashes = pin_pack.release_hashes(self.RELEASE)
        self.assertEqual(hashes["main/binary-amd64/Packages.xz"], "abc123")
        self.assertNotIn("x", hashes)

    def test_package_stanzas_are_parsed(self):
        packages = pin_pack.parse_packages(self.PACKAGES)
        self.assertEqual(packages["libq"]["Filename"], "pool/q.deb")

    def test_a_package_comes_from_the_first_suite_that_has_it(self):
        original = pin_pack.suite_packages
        pin_pack.suite_packages = lambda snapshot, suite: (
            {"libz": {"Version": "9", "Filename": "pool/z9.deb", "SHA256": "cc", "Size": "5"}} if suite == "jammy-updates" else
            {"libz": {"Version": "1", "Filename": "pool/z.deb", "SHA256": "aa", "Size": "3"},
             "libq": {"Version": "2", "Filename": "pool/q.deb", "SHA256": "bb", "Size": "4"}})
        try:
            pinned = pin_pack.pin("20260101T000000Z", [("libz", ["jammy-updates", "jammy"]), ("libq", ["jammy-updates", "jammy"])])
        finally:
            pin_pack.suite_packages = original
        by_name = {entry["name"]: entry for entry in pinned["packages"]}
        self.assertEqual(by_name["libz"]["sha256"], "cc")
        self.assertEqual(by_name["libq"]["suite"], "jammy")
        self.assertTrue(by_name["libq"]["url"].startswith("https://snapshot.ubuntu.com/ubuntu/20260101T000000Z/pool/"))

    def test_a_bad_spec_is_refused(self):
        for spec in [{}, {"snapshot": "latest", "packages": []},
                     {"snapshot": "20260101T000000Z", "packages": [{"name": "../x", "suites": ["jammy"]}]},
                     {"snapshot": "20260101T000000Z", "packages": [{"name": "libz", "suites": []}]}]:
            with self.assertRaises(pin_pack.SpecError):
                pin_pack.validate_spec(spec)


if __name__ == "__main__":
    unittest.main()
