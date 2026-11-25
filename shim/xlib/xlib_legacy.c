/*
 * File: shim/xlib/xlib_legacy.c
 * Purpose: Old Xlib entry points that demo programs and libraries still import: XSetStandardProperties, XSetNormalHints, XAllocColor,
 *          XGrabServer, XUngrabServer, XGetDefault, XESetCloseDisplay and XExtendedMaxRequestSize
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the compatibility survey (XSetNormalHints and XSetStandardProperties each block 8 programs). The shim's display is one 24-bit
 * TrueColor screen with no other clients, so a color is its own pixel value, a server grab succeeds and no resource database exists.
 */

#include "xlib_internal.h"

#define COLOR_SHIFT_RED 16
#define COLOR_SHIFT_GREEN 8
#define COLOR_CHANNEL_SHIFT 8
#define XL_DO_ALL_COLORS 7
#define XL_STATUS_OK 1

/* struct XColor: pixel, three 16-bit channels, flags and a pad byte. */
typedef struct {
    uint64_t pixel;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    char flags;
    char padding;
} XlColor;

LIBC_FN int sysv_XStoreName(XlDisplay *display, XlId window, const char *name);

LIBC_FN int sysv_XSetStandardProperties(XlDisplay *display, XlId window, const char *name, const char *icon_name, XlId icon_pixmap, char **argv,
                                        int argc, void *hints)
{
    (void)icon_name;
    (void)icon_pixmap;
    (void)argv;
    (void)argc;
    (void)hints;
    if (name != NULL) {
        sysv_XStoreName(display, window, name);
    }
    return 1;
}

LIBC_FN int sysv_XSetNormalHints(XlDisplay *display, XlId window, void *hints)
{
    (void)display;
    (void)window;
    (void)hints;
    return 1;
}

/* On a 24-bit TrueColor visual a pixel is the three channels' high bytes packed, and the color the server gives back is what was asked for. */
LIBC_FN int sysv_XAllocColor(XlDisplay *display, XlId colormap, XlColor *color)
{
    (void)display;
    (void)colormap;
    color->pixel = ((uint64_t)(color->red >> COLOR_CHANNEL_SHIFT) << COLOR_SHIFT_RED) |
                   ((uint64_t)(color->green >> COLOR_CHANNEL_SHIFT) << COLOR_SHIFT_GREEN) | (uint64_t)(color->blue >> COLOR_CHANNEL_SHIFT);
    color->flags = XL_DO_ALL_COLORS;
    return XL_STATUS_OK;
}

LIBC_FN int sysv_XGrabServer(XlDisplay *display)
{
    (void)display;
    return 1;
}

LIBC_FN int sysv_XUngrabServer(XlDisplay *display)
{
    (void)display;
    return 1;
}

/* There is no X resource database (no .Xdefaults), so no default is ever set. */
LIBC_FN char *sysv_XGetDefault(XlDisplay *display, const char *program, const char *option)
{
    (void)display;
    (void)program;
    (void)option;
    return NULL;
}

LIBC_FN void *sysv_XESetCloseDisplay(XlDisplay *display, int extension, void *handler)
{
    (void)display;
    (void)extension;
    (void)handler;
    return NULL;
}

/* The core protocol's request length limit, in 4-byte units, is all the shim supports. */
LIBC_FN int64_t sysv_XExtendedMaxRequestSize(XlDisplay *display)
{
    (void)display;
    return 0;
}
