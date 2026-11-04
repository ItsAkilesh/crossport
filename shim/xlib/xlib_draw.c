/*
 * File: shim/xlib/xlib_draw.c
 * Purpose: Pixel drawing into drawables: rectangles, lines, points, horizontal spans and area copies
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Everything draws into the 32-bit pixel buffers of xlib_resource.c with the GC's raster operation, clipped
 * to the drawable. Lines wider than one pixel are drawn with a square brush at every point, which differs
 * from a real server along diagonals. Wide-line joins and dashes are not drawn yet. The libX11 entry points that
 * call these are in xlib_draw_api.c; polygons and arcs are in xlib_polygon.c.
 */

#include <string.h>

#include "xlib_internal.h"

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

void xl_fill_spans(XlId id, const XlGc *gc, int32_t y, const int32_t *bounds, size_t count)
{
    int32_t left = 0;
    int32_t right = 0;
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(id);
    int is_window = drawable != NULL && drawable->is_window;
    for (size_t i = 0; drawable != NULL && i < count; i++) {
        if (bounds[2 * i + 1] > bounds[2 * i]) {
            fill_clipped(drawable, gc, bounds[2 * i], y, bounds[2 * i + 1] - bounds[2 * i], 1);
            left = i == 0 || bounds[2 * i] < left ? bounds[2 * i] : left;
            right = i == 0 || bounds[2 * i + 1] > right ? bounds[2 * i + 1] : right;
        }
    }
    xl_unlock();
    if (is_window && right > left) {
        xl_invalidate(id, left, y, right - left, 1);
    }
}
