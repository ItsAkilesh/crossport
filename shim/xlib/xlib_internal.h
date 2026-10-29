/*
 * File: shim/xlib/xlib_internal.h
 * Purpose: State and helpers shared by the Xlib shim's source files: drawables, graphics contexts, the display and its event queue
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Every window and pixmap is a drawable: a record with a 32-bit pixel buffer. Drawing writes into the buffer
 * and marks the window's Win32 window dirty; the window's WM_PAINT copies the buffer to the screen. The
 * records live in one table guarded by one lock. Nothing may call a Win32 function that can dispatch a
 * window message (ShowWindow, SetWindowPos, DestroyWindow, UpdateWindow) while holding the lock, because
 * the window procedure takes it too.
 */

#ifndef CROSSPORT_XLIB_INTERNAL_H
#define CROSSPORT_XLIB_INTERNAL_H

#include "xlib_abi.h"
#include "xcb_internal.h"

#define XL_ROOT_DEPTH 24
#define XL_PIXEL_MASK 0x00ffffffu
#define XL_ALL_PLANES UINT64_MAX

/* Raster operations (the GX constants). */
enum {
    XL_GX_CLEAR = 0, XL_GX_AND = 1, XL_GX_AND_REVERSE = 2, XL_GX_COPY = 3, XL_GX_AND_INVERTED = 4, XL_GX_NOOP = 5,
    XL_GX_XOR = 6, XL_GX_OR = 7, XL_GX_NOR = 8, XL_GX_EQUIV = 9, XL_GX_INVERT = 10, XL_GX_OR_REVERSE = 11,
    XL_GX_COPY_INVERTED = 12, XL_GX_OR_INVERTED = 13, XL_GX_NAND = 14, XL_GX_SET = 15
};

typedef struct XlDrawable {
    XlId id;
    int is_window;
    int32_t depth;
    int32_t width;
    int32_t height;
    uint32_t *pixels;
    int64_t event_mask;
    uint32_t background;
    int is_mapped;
    int needs_expose;
    int32_t reported_width;
    int32_t reported_height;
    struct XlDrawable *next;
} XlDrawable;

typedef struct {
    XlGcValues values;
    XlId id;
} XlGc;

/* The drawable table. Records stay valid until xl_drawable_remove; hold the lock while using one. */
void xl_lock(void);
void xl_unlock(void);
XlDrawable *xl_drawable_find(XlId id);
XlDrawable *xl_drawable_add(XlId id, int is_window, int32_t depth, int32_t width, int32_t height, uint32_t background);
void xl_drawable_remove(XlId id);
/* Gives a window a buffer of the new size, keeping the old pixels at the top left, and asks for an Expose. */
void xl_drawable_resize(XlDrawable *drawable, int32_t width, int32_t height);

/* Called by the window procedure (on the thread that owns the window) for WM_PAINT and WM_SIZE. */
int xl_paint_window(HWND window);
void xl_size_window(HWND window, int width, int height);

/* Draws into a drawable; each takes the lock itself. Coordinates are clipped to the drawable. */
void xl_fill_rectangle(XlId drawable, const XlGc *gc, int32_t x, int32_t y, int32_t width, int32_t height);
void xl_draw_line(XlId drawable, const XlGc *gc, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void xl_draw_point(XlId drawable, const XlGc *gc, int32_t x, int32_t y);
void xl_copy_area(XlId source, XlId target, const XlGc *gc, int32_t sx, int32_t sy, int32_t width, int32_t height,
                  int32_t dx, int32_t dy);
/* Marks a region of a window as needing to reach the screen. */
void xl_invalidate(XlId drawable, int32_t x, int32_t y, int32_t width, int32_t height);
/* Copies the pixels of a window that changed since the last call to the screen now. */
void xl_present_all(void);
/* Combines a source pixel with the one already there, as the raster operation says. */
uint32_t xl_combine(uint32_t source, uint32_t destination, int32_t function);

/* The display: one per XOpenDisplay, with its own queue of converted events. */
typedef struct {
    XlDisplay pub;
    XlScreen screen;
    XlDepth depth;
    XlVisual visual;
    XlScreenFormat formats[3];
    XlGc default_gc;
    XlEvent *queue;
    size_t queue_head;
    size_t queue_count;
    size_t queue_capacity;
    uint32_t next_resource;
} XlDisplayData;

XlDisplayData *xl_display_data(XlDisplay *display);
uint64_t xl_next_request(XlDisplay *display);
SYSV XlId xl_allocate_id(XlDisplay *display);

/* Event queue: converts what the Win32 side produced and keeps what the program has not read yet. */
SYSV void xl_events_fill(XlDisplay *display, int should_wait);
SYSV int xl_events_push_front(XlDisplay *display, const XlEvent *event);
SYSV int xl_events_push_back(XlDisplay *display, const XlEvent *event);
/* Removes the first queued event for which `matches` is true; returns 0 when there is none. */
typedef int (SYSV *XlEventMatch)(const XlEvent *event, void *context);
SYSV int xl_events_take(XlDisplay *display, XlEventMatch matches, void *context, XlEvent *out, int should_remove);
/* Queues an event of the Xlib kind for a window the program selected (the mask decides). */
SYSV void xl_events_post_structure(XlDisplay *display, int type, XlId window);

/* The mask a core event needs a window to have selected; 0 for events delivered regardless. */
int64_t xl_event_mask_for(int type, uint32_t state);

/* Wraps the program's `long` into the fixed-size types: pixel values are 32 bits wide in the buffers. */
static inline uint32_t xl_pixel(uint64_t value)
{
    return (uint32_t)value & XL_PIXEL_MASK;
}

#endif
