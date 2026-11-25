"""
File: tools/appsweep/sweep.py
Purpose: Runs the whole application sweep: write the cases, run them on Linux in WSL, then translate and run them on Windows and report
Author: Akilesh
Date: 2026-11-24

Usage: python tools/appsweep/sweep.py OUT_DIR [--distro Ubuntu-24.04]
Needs a WSL distribution with the applications installed (see PACKAGES), the translator built, and the shim built.
OUT_DIR must be on a drive WSL can see (it is passed as /mnt/<drive>/...).
"""

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PACKAGES = ("bc dc tree file zip unzip bzip2 xz-utils zstd lz4 patch ed mawk lua5.4 sqlite3 jq figlet units pv ncdu less nano vim-tiny cpio "
            "rsync curl bsdextrautils ripgrep fd-find")


def wsl_path(path: Path) -> str:
    drive, rest = path.resolve().as_posix().split(":", 1)
    return f"/mnt/{drive.lower()}{rest}"


def run(command: list[str]) -> None:
    result = subprocess.run(command, text=True)
    if result.returncode != 0:
        sys.exit(f"failed: {' '.join(command[:4])} ...")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir")
    parser.add_argument("--distro", default="Ubuntu-24.04")
    arguments = parser.parse_args()
    out_dir = Path(arguments.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    cases = out_dir / "cases.json"
    run([sys.executable, str(HERE / "cases.py"), str(cases)])
    run(["wsl.exe", "-d", arguments.distro, "-u", "root", "--", "python3", wsl_path(HERE / "run_linux.py"), wsl_path(cases), wsl_path(out_dir)])
    run([sys.executable, str(HERE / "run_windows.py"), str(cases), str(out_dir), str(out_dir / "report.json")])


if __name__ == "__main__":
    main()
