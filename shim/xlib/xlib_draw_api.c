/*
 * File: shim/xlib/xlib_draw_api.c
 * Purpose: The libX11 drawing entry points (rectangles, lines, points, area copies, clearing, pixmaps) over the pixel primitives
 * Author: Akilesh
 * Date: 2026-11-04
 */

#include "xlib_internal.h"

#define COORD_MODE_PREVIOUS 1

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

LIBC_FN int sysv_XDrawRectangles(XlDisplay *display, XlId drawable, const XlGc *gc, const XlRectangle *rectangles, int count)
{
    for (int i = 0; i < count; i++) {
        sysv_XDrawRectangle(display, drawable, gc, rectangles[i].x, rectangles[i].y, rectangles[i].width, rectangles[i].height);
    }
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
