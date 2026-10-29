/*
 * File: shim/xlib/xlib_window.c
 * Purpose: libX11 window functions: create, map, destroy, move and resize, attributes, geometry, titles and window-manager hints
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * A window is a Win32 window (made through the xcb layer, so xcb and Xlib windows are the same thing) plus
 * a drawable record that holds its pixels and the events the program selected. Window-manager hints have
 * no Windows counterpart worth honouring yet; the functions accept them and return success.
 */

#include <string.h>

#include "xlib_internal.h"

#define CW_BACK_PIXEL (1u << 1)
#define CW_EVENT_MASK (1u << 11)
#define CONFIGURE_X 0x0001u
#define CONFIGURE_Y 0x0002u
#define CONFIGURE_WIDTH 0x0004u
#define CONFIGURE_HEIGHT 0x0008u
#define INPUT_OUTPUT 1
#define NORTH_WEST_GRAVITY 1
#define IS_UNMAPPED 0
#define IS_VIEWABLE 2
#define MODE_REPLACE 0
#define FORMAT_32 32
#define FORMAT_8 8
#define MAX_PROTOCOLS 16

LIBC_FN void *sysv_malloc(size_t size);

static const XlSetAttributes *attributes_or_zero(const XlSetAttributes *attributes)
{
    static const XlSetAttributes none;
    return attributes != NULL ? attributes : &none;
}

LIBC_FN XlId sysv_XCreateWindow(XlDisplay *display, XlId parent, int x, int y, unsigned width, unsigned height,
                                unsigned border_width, int depth, unsigned window_class, XlVisual *visual,
                                uint64_t value_mask, const XlSetAttributes *attributes)
{
    const XlSetAttributes *values = attributes_or_zero(attributes);
    XlId id = xl_allocate_id(display);
    uint32_t background = (value_mask & CW_BACK_PIXEL) ? xl_pixel(values->background_pixel) : 0;
    xl_next_request(display);
    uint32_t safe_width = width > 0 ? width : 1;
    uint32_t safe_height = height > 0 ? height : 1;
    sysv_xcb_create_window(NULL, (uint8_t)(depth > 0 ? depth : XL_ROOT_DEPTH), (XcbId)id, (XcbId)parent, (int16_t)x,
                           (int16_t)y, (uint16_t)safe_width, (uint16_t)safe_height, (uint16_t)border_width,
                           (uint16_t)window_class, visual != NULL ? (XcbId)visual->visual_id : 0, 0, NULL);
    if (xcb_window_handle((XcbId)id) == NULL) {
        return 0;
    }
    xl_lock();
    XlDrawable *drawable = xl_drawable_add(id, 1, depth > 0 ? depth : XL_ROOT_DEPTH, (int32_t)safe_width,
                                           (int32_t)safe_height, background);
    if (drawable != NULL && (value_mask & CW_EVENT_MASK)) {
        drawable->event_mask = values->event_mask;
    }
    xl_unlock();
    return drawable != NULL ? id : 0;
}

LIBC_FN XlId sysv_XCreateSimpleWindow(XlDisplay *display, XlId parent, int x, int y, unsigned width, unsigned height,
                                      unsigned border_width, uint64_t border, uint64_t background)
{
    (void)border;
    XlSetAttributes attributes = {.background_pixel = background};
    return sysv_XCreateWindow(display, parent, x, y, width, height, border_width, XL_ROOT_DEPTH, INPUT_OUTPUT, NULL,
                              CW_BACK_PIXEL, &attributes);
}

LIBC_FN int sysv_XMapWindow(XlDisplay *display, XlId window)
{
    xl_next_request(display);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    int is_new = drawable != NULL && !drawable->is_mapped;
    if (drawable != NULL) {
        drawable->is_mapped = 1;
        drawable->needs_expose = is_new || drawable->needs_expose;
    }
    xl_unlock();
    if (is_new) {
        xl_events_post_structure(display, XL_MAP_NOTIFY, window);
    }
    sysv_xcb_map_window(NULL, (XcbId)window);
    return 1;
}

LIBC_FN int sysv_XMapRaised(XlDisplay *display, XlId window)
{
    return sysv_XMapWindow(display, window);
}

