# Changelog

Versioning: `0.<milestone>.<patch>` while Crossport is pre-1.0. The minor number is the roadmap milestone
being worked on. A feature commit that adds user-visible capability (a new flag, a new class of program
that runs) bumps the patch number; a fix does not. The version lives in `translator/Cargo.toml` and is
bumped in the same commit as the change. Releases are tagged by the maintainer, not by automation.

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
