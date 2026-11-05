<!--
  File: docs/DESIGN.md
  Purpose: Technical design for the Crossport ELF-to-PE static translator
  Author: Akilesh
  Date: 2026-10-29
-->

# Crossport Design

## 1. Translation layers

| Layer | Needed for Linux x86-64 to Windows x86-64? | Reason |
|---|---|---|
| Instruction set | No | Same ISA on both sides |
| Binary format (ELF to PE) | Yes | Windows only loads PE images |
| OS interface (Linux ABI to Win32/NT) | Yes | glibc, syscalls, graphics, and windowing calls must land on Windows implementations |

Crossport performs only the last two, ahead of time.

## 2. Pipeline

### 2.1 Unpack

Locate the end of the AppImage ELF runtime to find the SquashFS offset, then extract the AppDir. No FUSE and no mounting.

### 2.2 Analyze

Walk `DT_NEEDED` entries from the main executable to build the full dependency graph. Classify each library:

- **Bundled:** shipped inside the AppDir, translated to PE.
- **Host:** on the AppImage excludelist (glibc, libGL, libX11, and similar), replaced by a Crossport shim.

### 2.3 Relink (ELF to PE)

| ELF construct | PE result |
|---|---|
| Sections and segments | PE sections with matching protections |
| `R_X86_64_RELATIVE` | PE base relocations |
| `R_X86_64_GLOB_DAT`, `R_X86_64_JUMP_SLOT` | Import address table entries (GOT layout preserved, filled by the Windows loader) |
| Dynamic symbol table | PE export table |
| `.init_array`, `.fini_array` | Called from a generated `DllMain` or entry stub |

ELF uses a flat global symbol namespace with interposition, while PE binds each import to a named DLL. Crossport resolves every symbol once at translation time using ELF lookup order and bakes in explicit bindings. The resolution table is also embedded so the shim can serve runtime `dlopen` and `dlsym`.

### 2.4 Rewrite

- **Raw `syscall` instructions:** found statically and redirected into the shim through trampolines (the 2-byte `syscall` cannot be replaced in place by a 5-byte `call`). Most syscalls live in glibc, which is replaced entirely, so the remaining sites should be few. The corpus tooling measures this.
- **Thread-local storage:** Linux uses the `FS` segment for TLS; Windows uses `GS` for the TEB. All `%fs:` relative accesses and the `__tls_get_addr` model are rewritten to use Windows TLS slots.

### 2.5 Bind and package

Emit the translated `.exe` and DLLs, the shim DLLs, and the AppDir data files into a Windows bundle.

### 2.6 Milestone 1 implementation notes

- **Layout:** each `PT_LOAD` segment becomes one PE section at `RVA = vaddr + 0x1000`. The uniform shift leaves room for the PE headers and keeps every RIP-relative reference inside the program valid. Section alignment and file alignment are both 0x1000, so a section's file offset equals its RVA.
- **Relocations:** `R_X86_64_RELATIVE` slots are filled with `image base + bias + addend` and get a `DIR64` base relocation, so the Windows loader can move the image.
- **Imports:** every strongly imported symbol is imported from `crossport_libc.dll`. The GOT is not the IAT. The shim copies each loader-filled IAT entry into its GOT slot before the ELF code runs, which keeps the ELF code unchanged.
- **Weak imports** (`__gmon_start__`, `_ITM_*`, `__cxa_finalize`) resolve to null, so the C runtime's "is it present?" checks behave as they do on a static link.
- **Entry:** the PE entry is a 13-byte stub that passes a start-info table to `crossport_start`. That function binds the GOT, builds the initial stack the Linux kernel would (argc, argv, envp, auxv), and jumps to the ELF `_start`. `__libc_start_main` runs `DT_INIT`, `DT_INIT_ARRAY`, `main`, then `DT_FINI_ARRAY` and `DT_FINI` on exit.
- **Corpus build flags:** `-fno-stack-protector` (the canary reads `%fs:0x28`, which needs the TLS work of Milestone 3), `-mno-red-zone`, `-fcf-protection=none` and `-fno-plt`.