LIBC_FN int sysv_XUnmapWindow(XlDisplay *display, XlId window)
{
    xl_next_request(display);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    int was_mapped = drawable != NULL && drawable->is_mapped;
    if (drawable != NULL) {
        drawable->is_mapped = 0;
    }
    xl_unlock();
    sysv_xcb_unmap_window(NULL, (XcbId)window);
    if (was_mapped) {
        xl_events_post_structure(display, XL_UNMAP_NOTIFY, window);
    }
    return 1;
}

LIBC_FN int sysv_XDestroyWindow(XlDisplay *display, XlId window)
{
    xl_next_request(display);
    xl_events_post_structure(display, XL_DESTROY_NOTIFY, window);
    sysv_xcb_destroy_window(NULL, (XcbId)window);
    xl_lock();
    xl_drawable_remove(window);
    xl_unlock();
    return 1;
}

static SYSV int configure(XlDisplay *display, XlId window, uint16_t mask, const uint32_t *values)
{
    xl_next_request(display);
    sysv_xcb_configure_window(NULL, (XcbId)window, mask, values);
    return 1;
}

LIBC_FN int sysv_XMoveWindow(XlDisplay *display, XlId window, int x, int y)
{
    uint32_t values[] = {(uint32_t)x, (uint32_t)y};
    return configure(display, window, CONFIGURE_X | CONFIGURE_Y, values);
}

LIBC_FN int sysv_XResizeWindow(XlDisplay *display, XlId window, unsigned width, unsigned height)
{
    uint32_t values[] = {width, height};
    return configure(display, window, CONFIGURE_WIDTH | CONFIGURE_HEIGHT, values);
}

LIBC_FN int sysv_XMoveResizeWindow(XlDisplay *display, XlId window, int x, int y, unsigned width, unsigned height)
{
    uint32_t values[] = {(uint32_t)x, (uint32_t)y, width, height};
    return configure(display, window, CONFIGURE_X | CONFIGURE_Y | CONFIGURE_WIDTH | CONFIGURE_HEIGHT, values);
}

LIBC_FN int sysv_XSelectInput(XlDisplay *display, XlId window, int64_t mask)
{
    xl_next_request(display);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    if (drawable != NULL) {
        drawable->event_mask = mask;
    }
    xl_unlock();
    return 1;
}

LIBC_FN int sysv_XChangeWindowAttributes(XlDisplay *display, XlId window, uint64_t value_mask, const XlSetAttributes *attributes)
{
    xl_next_request(display);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    if (drawable != NULL && (value_mask & CW_EVENT_MASK)) {
        drawable->event_mask = attributes->event_mask;
    }
    if (drawable != NULL && (value_mask & CW_BACK_PIXEL)) {
        drawable->background = xl_pixel(attributes->background_pixel);
    }
    xl_unlock();
    return 1;
}

LIBC_FN int sysv_XSetWindowBackground(XlDisplay *display, XlId window, uint64_t pixel)
{
    XlSetAttributes attributes = {.background_pixel = pixel};
    return sysv_XChangeWindowAttributes(display, window, CW_BACK_PIXEL, &attributes);
}

LIBC_FN int sysv_XGetWindowAttributes(XlDisplay *display, XlId window, XlWindowAttributes *out)
{
    XlDisplayData *data = xl_display_data(display);
    RECT outer = {0};
    HWND handle = xcb_window_handle((XcbId)window);
    if (handle != NULL) {
        GetWindowRect(handle, &outer);
    }
    memset(out, 0, sizeof(*out));
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    int is_found = drawable != NULL;
    if (drawable != NULL) {
        out->width = drawable->width;
        out->height = drawable->height;
        out->depth = drawable->depth;
        out->map_state = drawable->is_mapped ? IS_VIEWABLE : IS_UNMAPPED;
        out->all_event_masks = drawable->event_mask;
        out->your_event_mask = drawable->event_mask;
    }
    xl_unlock();
    out->x = outer.left;
    out->y = outer.top;
    out->visual = &data->visual;
    out->root = data->screen.root;
    out->window_class = INPUT_OUTPUT;
    out->win_gravity = NORTH_WEST_GRAVITY;
    out->colormap = data->screen.colormap;
    out->map_installed = 1;
    out->screen = &data->screen;
    return is_found;
}

