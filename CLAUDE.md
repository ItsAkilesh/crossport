<!--
  File: CLAUDE.md
  Purpose: Standing instructions for coding agents working on Crossport
  Author: Akilesh
  Date: 2026-10-28
-->

# Crossport: agent instructions

Read docs/DESIGN.md and docs/ROADMAP.md before starting any task.

## Rules

- Work milestone by milestone, in roadmap order. Do not start a milestone
  until the previous one meets its exit criterion.
- Every change must be verifiable: add or update a test, and run it.
- Use Conventional Commits: type(scope): description.
- Never add Co-Authored-By trailers or list Claude as a co-author.
- Never use em dashes in docs or comments.
- Every source file starts with a header: name, purpose, author, date.
  Update the header when the file is modified.
- When a design decision in docs/DESIGN.md section 6 must be made,
  stop and ask instead of choosing silently.
- Pin all dependency and CI action versions. Check for known CVEs before
  adding any package.
