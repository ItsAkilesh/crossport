<!--
  File: docs/DESIGN.md
  Purpose: Technical design for the Crossport ELF-to-PE static translator
  Author: Akilesh
  Date: 2026-10-28
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

- Implementation language for the translator (Rust is the current leaning, using existing ELF and PE crates).
- Base for the libc shim: write from scratch, or adapt Cygwin, newlib, or mingw-w64 code (license implications).
- Output form: loose bundle directory versus single-file executable with an embedded payload.
- Project license.
