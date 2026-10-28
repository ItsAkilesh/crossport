<!--
  File: docs/ROADMAP.md
  Purpose: Milestones and exit criteria for Crossport
  Author: Akilesh
  Date: 2026-10-28
-->

# Crossport Roadmap

## Milestone 0: Verification harness (done)

- Windows CI runner executes test binaries.
- Differential test runner compares Linux and Windows output.
- **Exit criterion:** a natively compiled Windows "hello world" passes a differential test against its Linux build.

## Milestone 1: Hello world translation (done)

- Translate a dynamically linked C program that only calls `puts`.
- Minimal shim providing `puts` and process startup and exit.
- **Exit criterion:** translated binary runs on Windows and passes its differential test.

## Milestone 2: Shared libraries (done)

- Multiple `.so` files relinked into DLLs.
- Relocations, GOT binding, `.init_array` and `.fini_array`.
- **Exit criterion:** a program using two bundled libraries passes.

## Milestone 3: TLS and threads

- FS-relative TLS rewriting.
- pthreads on Windows threads.
- **Exit criterion:** a multithreaded test suite using thread-local variables passes.

## Milestone 4: CLI corpus

- Corpus of non-JIT CLI AppImages.
- Shim coverage prioritized by usage frequency.
- **Exit criterion:** a target pass rate on the corpus, to be set once the corpus exists.

## Milestone 5: Vulkan

- Vulkan passthrough with a minimal window.
- **Exit criterion:** a Vulkan sample renders correctly.

## Milestone 6: GUI

- X11 / Wayland client translation onto Win32 windowing.
- **Exit criterion:** a real GTK or Qt AppImage runs with usable input and rendering.
