/*
 * File: shim/xlib/xlib_draw.c
 * Purpose: Pixel drawing into drawables (rectangles, lines, points, polygons, area copies) and the libX11 functions that use it
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Everything draws into the 32-bit pixel buffers of xlib_resource.c with the GC's raster operation, clipped
 * to the drawable. Lines wider than one pixel are drawn with a square brush at every point, which differs
 * from a real server along diagonals. Arcs, wide-line joins and dashes are not drawn yet.
 */

#include <string.h>

#include "xlib_internal.h"

#define COORD_MODE_PREVIOUS 1

uint32_t xl_combine(uint32_t source, uint32_t destination, int32_t function)
{
    uint32_t result;
    switch (function) {
    case XL_GX_CLEAR: result = 0; break;
    case XL_GX_AND: result = source & destination; break;
    case XL_GX_AND_REVERSE: result = source & ~destination; break;
    case XL_GX_AND_INVERTED: result = ~source & destination; break;
    case XL_GX_NOOP: result = destination; break;
    case XL_GX_XOR: result = source ^ destination; break;
    case XL_GX_OR: result = source | destination; break;
    case XL_GX_NOR: result = ~(source | destination); break;
    case XL_GX_EQUIV: result = ~(source ^ destination); break;
    case XL_GX_INVERT: result = ~destination; break;
    case XL_GX_OR_REVERSE: result = source | ~destination; break;
    case XL_GX_COPY_INVERTED: result = ~source; break;
    case XL_GX_OR_INVERTED: result = ~source | destination; break;
    case XL_GX_NAND: result = ~(source & destination); break;
    case XL_GX_SET: result = XL_PIXEL_MASK; break;
    default: result = source; break;
    }
    return result & XL_PIXEL_MASK;
}

static void put_pixel(XlDrawable *drawable, int32_t x, int32_t y, uint32_t pixel, int32_t function)
{
    if (x >= 0 && y >= 0 && x < drawable->width && y < drawable->height) {
        uint32_t *slot = &drawable->pixels[(size_t)y * (size_t)drawable->width + (size_t)x];
        *slot = function == XL_GX_COPY ? pixel : xl_combine(pixel, *slot, function);
    }
}

static void fill_clipped(XlDrawable *drawable, const XlGc *gc, int32_t x, int32_t y, int32_t width, int32_t height)
{
    int32_t left = x < 0 ? 0 : x;
    int32_t top = y < 0 ? 0 : y;
    int32_t right = x + width > drawable->width ? drawable->width : x + width;
    int32_t bottom = y + height > drawable->height ? drawable->height : y + height;
    uint32_t pixel = xl_pixel(gc->values.foreground);
    for (int32_t row = top; row < bottom; row++) {
        for (int32_t column = left; column < right; column++) {
            put_pixel(drawable, column, row, pixel, gc->values.function);
        }
    }
}

void xl_fill_rectangle(XlId id, const XlGc *gc, int32_t x, int32_t y, int32_t width, int32_t height)
{
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(id);
    int is_window = drawable != NULL && drawable->is_window;
    if (drawable != NULL && width > 0 && height > 0) {
        fill_clipped(drawable, gc, x, y, width, height);
    }
    xl_unlock();
    if (is_window) {
        xl_invalidate(id, x, y, width, height);
    }
}

static void plot(XlDrawable *drawable, const XlGc *gc, int32_t x, int32_t y)
{
    int32_t width = gc->values.line_width > 1 ? gc->values.line_width : 1;
    if (width == 1) {
        put_pixel(drawable, x, y, xl_pixel(gc->values.foreground), gc->values.function);
    } else {
        fill_clipped(drawable, gc, x - width / 2, y - width / 2, width, width);
    }
}

static int32_t absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

