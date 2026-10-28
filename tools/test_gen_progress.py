"""File: tools/test_gen_progress.py
Purpose: Tests for tools/gen_progress.py: summary math, validation and SVG well-formedness
Author: Akilesh
Date: 2026-10-28
"""

from __future__ import annotations

import json
import tempfile
import unittest
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import gen_progress

REPO_ROOT = Path(__file__).resolve().parent.parent


def sample() -> list[gen_progress.Milestone]:
    done = gen_progress.Milestone("M0", "A", "done", (gen_progress.Task("t", True),))
    nxt = gen_progress.Milestone("M1", "B & C", "next", (gen_progress.Task("<t>", False),))
    planned = gen_progress.Milestone("M2", "D", "planned", (gen_progress.Task("t", False),))
    return [done, nxt, planned]


class GenProgressTest(unittest.TestCase):
    def test_summary_counts_only_done_milestones(self) -> None:
        self.assertEqual(gen_progress.summarize(sample()), (1, 3))

    def test_summary_of_nothing_is_zero(self) -> None:
        self.assertEqual(gen_progress.summarize([]), (0, 0))

    def test_svg_is_well_formed_and_escapes_text(self) -> None:
        svg = gen_progress.render_svg(sample())
        root = ElementTree.fromstring(svg)
        self.assertTrue(root.tag.endswith("svg"))
        self.assertIn("B &amp; C", svg)
        self.assertIn("&lt;t&gt;", svg)

    def test_unknown_status_is_rejected(self) -> None:
        bad = {"milestones": [{"id": "M0", "title": "x", "status": "weird", "tasks": []}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "p.json"
            path.write_text(json.dumps(bad), encoding="utf-8")
            with self.assertRaises(ValueError):
                gen_progress.load_milestones(path)

    def test_checked_in_data_loads_and_matches_the_committed_svg(self) -> None:
        milestones = gen_progress.load_milestones(REPO_ROOT / "docs" / "progress.json")
        committed = (REPO_ROOT / "docs" / "progress.svg").read_text(encoding="utf-8")
        self.assertEqual(gen_progress.render_svg(milestones), committed.replace("\r\n", "\n"))


if __name__ == "__main__":
    unittest.main()
