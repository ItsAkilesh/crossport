"""
File: tools/appsweep/cases.py
Purpose: The applications and test invocations of the application sweep: real programs from Ubuntu 24.04, each with deterministic cases whose
         output Linux and the translated program must agree on
Author: Akilesh
Date: 2026-11-24

A case runs `binary args` in a fresh directory holding `files`, with `stdin` on standard input. The sweep compares standard output, standard
error, the exit code and the content of every file in the directory afterwards.
"""

import json
import sys
from pathlib import Path

SAMPLE_TEXT = "alpha 3\nbeta 1\ngamma 2\ndelta 5\nbeta 4\n"
SAMPLE_JSON = '{"name": "crossport", "tags": ["a", "b", "c"], "n": {"x": 1, "y": [2, 3]}}'
PATCH_ORIGINAL = "one\ntwo\nthree\nfour\n"
PATCH_TEXT = "--- a.txt\n+++ b.txt\n@@ -1,4 +1,4 @@\n one\n-two\n+TWO\n three\n four\n"
TREE_FILES = {"d/a.txt": "a\n", "d/e/b.txt": "bb\n", "d/e/f/c.txt": "ccc\n", "d/z.md": "z\n"}


def case(app: str, identifier: str, args: list[str], stdin: str = "", files: dict[str, str] | None = None, binary: str | None = None,
         ignore_files: list[str] | None = None, files_b64: dict[str, str] | None = None) -> dict:
    """`ignore_files` names outputs whose bytes legitimately differ (archives record the file system's owners and modes); "stdout" or "stderr"
    in it skips that stream (7-Zip prints this machine's open-file limit)."""
    return {"app": app, "binary": binary or app, "id": identifier, "args": args, "stdin": stdin, "files": files or {},
            "ignore_files": ignore_files or [], "files_b64": files_b64 or {}}


def calculators() -> list[dict]:
    return [
        case("bc", "pi", ["-l"], "scale=10; 4*a(1)\n"),
        case("bc", "power", [], "2^100\n"),
        case("bc", "function", [], "define f(n) { if (n < 2) return 1; return n * f(n-1) }\nf(20)\n"),
        case("dc", "power", [], "2 100 ^ p\n"),
        case("dc", "precision", [], "10 k 1 3 / p\n"),
        case("units", "convert", ["-t", "2 km", "m"]),
    ]


def archivers() -> list[dict]:
    files = {"in.txt": SAMPLE_TEXT * 40}
    cases = [case("bzip2", "round-trip", ["-k", "in.txt"], files=files), case("bzip2", "decompress-stdout", ["-dc", "in.txt.bz2"], files=files)]
    cases += [case("xz", "compress", ["-k", "-9", "in.txt"], files=files), case("zstd", "compress", ["-19", "-q", "in.txt"], files=files)]
    cases += [case("lz4", "compress", ["-q", "in.txt", "out.lz4"], files=files), case("gzip", "compress", ["-k", "-n", "in.txt"], files=files)]
    cases += [case("zip", "create", ["-q", "-X", "-r", "a.zip", "d"], files=TREE_FILES, ignore_files=["a.zip"]), case("unzip", "list-missing", ["-l", "nope.zip"])]
    cases += [case("tar", "create-list", ["-cf", "a.tar", "d"], files=TREE_FILES, ignore_files=["a.tar"]), case("tar", "usage", ["--version"])]
    return cases


