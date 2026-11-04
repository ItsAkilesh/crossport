<!--
  File: docs/logo/README.md
  Purpose: How the Crossport logo files are made, and the licence of the font its wordmark is drawn from
  Author: Akilesh
  Date: 2026-11-04
-->

# Logo

`mark.svg` is the mark on its own (also served as the site's favicon, `site/public/favicon.svg`). The two
wordmarks pair it with the name for dark and light backgrounds. The mark is three bars crossing a gate: amber
for Linux on the left, blue for Windows on the right, and the fuchsia line between them is the shim.

The files are generated, not drawn by hand:

```bash
pip install fonttools
python tools/gen_logo.py path/to/inter-latin-600-normal.woff
```

The letters of the wordmark are outlines of Inter SemiBold, so the wordmark looks the same on every machine.
Inter is licensed under the SIL Open Font License 1.1 (https://rsms.me/inter/). The mark itself uses no font.