### 2.7 Milestone 2 implementation notes

- **Module graph:** the executable is module 0; libraries follow in breadth-first `DT_NEEDED` order, which is also the ELF global symbol lookup order. Libraries are found in `--lib-dir` directories (default: the executable's directory) and written as `<soname>.dll`. `libc.so.6` is provided by the shim. Names from `DT_NEEDED` are validated to be plain file names before they touch the file system.
- **Initialisation order:** dependencies before dependents, the executable last (depth-first post-order); finalisers run in reverse. Cycles are reported with the offending path. Constructors run from the shim, not from `DllMain`, because `DllMain` runs under the Windows loader lock and a constructor that touches another DLL could deadlock.
- **Symbol resolution:** done once, at translation time, with ELF semantics: the first module in scope order that exports a name wins, even over the importer's own definition (interposition works). A symbol nobody defines is an error at translation time unless it is weak, in which case it resolves to null. The set of shim exports is read from the built DLL, so a missing libc function fails the translation, not the run.
- **Binding kinds:** a symbol defined by the importer becomes a base-relocated absolute pointer. A symbol defined by another library becomes a PE import by name (this works for data as well as functions). A symbol whose first definer is the executable becomes a startup binding to an address inside the executable, so libraries never import from the executable and the import graph stays acyclic.
- **Copy relocations:** `R_X86_64_COPY` is how a PIE executable references library data (for example a library's global counter). The executable gets its own copy, and by ELF scope rules every module then binds to that copy. The shim performs the copy at startup, after all bindings, from the library's definition (found through the IAT), like `ld.so`.
- **Start info:** every module carries a start-info table (version 2) with three record arrays: IAT bindings, bindings into the executable, and copies. Libraries export it as `__crossport_info`; the executable's table also lists the dependency DLLs in initialisation order. The contract lives in `translator/src/shim_abi.rs` and `shim/crossport_libc.c`.
- **Hardening:** the translator treats ELF input as hostile. Offsets use checked arithmetic, table sizes and module counts are capped, segment sizes are bounded, `PT_TLS` and TLS relocations are rejected with a clear error, and mutation sweeps over real binaries assert it never panics. Output is deterministic.

### 2.8 Milestone 3 implementation notes

- **Why the code is rewritten.** Linux reaches thread-local storage through `%fs`. Windows x64 resets a user-set FS base at the next context switch (measured: `wrfsbase` works, and the value is gone about 31 ms later), so translated code must never execute an `%fs` access.
- **Static TLS.** Every module's `PT_TLS` block gets a fixed negative offset from one per-thread pointer: the executable first (so its local-exec offsets are exactly right), then libraries. Because all modules are known at translation time, `TPOFF64`, `DTPOFF64` and `DTPMOD64` relocations become constants. `DTPMOD64` carries the module's block offset instead of an abstract id, so `__tls_get_addr` is `tp + module + offset` with no table.
- **Thread pointer.** It lives in a Windows TLS slot, read as `gs:[0x1480 + 8 * slot]`. The slot number is only known at startup, so stubs carry a placeholder displacement and the translator emits a fixup list. The shim patches the stubs, then makes the stub section executable and read-only. `.text` is never modified at run time.
- **Rewrite kinds.** Function ranges come from `.eh_frame`, so only real code is decoded. `%fs:0x28` (stack canary) is rewritten in place to a RIP-relative read of a per-module cell. Every other FS-relative access, whether local-exec absolute, initial-exec `%fs:0(%rbp)` or a thread-pointer read, becomes a 5-byte `jmp` to a stub that saves a scratch register, loads the thread pointer, runs the original instruction re-encoded against it, restores the register and jumps back. The stub never uses the stack (translated code may use the red zone) and never changes the flags; the scratch register is saved in `TEB.ArbitraryUserPointer`.
- **Fail closed.** The translator refuses, with the instruction's address, sites it cannot patch safely: shorter than 5 bytes, `%rsp`-relative, RIP-relative or vector-indexed operands, TLS descriptors, and a module that has no `.eh_frame` falls back to sweeping its executable segments. Per-module counts of canary and stub sites are printed by `crossport translate`.
- **Threads.** The shim implements pthreads on Win32 threads: create, join, detach, self, exit and attributes; normal, recursive and error-checking mutexes whose static initialisers work unchanged (a zeroed `pthread_mutex_t` is a valid unlocked `SRWLOCK`); condition variables; `pthread_once`; thread-specific keys with destructors; and a per-thread `errno`. A `pthread_t` is a heap object that outlives the Windows thread until it is joined or detached.
- **Dynamic loader.** `ld-linux-x86-64.so.2` (home of `__tls_get_addr`), `libpthread.so.0`, `libdl.so.2` and `librt.so.1` are provided by the shim, like `libc.so.6`.
- **Known limits.** Translated code must run on the main thread or a thread created by `pthread_create`; a foreign thread has no TLS block. `dlopen` and TLS descriptors are not supported. A process that already uses all 64 inline Windows TLS slots cannot start.
- **Verification.** CI translates `tls_basic` (all four TLS models and live canaries) and `threads`, stresses `threads` 200 times for identical output, and requires `canary_smash` to abort with exit 134 and glibc's message.

### 2.10 Milestone 6 groundwork (measured on a real Qt AppImage, 2026-10-29)

`crossport coverage --allow-missing` measured KeePassXC 2.8.0 (a Qt 5 AppImage, 52 MB) without running it.
What it showed, and what has been done about it:

- **AppRun is a shell script.** Graphical AppImages start through a launcher script that sets the
  environment and runs the real program. Crossport does not run scripts, so `unpack` now takes the program
  named by the `Exec=` line of the AppDir's `.desktop` file (it must be an ELF file under `usr/bin` and
  similar). The environment the script would have set (`QT_PLUGIN_PATH`, `LD_LIBRARY_PATH`, `APPDIR`) is
  not reproduced yet; Qt's `qt.conf` covers part of it.
- **Many libraries are not bundled.** The AppImage rules let an AppImage rely on the host for glibc,
  libstdc++, libgcc_s, libz, freetype, fontconfig, harfbuzz, libX11, libGL and EGL, and a few others. On
  Windows nothing provides them, so each must come from somewhere. See the open question in section 6.
- **C++ needs the unwinder to find translated code.** Exceptions are unwound with the DWARF tables in
  `.eh_frame`, which libgcc and libc++abi locate through `dl_iterate_phdr`, or `_dl_find_object` on glibc
  2.35 and newer. The shim implements both. It describes each translated module with two synthetic
  program headers, a `PT_LOAD` covering the image and a `PT_GNU_EH_FRAME` for the module's
  `.eh_frame_hdr`, whose address the translator records in the start info (version 4). The tables are
  position independent, so the unwinder reads them in place. `cxx_exceptions` tests it with the C++
  runtime linked statically.
- **Descriptors are no longer only files.** Event loops need pipes, eventfd, dup, fcntl, ioctl, poll,
  select, epoll, timerfd and sockets. A descriptor now has a kind (file, pipe, event, socket, epoll,
  timer) that decides how it is read, written and waited on. Win32 events serve eventfds, timerfds and
  sockets; pipes and consoles, which have none, are polled in short slices. epoll is built on the same
  poll engine and delivers level-triggered events (EPOLLONESHOT is honoured). Sockets are Winsock sockets
  kept non-blocking, with blocking emulated by waiting on the socket's event; Unix-domain paths go
  through the path mapper. `posix_io`, `sockets` and `event_loop` test them.
- **Child processes.** `fork` has no efficient Windows counterpart, so it fails with ENOSYS. The spawn
  family maps onto `CreateProcess`: `posix_spawn` and `posix_spawnp` with file actions for descriptors 0
  to 2, `waitpid`, `kill` (signals other than 0 terminate the process and are reported to `waitpid`),
  `system` and `popen` through `cmd.exe /c`, and `exec*` as "run the program, then exit with its
  status". Arguments are quoted so they read back identically; a child given its own environment still
  gets `SystemRoot`. `process` tests it.
- **File mappings.** `mmap` supports files (shared and private views, offsets that are not a multiple of
  Windows' 64 KiB granularity, `msync`, `mprotect`) and anonymous memory (`mremap`, `MADV_DONTNEED`).
  Unmapping part of a file view is accepted but does nothing. `mmap_files` tests it.
- **Wide characters and locales.** The shim's locale is one built-in UTF-8 locale. Multibyte and wide
  conversion, `isw*`/`tow*`, `locale_t` objects and the `_l` variants, `nl_langinfo`, and `iconv` between
  UTF-8, ASCII, Latin-1, UTF-16 and UTF-32 are implemented, which is what libstdc++ needs.
- **Breadth.** Measuring libstdc++, libgcc_s and KeePassXC (84 modules with a runtime pack of 30 Ubuntu
  libraries) found everyday functions still missing (strtok, getopt, getline, asprintf, qsort_r,
  drand48, tsearch, hsearch, single-precision libm, user and group lookups, name services) and they were
  added, with `libc_breadth` comparing them against glibc. Functions with no Windows counterpart
  (inotify, mounts, xattrs, SysV IPC) answer ENOSYS, or accept and ignore harmless requests, in
  `stubs.c`. With them, libstdc++ 12 and 14 and libgcc_s have no missing symbols, and KeePassXC's
  usage-weighted coverage went from 50% to 67%; what remains is X11, GL and EGL.
- **One step from AppImage to bundle.** `crossport convert app.AppImage --shim ... --out dir` unpacks,
  finds the program (through the desktop entry when AppRun is a script), translates it and its
  libraries, and writes `crossport.env`, which the shim applies at start (`APPDIR` and any variables a
  launcher would have set; `${BUNDLE}` is the bundle directory). The shim also sets `HOME`, `USER` and
  `LOGNAME` when Windows does not. Host libraries the AppImage lacks (libstdc++ and similar) come from
  `--runtime <dir>`, searched after the AppDir.
- **The shim must stand alone.** It is linked with `-static-libgcc` so it needs no MinGW runtime DLL (it
  did, until a child process started with a bare environment failed to load it), and it never uses
  compiler TLS (emulated TLS calls pthread functions that resolve to the shim's own SysV exports).
  Microsoft-convention code does not call SysV functions: older GCC releases crash compiling that mix.
- **Smaller pieces:** `dlopen`, `dlsym`, `dlerror` and `dladdr` answer for the shim's own libraries
  (`dlopen` of bundled libraries is still out of scope), `aligned_alloc`, `posix_memalign` and
  `memalign` (page-backed above 16-byte alignment), read-write locks, condition variables with
  monotonic clocks, random numbers (`getrandom`, `arc4random`), and the `_FORTIFY_SOURCE` variants of
  the string, read, poll, scanf and select calls.

### 2.11 Milestone 6: in-process Xlib

`shim/xlib/` provides libX11 on top of the xcb layer, with no X server. A `Display` is memory laid out like
Xlib's public part (checked against the real headers by `tests/shim/xlib_layout_check.c`). Every window and
pixmap is a drawable record with a 32-bit BGRX pixel buffer (the visual is 24-bit TrueColor, stored in 32
bits per pixel as on Xorg, which `XListPixmapFormats` reports so toolkits size their buffers correctly);
drawing writes the buffer and the window's `WM_PAINT` copies it to the screen, asking the program for an
`Expose` only when the buffer is new or resized. Win32 messages become wire-format events in one queue, and
each display converts them to `XEvent`s, dropping events the window did not select and `ConfigureNotify`s
that report an unchanged size. Focus, enter and leave events come from the matching Win32 messages.

- **Input:** Windows scancodes map to evdev keycodes plus 8, with a US keysym table; `XLookupString` handles
  Shift, Caps Lock and Control. There is no input method (`XOpenIM` returns NULL).
  Windows reports key and button state after an event and X reports it from before, so the converter undoes
  the change for the key or button that caused the event. A window's position is its client origin in every
  call and event, as in X. XTest (`xlib_xtest.c`) turns `XTestFake*` calls into real input (`SendInput`,
  `SetCursorPos`), after bringing one of the program's windows to the foreground. A pointer move is
  delivered by Windows as a low-priority message, so the call dispatches the program's messages before it
  returns, which keeps injected events in order. `xlib_input` compares the result with Xvfb.
- **Properties:** a table keyed by window and atom, kept in wire form and widened to 8-byte longs for
  format-32 reads. `PropertyNotify` is posted when selected. The title and `WM_PROTOCOLS` properties also
  reach the xcb layer.
- **Waiting:** the display descriptor is an eventfd that is readable exactly while events are pending, and
  `poll` pumps window messages while it waits, so toolkit event loops block instead of spinning.
- **Not provided:** extensions answer absent (so clients use the core protocol), selections answer "no
  owner", and cursors and most window-manager hints are accepted and ignored.
- **Drawing:** rectangles, 1-pixel lines, points, area copies and images (ZPixmap 32 and 24 bit, XYBitmap).
  Arcs, polygons, text and fonts are not done.

`sdl2_input` runs libSDL2's keyboard, text, pointer and wheel handling on the shim, driven by XTest, and matches
Linux. `xlib_draw` compares drawing with a real server under Xvfb in CI. `sdl2_window` runs the unmodified Linux
libSDL2 (X11 driver, window-surface path) and matches Linux pixel for pixel, including a hash of the whole
surface. SDL2 uses libraries the machine may not have (libasound and similar), so that case translates with
`--stub-missing`: a library or symbol that nobody provides is bound to a shim function that returns 0 and
the translator lists what it stubbed. A stubbed symbol fails quietly, so a case that needs one must say so.

OpenGL (`shim/gl/`) follows the WGL passthrough decision in section 6. `tools/gen-gl-thunks.py` reads the
pinned `gl.xml` and emits one thunk per desktop command, which resolves its real entry point on first use
(`opengl32.dll` exports GL 1.1; later commands come from `wglGetProcAddress`, which needs a current context,
so a thunk with no target yet is retried on its next call). Commands that take or return function pointers
(the debug-message callbacks) are written by hand and never call the program back. GLX is one framebuffer
configuration and one visual, because a window gets one Win32 pixel format: double-buffered RGBA, 8 bits per
channel, 24-bit depth, 8-bit stencil. GLX creates contexts without a drawable, so they are made on a hidden
window with that format and bind to real windows later. A window an OpenGL context draws to is flagged so the
software pixel buffer is never copied over it. `gl_window` matches Linux (Mesa under Xvfb) against Windows
(Mesa llvmpipe's `opengl32.dll`, staged by `tools/ci/install-opengl-windows.sh`) frame for frame.

EGL (`egl_*.c`) is the same layer under a different API: one display and one configuration, window surfaces
(an X window) and pbuffers (a hidden window of the requested size), and contexts made through the same
bootstrap window. A program that binds the OpenGL ES API gets a context through WGL's ES profile
(`WGL_EXT_create_context_es2_profile`), so a driver without it refuses with `EGL_BAD_MATCH`; OpenGL ES 1 is
not offered. Choosing a configuration filters on what a program can ask for (sizes are minimums, and
multisampling, pixmaps or ES 3 find nothing), so a program falls back as it would on a smaller driver. The
error code is per thread, kept in a thread cell. `egl_window` matches Linux on a window surface, a pbuffer and
an ES 2 context.

Crashes in translated code are reported by `shim/crash.c`: the exception kind, the faulting module and offset,
and a rough call trail. Windows cannot unwind through translated frames (they have no unwind data), so a
vectored handler does the reporting instead of the unhandled-exception filter. `tools/ci/symbolize.py` turns
the shim offsets in a report into function names.

The clipboard is bridged for text (`xlib_clipboard.c`, `xlib_selection.c`). When a program takes CLIPBOARD, the shim
asks it for UTF8_STRING like any requestor (a pseudo window stands in) and puts the answer on the Windows clipboard.
When nobody in the process owns CLIPBOARD but Windows holds text, requests are answered from it and a pseudo owner is
reported. `GetClipboardSequenceNumber` tells when another program has replaced a published copy, and the owner is then
sent `SelectionClear`. Other formats (images, files) are not bridged.

The runtime pack is built by `tools/runtime/`: `pack-spec.json` names packages and suites, `pin_pack.py` writes
`pack.json` (URL and SHA-256 of every package, from a dated snapshot), and `fetch_pack.py <pack.json> <cache>
<out>` verifies and unpacks the libraries; pass `<out>` to `--runtime`.

A real SDL2 program is part of the corpus: `tests/corpus/apps/sdl2_tests` builds SDL 2.30.9 and its `testautomation`
harness from pinned source, packages them as an AppImage, and runs the suites on Linux under Xvfb and translated on
Windows. The Render and Video suites found two real gaps (`XQueryTree` and the text-property calls, whose stubs left
their outputs unset, so SDL freed garbage). A stub that returns success without writing its outputs is the pattern
to avoid.

Milestone 6 is complete; Qt and GTK applications are Milestone 7.

## 3. Shim ABI

Translated code keeps Linux conventions internally. Only the boundary into shims needs care.

- **Calling convention:** shim exports use `__attribute__((sysv_abi))`; internal calls into Win32 use the Microsoft x64 convention. GCC and Clang generate the thunks.
- **Data model:** Linux is LP64, Windows is LLP64. Shims use Linux type definitions and struct layouts (`stat`, `dirent`, `timespec`, and others).
- **`long double`:** 80-bit on Linux; handled explicitly at the boundary.

### Shim set (driven by the AppImage excludelist)

1. glibc and pthreads on Win32/NT (largest component)
2. Vulkan: near passthrough to `vulkan-1.dll`
3. OpenGL: vendor WGL driver or Mesa Zink
4. X11 / xcb / Wayland client libraries on Win32 windowing (hardest component)

## 4. Known limits

- **JIT code** is generated at runtime and cannot be statically translated. JIT-based apps are out of scope.
- **`fork()`** has no efficient Windows equivalent.
- **Signals** map imperfectly onto structured exception handling.
- **Filesystem semantics** (case sensitivity, `/proc`, `/dev`, symlinks, permission bits) require emulation in the shim.

## 5. Verification harness

Built before the translator, because agents and humans both need fast, objective feedback.

- **Windows CI:** every translated binary runs on real Windows automatically.
- **Differential testing:** run the original on Linux and the translation on Windows, then compare stdout, stderr, exit code, and files written.
- **Coverage dashboard:** count glibc symbols and syscall sites used across the corpus versus those implemented.
- **Crash diagnostics:** preserve symbol information through translation so `cdb` crash addresses map back to original ELF functions.

## 6. Open decisions

### Decided

- **Translator language:** Rust, with the `object` crate (pinned) for reading ELF. The PE image is written by Crossport itself.
- **libc shim base (Milestone 1):** written from scratch in C. Adapting newlib or mingw-w64 can be revisited at Milestone 4, when the surface grows.
- **Output form:** a loose bundle directory (`.exe`, DLLs, data). A single-file executable can come later.
- **Project license:** MIT.
- **Milestone 5 sample (decided 2026-10-29):** offscreen first. The sample renders to an image, reads it
  back and writes a PPM, so Linux and Windows output can be compared byte for byte with no window. A
  windowed triangle comes second.
- **Milestone 5 window surface (decided 2026-10-29):** a minimal xcb shim (connect, create window, map)
  backed by one Win32 window, with `VK_KHR_xcb_surface` implemented on top of `VK_KHR_win32_surface`. It
  is the seed of the Milestone 6 X11 work.
- **Milestone 5 CI driver (decided 2026-10-29, revised the same day):** Mesa lavapipe on both runners,
  because hosted runners have no GPU. SwiftShader was the first choice, but it publishes no binaries, so
  it would have meant building it from source on both operating systems. Linux gets lavapipe from the
  distribution packages; Windows gets the LunarG Vulkan runtime (the loader) and the mesa-dist-win
  lavapipe build, each pinned and checksum-verified by `tools/ci/install-vulkan-windows.sh`. The two
  Mesa versions can differ, so the sample prints only driver-independent output: colour probes in
  sixteenths and a hash of which pixels the triangle covers.

- **Milestone 6 OpenGL backend (decided 2026-11-01):** WGL passthrough. The shim exports libGL, libGLX and
  libEGL and forwards to `opengl32.dll` through WGL, with one thunk per command generated from a pinned
  `gl.xml` (as the Vulkan layer is from `vk.xml`). There is no new runtime dependency. CI runners have no
  GPU, so Windows CI uses a pinned, checksum-verified Mesa llvmpipe `opengl32.dll`, as Milestone 5 does
  with lavapipe. ANGLE and Mesa Zink were considered and not chosen (a large pinned dependency to ship and
  keep patched, and a second native API to build, respectively).

- **Distributions (decided 2026-11-04):** Crossport is one project with a separate distribution for each
  target platform, and each translates AppImages built for its own platform's architecture. The Windows
  x86-64 distribution translates x86-64 AppImages. The macOS Apple silicon distribution (Milestone 10)
  translates aarch64 AppImages (aarch64 ELF to arm64 Mach-O), natively, with no emulation. There is no
  universal package, and Rosetta 2 is not used: the Rosetta route (x86-64 Mach-O run by Apple's translator)
  was considered and rejected, because the runtime translation would be Apple's, it costs speed, and Apple has
  announced that Rosetta will be reduced. An app that wants both platforms publishes an x86-64 AppImage and an
  aarch64 AppImage. The shim is therefore a portable core plus one backend per target.

- **Host libraries (decided 2026-11-04):** the runtime pack is built from pinned, checksum-verified Ubuntu
  packages (the newest libstdc++ and libgcc_s, because an AppImage built with a newer GCC needs the newer
  symbols, and the oldest LTS for the rest), fetched by a script like `tools/ci/install-vulkan-windows.sh`.
  Libraries inside the AppImage always win over the pack, and anything that touches the operating system is
  implemented by the shim. Crossport does not host binaries. A library nobody supplies is bound to
  `--stub-missing`.
- **Red zone (decided 2026-11-04):** translated code runs only on threads that never enter an alertable wait,
  and window messages and I/O completions are delivered on separate shim threads, so Windows never dispatches
  an APC onto a thread that is using the 128 bytes below `rsp`. Exceptions are the exception: the vectored crash
  handler only reports and never resumes translated code.
- **Core X fonts (decided 2026-11-04):** `XLoadQueryFont` and the other server-side font calls are not
  implemented and fail cleanly. Toolkits draw text with freetype and fontconfig from the runtime pack, which
  is what Milestone 6 and 7 programs use. Revisit if a real program needs core fonts.

### 2.9 Milestone 5 implementation notes

- **Entry points.** `libvulkan.so.1` is provided by the shim. `tools/gen-vulkan-thunks.py` reads the
  pinned registry (`third_party/vulkan/vk.xml`) and emits one `LIBC_FN` thunk per command, which calls the
  real entry point in `vulkan-1.dll`. Vulkan's types are fixed width, so the only difference between the
  two sides is the calling convention, and the thunk bridges that. Registry types map to ABI classes
  (pointers, 32 and 64-bit integers, floats); a command that takes a struct by value fails generation.
- **Loading.** `vulkan-1.dll` is loaded on first use, so programs that never use Vulkan do not need it.
  Core commands are bound from the loader's exports; extension commands come from
  `vkGetInstanceProcAddr` with the first instance the program created. With no loader, result-returning
  commands fail with `VK_ERROR_INITIALIZATION_FAILED`.
- **GetProcAddr.** `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` return the shim's SysV thunks, never
  the real Microsoft-convention pointers, and return null for anything the real loader does not provide.
- **Known limit.** Function pointers the program passes into Vulkan (allocation callbacks, the debug
  messenger callback) are called by the driver with the Microsoft convention and are not yet thunked in
  that direction.

### Open

Nothing is open at the moment.
