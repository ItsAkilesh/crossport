/*
 * File: shim/xlib/xlib_formats.c
 * Purpose: XListPixmapFormats: the pixel storage of each depth, which toolkits use to size their image buffers
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * A depth-24 visual is stored in 32 bits per pixel here, as on Xorg. Clients such as SDL2 read that to choose
 * the layout of the buffer they hand to XPutImage, so an answer of "none" makes them assume 3 bytes per pixel.
 */

#include "xlib_internal.h"

LIBC_FN void *sysv_malloc(size_t size);

LIBC_FN XlPixmapFormat *sysv_XListPixmapFormats(XlDisplay *display, int *count)
{
    const XlDisplayData *data = xl_display_data(display);
    size_t total = sizeof(data->formats) / sizeof(data->formats[0]);
    XlPixmapFormat *list = (XlPixmapFormat *)sysv_malloc(total * sizeof(*list));
    *count = 0;
    if (list == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < total; i++) {
        list[i] = (XlPixmapFormat){.depth = data->formats[i].depth, .bits_per_pixel = data->formats[i].bits_per_pixel,
                                   .scanline_pad = data->formats[i].scanline_pad};
    }
    *count = (int)total;
    return list;
}
