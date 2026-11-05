# Changelog

Versioning: `0.<milestone>.<patch>` while Crossport is pre-1.0. The minor number is the roadmap milestone
being worked on. A feature commit that adds user-visible capability (a new flag, a new class of program
that runs) bumps the patch number; a fix does not. The version lives in `translator/Cargo.toml` and is
bumped in the same commit as the change. Releases are tagged by the maintainer, not by automation.

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
