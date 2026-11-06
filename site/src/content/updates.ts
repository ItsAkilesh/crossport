/*
 * File: site/src/content/updates.ts
 * Purpose: The latest changes and the installer screenshots shown on the page, taken from CHANGELOG.md, docs/ROADMAP.md and installer/README.md
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * Mirrors CHANGELOG.md (0.7.5 to 0.7.7 and the installer entry); when those change, change this. The screenshots
 * are the ones in docs/images, copied to public/screens. No screenshot of the KeePassXC window is in the
 * repository yet, so the page shows an empty, labelled slot for it instead of an invented picture.
 */

export interface Update {
  readonly version: string;
  readonly title: string;
  readonly summary: string;
}

export const UPDATES: readonly Update[] = [
  {
    version: "0.7.7",
    title: "KeePassXC shows its window",
    summary:
      "KeePassXC 2.8.0 (Qt 6) runs translated: Qt's xcb platform paints the main window and a dialog, with text, icons and menus, on the in-process X11, and the keyboard and mouse reach it. Still open for Qt: XKB, icon themes beyond the built-in ones, auto-type, a check that the window's contents are right, and GTK.",
  },
  {
    version: "0.7.6",
    title: "A real Qt application starts",
    summary:
      "KeePassXC 2.8.0 (153 modules with its plugins) starts on Windows, brings up Qt's xcb platform and answers --version and --help as on Linux. Downloads of pinned files are retried on network failures.",
  },
  {
    version: "0.7.5",
    title: "The xcb requests a toolkit makes",
    summary:
      "About 100 xcb functions on top of the shim's windows, properties, pixel buffers and keyboard tables, checked against a real X server by the xcb_requests test.",
  },
  {
    version: "Installer",
    title: "A setup wizard and an AppImage launcher",
    summary:
      "installer/ builds crossport-setup, which installs Crossport, and crossport-open, which converts an AppImage into an installed Windows app. Not yet done: code signing, update checks, a release download, the runtime pack inside the installer.",
  },
];

export interface Screenshot {
  readonly file: string;
  readonly caption: string;
  readonly alt: string;
}

export const SETUP_SCREENSHOTS: readonly Screenshot[] = [
  { file: "setup-1-welcome.png", caption: "Setup: welcome", alt: "Crossport Setup, welcome page" },
  { file: "setup-3-folder.png", caption: "Setup: install folder", alt: "Crossport Setup, install folder page" },
  { file: "setup-4-options.png", caption: "Setup: options", alt: "Crossport Setup, options page" },
];

export const LAUNCHER_SCREENSHOTS: readonly Screenshot[] = [
  { file: "lua-1-name.png", caption: "Launcher: name", alt: "Launcher, name page" },
  { file: "lua-3-shortcuts.png", caption: "Launcher: shortcuts", alt: "Launcher, shortcuts page" },
  { file: "lua-5-result.png", caption: "Launcher: installed", alt: "Launcher, app installed" },
  { file: "vlc-5-result.png", caption: "Launcher: an app that cannot be converted yet", alt: "Launcher, this app cannot be converted yet" },
];

export const INSTALLER_FACTS: readonly string[] = [
  "Setup installs the translator, the shim and the launcher to a folder you choose, with Start menu and desktop shortcuts, a PATH entry, the .AppImage association and an entry in Apps and features.",
  "The launcher reads the app's name and icon from the image, runs crossport convert, registers the app with its own uninstaller, and shows a readable report when an app is unsupported.",
  "An install is machine scope (all users) under Program Files or a folder you cannot write to, otherwise your own account; only machine scope asks for elevation.",
  "A failed conversion leaves no folder, registry key or shortcut behind, and an uninstall refuses to run while the app is open.",
  "Not code signed, and the wizard is not DPI aware yet, so Windows scales it.",
];