/* Bresenham's line, including both end points. */
static void trace_line(XlDrawable *drawable, const XlGc *gc, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t dx = absolute(x2 - x1);
    int32_t dy = -absolute(y2 - y1);
    int32_t step_x = x1 < x2 ? 1 : -1;
    int32_t step_y = y1 < y2 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        plot(drawable, gc, x1, y1);
        if (x1 == x2 && y1 == y2) {
            return;
        }
        int32_t doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x1 += step_x;
        }
        if (doubled <= dx) {
            error += dx;
            y1 += step_y;
        }
    }
}

void xl_draw_line(XlId id, const XlGc *gc, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(id);
    int is_window = drawable != NULL && drawable->is_window;
    if (drawable != NULL) {
        trace_line(drawable, gc, x1, y1, x2, y2);
    }
    xl_unlock();
    if (is_window) {
        int32_t pad = gc->values.line_width + 1;
        int32_t left = x1 < x2 ? x1 : x2;
        int32_t top = y1 < y2 ? y1 : y2;
        xl_invalidate(id, left - pad, top - pad, absolute(x2 - x1) + 2 * pad + 1, absolute(y2 - y1) + 2 * pad + 1);
    }
}

void xl_draw_point(XlId id, const XlGc *gc, int32_t x, int32_t y)
{
    xl_draw_line(id, gc, x, y, x, y);
}

void xl_copy_area(XlId source, XlId target, const XlGc *gc, int32_t sx, int32_t sy, int32_t width, int32_t height,
                  int32_t dx, int32_t dy)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    uint32_t *copy = (uint32_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)width * (size_t)height * sizeof(uint32_t));
    if (copy == NULL) {
        return;
    }
    xl_lock();
    XlDrawable *from = xl_drawable_find(source);
    XlDrawable *to = xl_drawable_find(target);
    int is_window = to != NULL && to->is_window;
    if (from != NULL && to != NULL) {
        for (int32_t row = 0; row < height; row++) {
            for (int32_t column = 0; column < width; column++) {
                int32_t x = sx + column;
                int32_t y = sy + row;
                int is_inside = x >= 0 && y >= 0 && x < from->width && y < from->height;
                copy[(size_t)row * (size_t)width + (size_t)column] = is_inside ? from->pixels[(size_t)y * (size_t)from->width + (size_t)x] : 0;
            }
        }
        for (int32_t row = 0; row < height; row++) {
            for (int32_t column = 0; column < width; column++) {
                put_pixel(to, dx + column, dy + row, copy[(size_t)row * (size_t)width + (size_t)column], gc->values.function);
            }
        }
    }
    xl_unlock();
    HeapFree(GetProcessHeap(), 0, copy);
    if (is_window) {
        xl_invalidate(target, dx, dy, width, height);
    }
}

LIBC_FN int sysv_XFillRectangle(XlDisplay *display, XlId drawable, const XlGc *gc, int x, int y, unsigned width, unsigned height)
{
    xl_next_request(display);
    xl_fill_rectangle(drawable, gc, x, y, (int32_t)width, (int32_t)height);
    return 1;
}

LIBC_FN int sysv_XFillRectangles(XlDisplay *display, XlId drawable, const XlGc *gc, const XlRectangle *rectangles, int count)
{
    xl_next_request(display);
    for (int i = 0; i < count; i++) {
        xl_fill_rectangle(drawable, gc, rectangles[i].x, rectangles[i].y, rectangles[i].width, rectangles[i].height);
    }
    return 1;
}

LIBC_FN int sysv_XDrawRectangle(XlDisplay *display, XlId drawable, const XlGc *gc, int x, int y, unsigned width, unsigned height)
{
    xl_next_request(display);
    int32_t right = x + (int32_t)width;
    int32_t bottom = y + (int32_t)height;
    xl_draw_line(drawable, gc, x, y, right, y);
    xl_draw_line(drawable, gc, right, y, right, bottom);
    xl_draw_line(drawable, gc, right, bottom, x, bottom);
    xl_draw_line(drawable, gc, x, bottom, x, y);
    return 1;
}

LIBC_FN int sysv_XDrawLine(XlDisplay *display, XlId drawable, const XlGc *gc, int x1, int y1, int x2, int y2)
{
    xl_next_request(display);
    xl_draw_line(drawable, gc, x1, y1, x2, y2);
    return 1;
}

