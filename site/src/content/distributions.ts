/*
 * File: site/src/content/distributions.ts
 * Purpose: The per-platform distributions, install steps and honest limits shown on the page
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * Mirrors README.md ("One project, one distribution per platform", "Use Crossport on Windows") and
 * docs/DESIGN.md section 6; when those change, change this.
 */

export interface Distribution {
  readonly id: string;
  readonly platform: string;
  readonly translates: string;
  readonly status: string;
  readonly isAvailable: boolean;
}

export const DISTRIBUTIONS: readonly Distribution[] = [
  {
    id: "windows",
    platform: "Windows, x86-64",
    translates: "x86-64 AppImages into Windows PE files, keeping the machine code",
    status: "Under way: Milestones 0 to 9",
    isAvailable: true,
  },
  {
    id: "macos",
    platform: "macOS, Apple silicon",
    translates: "aarch64 AppImages into Mach-O arm64 files, natively",
    status: "Planned: Milestone 10",
    isAvailable: false,
  },
];

export interface InstallStep {
  readonly title: string;
  readonly command: string;
}

export const INSTALL_STEPS: readonly InstallStep[] = [
  { title: "Get the source", command: "git clone https://github.com/ItsAkilesh/crossport.git\ncd crossport" },
  { title: "Build the translator", command: "cargo build --release --locked --manifest-path translator/Cargo.toml" },
  { title: "Build the shim", command: "bash tools/build-shim.sh" },
  {
    title: "Convert an AppImage",
    command:
      "translator/target/release/crossport convert MyApp.AppImage --shim build/shim/crossport_libc.dll --out C:/Apps/MyApp",
  },
];

export const REQUIREMENTS: readonly string[] = [
  "Windows 10 or 11, 64-bit",
  "Git (it includes Git Bash)",
  "Rust, the stable toolchain",
  "A 64-bit mingw-w64 gcc on your PATH",
  "Python 3",
];

export const LIMITS: readonly string[] = [
  "KeePassXC (Qt 6) shows its window, but Qt support is not finished: no XKB yet, and its contents are not checked. GTK applications do not run yet.",
  "Sound is not done. The clipboard works for text.",
  "Programs that make raw Linux system calls (rare outside the C library) are not translated; the translator warns about them.",
  "Electron, the JVM and other programs that generate machine code at run time are out of scope.",
  "A setup wizard and an AppImage launcher exist (installer/package.sh builds them) but there is no signed download yet. You build Crossport once, then double-click AppImages or convert them by command.",
];

export const AUTHOR_NAME = "Akilesh Srinivasa Kumar";

export const REPOSITORY_URL = "https://github.com/ItsAkilesh/crossport";
