"""
File: tools/appsweep/run_linux.py
Purpose: The Linux half of the application sweep: stage each program with its shared libraries, run every case and record what Linux does
Author: Akilesh
Date: 2026-11-24

Usage (inside the Ubuntu distribution): python3 run_linux.py CASES_JSON OUT_DIR
Writes OUT_DIR/stage/bin, OUT_DIR/stage/libs and OUT_DIR/linux_results.json.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

SHIM_PROVIDED = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1"}
TIMEOUT_SECONDS = 30
FIXED_MTIME = 1700000000
DATA_DIRECTORIES = ["share/units", "share/figlet", "share/misc", "share/file", "share/perl", "share/perl5", "share/git-core", "lib/x86_64-linux-gnu/perl",
                    "lib/x86_64-linux-gnu/perl-base", "lib/x86_64-linux-gnu/graphviz", "share/graphviz", "share/ImageMagick-6", "lib/git-core"]
# under /usr: data the staged programs read at run time
DATA_FILES = [("/usr/lib/file/magic.mgc", "usr/share/misc/magic.mgc"), ("/usr/lib/file/magic.mgc", "usr/lib/file/magic.mgc")]
ENVIRONMENT = {"PATH": "/usr/bin:/bin", "LC_ALL": "C.UTF-8", "TERM": "dumb", "TZ": "UTC"}


def libraries(path: str) -> list[str]:
    result = subprocess.run(["ldd", path], capture_output=True, text=True)
    return [parts[-2] for parts in (line.split() for line in result.stdout.splitlines()) if "=>" in parts and parts[-2].startswith("/")]


def stage(binary: str, out_dir: str) -> str | None:
    path = shutil.which(binary, path="/usr/bin")
    if path is None:
        return None
    shutil.copy(path, f"{out_dir}/stage/bin/{binary}")
    for library in libraries(path):
        base = os.path.basename(library)
        destination = f"{out_dir}/stage/libs/{base}"
        if base not in SHIM_PROVIDED and not base.startswith("ld-linux") and not os.path.exists(destination):
            shutil.copy(os.path.realpath(library), destination)
    return path


def stage_data(out_dir: str) -> None:
    """Copies the programs' data directories into OUT_DIR/appdir/usr, which the shim uses as the system root (APPDIR)."""
    for name in DATA_DIRECTORIES:
        source = f"/usr/{name}"
        if os.path.isdir(source):
            shutil.copytree(source, f"{out_dir}/appdir/usr/{name}", symlinks=False, dirs_exist_ok=True, ignore_dangling_symlinks=True)
    for source, relative in DATA_FILES:
        if os.path.exists(source):
            os.makedirs(os.path.dirname(f"{out_dir}/appdir/{relative}"), exist_ok=True)
            shutil.copy(source, f"{out_dir}/appdir/{relative}")


def write_files(directory: str, files: dict[str, str]) -> None:
    """Creates the files with a fixed modification time (and the directories above them) so archives are the same on both systems."""
    for name, text in files.items():
        target = os.path.join(directory, name)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        os.utime(target, (FIXED_MTIME, FIXED_MTIME))
    for folder, _, _ in os.walk(directory):
        os.utime(folder, (FIXED_MTIME, FIXED_MTIME))


def snapshot(directory: str) -> dict[str, str]:
    """Relative path to the SHA-256 of every file below the directory."""
    found = {}
    for folder, _, names in os.walk(directory):
        for name in names:
            full = os.path.join(folder, name)
            found[os.path.relpath(full, directory).replace(os.sep, "/")] = hashlib.sha256(open(full, "rb").read()).hexdigest()
    return found


def run_case(case: dict, path: str) -> dict:
    with tempfile.TemporaryDirectory(prefix="sweep-") as directory:
        write_files(directory, case["files"])
        args = [path] + [a.replace("{file_url}", "file://" + directory).replace("{cwd}", directory) for a in case["args"]]
        try:
            result = subprocess.run(args, input=case["stdin"].encode(), capture_output=True, timeout=TIMEOUT_SECONDS, cwd=directory,
                                    env={**ENVIRONMENT, "HOME": directory})
        except subprocess.TimeoutExpired:
            return {"rc": "timeout", "stdout": "", "stderr": "", "files": {}}
        return {"rc": result.returncode, "stdout": result.stdout.decode("utf-8", "replace"), "stderr": result.stderr.decode("utf-8", "replace"),
                "files": snapshot(directory)}


def main() -> None:
    cases = json.load(open(sys.argv[1], encoding="utf-8"))
    out_dir = sys.argv[2]
    os.makedirs(f"{out_dir}/stage/bin", exist_ok=True)
    os.makedirs(f"{out_dir}/stage/libs", exist_ok=True)
    stage_data(out_dir)
    paths = {binary: stage(binary, out_dir) for binary in sorted({c["binary"] for c in cases})}
    results = {}
    for case in cases:
        path = paths[case["binary"]]
        results[f"{case['app']}/{case['id']}"] = run_case(case, path) if path else {"rc": "missing", "stdout": "", "stderr": "", "files": {}}
    json.dump(results, open(f"{out_dir}/linux_results.json", "w"))
    print(f"staged {sum(1 for p in paths.values() if p)} programs, recorded {len(results)} Linux results")


if __name__ == "__main__":
    main()
