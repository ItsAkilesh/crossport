#!/usr/bin/env python3
"""File: tools/gen_progress.py
Purpose: Render docs/progress.json into docs/progress.svg, the README progress indicator
Author: Akilesh
Date: 2026-10-28

Usage: python tools/gen_progress.py [input.json] [output.svg]
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from html import escape
from pathlib import Path

WIDTH = 880
PADDING = 24
FOOTER_HEIGHT = 52
FOOTER_TEXT = "Milestones differ greatly in size. M6 is a first real GUI app, not the finished product."
HEADER_Y = 38
BAR_Y = 56
BAR_HEIGHT = 14
BAR_GAP = 4
ROWS_Y = 96
ROW_HEIGHT = 40
CELL_SIZE = 16
CELL_GAP = 5
CELLS_X = 420
FONT = "-apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"

STATUS_LABELS = {"done": "done", "active": "in progress", "next": "up next", "planned": "planned"}

STYLE = """
  :root { --fg:#1F2328; --muted:#656D76; --line:#D0D7DE; --bg:#FFFFFF;
          --done:#1A7F37; --next:#BF8700; --todo:#D0D7DE; }
  @media (prefers-color-scheme: dark) {
    :root { --fg:#E6EDF3; --muted:#8D96A0; --line:#30363D; --bg:#0D1117;
            --done:#2EA043; --next:#D29922; --todo:#30363D; }
  }
  text { font-family: %s; fill: var(--fg); }
  .muted { fill: var(--muted); }
""" % FONT


@dataclass(frozen=True)
class Task:
    title: str
    is_done: bool


@dataclass(frozen=True)
class Milestone:
    id: str
    title: str
    status: str
    tasks: tuple[Task, ...]


def load_milestones(path: Path) -> list[Milestone]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    milestones = []
    for item in raw["milestones"]:
        if item["status"] not in STATUS_LABELS:
            raise ValueError(f"unknown status {item['status']!r} for {item['id']}")
        tasks = tuple(Task(t["title"], bool(t["done"])) for t in item["tasks"])
        milestones.append(Milestone(item["id"], item["title"], item["status"], tasks))
    return milestones


def summarize(milestones: list[Milestone]) -> tuple[int, int]:
    """Returns (done milestones, total milestones). No percentage: milestones differ greatly in size."""
    done = sum(1 for m in milestones if m.status == "done")
    total = len(milestones)
    return done, total


def status_color(status: str) -> str:
    return {"done": "var(--done)", "active": "var(--next)", "next": "var(--next)"}.get(status, "var(--todo)")


def status_text_color(status: str) -> str:
    """Text needs more contrast than the grey fill used for planned cells."""
    return "var(--muted)" if status == "planned" else status_color(status)


def render_header(done: int, total: int) -> str:
    return (
        f'<text x="{PADDING}" y="{HEADER_Y}" font-size="20" font-weight="700">Milestones</text>'
        f'<text x="{WIDTH - PADDING}" y="{HEADER_Y}" font-size="15" text-anchor="end" '
        f'class="muted">{done} of {total} milestones done</text>'
    )


def render_overall_bar(milestones: list[Milestone]) -> str:
    count = len(milestones)
    usable = WIDTH - 2 * PADDING - BAR_GAP * (count - 1)
    segment = usable / count
    parts = []
    for index, milestone in enumerate(milestones):
        x = PADDING + index * (segment + BAR_GAP)
        parts.append(
            f'<rect x="{x:.1f}" y="{BAR_Y}" width="{segment:.1f}" height="{BAR_HEIGHT}" '
            f'rx="4" fill="{status_color(milestone.status)}">'
            f"<title>{escape(milestone.id)} {escape(milestone.title)}: "
            f"{STATUS_LABELS[milestone.status]}</title></rect>"
        )
    return "".join(parts)


def render_cell(x: float, y: float, milestone: Milestone, task: Task) -> str:
    if task.is_done:
        style = f'fill="{status_color("done")}"'
    elif milestone.status in ("active", "next"):
        style = f'fill="none" stroke="{status_color("next")}" stroke-width="2"'
    else:
        style = f'fill="{status_color("planned")}"'
    return (
        f'<rect x="{x}" y="{y}" width="{CELL_SIZE}" height="{CELL_SIZE}" rx="3" {style}>'
        f"<title>{escape(task.title)}</title></rect>"
    )


def render_row(index: int, milestone: Milestone) -> str:
    top = ROWS_Y + index * ROW_HEIGHT
    middle = top + ROW_HEIGHT / 2
    cells = "".join(
        render_cell(CELLS_X + i * (CELL_SIZE + CELL_GAP), middle - CELL_SIZE / 2, milestone, task)
        for i, task in enumerate(milestone.tasks)
    )
    return (
        f'<line x1="{PADDING}" x2="{WIDTH - PADDING}" y1="{top}" y2="{top}" '
        f'stroke="var(--line)" stroke-width="1"/>'
        f'<text x="{PADDING}" y="{middle + 5}" font-size="14" font-weight="700">{escape(milestone.id)}</text>'
        f'<text x="{PADDING + 40}" y="{middle + 5}" font-size="15">{escape(milestone.title)}</text>'
        f"{cells}"
        f'<text x="{WIDTH - PADDING}" y="{middle + 5}" font-size="13" text-anchor="end" '
        f'style="fill:{status_text_color(milestone.status)}">'
        f"{STATUS_LABELS[milestone.status]}</text>"
    )


def render_svg(milestones: list[Milestone]) -> str:
    done, total = summarize(milestones)
    rows_end = ROWS_Y + len(milestones) * ROW_HEIGHT
    height = rows_end + FOOTER_HEIGHT
    body = render_header(done, total) + render_overall_bar(milestones)
    body += "".join(render_row(i, m) for i, m in enumerate(milestones))
    body += (
        f'<text x="{PADDING}" y="{rows_end + 28}" font-size="12" class="muted">'
        f"{escape(FOOTER_TEXT)}</text>"
    )
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {height}" '
        f'width="{WIDTH}" role="img" aria-label="Crossport progress: {done} of {total} milestones done">'
        f"<!-- File: progress.svg | Purpose: README progress indicator, generated by tools/gen_progress.py "
        f"| Author: Akilesh | Date: 2026-10-28 -->"
        f"<style>{STYLE}</style>"
        f'<rect width="{WIDTH}" height="{height}" rx="12" fill="var(--bg)" '
        f'stroke="var(--line)"/>{body}</svg>\n'
    )


def main(argv: list[str]) -> int:
    source = Path(argv[1]) if len(argv) > 1 else Path("docs/progress.json")
    target = Path(argv[2]) if len(argv) > 2 else Path("docs/progress.svg")
    target.write_text(render_svg(load_milestones(source)), encoding="utf-8", newline="\n")
    print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
