"""
File: tools/appsweep/cases_more.py
Purpose: The second set of sweep cases: shells, build and version-control tools, language runtimes, cryptography, media and image tools, and
         more GNU coreutils
Author: Akilesh
Date: 2026-11-24
"""

from cases import SAMPLE_TEXT, case

ED25519_KEY = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIEhLJ7bS3jGtDQc0MHHiDQy2dDDrgpl+Jr6hdN7GbZJk sweep@example\n"
MAKEFILE = "all: a b\na:\n\techo building a\nb:\n\techo building b\n"
SCRIPT = "n=0\nwhile [ $n -lt 3 ]; do n=$((n+1)); done\necho done $n\n"
DIGRAPH = "digraph g { a -> b; b -> c; }\n"
PERL_MAP = "print join(',', map { $_ * 2 } 1..5), \"\\n\""


def shells_and_build() -> list[dict]:
    return [
        case("bash", "arithmetic", ["-c", "echo $((6*7)); for i in 1 2 3; do printf '%s,' $i; done; echo"]),
        case("bash", "arrays", ["-c", "x=(a b c); echo ${#x[@]} ${x[1]}"]),
        case("bash", "script-file", ["s.sh"], files={"s.sh": SCRIPT}),
        case("dash", "arithmetic", ["-c", "echo $((6*7)); x=5; echo ${x}"]),
        case("zsh", "arithmetic", ["-fc", "echo $((2**10)); print -l a b"]),
        case("make", "dry-run", ["-n", "-f", "Makefile"], files={"Makefile": MAKEFILE}),
        case("make", "version", ["--version"]),
        case("perl", "map", ["-e", PERL_MAP]),
        case("perl", "substitute", ["-pe", "s/a/A/g", "data.txt"], files={"data.txt": SAMPLE_TEXT}),
        case("git", "version", ["--version"]),
        case("git", "hash-object", ["hash-object", "--stdin"], SAMPLE_TEXT),
        case("git", "config-file", ["config", "--file", "c.cfg", "user.name", "Sweep"], files={"c.cfg": ""}),
        case("openssl", "version", ["version"]),
        case("openssl", "sha256", ["dgst", "-sha256", "data.txt"], files={"data.txt": SAMPLE_TEXT}),
        case("openssl", "base64", ["enc", "-base64", "-in", "data.txt"], files={"data.txt": SAMPLE_TEXT}),
        case("ssh-keygen", "fingerprint", ["-l", "-f", "k.pub"], files={"k.pub": ED25519_KEY}),
    ]


def conversion_tools() -> list[dict]:
    return [
        case("xxd", "dump", ["data.txt"], files={"data.txt": SAMPLE_TEXT}),
        case("xxd", "plain", ["-p", "data.txt"], files={"data.txt": SAMPLE_TEXT}),
        case("dos2unix", "convert", ["-n", "in.txt", "out.txt"], files={"in.txt": "a\r\nb\r\nc\r\n"}),
        case("dot", "plain-layout", ["-Tplain"], DIGRAPH),
        case("ffmpeg", "version", ["-hide_banner", "-version"]),
        case("ffmpeg", "solid-frame", ["-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=red:s=16x16:d=0.1", "-frames:v", "1", "out.ppm"]),
        case("convert", "solid-image", ["-size", "8x8", "xc:red", "out.ppm"]),
        case("tmux", "version", ["-V"]),
        case("htop", "version", ["--version"]),
    ]


def more_coreutils() -> list[dict]:
    files = {"data.txt": SAMPLE_TEXT, "other.txt": "beta 9\nzeta 8\n"}
    return [
        case("seq", "range", ["3", "2", "11"]),
        case("expr", "arithmetic", ["6", "*", "7"]),
        case("date", "epoch", ["-u", "-d", "@86400", "+%Y-%m-%d %H:%M:%S"]),
        case("comm", "columns", ["-12", "a.txt", "b.txt"], files={"a.txt": "a\nb\nc\n", "b.txt": "b\nc\nd\n"}),
        case("join", "key", ["data.txt", "other.txt"], files=files),
        case("nl", "number", ["data.txt"], files=files),
        case("fold", "wrap", ["-w", "5", "data.txt"], files=files),
        case("fmt", "reflow", ["-w", "20"], "one two three four five six seven eight\n"),
        case("tac", "reverse", ["data.txt"], files=files),
        case("rev", "reverse", [], SAMPLE_TEXT),
        case("split", "lines", ["-l", "2", "data.txt", "part-"], files=files),
        case("truncate", "grow", ["-s", "100", "t.bin"], files={"t.bin": ""}),
        case("sha1sum", "hash", ["data.txt"], files=files),
        case("uniq", "counts", ["-c"], "a\na\nb\nc\nc\nc\n"),
        case("paste", "merge", ["data.txt", "other.txt"], files=files),
        case("head", "lines", ["-n", "2", "data.txt"], files=files),
        case("tail", "lines", ["-n", "2", "data.txt"], files=files),
        case("wc", "counts", ["data.txt"], files=files),
        case("cat", "number", ["-n", "data.txt"], files=files),
        case("stat", "format", ["-c", "%s %F", "data.txt"], files=files),
        case("basename", "suffix", ["/a/b/c.txt", ".txt"]),
        case("tee", "copy", ["out.txt"], SAMPLE_TEXT),
    ]


def more_cases() -> list[dict]:
    return shells_and_build() + conversion_tools() + more_coreutils()
