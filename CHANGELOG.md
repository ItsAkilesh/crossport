# Changelog

Versioning: `0.<milestone>.<patch>` while Crossport is pre-1.0. The minor number is the roadmap milestone
being worked on. A feature commit that adds user-visible capability (a new flag, a new class of program
that runs) bumps the patch number; a fix does not. The version lives in `translator/Cargo.toml` and is
bumped in the same commit as the change. Releases are tagged by the maintainer, not by automation.

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
- Not yet done: code signing, update checks, a release download, the runtime pack inside the installer.

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
