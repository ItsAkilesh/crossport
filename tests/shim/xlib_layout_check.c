/*
 * File: tests/shim/xlib_layout_check.c
 * Purpose: Compile-time check, against libX11's real headers, of the structure layouts shim/xlib/xlib_abi.h asserts
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The shim's headers cannot include Xlib's (they target Linux types), so the numbers are repeated here. A
 * build of this file on Linux fails if the real layouts differ from what the shim assumes.
 */

#define XLIB_ILLEGAL_ACCESS 1
#include <stddef.h>
#include <X11/Xlib.h>

_Static_assert(sizeof(Screen) == 128, "Screen is 128 bytes");
_Static_assert(sizeof(Visual) == 56, "Visual is 56 bytes");
_Static_assert(sizeof(Depth) == 16, "Depth is 16 bytes");
_Static_assert(sizeof(ScreenFormat) == 24, "ScreenFormat is 24 bytes");
_Static_assert(sizeof(XPixmapFormatValues) == 12, "XPixmapFormatValues is 12 bytes");
_Static_assert(sizeof(XImage) == 136, "XImage is 136 bytes");
_Static_assert(offsetof(XImage, f) == 88, "XImage function table is at 88");
_Static_assert(offsetof(Display, default_screen) == 224, "default_screen is at 224");
_Static_assert(offsetof(Display, screens) == 232, "screens is at 232");
_Static_assert(sizeof(Display) == 296, "the public Display is 296 bytes");
_Static_assert(sizeof(XAnyEvent) == 40, "XAnyEvent is 40 bytes");
_Static_assert(sizeof(XKeyEvent) == 96, "XKeyEvent is 96 bytes");
_Static_assert(sizeof(XPropertyEvent) == 64, "XPropertyEvent is 64 bytes");
_Static_assert(sizeof(XFocusChangeEvent) == 48, "XFocusChangeEvent is 48 bytes");
_Static_assert(sizeof(XCrossingEvent) == 104, "XCrossingEvent is 104 bytes");
_Static_assert(sizeof(XExposeEvent) == 64, "XExposeEvent is 64 bytes");
_Static_assert(sizeof(XMapEvent) == 56, "XMapEvent is 56 bytes");
_Static_assert(sizeof(XConfigureEvent) == 88, "XConfigureEvent is 88 bytes");
_Static_assert(sizeof(XClientMessageEvent) == 96, "XClientMessageEvent is 96 bytes");
_Static_assert(sizeof(XEvent) == 192, "XEvent is 192 bytes");
_Static_assert(sizeof(XSetWindowAttributes) == 112, "XSetWindowAttributes is 112 bytes");
_Static_assert(sizeof(XWindowAttributes) == 136, "XWindowAttributes is 136 bytes");
_Static_assert(sizeof(XGCValues) == 128, "XGCValues is 128 bytes");

int main(void)
{
    return 0;
}
