<!--
  File: installer/README.md
  Purpose: How the Crossport setup program and AppImage launcher are built, tested and laid out
  Author: Akilesh
  Date: 2026-11-05
-->

# Crossport installer

One Rust crate, two programs, one library (decision: docs/DESIGN.md section 6).

| Program | What it does |
|---|---|
| `crossport-setup.exe` | Wizard that installs Crossport from a payload appended to itself. `--silent`, `--uninstall <dir>`, `--make-installer <out> --from <dir>`. |
| `crossport-open.exe` | Launcher for `.AppImage` files: wizard, then `crossport convert`, registration, shortcuts. `--silent`, `--uninstall <dir>`. |

Build and pack: `bash installer/package.sh <out-dir> [--runtime <pack-dir>]`.
Test: `cargo test --locked --manifest-path installer/Cargo.toml` (set `CROSSPORT_TEST_APPIMAGE` to also read a real image).

## Layout

- Pure logic (tests run anywhere): `names`, `paths` (scope and elevation rule), `regplan` (registry entries, PATH edits),
  `manifest` (uninstall record), `command` (quoting, convert arguments), `report` (readable conversion report), `cli`,
  `payload`, `ico`, `appimage_meta` (name and icon read from the SquashFS).
- Windows only: `win` (registry, shortcuts, elevation), `engine` (install and uninstall), `ops` (operations shared by the
  wizards and the silent modes), `ui` and `flows` (the wizard framework and the two wizards).

## Rules the code follows

- An install is machine scope (`HKLM`, all-users shortcuts) when its folder is under Program Files or not writable by
  the user, otherwise user scope. Elevation is requested only for machine scope, by relaunching with the same
  arguments, so the wizard itself never runs elevated.
- Every install writes `crossport-uninstall.manifest` and its own `uninstall.exe` (a copy of the program), so removing
  Crossport does not break installed apps. An uninstaller inside the folder it removes runs from a temporary copy.
- Uninstall refuses a running app before removing anything (it renames the folder first).
- A failed conversion leaves no folder, registry key or shortcut behind.
- Windows 10 and 11 protect the default-app choice, so the association entries add Crossport to Open with and make it the
  default only when nothing else is chosen.

## Known limits

Not code signed; the wizard is not DPI aware (Windows scales it); the window layout was driven and checked through the
Win32 control tree, not by eye; some functions in `ui` are longer than the 40 line guideline.