LIBC_FN int sysv_XGetGeometry(XlDisplay *display, XlId drawable_id, XlId *root, int *x, int *y, unsigned *width,
                              unsigned *height, unsigned *border_width, unsigned *depth)
{
    XlWindowAttributes attributes;
    int is_found = sysv_XGetWindowAttributes(display, drawable_id, &attributes);
    *root = attributes.root;
    *x = attributes.x;
    *y = attributes.y;
    *width = (unsigned)attributes.width;
    *height = (unsigned)attributes.height;
    *border_width = 0;
    *depth = (unsigned)attributes.depth;
    return is_found;
}

LIBC_FN int sysv_XStoreName(XlDisplay *display, XlId window, const char *name)
{
    xl_next_request(display);
    sysv_xcb_change_property(NULL, MODE_REPLACE, (XcbId)window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, FORMAT_8,
                             (uint32_t)strlen(name), name);
    return 1;
}

LIBC_FN int sysv_XChangeProperty(XlDisplay *display, XlId window, XlId property, XlId type, int format, int mode,
                                 const unsigned char *data, int count)
{
    xl_next_request(display);
    uint32_t packed[MAX_PROTOCOLS];
    const void *payload = data;
    if (format == FORMAT_32 && count > 0 && count <= MAX_PROTOCOLS) {
        /* Format-32 data arrives as an array of C longs (8 bytes each); the wire form is 32 bits. */
        const uint64_t *longs = (const uint64_t *)data;
        for (int i = 0; i < count; i++) {
            packed[i] = (uint32_t)longs[i];
        }
        payload = packed;
    }
    sysv_xcb_change_property(NULL, (uint8_t)mode, (XcbId)window, (XcbId)property, (XcbId)type, (uint8_t)format,
                             (uint32_t)count, payload);
    return 1;
}

/* Properties are not stored yet: every read answers "no such property". */
LIBC_FN int sysv_XGetWindowProperty(XlDisplay *display, XlId window, XlId property, int64_t offset, int64_t length,
                                    int should_delete, XlId required_type, XlId *actual_type, int *actual_format,
                                    uint64_t *item_count, uint64_t *bytes_after, unsigned char **data)
{
    (void)window;
    (void)property;
    (void)offset;
    (void)length;
    (void)should_delete;
    (void)required_type;
    xl_next_request(display);
    *actual_type = 0;
    *actual_format = 0;
    *item_count = 0;
    *bytes_after = 0;
    *data = NULL;
    return 0;
}

LIBC_FN int sysv_XSetWMProtocols(XlDisplay *display, XlId window, const XlId *protocols, int count)
{
    xl_next_request(display);
    XcbId delete_atom = xcb_atom_for("WM_DELETE_WINDOW", strlen("WM_DELETE_WINDOW"));
    for (int i = 0; i < count; i++) {
        if (protocols[i] == delete_atom) {
            xcb_window_set_delete_atom((XcbId)window, delete_atom);
        }
    }
    return 1;
}

LIBC_FN int sysv_XRaiseWindow(XlDisplay *display, XlId window)
{
    xl_next_request(display);
    HWND handle = xcb_window_handle((XcbId)window);
    if (handle != NULL) {
        SetWindowPos(handle, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return 1;
}

LIBC_FN int sysv_XSetInputFocus(XlDisplay *display, XlId window, int revert_to, uint64_t time)
{
    (void)revert_to;
    (void)time;
    xl_next_request(display);
    HWND handle = xcb_window_handle((XcbId)window);
    if (handle != NULL) {
        SetFocus(handle);
    }
    return 1;
}

/* Window-manager hints that Windows has no use for: accepted and dropped. */
LIBC_FN int sysv_XSetWMNormalHints(XlDisplay *display, XlId window, const void *hints)
{
    (void)display;
    (void)window;
    (void)hints;
    return 1;
}

LIBC_FN int sysv_XSetWMHints(XlDisplay *display, XlId window, const void *hints)
{
    (void)display;
    (void)window;
    (void)hints;
    return 1;
}

LIBC_FN int sysv_XSetClassHint(XlDisplay *display, XlId window, const void *hints)
{
    (void)display;
    (void)window;
    (void)hints;
    return 1;
}

LIBC_FN int sysv_XDefineCursor(XlDisplay *display, XlId window, XlId cursor)
{
    (void)display;
    (void)window;
    (void)cursor;
    return 1;
}

LIBC_FN int sysv_XUndefineCursor(XlDisplay *display, XlId window)
{
    (void)display;
    (void)window;
    return 1;
}
