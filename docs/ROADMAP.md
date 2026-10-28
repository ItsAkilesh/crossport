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

## Milestone 3: TLS and threads (done)

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

## Beyond Milestone 6

Finishing M6 proves the approach on a real GUI application. It does not make Crossport a finished
product. The milestones above are not equal in size (the GUI work alone was estimated at 6 to 18+
months), so the progress graphic counts milestones, not effort. What remains after M6:

- **Compatibility breadth:** far more of the libc, graphics, audio, input, fontconfig, D-Bus and
  toolkit-plugin surface, validated against a much larger corpus of real AppImages, including GUI apps.
- **Packaging and everyday use:** a launcher or installer, file associations, updates, uninstall and
  code signing, so a translated app feels like any other Windows application.
- **Performance and robustness:** profiling the shim and rewritten code paths, crash diagnostics that map
  back to the original ELF, and a security review of the translator and shim.
- **Other targets:** macOS and Windows on ARM.
- **Things deliberately out of scope until proven necessary:** applications that generate code at run
  time (Electron, the JVM, LuaJIT), `fork` and full signal semantics, static and non-PIE binaries, and
  `dlopen`.
- **Upkeep:** glibc, toolkit and AppImage-format changes keep moving, so the corpus and CI have to keep
  running to catch drift.
