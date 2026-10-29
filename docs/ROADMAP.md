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

## Milestone 4: CLI corpus (done)

- Corpus of non-JIT CLI AppImages: Lua 5.4.7, SQLite 3.46.1 (shell) and jq 1.7.1, each built from pinned
  upstream sources and packaged as a real type 2 AppImage, with 45 test invocations (arguments, stdin,
  files, exit codes, error output).
- Shim coverage prioritized by usage frequency: `crossport coverage` measured the gap (206 libc symbols),
  and the shim now provides a glibc-compatible C library (stdio with glibc's `FILE` layout, printf and
  sscanf, strtod, libm, time, directories, signals, setjmp, mappings) that covers 100% of what the three
  apps import.
- **Exit criterion:** all 45 invocations produce byte-identical stdout, stderr, exit code and files on
  Linux and on Windows, enforced in CI by a ratchet (`tests/corpus/baseline.json`): a test that passes
  there may never fail again. Tier 1 is these three apps.
- Known limits, to be widened by later corpus growth: no `fork`, `popen` or `system`, no file-backed
  `mmap`, no asynchronous signals, local time is UTC, and scanf reads strings only.

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
