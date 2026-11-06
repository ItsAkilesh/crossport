/*
 * File: shim/xlib/xlib_xcb_draw.c
 * Purpose: xcb requests that draw: graphics contexts, pixmaps, copying, images, and the cursor, font and colormap requests toolkits make
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * A toolkit's software backing store reaches the screen through xcb_put_image and xcb_copy_area. They draw into
 * the same pixel buffers the Xlib layer keeps for windows and pixmaps, and the window procedure shows them on
 * paint. Graphics contexts are ids here, not pointers: they are kept in a table and handed to the drawing code
 * as the structure it already uses. Cursors, fonts and colormaps have nothing to draw with, so those requests
 * are accepted and ignored.
 */

#include <string.h>

#include "xlib_internal.h"

#define MAX_GCS 256
#define GC_VALUE_BITS 23
#define FORMAT_XY_BITMAP 0
#define FORMAT_Z_PIXMAP 2
#define BYTES_PER_PIXEL 4
#define PIXEL_MASK 0x00ffffffu
#define ROW_ALIGN 4

typedef struct {
    XlId id;
    XlGc gc;
} GcSlot;

static GcSlot g_gcs[MAX_GCS];
static SRWLOCK g_lock = SRWLOCK_INIT;

static XcbCookie done(void)
{
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

/* The GC for an id, copied out so the caller needs no lock; a default one when the id is unknown. */
static XlGc copy_of_gc(XlId id)
{
    XlGc found = {.values = {.function = XL_GX_COPY, .plane_mask = XL_ALL_PLANES, .background = 1}};
    AcquireSRWLockShared(&g_lock);
    for (size_t i = 0; i < MAX_GCS; i++) {
        if (g_gcs[i].id == id && id != 0) {
            found = g_gcs[i].gc;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/* Applies the value list of a GC request: one 32-bit word per bit set in the mask, in bit order. */
static void apply_values(XlGcValues *target, uint32_t mask, const uint32_t *list)
{
    size_t next = 0;
    for (uint32_t bit = 0; bit < GC_VALUE_BITS; bit++) {
        if ((mask & (1u << bit)) == 0) {
            continue;
        }
        uint32_t value = list[next++];
        switch (bit) {
        case 0: target->function = (int32_t)value; break;
        case 1: target->plane_mask = value; break;
        case 2: target->foreground = value; break;
        case 3: target->background = value; break;
        case 4: target->line_width = (int32_t)value; break;
        case 5: target->line_style = (int32_t)value; break;
        case 6: target->cap_style = (int32_t)value; break;
        case 7: target->join_style = (int32_t)value; break;
        case 8: target->fill_style = (int32_t)value; break;
        case 9: target->fill_rule = (int32_t)value; break;
        case 15: target->subwindow_mode = (int32_t)value; break;
        case 16: target->graphics_exposures = (int32_t)value; break;
        case 17: target->clip_x_origin = (int32_t)value; break;
        case 18: target->clip_y_origin = (int32_t)value; break;
        default: break;
        }
    }
}

LIBC_FN XcbCookie sysv_xcb_create_gc(void *connection, XcbId id, XcbId drawable, uint32_t mask, const void *values)
{
    (void)connection;
    (void)drawable;
    GcSlot fresh = {.id = id};
    fresh.gc.id = id;
    fresh.gc.values = (XlGcValues){.function = XL_GX_COPY, .plane_mask = XL_ALL_PLANES, .background = 1,
                                   .graphics_exposures = 1, .dashes = 4};
    apply_values(&fresh.gc.values, mask, (const uint32_t *)values);
    AcquireSRWLockExclusive(&g_lock);
    GcSlot *slot = NULL;
    for (size_t i = 0; i < MAX_GCS; i++) {
        if (g_gcs[i].id == id) {
            slot = &g_gcs[i];
        } else if (slot == NULL && g_gcs[i].id == 0) {
            slot = &g_gcs[i];
        }
    }
    if (slot != NULL) {
        *slot = fresh;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return done();
}

LIBC_FN XcbCookie sysv_xcb_create_gc_checked(void *connection, XcbId id, XcbId drawable, uint32_t mask, const void *values)
{
    return sysv_xcb_create_gc(connection, id, drawable, mask, values);
}

LIBC_FN XcbCookie sysv_xcb_change_gc(void *connection, XcbId id, uint32_t mask, const void *values)
{
    (void)connection;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_GCS; i++) {
        if (g_gcs[i].id == id && id != 0) {
            apply_values(&g_gcs[i].gc.values, mask, (const uint32_t *)values);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return done();
}

LIBC_FN XcbCookie sysv_xcb_change_gc_checked(void *connection, XcbId id, uint32_t mask, const void *values)
{
    return sysv_xcb_change_gc(connection, id, mask, values);
}

LIBC_FN XcbCookie sysv_xcb_free_gc(void *connection, XcbId id)
{
    (void)connection;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_GCS; i++) {
        if (g_gcs[i].id == id) {
            g_gcs[i] = (GcSlot){0};
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return done();
}

LIBC_FN XcbCookie sysv_xcb_set_clip_rectangles(void *connection, uint8_t ordering, XcbId gc, int16_t x, int16_t y,
                                               uint32_t count, const void *rectangles)
{
    (void)connection;
    (void)ordering;
    (void)gc;
    (void)x;
    (void)y;
    (void)count;
    (void)rectangles;
    return done();
}

/* ------------------------------------------------------------------ pixmaps */

LIBC_FN XcbCookie sysv_xcb_create_pixmap(void *connection, uint8_t depth, XcbId pixmap, XcbId drawable, uint16_t width,
                                         uint16_t height)
{
    (void)connection;
    (void)drawable;
    xl_lock();
    xl_drawable_add(pixmap, 0, depth, width, height, 0);
    xl_unlock();
    return done();
}

LIBC_FN XcbCookie sysv_xcb_create_pixmap_checked(void *connection, uint8_t depth, XcbId pixmap, XcbId drawable,
                                                 uint16_t width, uint16_t height)
{
    return sysv_xcb_create_pixmap(connection, depth, pixmap, drawable, width, height);
}

LIBC_FN XcbCookie sysv_xcb_free_pixmap(void *connection, XcbId pixmap)
{
    (void)connection;
    xl_lock();
    xl_drawable_remove(pixmap);
    xl_unlock();
    return done();
}

LIBC_FN XcbCookie sysv_xcb_free_pixmap_checked(void *connection, XcbId pixmap)
{
    return sysv_xcb_free_pixmap(connection, pixmap);
}

/* ------------------------------------------------------------------ copying and images */

LIBC_FN XcbCookie sysv_xcb_copy_area(void *connection, XcbId source, XcbId target, XcbId gc, int16_t sx, int16_t sy,
                                     int16_t dx, int16_t dy, uint16_t width, uint16_t height)
{
    (void)connection;
    XlGc context = copy_of_gc(gc);
    xl_trace("copy_area %x -> %x %dx%d from %d,%d to %d,%d", source, target, width, height, sx, sy, dx, dy);
    xl_copy_area(source, target, &context, sx, sy, width, height, dx, dy);
    return done();
}

LIBC_FN XcbCookie sysv_xcb_clear_area(void *connection, uint8_t exposures, XcbId window, int16_t x, int16_t y,
                                      uint16_t width, uint16_t height)
{
    (void)connection;
    (void)exposures;
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    uint32_t background = drawable != NULL ? drawable->background : 0;
    int32_t full_width = drawable != NULL ? drawable->width : 0;
    int32_t full_height = drawable != NULL ? drawable->height : 0;
    xl_unlock();
    XlGc context = {.values = {.function = XL_GX_COPY, .foreground = background}};
    xl_fill_rectangle(window, &context, x, y, width != 0 ? width : full_width - x, height != 0 ? height : full_height - y);
    return done();
}

/* ZPixmap with 32 bits per pixel (depth 24 or 32): the rows are copied, the unused top byte cleared. */
static void put_pixels(XlDrawable *drawable, const uint8_t *data, int32_t width, int32_t height, int32_t dx, int32_t dy)
{
    for (int32_t row = 0; row < height; row++) {
        int32_t y = dy + row;
        if (y < 0 || y >= drawable->height) {
            continue;
        }
        for (int32_t column = 0; column < width; column++) {
            int32_t x = dx + column;
            if (x >= 0 && x < drawable->width) {
                uint32_t pixel;
                memcpy(&pixel, data + ((size_t)row * (size_t)width + (size_t)column) * BYTES_PER_PIXEL, sizeof(pixel));
                drawable->pixels[(size_t)y * (size_t)drawable->width + (size_t)x] = pixel & PIXEL_MASK;
            }
        }
    }
}

/* A bitmap (one bit per pixel, rows padded to 32 bits): 1 is the GC's foreground, 0 its background. */
static void put_bitmap(XlDrawable *drawable, const uint8_t *data, int32_t width, int32_t height, int32_t dx, int32_t dy,
                       int32_t left_pad, const XlGc *gc)
{
    size_t stride = (((size_t)(width + left_pad) + 31) / 32) * ROW_ALIGN;
    for (int32_t row = 0; row < height; row++) {
        int32_t y = dy + row;
        for (int32_t column = 0; column < width && y >= 0 && y < drawable->height; column++) {
            int32_t x = dx + column;
            size_t bit = (size_t)(column + left_pad);
            int is_set = (data[row * stride + bit / 8] >> (bit % 8)) & 1;
            if (x >= 0 && x < drawable->width) {
                drawable->pixels[(size_t)y * (size_t)drawable->width + (size_t)x] =
                    xl_pixel(is_set ? gc->values.foreground : gc->values.background);
            }
        }
    }
}

LIBC_FN XcbCookie sysv_xcb_put_image(void *connection, uint8_t format, XcbId target, XcbId gc, uint16_t width,
                                     uint16_t height, int16_t dx, int16_t dy, uint8_t left_pad, uint8_t depth,
                                     uint32_t data_length, const uint8_t *data)
{
    (void)connection;
    (void)data_length;
    XlGc context = copy_of_gc(gc);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(target);
    xl_trace("put_image target=%x known=%d format=%d depth=%d %dx%d at %d,%d", target, drawable != NULL, format, depth, width,
             height, dx, dy);
    if (drawable != NULL && format == FORMAT_Z_PIXMAP && depth != 1) {
        put_pixels(drawable, data, width, height, dx, dy);
    } else if (drawable != NULL && (format == FORMAT_XY_BITMAP || depth == 1)) {
        put_bitmap(drawable, data, width, height, dx, dy, left_pad, &context);
    }
    xl_unlock();
    xl_invalidate(target, dx, dy, width, height);
    return done();
}

LIBC_FN XcbCookie sysv_xcb_get_image(void *connection, uint8_t format, XcbId source, int16_t x, int16_t y, uint16_t width,
                                     uint16_t height, uint32_t plane_mask)
{
    (void)connection;
    (void)format;
    (void)plane_mask;
    size_t bytes = (size_t)width * height * BYTES_PER_PIXEL;
    XcbImageReply *reply = (XcbImageReply *)xcb_reply_allocate(sizeof(*reply), bytes);
    if (reply == NULL) {
        return xcb_reply_keep(NULL);
    }
    uint32_t *pixels = (uint32_t *)(reply + 1);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(source);
    if (drawable != NULL) {
        reply->depth = (uint8_t)drawable->depth;
        reply->visual = drawable->is_window ? xcb_roots()->screens[0].root_visual : 0;
        for (int32_t row = 0; row < height; row++) {
            for (int32_t column = 0; column < width; column++) {
                int32_t sx = x + column;
                int32_t sy = y + row;
                int is_inside = sx >= 0 && sx < drawable->width && sy >= 0 && sy < drawable->height;
                pixels[(size_t)row * width + (size_t)column] =
                    is_inside ? drawable->pixels[(size_t)sy * (size_t)drawable->width + (size_t)sx] : 0;
            }
        }
    }
    xl_unlock();
    return xcb_reply_keep(reply);
}

LIBC_FN XcbCookie sysv_xcb_get_image_unchecked(void *connection, uint8_t format, XcbId source, int16_t x, int16_t y,
                                               uint16_t width, uint16_t height, uint32_t plane_mask)
{
    return sysv_xcb_get_image(connection, format, source, x, y, width, height, plane_mask);
}

LIBC_FN XcbImageReply *sysv_xcb_get_image_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbImageReply *)xcb_reply_take(cookie, error);
}

LIBC_FN uint8_t *sysv_xcb_get_image_data(const XcbImageReply *reply)
{
    return (uint8_t *)(reply + 1);
}

LIBC_FN int sysv_xcb_get_image_data_length(const XcbImageReply *reply)
{
    return (int)(reply->length * ROW_ALIGN);
}

/* ------------------------------------------------------------------ requests with nothing to draw */

LIBC_FN XcbCookie sysv_xcb_create_colormap(void *connection, uint8_t alloc, XcbId colormap, XcbId window, XcbId visual)
{
    (void)connection;
    (void)alloc;
    (void)colormap;
    (void)window;
    (void)visual;
    return done();
}

LIBC_FN XcbCookie sysv_xcb_free_colormap(void *connection, XcbId colormap)
{
    (void)connection;
    (void)colormap;
    return done();
}

LIBC_FN XcbCookie sysv_xcb_create_cursor(void *connection, XcbId cursor, XcbId source, XcbId mask, uint16_t fore_red,
                                         uint16_t fore_green, uint16_t fore_blue, uint16_t back_red,
                                         uint16_t back_green, uint16_t back_blue, uint16_t x, uint16_t y)
{
    (void)connection;
    (void)cursor;
    (void)source;
    (void)mask;
    (void)fore_red;
    (void)fore_green;
    (void)fore_blue;
    (void)back_red;
    (void)back_green;
    (void)back_blue;
    (void)x;
    (void)y;
    return done();
}

LIBC_FN XcbCookie sysv_xcb_create_glyph_cursor(void *connection, XcbId cursor, XcbId source_font, XcbId mask_font,
                                               uint16_t source_char, uint16_t mask_char, uint16_t fore_red,
                                               uint16_t fore_green, uint16_t fore_blue, uint16_t back_red,
                                               uint16_t back_green, uint16_t back_blue)
{
    (void)connection;
    (void)cursor;
    (void)source_font;
    (void)mask_font;
    (void)source_char;
    (void)mask_char;
    (void)fore_red;
    (void)fore_green;
    (void)fore_blue;
    (void)back_red;
    (void)back_green;
    (void)back_blue;
    return done();
}

LIBC_FN XcbCookie sysv_xcb_free_cursor(void *connection, XcbId cursor)
{
    (void)connection;
    (void)cursor;
    return done();
}

LIBC_FN XcbCookie sysv_xcb_open_font(void *connection, XcbId font, uint16_t length, const char *name)
{
    (void)connection;
    (void)font;
    (void)length;
    (void)name;
    return done();
}

LIBC_FN XcbCookie sysv_xcb_close_font(void *connection, XcbId font)
{
    (void)connection;
    (void)font;
    return done();
}
