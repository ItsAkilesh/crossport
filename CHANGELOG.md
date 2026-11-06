# Changelog

Versioning: `0.<milestone>.<patch>` while Crossport is pre-1.0. The minor number is the roadmap milestone
being worked on. A feature commit that adds user-visible capability (a new flag, a new class of program
that runs) bumps the patch number; a fix does not. The version lives in `translator/Cargo.toml` and is
bumped in the same commit as the change. Releases are tagged by the maintainer, not by automation.

## Unreleased: hardening (Milestone 9 security work, no version bump)

- Fuzzing: `fuzz/` holds eight targets for hostile input (ELF analysis, translation with a hostile executable or
  library, the shim export reader, the AppImage and SquashFS reader, `convert`, the command line), seed corpora built
  from the test fixtures, and a stable-Rust driver for local runs. `.github/workflows/fuzz.yml` runs a 60 second smoke
  run per target on pull requests and a long scheduled run. `docs/THREAT_REVIEW.md` reviews how the translator handles
  hostile input.
- Fixed: an address in an ELF file (a symbol, relocation, entry point, array or code range) near the top of the 64-bit
  range overflowed the arithmetic that places it in the PE image and panicked a debug build (a release build produced
  a wrong address). Analysis now rejects any address beyond the 256 MiB module image limit.
- Fixed: an archive entry whose name is not UTF-8 made the SquashFS reader panic on Windows; `unpack` and `convert`
  now report a malformed archive instead.

## 0.7.10

- The corpus checks that a toolkit window really has contents. `CROSSPORT_DUMP_WINDOWS=<directory>` makes the shim
  write the pixels of every window that has painted to `window-<id>.ppm`, about twice a second while it paints (a run
  stopped by a timeout never reaches an exit handler, so the dump does not wait for one). A corpus test with a
  `window_check` entry (`keepassxc/gui-start`) runs with it, and `tools/corpus/window_check.py` decides: some window of
  at least 200x150 must use eight or more colours, with 1% of its pixels different from its most common colour; a
  window that never painted is one flat colour and fails. It also writes a PNG beside each dump, which CI uploads as
  the `app-windows-pictures` artifact, so a person can see what the test saw (the KeePassXC window shows the welcome
  page, logo and buttons).
- Found while building it: the check needed the window's pixels from the xcb path, so the dump follows painted
  windows instead of the Xlib "mapped" flag, which only Xlib's `XMapWindow` sets.

## 0.7.9

- GNU indirect functions work. A symbol of type `STT_GNU_IFUNC` holds a resolver that returns the real function, and
  an `R_X86_64_IRELATIVE` slot holds what a resolver returns. The translator used to bind a call to the resolver
  itself, so the translated Ubuntu zlib 1.2.11 (which selects `crc32_z` this way) returned the resolver's code
  address as every checksum, and libpng in KeePassXC reported `IHDR: CRC error` for its PNG icons. Each resolver now
  gets a small trampoline in the module (sections `.cifunc` and `.cifdt`) that saves the argument registers, runs the
  resolver on the first call, caches the answer and jumps to it; later calls cost one indirect jump. Symbols that
  share a resolver share a trampoline, a variadic target still sees its vector-register count, and a module may
  have at most 4096 resolvers.
- Two corpus cases: `zlib_crc` (crc32 and adler32 of every length up to 600 at 16 alignments, against a bitwise
  reference, through the system `libz.so.1`) and `ifunc` (an exported, a local and a variadic indirect function
  called through the PLT, through a pointer and from inside their library).

## 0.7.8

