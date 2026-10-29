<!--
  File: README.md
  Purpose: The single guide to Crossport: why it exists, how it works, what is built and how to run the harness.
  Author: Akilesh
  Date: 2026-10-28
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
> AppImages and Vulkan programs; graphical toolkit applications are the next step. See the status below.</sub>

[![CI](https://github.com/ItsAkilesh/crossport/actions/workflows/ci.yml/badge.svg)](https://github.com/ItsAkilesh/crossport/actions/workflows/ci.yml)
[![Rust](https://img.shields.io/badge/tooling-rust-000000?logo=rust&logoColor=white)](https://www.rust-lang.org)
[![Platforms](https://img.shields.io/badge/targets-linux%20%E2%86%92%20windows-0E7490)](#the-idea)
[![Milestones](https://img.shields.io/badge/milestones-5%20of%2010-1A7F37)](#what-you-get)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

<p align="center">
  <img src="docs/progress.svg" alt="Crossport progress: 5 of 10 milestones done" width="880">
</p>

<sub>Progress is generated from [docs/progress.json](docs/progress.json) by `python tools/gen_progress.py`.
Each cell is one roadmap task; the full list is in [docs/ROADMAP.md](docs/ROADMAP.md).
Milestones differ greatly in size; the graphics and toolkit milestones are the largest. See
"Beyond Milestone 9" in the roadmap.</sub>

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

Both platforms run x86-64, so the application's machine code executes as is. Crossport translates
the **binary format** and the **operating system interface**. Nothing is interpreted, emulated, or
virtualized at runtime.

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

## Contents

- [Why this exists](#why-this-exists)
- [The idea](#the-idea)
- [How it works](#how-it-works)
- [Scope](#scope)
- [Status](#status)
- [Run the test harness](#run-the-test-harness)
- [Repository layout](#repository-layout)
- [Technologies used](#technologies-used)
- [Thanks](#thanks)
- [License](#license)
- [Why Crossport?](#why-crossport)

## Scope

**In scope (first):** ahead-of-time compiled x86-64 Linux applications distributed as AppImages,
starting with command-line tools, then Vulkan and GUI applications.

**Out of scope (for now):**

- Apps that generate machine code at runtime (JIT compilers such as V8, LuaJIT, the JVM). This
  includes Electron apps themselves. Crossport is not a way to run Electron apps elsewhere; it is a
  way to make Electron unnecessary for portability.
- Non-x86-64 targets such as Windows on ARM.
- macOS, which is a later phase once the Windows path is proven.

## Status

Early. Milestones 0 to 5 are done. Real AppImages of Lua, SQLite and jq are unpacked, translated and
match their Linux output byte for byte on all 45 test invocations, and Vulkan programs render and present
through the Windows Vulkan loader (an offscreen triangle and an xcb window). Around them the shim now
provides most of what everyday programs use: C++ exceptions, pipes, sockets, epoll, timers, child
processes, file mappings, locales and iconv. `crossport convert` turns an AppImage into a runnable bundle
in one step. The next milestone is the graphics core (an in-process X11, then OpenGL and EGL), followed by real Qt
and GTK applications, an installer, and hardening; see docs/ROADMAP.md.

The verification harness came first so every step has fast, objective feedback: a `difftest` runner
records a program's exit code, stdout and stderr, and CI compares the Linux run against the Windows
run.

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
| `shim/` | Linux ABI implemented on Win32 (libc, pthreads, graphics, windowing) |
| `tests/corpus/` | Test applications and differential test definitions |
| `tools/` | The `difftest` runner, corpus scripts, coverage dashboard (planned) |
| `docs/` | Design, roadmap and logo |

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
