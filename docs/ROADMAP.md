<!--
  File: docs/ROADMAP.md
  Purpose: Milestones and exit criteria for Crossport
  Author: Akilesh
  Date: 2026-10-29
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
- Known limits at the end of the milestone (several lifted since, see Milestone 6 groundwork): no `fork`,
  `popen` or `system`, no file-backed `mmap`, no asynchronous signals, local time is UTC, and scanf reads
  strings only.

## Milestone 5: Vulkan (done)

- `libvulkan.so.1` is provided by the shim: 600+ entry points generated from the pinned Vulkan registry
  (`tools/gen-vulkan-thunks.py`) forward to the Windows loader (`vulkan-1.dll`), which is loaded on first
  use. `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` return the shim's own thunks.
- A minimal window: `libxcb.so.1` is provided by the shim on top of Win32 windows (connection, screens,
  atoms, window requests, and key, mouse, expose, configure and close events), and `VK_KHR_xcb_surface`
  is implemented on top of `VK_KHR_win32_surface`.
- Two samples: `vulkan_triangle` renders a triangle offscreen and prints driver-independent results
  (colour probes in sixteenths and a hash of which pixels the triangle covers); `vulkan_window` opens an
  xcb window and presents 30 frames.
- **Exit criterion:** both samples render and present correctly on Windows, compared with Linux in CI
  (a software Vulkan driver on both: Mesa lavapipe, with the Khronos loader built from pinned sources on
  Windows) and on a real GPU on the development machine.
- Known limits: function pointers the program passes into Vulkan (allocation and debug callbacks) are not
  yet thunked in the driver-to-program direction.

## Milestone 6: Graphics core

Groundwork done (measured on a real Qt AppImage, see docs/DESIGN.md section 2.10): C++ exceptions through
translated code, descriptor kinds with pipes, eventfd, epoll, timerfd and sockets, child processes, file
mappings, wide characters and locales, iconv, the rest of the everyday libc (usage-weighted coverage of
KeePassXC 50% to 80%), `crossport convert`, the desktop-entry fallback for shell-script launchers, and a
runtime directory for host libraries (libstdc++ and similar).

- **X11 in process:** an X server inside the shim behind libxcb and libX11. Drawing (pixmaps, graphics
  contexts, `PutImage`, `CopyArea`, `GetImage`) on a DIB per window, properties, input events, cursors,
  clipboard basics, and honest answers to extension queries. Xlib structures are mirrored by byte offset
  and checked against the real headers on Linux CI.
- **OpenGL and EGL:** `libGL`, `libGLX` and `libEGL` on WGL (or ANGLE), since toolkits probe for them.
- **Fonts and text input:** fontconfig and freetype data, XKB keymaps (`libxkbcommon`), XInput2 answers.
- **Exit criterion:** an SDL2 AppImage (software renderer) and a small GL sample run on Windows with
  working input and match their Linux output where output is defined; Xlib and xcb samples are compared in
  CI under Xvfb.

## Milestone 7: Toolkit applications

- **Runtime pack:** decide and build where host libraries come from (docs/DESIGN.md section 6), pinned and
  checksum-verified, translated once and shipped with the translator.
- **`dlopen` of translated libraries:** Qt and GTK load plugins at run time, so plugins in the AppDir are
  translated too and `dlopen` finds them.
- **Qt first** (the `xcb` platform plugin on the in-process X), then **GTK**, with D-Bus over Unix-domain
  sockets, fontconfig, GSettings and icon themes as needed.
- **The red zone:** a design answer for threads that receive window messages (docs/DESIGN.md section 6).
- **Exit criterion:** a real Qt AppImage and a real GTK AppImage run with usable input and rendering, and
  a regression suite of their startup and basic interaction runs in CI.

## Milestone 8: Everyday use

- **Packaging:** a launcher or installer that turns an AppImage into a normal Windows application: Start
  menu entry, icon, file associations, uninstall, per-user install without admin rights.
- **Signing and updates:** code signing for the generated executables and DLLs, update checks, and a
  reproducible translation so a given AppImage always yields the same bytes.
- **First-run experience:** one drag-and-drop or one command; clear messages when an app uses something
  unsupported (a report of what is missing rather than a crash).
- **Exit criterion:** someone who has never seen Crossport installs and runs three different AppImages
  from a signed installer with no terminal.

## Milestone 9: Hardening and breadth

- **Performance:** profile the shim and the rewritten code paths, remove avoidable copies, and measure
  against native Linux on the same hardware.
- **Security:** a review of the translator (hostile ELF and SquashFS input) and the shim (paths, sockets,
  environment), fuzzing in CI, and least-privilege defaults.
- **Crash diagnostics:** map crash addresses back to original ELF functions, and ship a symbolized crash
  report.
- **Corpus growth:** many more real AppImages, including audio, networking and database apps, each
  with a differential or smoke test and the pass-rate ratchet.
- **Exit criterion:** the pass-rate ratchet covers 50 or more AppImages, a fuzzing run of 24 hours finds
  no crash in the translator, and the performance report is published.

## Beyond Milestone 9

The milestones are not equal in size (the graphics and toolkit work alone were estimated at 6 to 18+
months), so the progress graphic counts milestones, not effort. After M9:

- **Other targets:** macOS and Windows on ARM.
- **Things deliberately out of scope until proven necessary:** applications that generate code at run
  time (Electron, the JVM, LuaJIT), `fork` and full signal semantics, static and non-PIE binaries.
- **Upkeep:** glibc, toolkit and AppImage-format changes keep moving, so the corpus and CI have to keep
  running to catch drift.
