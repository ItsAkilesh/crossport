"""
File: tools/appsweep/run_windows.py
Purpose: The Windows half of the application sweep: translate each staged program, run every case on Windows and compare with what Linux did
Author: Akilesh
Date: 2026-11-24

Usage: python run_windows.py CASES_JSON OUT_DIR REPORT_JSON   (after run_linux.py has filled OUT_DIR)
Needs translator/target/release/crossport.exe and build/shim/crossport_libc.dll in the repository.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
TRANSLATOR = REPO / "translator" / "target" / "release" / "crossport.exe"
SHIM = REPO / "build" / "shim" / "crossport_libc.dll"
TRANSLATE_TIMEOUT = 180
RUN_TIMEOUT = 30
CRASH_CODE = 0xC0000000
FIXED_MTIME = 1700000000


def stop_bus() -> None:
    """A running session-bus daemon locks crossport-bus.exe inside its bundle, so end it before the bundle is replaced."""
    subprocess.run(["taskkill", "/F", "/IM", "crossport-bus.exe"], capture_output=True)


def translate(binary: str, out_dir: Path) -> tuple[Path | None, str]:
    """Translates one staged program. Returns the executable, or None and the translator's last message."""
    stop_bus()
    bundle = out_dir / "bundles" / binary
    shutil.rmtree(bundle, ignore_errors=True)
    command = [str(TRANSLATOR), "translate", str(out_dir / "stage" / "bin" / binary), "--lib-dir", str(out_dir / "stage" / "libs"),
               "--shim", str(SHIM), "--out", str(bundle)]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=TRANSLATE_TIMEOUT)
    except subprocess.TimeoutExpired:
        return None, "translation timeout"
    if result.returncode != 0:
        return None, (result.stderr.strip().splitlines() or ["(no message)"])[-1]
    return [p for p in bundle.glob("*.exe") if p.name != "crossport-bus.exe"][0], ""


def write_files(directory: str, files: dict[str, str]) -> None:
    """Creates the files with a fixed modification time (and the directories above them), as the Linux side does."""
    for name, text in files.items():
        target = os.path.join(directory, name)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        os.utime(target, (FIXED_MTIME, FIXED_MTIME))
    for folder, _, _ in os.walk(directory):
        os.utime(folder, (FIXED_MTIME, FIXED_MTIME))


def snapshot(directory: str) -> dict[str, str]:
    found = {}
    for folder, _, names in os.walk(directory):
        for name in names:
            full = os.path.join(folder, name)
            found[os.path.relpath(full, directory).replace(os.sep, "/")] = hashlib.sha256(open(full, "rb").read()).hexdigest()
    return found


def run_case(case: dict, exe: Path, appdir: Path) -> dict:
    with tempfile.TemporaryDirectory(prefix="sweep-") as directory:
        write_files(directory, case["files"])
        args = [str(exe)] + [a.replace("{file_url}", "file:///" + Path(directory).as_posix()).replace("{cwd}", Path(directory).as_posix()) for a in case["args"]]
        environment = {**os.environ, "HOME": directory, "LC_ALL": "C.UTF-8", "TERM": "dumb", "TZ": "UTC", "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent", "APPDIR": str(appdir)}
        try:
            result = subprocess.run(args, input=case["stdin"].encode(), capture_output=True, timeout=RUN_TIMEOUT, cwd=directory, env=environment)
        except subprocess.TimeoutExpired:
            return {"rc": "timeout", "stdout": "", "stderr": "", "files": {}}
        return {"rc": result.returncode, "stdout": result.stdout.decode("utf-8", "replace"), "stderr": result.stderr.decode("utf-8", "replace"),
                "files": snapshot(directory)}


def normalize(text: str, binary: str, exe: Path | None) -> str:
    """Program names as printed by Linux (/usr/bin/x) and by the translation (its bundle path) both reduce to x; line ends are unified."""
    text = text.replace("\r\n", "\n").replace(f"/usr/bin/{binary}", binary)
    if exe is not None:
        text = text.replace(f"{exe.parent.as_posix()}/{exe.stem}", binary).replace(str(exe), binary).replace(exe.name, binary)
    return text


def compare(binary: str, exe: Path, linux: dict, windows: dict, ignore: list[str]) -> tuple[str, list[str]]:
    if windows["rc"] == "timeout":
        return "timeout", []
    if isinstance(windows["rc"], int) and (windows["rc"] < 0 or windows["rc"] >= CRASH_CODE):
        return "crash", [f"exit code {windows['rc']:#x}"]
    differences = [name for name in ("stdout", "stderr") if normalize(windows[name], binary, exe) != normalize(linux[name], binary, None)]
    differences += ["exit code"] if windows["rc"] != linux["rc"] else []
    ignored = set(ignore)
    kept = lambda files: {name: digest for name, digest in files.items() if name not in ignored}
    differences += ["files"] if kept(windows["files"]) != kept(linux["files"]) else []
    return ("matches Linux", []) if not differences else ("differs", differences)


def main() -> None:
    cases = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    out_dir = Path(sys.argv[2])
    linux = json.loads((out_dir / "linux_results.json").read_text())
    executables = {}
    report = {}
    for binary in sorted({c["binary"] for c in cases}):
        executables[binary] = translate(binary, out_dir)
        print(f"{binary}: {'translated' if executables[binary][0] else executables[binary][1][:150]}", flush=True)
    for case in cases:
        key = f"{case['app']}/{case['id']}"
        exe, message = executables[case["binary"]]
        if exe is None:
            report[key] = {"outcome": "does not translate", "detail": [message]}
            continue
        windows = run_case(case, exe, out_dir / "appdir")
        outcome, detail = compare(case["binary"], exe, linux[key], windows, case.get("ignore_files", []))
        report[key] = {"outcome": outcome, "detail": detail, "windows": windows if outcome != "matches Linux" else {}}
    stop_bus()
    Path(sys.argv[3]).write_text(json.dumps(report, indent=1), encoding="utf-8")
    counts: dict[str, int] = {}
    for entry in report.values():
        counts[entry["outcome"]] = counts.get(entry["outcome"], 0) + 1
    print(json.dumps(counts))


if __name__ == "__main__":
    main()
