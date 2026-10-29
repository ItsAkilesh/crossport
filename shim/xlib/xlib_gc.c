/*
 * File: shim/xlib/xlib_gc.c
 * Purpose: libX11 graphics contexts: XCreateGC, XChangeGC, the XSet* attribute setters and XGetGCValues
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * A GC is a block of memory holding its XGCValues. Programs only pass the pointer back, except for
 * XGContextFromGC, which reads an id that is the block's own address. Values that need no work to honour
 * (clip masks, tiles, fonts) are stored and ignored by the drawing code.
 */

#include <string.h>

#include "xlib_internal.h"

#define GC_FUNCTION (1u << 0)
#define GC_PLANE_MASK (1u << 1)
#define GC_FOREGROUND (1u << 2)
#define GC_BACKGROUND (1u << 3)
#define GC_LINE_WIDTH (1u << 4)
#define GC_LINE_STYLE (1u << 5)
#define GC_CAP_STYLE (1u << 6)
#define GC_JOIN_STYLE (1u << 7)
#define GC_FILL_STYLE (1u << 8)
#define GC_FILL_RULE (1u << 9)
#define GC_TILE (1u << 10)
#define GC_STIPPLE (1u << 11)
#define GC_TS_X_ORIGIN (1u << 12)
#define GC_TS_Y_ORIGIN (1u << 13)
#define GC_FONT (1u << 14)
#define GC_SUBWINDOW_MODE (1u << 15)
#define GC_GRAPHICS_EXPOSURES (1u << 16)
#define GC_CLIP_X_ORIGIN (1u << 17)
#define GC_CLIP_Y_ORIGIN (1u << 18)
#define GC_CLIP_MASK (1u << 19)
#define GC_DASH_OFFSET (1u << 20)
#define GC_DASH_LIST (1u << 21)
#define GC_ARC_MODE (1u << 22)

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);

static void apply(XlGcValues *target, uint64_t mask, const XlGcValues *values)
{
    if (mask & GC_FUNCTION) target->function = values->function;
    if (mask & GC_PLANE_MASK) target->plane_mask = values->plane_mask;
    if (mask & GC_FOREGROUND) target->foreground = values->foreground;
    if (mask & GC_BACKGROUND) target->background = values->background;
    if (mask & GC_LINE_WIDTH) target->line_width = values->line_width;
    if (mask & GC_LINE_STYLE) target->line_style = values->line_style;
    if (mask & GC_CAP_STYLE) target->cap_style = values->cap_style;
    if (mask & GC_JOIN_STYLE) target->join_style = values->join_style;
    if (mask & GC_FILL_STYLE) target->fill_style = values->fill_style;
    if (mask & GC_FILL_RULE) target->fill_rule = values->fill_rule;
    if (mask & GC_TILE) target->tile = values->tile;
    if (mask & GC_STIPPLE) target->stipple = values->stipple;
    if (mask & GC_TS_X_ORIGIN) target->ts_x_origin = values->ts_x_origin;
    if (mask & GC_TS_Y_ORIGIN) target->ts_y_origin = values->ts_y_origin;
    if (mask & GC_FONT) target->font = values->font;
    if (mask & GC_SUBWINDOW_MODE) target->subwindow_mode = values->subwindow_mode;
    if (mask & GC_GRAPHICS_EXPOSURES) target->graphics_exposures = values->graphics_exposures;
    if (mask & GC_CLIP_X_ORIGIN) target->clip_x_origin = values->clip_x_origin;
    if (mask & GC_CLIP_Y_ORIGIN) target->clip_y_origin = values->clip_y_origin;
    if (mask & GC_CLIP_MASK) target->clip_mask = values->clip_mask;
    if (mask & GC_DASH_OFFSET) target->dash_offset = values->dash_offset;
    if (mask & GC_DASH_LIST) target->dashes = values->dashes;
    if (mask & GC_ARC_MODE) target->arc_mode = values->arc_mode;
}

LIBC_FN XlGc *sysv_XCreateGC(XlDisplay *display, XlId drawable, uint64_t mask, const XlGcValues *values)
{
    (void)drawable;
    XlGc *gc = (XlGc *)sysv_malloc(sizeof(*gc));
    if (gc == NULL) {
        return NULL;
    }
    xl_next_request(display);
    memset(gc, 0, sizeof(*gc));
    gc->values = (XlGcValues){.function = XL_GX_COPY, .plane_mask = XL_ALL_PLANES, .foreground = 0, .background = 1,
                              .fill_rule = 0, .graphics_exposures = 1, .dashes = 4};
    gc->id = (XlId)(uintptr_t)gc;
    if (values != NULL) {
        apply(&gc->values, mask, values);
    }
    return gc;
}

