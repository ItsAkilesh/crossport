/*
 * File: shim/xlib/xlib_resource.c
 * Purpose: The drawable table (windows and pixmaps with their pixel buffers) and the Win32 side of painting them
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Windows keep a buffer the size of their client area. WM_PAINT copies it to the window; a window whose
 * buffer was just created or resized also asks the program for an Expose, because its contents are blank.
 */

#include <string.h>

#include "xlib_internal.h"

#define BUCKET_COUNT 1024
#define MAX_PRESENTED_WINDOWS 64
#define BITS_PER_PIXEL 32

static XlDrawable *g_buckets[BUCKET_COUNT];
static SRWLOCK g_lock = SRWLOCK_INIT;

void xl_lock(void)
{
    AcquireSRWLockExclusive(&g_lock);
}

void xl_unlock(void)
{
    ReleaseSRWLockExclusive(&g_lock);
}

static size_t bucket_of(XlId id)
{
    return (size_t)((id ^ (id >> 10)) % BUCKET_COUNT);
}

XlDrawable *xl_drawable_find(XlId id)
{
    for (XlDrawable *drawable = g_buckets[bucket_of(id)]; drawable != NULL; drawable = drawable->next) {
        if (drawable->id == id) {
            return drawable;
        }
    }
    return NULL;
}

static uint32_t *allocate_pixels(int32_t width, int32_t height, uint32_t background)
{
    size_t count = (size_t)width * (size_t)height;
    uint32_t *pixels = (uint32_t *)HeapAlloc(GetProcessHeap(), 0, count * sizeof(uint32_t) + sizeof(uint32_t));
    for (size_t i = 0; pixels != NULL && i < count; i++) {
        pixels[i] = background;
    }
    return pixels;
}

XlDrawable *xl_drawable_add(XlId id, int is_window, int32_t depth, int32_t width, int32_t height, uint32_t background)
{
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    XlDrawable *drawable = (XlDrawable *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*drawable));
    uint32_t *pixels = allocate_pixels(width, height, background);
    if (drawable == NULL || pixels == NULL) {
        HeapFree(GetProcessHeap(), 0, drawable);
        HeapFree(GetProcessHeap(), 0, pixels);
        return NULL;
    }
    *drawable = (XlDrawable){.id = id, .is_window = is_window, .depth = depth, .width = width, .height = height,
                             .pixels = pixels, .background = background, .needs_expose = is_window,
                             .reported_width = width, .reported_height = height};
    size_t bucket = bucket_of(id);
    drawable->next = g_buckets[bucket];
    g_buckets[bucket] = drawable;
    return drawable;
}

void xl_drawable_remove(XlId id)
{
    for (XlDrawable **link = &g_buckets[bucket_of(id)]; *link != NULL; link = &(*link)->next) {
        if ((*link)->id == id) {
            XlDrawable *gone = *link;
            *link = gone->next;
            HeapFree(GetProcessHeap(), 0, gone->pixels);
            HeapFree(GetProcessHeap(), 0, gone);
            return;
        }
    }
}

void xl_drawable_resize(XlDrawable *drawable, int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0 || (width == drawable->width && height == drawable->height)) {
        return;
    }
    uint32_t *pixels = allocate_pixels(width, height, drawable->background);
    if (pixels == NULL) {
        return;
    }
    int32_t kept_width = width < drawable->width ? width : drawable->width;
    int32_t kept_height = height < drawable->height ? height : drawable->height;
    for (int32_t row = 0; row < kept_height; row++) {
        memcpy(pixels + (size_t)row * (size_t)width, drawable->pixels + (size_t)row * (size_t)drawable->width,
               (size_t)kept_width * sizeof(uint32_t));
    }
    HeapFree(GetProcessHeap(), 0, drawable->pixels);
    drawable->pixels = pixels;
    drawable->width = width;
    drawable->height = height;
    drawable->needs_expose = drawable->is_window;
}

void xl_size_window(HWND window, int width, int height)
{
    XlId id = xcb_window_id(window);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(id);
    if (drawable != NULL && drawable->is_window) {
        xl_drawable_resize(drawable, width, height);
    }
    xl_unlock();
}

static void blit(HDC context, const XlDrawable *drawable)
{
    BITMAPINFO info = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = drawable->width,
                                     .biHeight = -drawable->height, .biPlanes = 1, .biBitCount = BITS_PER_PIXEL,
                                     .biCompression = BI_RGB}};
    SetDIBitsToDevice(context, 0, 0, (DWORD)drawable->width, (DWORD)drawable->height, 0, 0, 0, (UINT)drawable->height,
                      drawable->pixels, &info, DIB_RGB_COLORS);
}

/* Returns 1 when the window belongs to Xlib (and so has been painted), 0 to let the caller paint it. */
int xl_paint_window(HWND window)
{
    XlId id = xcb_window_id(window);
    xl_lock();
    XlDrawable *known = xl_drawable_find(id);
    int is_known = known != NULL && known->is_window;
    xl_unlock();
    if (!is_known) {
        return 0;
    }
    PAINTSTRUCT paint;
    HDC context = BeginPaint(window, &paint);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(id);
    int needs_expose = drawable != NULL && drawable->needs_expose;
    if (drawable != NULL) {
        blit(context, drawable);
        drawable->needs_expose = 0;
    }
    xl_unlock();
    EndPaint(window, &paint);
    if (needs_expose) {
        RECT client;
        GetClientRect(window, &client);
        XcbExposeEvent expose = {.response_type = XCB_EVENT_EXPOSE, .window = (XcbId)id, .width = (uint16_t)client.right,
                                 .height = (uint16_t)client.bottom};
        XcbEvent event = {0};
        memcpy(&event, &expose, sizeof(expose));
        xcb_events_push(&event);
    }
    return 1;
}

void xl_invalidate(XlId drawable, int32_t x, int32_t y, int32_t width, int32_t height)
{
    HWND window = xcb_window_handle((XcbId)drawable);
    if (window != NULL) {
        RECT area = {x, y, x + width, y + height};
        InvalidateRect(window, &area, FALSE);
    }
}

void xl_present_all(void)
{
    HWND windows[MAX_PRESENTED_WINDOWS];
    size_t count = 0;
    xl_lock();
    for (size_t bucket = 0; bucket < BUCKET_COUNT; bucket++) {
        for (XlDrawable *drawable = g_buckets[bucket]; drawable != NULL && count < MAX_PRESENTED_WINDOWS; drawable = drawable->next) {
            HWND window = drawable->is_window ? xcb_window_handle((XcbId)drawable->id) : NULL;
            if (window != NULL) {
                windows[count++] = window;
            }
        }
    }
    xl_unlock();
    for (size_t i = 0; i < count; i++) {
        UpdateWindow(windows[i]);
    }
}