- XKB: the in-process X server now has the XKEYBOARD extension, so Qt's `xcb` platform finds it (`XKeyboard
  extension not present` is gone) and builds its keymap with libxkbcommon-x11. libxcb-xkb sends its requests
  through `xcb_send_request`, and the shim answers them at the wire level, in the protocol's layout:
  - UseExtension, PerClientFlags, GetState, GetControls, GetDeviceInfo and SelectEvents, which is what
    `xkb_x11_setup_xkb_extension` and `xkb_x11_get_core_keyboard_device_id` need.
  - GetMap (the four canonical key types, one group of symbols and the modifier map), GetNames (component, type,
    level, group and key names as atoms) and empty compat and indicator maps, which is what
    `xkb_x11_keymap_new_from_device` reads. The keymap is the US layout the core protocol already reports.
  - XkbStateNotify: a client that selects it gets an event whenever Shift, Lock, Control or Alt changes, so a
    toolkit's own keymap state follows the keyboard and Shift or Caps Lock change the text a key makes.
  - Xlib: `XkbQueryExtension` and `XQueryExtension("XKEYBOARD")` report the extension; `XkbUseExtension`,
    `XkbLibraryVersion`, `XkbGetState`, `XkbGetIndicatorState`, `XkbLockGroup`, `XkbLatchGroup`,
    `XkbLockModifiers`, `XkbLatchModifiers`, `XkbSelectEvents`, `XkbSelectEventDetails` and `XkbBell` answer;
    `XkbGetMap` still answers NULL (the keymap is read through the xcb requests, not Xlib's descriptor
    structures), so GTK's use of it is for Milestone 7's GTK step.
  - `xkb_requests` joins the corpus: it sends the extension's requests and compares the answers with Xvfb.
- Not yet: XkbStateNotify for Xlib clients, group switching (one layout), compose files beyond what
  libxkbcommon finds in the runtime pack.

## 0.7.7

- KeePassXC 2.8.0 shows its window: the Qt `xcb` platform runs on the in-process X11, paints the main window and the
  "check for updates" dialog with text, icons and menus, and receives the keyboard and the mouse. Found by running
  the converted app and following each failure to its cause:
  - `crossport convert` writes a `qt.conf` beside the program inside the AppDir when the app has none, a
    `fonts.conf` that points fontconfig at the Windows fonts directory, and `CROSSPORT_PROGRAM` in `crossport.env`.
    `QT_PLUGIN_PATH` alone cannot work: Qt splits it at every colon and a Windows path has one after the drive.
  - `readlink("/proc/self/exe")` answers with the program's original place inside the AppDir, so the `../share`,
    `../lib` and `../plugins` that toolkits and applications look for next to their program resolve as on Linux.
    Before, Qt guessed the path from `argv[0]` and the current directory and failed when they were on different drives.
  - The thread pointer read inline (`%fs:0x10`, glibc's `pthread_self`) now equals what `pthread_self()` returns.
    Qt compared the two, decided every object lived in another thread, and queued every slot call: a deleted
    button stayed in its dialog's lists and the next layout crashed.
  - `/dev/urandom` and `/dev/random` open and read from the system generator (Botan seeds itself there).
  - Mapping a window with `xcb_map_window` reports MapNotify, and unmapping reports UnmapNotify. A toolkit treats
    a window as exposed only after its MapNotify and paints nothing before that. Xlib keeps reporting its own.
  - `CROSSPORT_TRACE=1` prints the drawing requests and paints of the X layer to stderr.
- A startup run of KeePassXC joins the corpus (`keepassxc/gui-start`: it must still be running after 40 seconds,
  as it is on Linux under Xvfb).
- Not yet: XKB (keys that need a keymap), icon themes beyond the built-in ones, auto-type, a check that
  the window's contents are right, and GTK.

## 0.7.6

- A real Qt application runs: KeePassXC 2.8.0 (Qt 6, 153 modules with its plugins) starts translated on Windows,
  brings up Qt's xcb platform on the in-process X11, and answers `--version` and `--help` as on Linux. It has no
  keyboard yet (XKB is absent) and was not shown a window.
- Downloads of pinned files are retried on network failures.

## 0.7.5

- The xcb requests a toolkit makes: about 100 functions on top of the shim's windows, properties, pixel buffers and
  keyboard tables. Replies are kept under their sequence number until collected. Properties round trip with the
  wire protocol's offsets, types and deletion; windows, pixmaps and graphics contexts can be made and drawn to
  (`xcb_put_image`, `xcb_get_image`, `xcb_copy_area`); atoms have names; `xcb_send_event` reaches the program's own
  queue; every extension is reported absent. The setup now carries a depth, a visual and pixmap formats.
  `xcb_requests` compares the answers with a real X server.
- A window can be as small as X allows: Windows' minimum window size is lowered by the window procedure.

## Installer (separate crate, not versioned with the translator)

- `installer/` adds a native Windows setup wizard (`crossport-setup`) and an `.AppImage` launcher (`crossport-open`).
  Setup installs Crossport to a folder of the user's choice, with Start menu and desktop shortcuts, a PATH entry,
  the `.AppImage` association and an uninstall entry in Apps and features. The launcher reads the name and icon from
  an image, runs `crossport convert` into `Program Files\Crossport\<app>` (or a per-user folder that needs no
  elevation), registers the app with its own uninstaller, and shows a readable report when an app is unsupported.
  A failed conversion leaves nothing behind, and an uninstall refuses to run while the app is open.
- Releases: pushing a tag `vMAJOR.MINOR.PATCH` runs `.github/workflows/release.yml`, which builds the translator, the shim
  and the installer and publishes `Crossport-Setup.exe` with `SHA256SUMS.txt`. The tag becomes the programs' version.
- The runtime pack goes inside the installer (`installer/package.sh --runtime`), is installed in `<folder>\runtime` and
  is passed to every conversion.
- Optional Authenticode signing (`tools/ci/sign-windows.ps1`), run only when the secret `CROSSPORT_SIGN_PFX_BASE64`
  exists; the setup program finds its payload in a file signed after packing. See `docs/RELEASING.md`.
- `--check-update` (both programs): one WinHTTP request to the GitHub releases API on request, never in the background,
  allowlisted host and path, 8 second timeouts, no redirects; offers to open the release page.
- Not yet done: the first release, a real signing certificate, reproducible translation.

## 0.7.4

- A window thread: every X window now belongs to one shim thread that runs the message loop, so events arrive
  whatever the program's own threads are doing. Toolkits create windows on one thread and read events on another;
  a Win32 window only receives messages on the thread that made it, which would have left Qt without events.
  Creating, destroying and focusing a window run on that thread (other calls are carried out for the caller), a
  thread waiting for an event sleeps on a condition variable, and a poll on the display descriptor wakes when an
  event arrives.

## 0.7.3

- Images Windows accepts: when ELF segments leave a gap between them, the section before the gap now runs up to the
  next one. Windows refuses an image whose sections are not contiguous (`0xC000007B`), which hit 7 of the 153
  modules of KeePassXC (zlib, expat, freetype, fontconfig, harfbuzz, libusb, wayland-client) and would have hit
  other programs.
- `unpack` keeps the read and execute bits of files on a Unix host, so a prebuilt AppImage can be run in place.
- KeePassXC 2.8.0 joins the corpus (a prebuilt image, converted with the runtime pack and Qt's plugin path set by
  `convert`); `libX11-xcb` is provided by the shim.

## 0.7.2

First results from a real Qt application, KeePassXC 2.8.0 (Qt 6, 142 modules with its plugins):

- A thread-local access shorter than a jump (for example `mov %fs:0x28(%rdx),%eax`, 4 bytes) used to stop the
  translation. It now moves into a stub together with its neighbours: the shortest straight-line run of up to
  three instructions on each side that is long enough, has no branch landing inside it, and may include
  conditional jumps (re-encoded to reach their targets). Two adjacent short accesses share one stub.
- "Nobody provides this symbol" now respects scopes: a symbol that only a plugin defines no longer looks provided
  to a library loaded at startup, so `--stub-missing` binds it instead of the translation failing.
- Rewrite errors name the module and show the instruction's bytes.

## 0.7.1

- Red zone: audited and enforced. Nothing in the shim enters an alertable wait or queues an APC, and a check in
  CI (`tools/ci/check_alertable.py`) keeps it that way, so Windows never runs asynchronous code on the stack
  below a translated thread's stack pointer. `red_zone` runs red-zone leaf functions on several threads while
  the X11 layer dispatches events, and matches Linux.
- The translator now counts raw `syscall` instructions in each module and warns about them; they are not
  translated, and until now nothing said so.

## 0.7.0

Milestone 7 starts with the plugin loading toolkits need.

- `dlopen` opens translated plugins. `--plugin-dir <dir>` (repeatable; `convert` also scans the AppDir's
  `usr/plugins`) names directories whose shared objects are translated as DLLs of their own, with the libraries
  only they need. Opening one loads those libraries first, binds the plugin, runs its constructors, and gives
  `dlsym` its exported functions. A plugin looks symbols up in the program and its libraries first, then in itself
  and its own libraries, so two plugins that export one name each use their own.
- Plugins reach the program's exported functions and can use thread-local variables; C++ exceptions unwind
  through them and crash reports cover them. `dlclose` still leaves a plugin loaded.

## 0.6.12

- A real SDL2 program runs: SDL 2.30.9's own test harness, built from source, packaged as an AppImage and
  translated, passes the Platform, Rect, Pixels, Surface, Render, Keyboard and Events suites exactly as on Linux,
  and the Video suite with the same two failing cases as on Linux.
- Window titles: `XmbTextListToTextProperty`, `Xutf8TextListToTextProperty`, `XStringListToTextProperty` and
  `XSetTextProperty` are implemented; before, SDL freed a garbage pointer when it set a title and the program
  crashed. `XQueryTree` answers with the flat window tree the shim keeps.

## 0.6.11

- Runtime pack: `tools/runtime/pack-spec.json` lists the Ubuntu packages that supply freetype, fontconfig, glib,
  harfbuzz, libxkbcommon and the libstdc++ and libgcc_s runtimes; `pin_pack.py` resolves them against a dated
  Ubuntu snapshot (checking the index against the suite's InRelease hash) into `pack.json`; `fetch_pack.py`
  downloads each package, verifies its SHA-256 and unpacks the shared libraries into one directory for
  `--runtime`. Crossport hosts nothing.

## 0.6.10

- Clipboard: text copied in a Windows program can be pasted in a translated one, and text a translated program copies
  can be pasted in Windows. The shim converts between UTF-8 with LF and Windows' UTF-16 with CRLF, answers requests for
  `CLIPBOARD` from the Windows text when no program owns it, and sends `SelectionClear` to a translated owner when
  another Windows program replaces its copy. Only plain text is bridged.

## 0.6.9

- Faster drawing: `XPutImage`, `XCopyArea`, `XGetImage` and rectangle fills copy whole rows when the pixels are 32-bit
  and the operation is a plain copy. At 1080p, `XPutImage` takes 1.9 ms per frame instead of 12.6, `XCopyArea` 1.8
  instead of 8.3, and a full fill 0.7 instead of 2.5.
- Polygons are filled at pixel coordinates, as X does, and now match a real server pixel for pixel.
- `XTestFakeMotionEvent` waits until the window has reported the move, so injected events keep their order.

## 0.6.8

- Xlib drawing: `XFillPolygon` follows the X fill rule exactly (pixel centres, both fill rules, relative
  coordinates), `XDrawRectangles` is added, and arcs (`XDrawArc`, `XFillArc` and their array forms) are drawn
  approximately. The drawing code is split into primitives, entry points and polygons.
- Selections: `SelectionClear` is sent as Xorg sends it, to the previous owner when the selection passes to
  another client or is released.

## 0.6.7

- Selections: the clipboard conversation between the windows of one process works. Ownership, `SelectionClear`,
  `SelectionRequest` and `SelectionNotify` follow the X protocol, so a program such as SDL2 can set and read its
  clipboard. Windows' own clipboard is not connected yet.
- The input samples park the pointer away from their window first, because Windows keeps the pointer where
  the previous program left it.

## 0.6.6

- SDL2 input: an unmodified Linux SDL2 program receives keyboard, text, pointer and wheel events through the shim
  and sees the same events, scancodes, keycodes, modifiers and coordinates as on Linux.

## 0.6.5

- Input: `libXtst.so.6` is provided by the shim. A program can inject keys, pointer moves, clicks and wheel
  notches into its own display, and they reach it through Windows input and the window procedure. Window
  positions are now where X puts them (the client area's origin), key and button events carry the state from
  before the event as X reports it, and input events carry root coordinates. A sample matches Linux event for
  event.

## 0.6.4

- EGL on Windows: `libEGL.so.1`, `libGLESv2.so.2` and `libGLESv1_CM.so.1` are provided by the shim on the same
  WGL layer. It offers one display and configuration, window and pbuffer surfaces, OpenGL and OpenGL ES 2
  contexts (through WGL's ES profile), surfaceless contexts and `eglGetProcAddress`. An EGL sample matches
  Linux output.

## 0.6.3

- OpenGL on Windows: `libGL.so.1`, `libGLX.so.0` and `libOpenGL.so.0` are provided by the shim on top of
  `opengl32.dll` (WGL). Every OpenGL command is forwarded by a thunk generated from the pinned registry, and
  GLX provides visuals, framebuffer configurations, contexts (including OpenGL 3.0+ attributes), buffer swaps
  and `glXGetProcAddress`. A GLX sample matches Linux output pixel for pixel.

## 0.6.2

- An unmodified Linux SDL2 program (X11 driver, window surface) opens a window, draws and exits on Windows,
  and its output matches Linux pixel for pixel. This needed `XListPixmapFormats`, a correct
  `XWithdrawWindow`, and the `sdl2_window` case translating with `--stub-missing`.

## 0.6.1

- Crash reports: an unhandled exception in a translated program now prints its kind, the faulting module and
  offset, the address and access type, and a rough call trail on stderr (shim/crash.c).
- CI runs its jobs in parallel with caches, and a manual run can name a single corpus case.

## 0.6.0

- `--stub-missing` for `translate` and `convert`: libraries and symbols that nobody provides are bound to
  a shim stub that returns 0, and the translator lists what it stubbed.
- In-process Xlib and xcb in the shim: windows, events, keyboard, properties, drawing, images.
- SDL2 window sample added to the corpus (Milestone 6).
