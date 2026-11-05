<!--
  File: README.md
  Purpose: The single guide to Crossport: why it exists, how it works, what is built, how to install and use it, and how to run the harness.
  Author: Akilesh
  Date: 2026-11-04
-->

<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logo/wordmark-dark.svg">
    <img alt="Crossport" src="docs/logo/wordmark-light.svg" height="96">
  </picture>
</h1>

<p align="center"><b>Native Linux apps, translated ahead of time into native Windows apps. No emulation, no virtual machine, no bundled browser.</b></p>

> **Nobody likes Electron apps.**
>
> Not the people who run them, with their chat client eating a gigabyte of RAM. Not the laptops
> whose fans spin up to show a to-do list. Developers ship a whole copy of Chromium because it is the
> only affordable way to be everywhere at once. So let us make the other way affordable: build it
> once, run it natively on every desktop, and keep the browser for browsing.
>
> <sub>Crossport is a working name and the project is at an early stage. It translates real command-line
> AppImages and runs SDL2, Vulkan, OpenGL and EGL programs; Qt and GTK applications are the next step.
> There is no installer yet: see [Use Crossport on Windows](#use-crossport-on-windows) to build and use it.</sub>

[![CI](https://github.com/ItsAkilesh/crossport/actions/workflows/ci.yml/badge.svg)](https://github.com/ItsAkilesh/crossport/actions/workflows/ci.yml)
[![Rust](https://img.shields.io/badge/tooling-rust-000000?logo=rust&logoColor=white)](https://www.rust-lang.org)
[![Platforms](https://img.shields.io/badge/targets-linux%20%E2%86%92%20windows-0E7490)](#the-idea)
[![Milestones](https://img.shields.io/badge/milestones-5%20of%2011-1A7F37)](#what-you-get)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Website](https://img.shields.io/badge/website-itsakilesh.github.io%2Fcrossport-0E7490)](https://itsakilesh.github.io/crossport/)

<p align="center">
  <img src="docs/progress.svg" alt="Crossport progress: 5 of 11 milestones done" width="880">
</p>

<sub>Progress is generated from [docs/progress.json](docs/progress.json) by `python tools/gen_progress.py`.
Each cell is one roadmap task; the full list is in [docs/ROADMAP.md](docs/ROADMAP.md).
Milestones differ greatly in size; the graphics and toolkit milestones are the largest. See
"Beyond Milestone 10" in the roadmap.</sub>

Crossport takes an existing Linux AppImage and turns it into a native Windows application by
translating the executable format and the operating system interface, not the machine code. The app
runs at full speed, uses a normal amount of memory, and looks and behaves like any other program on
the machine.

If you also wish desktop software felt lighter, a star on the repo is the nicest encouragement.

## Why this exists

Cross-platform desktop software has quietly converged on one answer: ship a copy of Chromium with
every app. Electron and similar frameworks made portability easy, but users pay for it in memory,
disk space, startup time, battery life, and apps that never quite feel at home on their OS. A chat
client should not need the footprint of a web browser.

Developers choose Electron not because they want a browser in their app, but because building and
maintaining truly native apps for Linux, Windows, and macOS is expensive. Crossport attacks that
cost directly. The goal is near-native applications that run across platforms, without making every
developer pay the Chromium tax for portability.

## The idea

AppImage already proved that a Linux app can be packaged once and run on almost every distribution.
Crossport extends that idea across operating systems using **static binary translation**:

1. Take an existing AppImage.
2. Convert its ELF binaries into native Windows PE executables and DLLs.
3. Replace the small set of host-provided Linux libraries (glibc, graphics, display server) with
   shims implemented on Win32.
4. Produce a Windows-native bundle that the Windows loader runs directly.

The AppImage and its target run the same architecture (x86-64 for Windows, aarch64 for Apple silicon), so
the application's machine code executes as is. Crossport translates the **binary format** and the
**operating system interface**. Nothing is interpreted, emulated, or virtualized at runtime. That is why each
target platform has its own distribution; see [One project, one distribution per platform](#one-project-one-distribution-per-platform).

This is the same principle used by AnyPS5 (relinking console executables into native formats) and,
in the opposite direction, by Wine.

## How it works

```
AppImage
  | 1. Unpack: locate the SquashFS payload, extract the AppDir
  v
AppDir (ELF executable + bundled .so files)
  | 2. Analyze: build the dependency graph, classify each library
  |      bundled -> translate
  |      host    -> replace with a Crossport shim
  v
  | 3. Relink: each ELF object becomes a PE image (.exe / .dll)
  | 4. Rewrite: syscall sites and FS-relative TLS accesses
  | 5. Bind: resolve imports against translated DLLs and shims
  v
Windows-native bundle (.exe + DLLs + data)
```

See [docs/DESIGN.md](docs/DESIGN.md) for the technical design.

## What you get

Crossport is built in milestones, each with an exit criterion that a test can check. The full list
lives in [docs/ROADMAP.md](docs/ROADMAP.md).

- [x] **Milestone 0:** verification harness (Windows CI and a Linux-versus-Windows differential test)
- [x] **Milestone 1:** translate a dynamically linked "hello world"
- [x] **Milestone 2:** shared libraries relinked into DLLs
- [x] **Milestone 3:** thread-local storage and threads
- [x] **Milestone 4:** a corpus of command-line AppImages (Lua, SQLite, jq)
- [x] **Milestone 5:** Vulkan rendering in a window
- [ ] **Milestone 6:** graphics core (X11 in process, OpenGL and EGL, SDL2 apps)
- [ ] **Milestone 7:** real Qt and GTK applications
- [ ] **Milestone 8:** everyday use (installer, file associations, signing)
- [ ] **Milestone 9:** hardening and breadth (performance, security, a 50-app corpus)
- [ ] **Milestone 10:** the macOS distribution for Apple silicon, translating aarch64 AppImages (planned)

## Contents

- [Why this exists](#why-this-exists)
- [The idea](#the-idea)
- [How it works](#how-it-works)
- [One project, one distribution per platform](#one-project-one-distribution-per-platform)
- [Scope](#scope)
- [Status](#status)
- [Use Crossport on Windows](#use-crossport-on-windows)
- [Run the test harness](#run-the-test-harness)
- [Repository layout](#repository-layout)
- [Technologies used](#technologies-used)
- [Thanks](#thanks)
- [License](#license)
- [Why Crossport?](#why-crossport)

## One project, one distribution per platform

Crossport is a single project, but it is not a one-size-fits-all package. Each target platform gets its own
distribution, and each distribution translates the AppImages built for that platform's own architecture:

| Distribution | Translates | Status |
|---|---|---|
| Windows, x86-64 | x86-64 AppImages into Windows PE files, keeping the machine code | Under way (Milestones 0 to 9) |
| macOS, Apple silicon | aarch64 AppImages into Mach-O arm64 files, natively | Planned (Milestone 10) |

There is no universal download, no fat bundle and no emulation (Rosetta 2 is not used). An application that
wants to reach both platforms publishes an x86-64 AppImage and an aarch64 AppImage, and each distribution
converts the one that matches it. The guide below, "Use Crossport on Windows", is for the Windows
distribution.

## Scope

**In scope (first):** ahead-of-time compiled x86-64 Linux applications distributed as AppImages,
starting with command-line tools, then Vulkan and GUI applications.

**Out of scope (for now):**

- Apps that generate machine code at runtime (JIT compilers such as V8, LuaJIT, the JVM). This
  includes Electron apps themselves. Crossport is not a way to run Electron apps elsewhere; it is a
  way to make Electron unnecessary for portability.
- Non-x86-64 targets such as Windows on ARM.
- macOS on Apple silicon is planned as Milestone 10, once the Windows path is proven. It is a separate
  distribution that translates aarch64 AppImages natively; see "One project, one distribution per platform".

## Status

Early. Milestones 0 to 5 are done and Milestone 6 (the graphics core) is under way. Real AppImages of Lua,
SQLite and jq are unpacked, translated and match their Linux output byte for byte on all 45 test
invocations. Vulkan programs render and present through the Windows Vulkan loader. An unmodified Linux SDL2
(X11 driver) opens a window, draws and receives keyboard, text, pointer and wheel events, and OpenGL
(through GLX) and EGL programs, including an OpenGL ES 2 context, render the same pixels as on Linux. All of
that runs on an in-process X11 (Xlib and xcb on Win32 windows) with no X server. `crossport convert` turns an
AppImage into a runnable bundle in one step. The clipboard works for text. Milestone 6 is complete: a real SDL2 program (SDL's own test harness) runs, and so do GL samples. What is not done:
text drawing through fonts, XKB, and real Qt and GTK applications (Milestone 7), then an installer and hardening; see docs/ROADMAP.md and
[CHANGELOG.md](CHANGELOG.md).

The verification harness came first so every step has fast, objective feedback: a `difftest` runner
records a program's exit code, stdout and stderr, and CI compares the Linux run against the Windows
run.

## Use Crossport on Windows

There is no installer or download yet (that is Milestone 8), so for now you build the two pieces yourself,
once, and then convert each AppImage you want to run. Everything stays in folders you choose: nothing is
installed system-wide, no registry keys are written and no administrator rights are needed.

### What works today

| Kind of AppImage | Status |
|---|---|
| Command-line tools (the corpus has Lua, SQLite and jq) | Works. Output matches Linux byte for byte. |
| C++ programs that use exceptions, threads, sockets, pipes or child processes | Works in the test corpus. |
| Vulkan programs (offscreen and in a window) | Works. Needs a Windows Vulkan driver. |
| SDL2 programs using the X11 driver (windows, software drawing, keyboard, mouse) | Works for the test programs. |
| OpenGL (GLX) and EGL programs, including OpenGL ES 2 | Works for the test programs. Needs an OpenGL driver. |
| Qt and GTK applications | **Not yet.** They need fonts, XKB, D-Bus and more (Milestone 7). Try `coverage` first. |
| Anything that draws text through X fonts or fontconfig, or uses sound | Not yet. Sound libraries are not provided, so audio is silent when they are stubbed. |
| Electron, the JVM and other apps that generate machine code at run time | Out of scope. |

Most graphical AppImages in the wild are Qt, GTK or Electron, so **most will not run yet.** Command-line
tools and SDL2, Vulkan and OpenGL programs are the ones to try.

### What you need

To run a converted app: Windows 10 or 11, 64-bit. The converted folder is self-contained (no Visual C++
redistributable, no Linux layer). Graphics programs also need the matching driver: a Vulkan driver (installed
with most GPU drivers) for Vulkan, and an OpenGL driver for OpenGL and EGL. See
[Graphics programs](#graphics-programs).

To build Crossport you also need:

- [Git](https://git-scm.com/download/win) (it includes Git Bash, which runs the build script)
- [Rust](https://rustup.rs) (the stable toolchain)
- A 64-bit mingw-w64 `gcc` on your `PATH`, for example from [WinLibs](https://winlibs.com) or MSYS2 (UCRT).
  `gcc --version` should mention `x86_64-w64-mingw32`.
- Python 3

### Install (build from source)

In Git Bash:

```bash
git clone https://github.com/ItsAkilesh/crossport.git
cd crossport

# 1. The translator (a few minutes the first time)
cargo build --release --locked --manifest-path translator/Cargo.toml

# 2. The shim: the Linux system interface, implemented on Windows (several minutes)
bash tools/build-shim.sh
```

That produces the two files you need:

- `translator/target/release/crossport.exe`
- `build/shim/crossport_libc.dll`

Optionally copy them to a folder of your own, for example `%LOCALAPPDATA%\Crossport`, and add that folder to
your `PATH`. The rest of this guide writes `crossport` for the translator and `crossport_libc.dll` for the
shim, so use their full paths if you did not do that.

### Convert an AppImage and run it

Download an x86-64 Linux AppImage from the project that makes it, then convert it into a new, empty folder:

```bash
crossport convert MyApp-x86_64.AppImage --shim crossport_libc.dll --out C:/Apps/MyApp
```

`convert` unpacks the AppImage, translates its program and every bundled library into Windows files, copies
the shim next to them and writes a small `crossport.env` file holding the variables the AppImage's launcher
script would have set. When it finishes, the folder looks like this:

```
C:/Apps/MyApp/
  myapp.exe              the translated program (named after the AppImage's program)
  *.dll                  its translated libraries
  crossport_libc.dll     the shim
  crossport.env          environment for the program (APPDIR and the like)
  appdir/                the unpacked AppImage: data files, plugins, icons
```

Run `myapp.exe` from that folder (double-click it, or start it from a terminal to see its output). The
folder can be moved as a whole. To start it from the Start menu or the desktop, make a shortcut to the
`.exe` yourself; file associations and a launcher arrive with Milestone 8.

Conversion prints what it did. Two lines are worth reading:

- `program: ...` is the file the AppImage runs. If it starts through a shell script, Crossport takes the
  program from the `Exec=` line of the AppImage's desktop file.
- `N symbols nobody provides are bound to a stub that returns 0` appears only with `--stub-missing` (below).

### When conversion stops or the app misbehaves

| You see | What it means and what to do |
|---|---|
| `crossport: library libfoo.so.1 was not found` | The AppImage expects a library from its Linux host that it does not bundle (libstdc++ is a common one). Put Linux copies of such libraries in a folder and pass `--runtime <folder>` (repeatable), or add `--stub-missing`. |
| `--stub-missing` was used and the app starts but behaves oddly | A stubbed library or function returns 0 (null, false, empty) if it is called, without any error. The list the translator printed is the first place to look. The flag is a way to get a program running that never really needs the missing piece (a sound library it does not use, for example). |
| The translator reports symbols that no module and no shim provides | The shim does not implement some function the program uses. Run `crossport coverage` (see below) and open an issue with the list. |
| `crossport: access violation at myapp.exe+0x1234, reading 0x0` followed by `from ...` lines | The program crashed. The first line names the module, offset and kind of fault; the lines below are a rough call trail. Include them in a bug report. `python tools/ci/symbolize.py` turns offsets inside `crossport_libc.dll` into function names (it needs `nm` and `objdump` from mingw-w64). |
| A window opens but stays blank, or OpenGL or Vulkan setup fails | See [Graphics programs](#graphics-programs). |
| It runs on Linux but not here | The test harness below is how Crossport compares the two; a failing case is a useful bug report. |

To see how well Crossport covers an AppImage before converting it, unpack it and ask for a coverage report:

```bash
crossport unpack MyApp-x86_64.AppImage --out C:/Apps/MyApp-unpacked
crossport coverage --shim crossport_libc.dll --appdir C:/Apps/MyApp-unpacked --allow-missing C:/Apps/MyApp-unpacked/usr/bin/myapp
```

The report counts the C library functions the program and its libraries use against the ones the shim
implements. A program that uses functions the shim lacks will not translate in strict mode.

### Graphics programs

- **Vulkan** programs use the Vulkan driver that comes with your graphics card driver (`vulkan-1.dll`).
- **OpenGL and EGL** programs use the OpenGL driver of your graphics card (`opengl32.dll`).
- On a machine with no graphics driver (a virtual machine or a CI runner), the test suite uses a software
  renderer: the pinned Mesa build described in `tools/ci/install-opengl-windows.sh` provides `opengl32.dll`
  and `libgallium_wgl.dll`. Putting those two files next to the program's `.exe` makes it use them.
  Setting `GALLIUM_DRIVER=llvmpipe` selects Mesa's CPU renderer.
- Every window the program creates is an ordinary Windows window, and keyboard and mouse input arrive as
  X11 events, so SDL2 and similar libraries see the input they expect.

### Update and remove

- **Update:** `git pull`, then repeat the two build commands, then convert the AppImage again. A converted
  folder embeds the shim it was built with, so existing folders keep working but do not get fixes until you
  convert them again.
- **Remove:** delete the converted folder, the `crossport` clone and any folder you copied the tools to.
  Crossport itself writes nothing elsewhere. A converted application may of course keep its own settings and
  data where it chooses, as it would on Linux (under the home directory the shim reports).

### A note on licences

A converted folder contains the application's own code, translated, together with Crossport's shim (MIT).
The application's licence still applies to its part, so check it before sharing a converted copy. Crossport
does not host or distribute anyone's applications.

## Run the test harness

You need [Rust](https://rustup.rs), a C compiler (`gcc`) and a bash shell. On Windows, Git Bash and
a mingw-w64 `gcc` work.

```bash
git clone https://github.com/ItsAkilesh/crossport.git
cd crossport

# Unit tests for the runner
cargo test --manifest-path tools/difftest/Cargo.toml

# Build the "hello" corpus case natively and record its result
tools/run-case.sh hello linux      # or: tools/run-case.sh hello windows

# Compare two recorded results (exit code 0 means identical)
tools/difftest/target/release/difftest compare results/hello-linux.json results/hello-windows.json
```

CI runs the same steps on Linux and Windows and fails the build if the two results differ.

To translate the dynamically linked `hello_dyn` case on Windows, take the ELF that the Linux CI job
uploads (artifact `elf-corpus`), place it under `build/elf/` (so the program is `build/elf/hello_dyn/hello_dyn`), then:

```bash
tools/run-translated.sh hello_dyn     # builds the shim, translates, runs, records the result
```

To convert a whole AppImage in one step (unpack, translate its program and libraries, and write the
environment file the program needs), give `convert` the image, the shim and an empty output directory:

```bash
translator/target/release/crossport convert app.AppImage --shim build/shim/crossport_libc.dll --out build/app
build/app/app.exe        # the unpacked AppDir is in build/app/appdir
```

When the AppImage starts through a shell script, the program is taken from the `Exec=` line of its
desktop file. Libraries the AppImage expects from its host (libstdc++ and similar) can be supplied with
`--runtime <dir>`, a directory of Linux copies of them.

You can also drive the translator directly:

```bash
translator/target/release/crossport analyze build/elf/hello_dyn/hello_dyn
translator/target/release/crossport translate build/elf/two_libs/two_libs --shim build/shim/crossport_libc.dll --lib-dir build/elf/two_libs --out build/bundle/two_libs
```

## Repository layout

| Path | Contents |
|---|---|
| `translator/` | AppImage unpacker, ELF analyzer, ELF-to-PE relinker, instruction rewriter |
| `shim/` | Linux ABI implemented on Win32 (libc, pthreads, Xlib and xcb, OpenGL, EGL, Vulkan) |
| `tests/corpus/` | Test applications and differential test definitions |
| `tools/` | The `difftest` runner, corpus scripts, coverage dashboard (planned) |
| `third_party/` | Pinned Khronos registries (Vulkan, OpenGL) used to generate the shim's entry points |
| `site/` | The project page ([itsakilesh.github.io/crossport](https://itsakilesh.github.io/crossport/)): Vite, React, three.js and Glisse, built and deployed by `.github/workflows/site.yml` |
| `docs/` | Design, roadmap and logo |
| `CHANGELOG.md` | Versions and what changed |

## Technologies used

- [Rust](https://www.rust-lang.org) for the translator and tooling, with [object](https://crates.io/crates/object) for reading ELF and [serde](https://serde.rs) for result files
- [GitHub Actions](https://docs.github.com/actions) for the Linux and Windows runners
- C (`gcc` / mingw-w64) for the test corpus and the libc shim
- [iced-x86](https://crates.io/crates/iced-x86) to decode and re-encode the instructions that touch thread-local storage

## Thanks

- **AnyPS5:** relinks PS5 executables into native Linux/Windows formats and reimplements system
  libraries; the closest model for Crossport's method.
- **Wine:** the Windows ABI on Linux; the mirror image of Crossport's shim layer.
- **WSL1:** Linux syscalls translated inside the NT kernel.
- **Cygwin:** POSIX semantics on Win32, using the LP64 data model.
- **taviso/loadlibrary:** loads Windows DLLs into Linux processes with calling-convention thunks.
- **Cosmopolitan / APE:** single binaries that run natively on multiple operating systems.
- **AppImage:** for making "build once, run anywhere on Linux" a solved problem.

## License

[MIT](LICENSE). Use it, fork it, make it yours.

## Why Crossport?

### Why this exists

Electron won because it made one codebase run everywhere, not because anyone loves it. The cost
lands on every user, every day, in memory and battery. I want the same reach without the browser:
apps that start instantly, sip memory and feel native. Building the app once for Linux and
translating it is a path to that.

### Who this is for

- **Users** who are tired of apps that cost more RAM than the work they do.
- **Developers** who want to ship on Windows without maintaining a second native codebase or a
  bundled browser.
- **Systems tinkerers** who enjoy ELF, PE, loaders and ABIs and want to see how far static
  translation can go.

It is probably not for you if your app depends on a JIT, targets ARM, or needs macOS today. Those
are outside the first phases.

### Why the name

A *port* is what you do when you move software to another platform, and a port is also where two
things connect. Crossport is both: it carries an app across to another operating system, and the
logo shows the idea, a Linux plug sliding into a Windows socket. The name is a working one and may
change.