def text_tools() -> list[dict]:
    files = {"data.txt": SAMPLE_TEXT}
    return [
        case("mawk", "sum", ["{ s += $2 } END { print s }", "data.txt"], files=files),
        case("mawk", "fields", ["-F", "e", "{ print $1 \"|\" NF }", "data.txt"], files=files),
        case("grep", "count", ["-c", "beta", "data.txt"], files=files),
        case("grep", "regex", ["-E", "^(a|g).*[0-9]$", "data.txt"], files=files),
        case("grep", "invert-number", ["-vn", "a", "data.txt"], files=files),
        case("sed", "substitute", ["s/a/A/g; 2d", "data.txt"], files=files),
        case("sed", "in-place", ["-i", "s/beta/BETA/", "data.txt"], files=files),
        case("sort", "numeric-key", ["-k2,2n", "data.txt"], files=files),
        case("sort", "unique", ["-u", "-k1,1", "data.txt"], files=files),
        case("cut", "fields", ["-d", " ", "-f", "2", "data.txt"], files=files),
        case("tr", "upper", ["a-z", "A-Z"], SAMPLE_TEXT),
        case("diff", "unified", ["-u", "a.txt", "b.txt"], files={"a.txt": PATCH_ORIGINAL, "b.txt": PATCH_ORIGINAL.replace("two", "2")}),
        case("patch", "apply", ["a.txt", "p.diff"], files={"a.txt": PATCH_ORIGINAL, "p.diff": PATCH_TEXT}),
        case("ed", "script", ["-s", "data.txt"], "1d\n$a\nlast line\n.\nw\nq\n", files=files),
        case("hexdump", "canonical", ["-C", "data.txt"], files=files),
        case("column", "table", ["-t", "data.txt"], files=files),
        case("less", "as-cat", ["-F", "-X", "data.txt"], files=files),
        case("pv", "copy", ["-q", "data.txt"], files=files),
        case("od", "bytes", ["-An", "-tx1", "data.txt"], files=files),
        case("base64", "encode", ["data.txt"], files=files),
        case("sha256sum", "hash", ["data.txt"], files=files),
        case("md5sum", "hash", ["data.txt"], files=files),
    ]


def data_and_scripting() -> list[dict]:
    return [
        case("lua5.4", "arithmetic", ["-e", "local t = {} for i = 1, 10 do t[#t+1] = i * i end print(table.concat(t, ','))"]),
        case("lua5.4", "string-patterns", ["-e", "print(('hello world'):gsub('o', '0'))"]),
        case("sqlite3", "memory-query", [":memory:"], "create table t(a, b); insert into t values (1,'x'),(2,'y'); select a, b from t order by a desc;\n"),
        case("sqlite3", "aggregate", [":memory:"], "create table t(n); insert into t values (3),(4),(5); select sum(n), avg(n), count(*) from t;\n"),
        case("jq", "path", ["-r", ".tags[]"], SAMPLE_JSON),
        case("jq", "transform", ["-c", "{n: .n.y | add, t: (.tags | length)}"], SAMPLE_JSON),
        case("cpio", "list-empty", ["-t"], ""),
        case("figlet", "banner", ["Hi"]),
    ]


def filesystem_tools() -> list[dict]:
    return [
        case("tree", "layout", ["-a", "--noreport", "d"], files=TREE_FILES),
        case("tree", "dirs-first", ["--dirsfirst", "--noreport", "d"], files=TREE_FILES),
        case("find", "by-name", ["d", "-name", "*.txt"], files=TREE_FILES),
        case("fdfind", "by-extension", ["-e", "txt", ".", "d"], files=TREE_FILES),
        case("rg", "search", ["--no-heading", "-n", "beta", "d"], files={"d/x.txt": SAMPLE_TEXT}),
        case("file", "text", ["-b", "--mime-type", "data.txt"], files={"data.txt": SAMPLE_TEXT}),
        case("ncdu", "version", ["--version"]),
        case("nano", "version", ["--version"]),
        case("vim.tiny", "ex-script", ["-es", "-u", "NONE", "-c", "%s/beta/BETA/g|wq", "data.txt"], files={"data.txt": SAMPLE_TEXT}),
        case("curl", "version", ["--version"]),
        case("curl", "refused", ["-s", "--max-time", "15", "http://127.0.0.1:9/"]),
        case("rsync", "local-copy", ["-a", "d/", "e/"], files=TREE_FILES),
    ]


def all_cases() -> list[dict]:
    from cases_more import more_cases
    from cases_third import third_cases

    return calculators() + archivers() + text_tools() + data_and_scripting() + filesystem_tools() + more_cases() + third_cases()


if __name__ == "__main__":
    Path(sys.argv[1]).write_text(json.dumps(all_cases(), indent=1), encoding="utf-8")
    print(f"wrote {len(all_cases())} cases for {len({c['app'] for c in all_cases()})} applications")