LIBC_FN int sysv_XDrawLines(XlDisplay *display, XlId drawable, const XlGc *gc, const XlPoint *points, int count, int mode)
{
    xl_next_request(display);
    int32_t x = count > 0 ? points[0].x : 0;
    int32_t y = count > 0 ? points[0].y : 0;
    for (int i = 1; i < count; i++) {
        int32_t next_x = mode == COORD_MODE_PREVIOUS ? x + points[i].x : points[i].x;
        int32_t next_y = mode == COORD_MODE_PREVIOUS ? y + points[i].y : points[i].y;
        xl_draw_line(drawable, gc, x, y, next_x, next_y);
        x = next_x;
        y = next_y;
    }
    return 1;
}

LIBC_FN int sysv_XDrawSegments(XlDisplay *display, XlId drawable, const XlGc *gc, const XlSegment *segments, int count)
{
    xl_next_request(display);
    for (int i = 0; i < count; i++) {
        xl_draw_line(drawable, gc, segments[i].x1, segments[i].y1, segments[i].x2, segments[i].y2);
    }
    return 1;
}

LIBC_FN int sysv_XDrawPoint(XlDisplay *display, XlId drawable, const XlGc *gc, int x, int y)
{
    xl_next_request(display);
    xl_draw_point(drawable, gc, x, y);
    return 1;
}

LIBC_FN int sysv_XDrawPoints(XlDisplay *display, XlId drawable, const XlGc *gc, const XlPoint *points, int count, int mode)
{
    xl_next_request(display);
    int32_t x = 0;
    int32_t y = 0;
    for (int i = 0; i < count; i++) {
        x = mode == COORD_MODE_PREVIOUS ? x + points[i].x : points[i].x;
        y = mode == COORD_MODE_PREVIOUS ? y + points[i].y : points[i].y;
        xl_draw_point(drawable, gc, x, y);
    }
    return 1;
}

LIBC_FN int sysv_XCopyArea(XlDisplay *display, XlId source, XlId target, const XlGc *gc, int sx, int sy, unsigned width,
                           unsigned height, int dx, int dy)
{
    xl_next_request(display);
    xl_copy_area(source, target, gc, sx, sy, (int32_t)width, (int32_t)height, dx, dy);
    return 1;
}

/* Paints a window area with its background pixel, which is what XClearArea means. */
LIBC_FN int sysv_XClearArea(XlDisplay *display, XlId window, int x, int y, unsigned width, unsigned height, int should_expose)
{
    (void)should_expose;
    xl_next_request(display);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    uint32_t background = drawable != NULL ? drawable->background : 0;
    int32_t full_width = drawable != NULL ? drawable->width : 0;
    int32_t full_height = drawable != NULL ? drawable->height : 0;
    xl_unlock();
    XlGc gc = {.values = {.function = XL_GX_COPY, .foreground = background}};
    xl_fill_rectangle(window, &gc, x, y, width != 0 ? (int32_t)width : full_width - x, height != 0 ? (int32_t)height : full_height - y);
    return 1;
}

LIBC_FN int sysv_XClearWindow(XlDisplay *display, XlId window)
{
    return sysv_XClearArea(display, window, 0, 0, 0, 0, 0);
}

LIBC_FN XlId sysv_XCreatePixmap(XlDisplay *display, XlId parent, unsigned width, unsigned height, unsigned depth)
{
    (void)parent;
    xl_next_request(display);
    XlId id = xl_allocate_id(display);
    xl_lock();
    XlDrawable *drawable = xl_drawable_add(id, 0, (int32_t)depth, width > 0 ? (int32_t)width : 1, height > 0 ? (int32_t)height : 1, 0);
    xl_unlock();
    return drawable != NULL ? id : 0;
}

LIBC_FN int sysv_XFreePixmap(XlDisplay *display, XlId pixmap)
{
    xl_next_request(display);
    xl_lock();
    xl_drawable_remove(pixmap);
    xl_unlock();
    return 1;
}
