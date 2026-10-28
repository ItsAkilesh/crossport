<!--
  File: README.md
  Purpose: Project overview, motivation, and high-level architecture for Crossport
  Author: Akilesh
  Date: 2026-10-28
-->

# Crossport

**Native Linux apps, translated ahead of time into native Windows apps. No emulation, no virtual machine, no bundled browser.**

> Crossport is a working name and may change.

## Why this exists

Cross-platform desktop software has quietly converged on one answer: ship a copy of Chromium with every app. Electron and similar frameworks made portability easy, but users pay for it in memory, disk space, startup time, battery life, and apps that never quite feel at home on their OS. A chat client should not need the footprint of a web browser.

Developers choose Electron not because they want a browser in their app, but because building and maintaining truly native apps for Linux, Windows, and macOS is expensive. Crossport attacks that cost directly. The goal is near-native applications that run across platforms, without making every developer pay the Chromium tax for portability.

## The idea

AppImage already proved that a Linux app can be packaged once and run on almost every distribution. Crossport extends that idea across operating systems using **static binary translation**:

1. Take an existing AppImage.
2. Convert its ELF binaries into native Windows PE executables and DLLs.
3. Replace the small set of host-provided Linux libraries (glibc, graphics, display server) with shims implemented on Win32.
4. Produce a Windows-native bundle that the Windows loader runs directly.

Both platforms run x86-64, so the application's machine code executes as is. Crossport translates the **binary format** and the **operating system interface**, not the instructions. Nothing is interpreted, emulated, or virtualized at runtime.

This is the same principle used by AnyPS5 (relinking console executables into native formats) and, in the opposite direction, by Wine.

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

## Scope

**In scope (first):** ahead-of-time compiled x86-64 Linux applications distributed as AppImages, starting with command-line tools, then Vulkan and GUI applications.

**Out of scope (for now):**

- Apps that generate machine code at runtime (JIT compilers such as V8, LuaJIT, the JVM). This includes Electron apps themselves. Crossport is not a way to run Electron apps elsewhere; it is a way to make Electron unnecessary for portability.
- Non-x86-64 targets such as Windows on ARM.
- macOS, which is a later phase once the Windows path is proven.

## Status

Early design. No working translator yet. Progress is tracked in [docs/ROADMAP.md](docs/ROADMAP.md).

## Repository layout

| Path | Contents |
|---|---|
| `translator/` | AppImage unpacker, ELF analyzer, ELF-to-PE relinker, instruction rewriter |
| `shim/` | Linux ABI implemented on Win32 (libc, pthreads, graphics, windowing) |
| `tests/corpus/` | Test applications and differential test definitions |
| `tools/` | Corpus analysis, coverage dashboard, CI helpers |
| `docs/` | Design and roadmap |

## Prior art

- **AnyPS5:** relinks PS5 executables into native Linux/Windows formats and reimplements system libraries; the closest model for Crossport's method.
- **Wine:** the Windows ABI on Linux; the mirror image of Crossport's shim layer.
- **WSL1:** Linux syscalls translated inside the NT kernel.
- **Cygwin:** POSIX semantics on Win32, using the LP64 data model.
- **taviso/loadlibrary:** loads Windows DLLs into Linux processes with calling-convention thunks.
- **Cosmopolitan / APE:** single binaries that run natively on multiple operating systems.

## License

To be decided.
