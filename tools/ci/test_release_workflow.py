"""File: tools/ci/test_release_workflow.py
Purpose: Checks that release.yml pins its actions, keeps secrets out of scripts and limits write access to the publish job
Author: Akilesh
Date: 2026-11-05
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
SIGN_SCRIPT = ROOT / "tools" / "ci" / "sign-windows.ps1"


def job_block(name: str) -> str:
    match = re.search(rf"^  {name}:\n(.*?)(?=^  \S|\Z)", WORKFLOW, re.S | re.M)
    assert match, name
    return match.group(1)


class Workflow(unittest.TestCase):
    def test_every_action_is_pinned_to_a_commit(self) -> None:
        uses = re.findall(r"uses:\s*(\S+)", WORKFLOW)
        self.assertTrue(uses)
        for reference in uses:
            self.assertRegex(reference, r"@[0-9a-f]{40}$", reference)

    def test_it_runs_only_for_version_tags(self) -> None:
        self.assertRegex(WORKFLOW, r"on:\n  push:\n    tags: \[")
        self.assertNotIn("pull_request", WORKFLOW)
        self.assertNotIn("workflow_dispatch", WORKFLOW)

    def test_secrets_and_event_values_only_reach_scripts_through_env(self) -> None:
        for line in WORKFLOW.splitlines():
            if re.search(r"\$\{\{\s*(secrets|github\.(ref_name|head_ref|event))", line):
                self.assertRegex(line, r"^\s+[A-Za-z0-9_]+: \$\{\{", line)

    def test_only_the_publish_job_can_write_and_it_sees_no_secrets(self) -> None:
        self.assertNotIn("contents: write", job_block("build"))
        publish = job_block("publish")
        self.assertIn("contents: write", publish)
        self.assertNotIn("secrets.CROSSPORT", publish)

    def test_signing_runs_only_when_a_certificate_exists(self) -> None:
        build = job_block("build")
        self.assertIn("secrets.CROSSPORT_SIGN_PFX_BASE64 != ''", build)
        self.assertIn("if: env.HAS_SIGNING == 'true'", build)
        self.assertIn("sign-windows.ps1", build)
        self.assertNotRegex(WORKFLOW, r"(?i)BEGIN (RSA |EC )?PRIVATE KEY|\.pfx\"? *<<")

    def test_the_certificate_is_removed_even_when_the_build_fails(self) -> None:
        self.assertRegex(WORKFLOW, r"Remove the signing certificate\n\s+if: always\(\)")

    def test_it_publishes_the_installer_and_its_checksum(self) -> None:
        self.assertIn("Crossport-Setup.exe", WORKFLOW)
        self.assertIn("SHA256SUMS.txt", WORKFLOW)
        self.assertIn("installer/package.sh", WORKFLOW)
        self.assertIn("--runtime build/runtime-pack", WORKFLOW)


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell is not installed")
class SignScript(unittest.TestCase):
    def run_script(self, timestamp: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            pfx = Path(directory, "x.pfx")
            pfx.write_bytes(b"not a certificate")
            env = {**os.environ, "CROSSPORT_SIGN_PFX": str(pfx), "CROSSPORT_SIGN_TIMESTAMP": timestamp}
            return subprocess.run(
                ["pwsh", "-NoProfile", "-File", str(SIGN_SCRIPT), str(pfx)],
                capture_output=True, text=True, env=env, check=False,
            )

    def test_a_timestamp_service_off_the_allowlist_is_refused(self) -> None:
        for bad in ("http://evil.example/ts", "http://user@timestamp.digicert.com/", "ftp://timestamp.digicert.com/"):
            result = self.run_script(bad)
            self.assertNotEqual(result.returncode, 0, bad)
            self.assertIn("allowlist", result.stdout + result.stderr, bad)

    def test_without_a_certificate_file_it_stops(self) -> None:
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPORT_SIGN")}
        result = subprocess.run(["pwsh", "-NoProfile", "-File", str(SIGN_SCRIPT), "a.exe"],
                                capture_output=True, text=True, env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CROSSPORT_SIGN_PFX", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
