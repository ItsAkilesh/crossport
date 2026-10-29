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

- Red zone: SysV code uses the 128 bytes below `rsp`, which Windows can overwrite during exception and APC dispatch. Milestone 1 builds the corpus with `-mno-red-zone`; real binaries will need a design answer before Milestone 4.