LIBC_FN int sysv_XFreeGC(XlDisplay *display, XlGc *gc)
{
    (void)display;
    sysv_free(gc);
    return 1;
}

LIBC_FN int sysv_XChangeGC(XlDisplay *display, XlGc *gc, uint64_t mask, const XlGcValues *values)
{
    xl_next_request(display);
    apply(&gc->values, mask, values);
    return 1;
}

LIBC_FN int sysv_XCopyGC(XlDisplay *display, XlGc *source, uint64_t mask, XlGc *target)
{
    xl_next_request(display);
    apply(&target->values, mask, &source->values);
    return 1;
}

LIBC_FN int sysv_XGetGCValues(XlDisplay *display, XlGc *gc, uint64_t mask, XlGcValues *out)
{
    (void)display;
    apply(out, mask, &gc->values);
    return 1;
}

LIBC_FN XlId sysv_XGContextFromGC(XlGc *gc)
{
    return gc->id;
}

LIBC_FN int sysv_XSetForeground(XlDisplay *display, XlGc *gc, uint64_t pixel)
{
    xl_next_request(display);
    gc->values.foreground = pixel;
    return 1;
}

LIBC_FN int sysv_XSetBackground(XlDisplay *display, XlGc *gc, uint64_t pixel)
{
    xl_next_request(display);
    gc->values.background = pixel;
    return 1;
}

LIBC_FN int sysv_XSetFunction(XlDisplay *display, XlGc *gc, int function)
{
    xl_next_request(display);
    gc->values.function = function;
    return 1;
}

LIBC_FN int sysv_XSetPlaneMask(XlDisplay *display, XlGc *gc, uint64_t mask)
{
    xl_next_request(display);
    gc->values.plane_mask = mask;
    return 1;
}

LIBC_FN int sysv_XSetLineAttributes(XlDisplay *display, XlGc *gc, unsigned width, int style, int cap, int join)
{
    xl_next_request(display);
    gc->values.line_width = (int32_t)width;
    gc->values.line_style = style;
    gc->values.cap_style = cap;
    gc->values.join_style = join;
    return 1;
}

LIBC_FN int sysv_XSetFillStyle(XlDisplay *display, XlGc *gc, int style)
{
    xl_next_request(display);
    gc->values.fill_style = style;
    return 1;
}

LIBC_FN int sysv_XSetFillRule(XlDisplay *display, XlGc *gc, int rule)
{
    xl_next_request(display);
    gc->values.fill_rule = rule;
    return 1;
}

LIBC_FN int sysv_XSetGraphicsExposures(XlDisplay *display, XlGc *gc, int is_enabled)
{
    xl_next_request(display);
    gc->values.graphics_exposures = is_enabled;
    return 1;
}

LIBC_FN int sysv_XSetSubwindowMode(XlDisplay *display, XlGc *gc, int mode)
{
    xl_next_request(display);
    gc->values.subwindow_mode = mode;
    return 1;
}

LIBC_FN int sysv_XSetFont(XlDisplay *display, XlGc *gc, XlId font)
{
    xl_next_request(display);
    gc->values.font = font;
    return 1;
}

LIBC_FN int sysv_XSetClipMask(XlDisplay *display, XlGc *gc, XlId pixmap)
{
    xl_next_request(display);
    gc->values.clip_mask = pixmap;
    return 1;
}

LIBC_FN int sysv_XSetClipOrigin(XlDisplay *display, XlGc *gc, int x, int y)
{
    xl_next_request(display);
    gc->values.clip_x_origin = x;
    gc->values.clip_y_origin = y;
    return 1;
}

/* Clip rectangles are accepted and not applied: drawing is clipped to the drawable only. */
LIBC_FN int sysv_XSetClipRectangles(XlDisplay *display, XlGc *gc, int x, int y, const XlRectangle *rectangles, int count,
                                    int ordering)
{
    (void)rectangles;
    (void)count;
    (void)ordering;
    return sysv_XSetClipOrigin(display, gc, x, y);
}
