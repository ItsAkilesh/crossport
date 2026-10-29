/*
 * File: shim/xlib/xlib_misc.c
 * Purpose: libX11 functions toolkits call at start-up: extension queries, visuals, colormaps, hints, cursors, pointer and selections
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Extensions are reported as absent, which every Xlib client must handle; clients then use the core
 * protocol. Window-manager hints and cursors are accepted and ignored where Windows has no equivalent.
 * Selections (the clipboard) answer "no owner" until clipboard support exists.
 */

#include <string.h>

#include "xlib_internal.h"

#define VISUAL_MASK_ID 0x1
#define VISUAL_MASK_SCREEN 0x2
#define VISUAL_MASK_DEPTH 0x4
#define VISUAL_MASK_CLASS 0x8
#define XL_TRUE_COLOR 4
#define GRAB_SUCCESS 0
#define POINTER_BUTTONS 5
#define SIZE_HINTS_BYTES 72
#define WM_HINTS_BYTES 56
#define CLASS_HINT_BYTES 16

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void *sysv_calloc(size_t count, size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN int sysv_XStoreName(XlDisplay *display, XlId window, const char *name);
LIBC_FN int sysv_XUnmapWindow(XlDisplay *display, XlId window);
LIBC_FN XlId sysv_XCreatePixmap(XlDisplay *display, XlId parent, unsigned width, unsigned height, unsigned depth);

typedef struct {
    XlVisual *visual;
    XlId visual_id;
    int32_t screen;
    int32_t depth;
    int32_t visual_class;
    uint64_t red_mask;
    uint64_t green_mask;
    uint64_t blue_mask;
    int32_t colormap_size;
    int32_t bits_per_rgb;
} XlVisualInfo;

typedef struct {
    unsigned char *value;
    XlId encoding;
    int32_t format;
    uint64_t item_count;
} XlTextProperty;

_Static_assert(sizeof(XlVisualInfo) == 64, "XVisualInfo is 64 bytes");
_Static_assert(sizeof(XlTextProperty) == 32, "XTextProperty is 32 bytes");

LIBC_FN int sysv_XQueryExtension(XlDisplay *display, const char *name, int *opcode, int *first_event, int *first_error)
{
    (void)display;
    (void)name;
    *opcode = 0;
    *first_event = 0;
    *first_error = 0;
    return 0;
}

LIBC_FN char **sysv_XListExtensions(XlDisplay *display, int *count)
{
    (void)display;
    *count = 0;
    return NULL;
}

LIBC_FN int sysv_XFreeExtensionList(char **list)
{
    sysv_free(list);
    return 1;
}

LIBC_FN int sysv_XkbQueryExtension(XlDisplay *display, int *opcode, int *event, int *error, int *major, int *minor)
{
    (void)display;
    (void)opcode;
    (void)event;
    (void)error;
    (void)major;
    (void)minor;
    return 0;
}

static void fill_visual_info(XlDisplayData *data, XlVisualInfo *info)
{
    *info = (XlVisualInfo){.visual = &data->visual, .visual_id = data->visual.visual_id, .screen = 0,
                           .depth = XL_ROOT_DEPTH, .visual_class = XL_TRUE_COLOR, .red_mask = data->visual.red_mask,
                           .green_mask = data->visual.green_mask, .blue_mask = data->visual.blue_mask,
                           .colormap_size = data->visual.map_entries, .bits_per_rgb = data->visual.bits_per_rgb};
}

LIBC_FN XlVisualInfo *sysv_XGetVisualInfo(XlDisplay *display, int64_t mask, const XlVisualInfo *wanted, int *count)
{
    XlDisplayData *data = xl_display_data(display);
    int is_match = !((mask & VISUAL_MASK_ID) && wanted->visual_id != data->visual.visual_id) &&
                   !((mask & VISUAL_MASK_SCREEN) && wanted->screen != 0) &&
                   !((mask & VISUAL_MASK_DEPTH) && wanted->depth != XL_ROOT_DEPTH) &&
                   !((mask & VISUAL_MASK_CLASS) && wanted->visual_class != XL_TRUE_COLOR);
    *count = 0;
    XlVisualInfo *info = is_match ? (XlVisualInfo *)sysv_malloc(sizeof(*info)) : NULL;
    if (info != NULL) {
        fill_visual_info(data, info);
        *count = 1;
    }
    return info;
}

LIBC_FN int sysv_XMatchVisualInfo(XlDisplay *display, int screen, int depth, int visual_class, XlVisualInfo *out)
{
    if (screen != 0 || depth != XL_ROOT_DEPTH || visual_class != XL_TRUE_COLOR) {
        return 0;
    }
    fill_visual_info(xl_display_data(display), out);
    return 1;
}

LIBC_FN XlId sysv_XVisualIDFromVisual(const XlVisual *visual)
{
    return visual->visual_id;
}

LIBC_FN XlId sysv_XCreateColormap(XlDisplay *display, XlId window, XlVisual *visual, int alloc)
{
    (void)window;
    (void)visual;
    (void)alloc;
    return xl_display_data(display)->screen.colormap;
}

LIBC_FN int sysv_XFreeColormap(XlDisplay *display, XlId colormap)
{
    (void)display;
    (void)colormap;
    return 1;
}

LIBC_FN void *sysv_XAllocSizeHints(void)
{
    return sysv_calloc(1, SIZE_HINTS_BYTES);
}

LIBC_FN void *sysv_XAllocWMHints(void)
{
    return sysv_calloc(1, WM_HINTS_BYTES);
}

LIBC_FN void *sysv_XAllocClassHint(void)
{
    return sysv_calloc(1, CLASS_HINT_BYTES);
}

LIBC_FN void *sysv_XGetWMHints(XlDisplay *display, XlId window)
{
    (void)display;
    (void)window;
    return NULL;
}

LIBC_FN int sysv_XGetClassHint(XlDisplay *display, XlId window, void *hint)
{
    (void)display;
    (void)window;
    (void)hint;
    return 0;
}

LIBC_FN int sysv_XGetWMNormalHints(XlDisplay *display, XlId window, void *hints, int64_t *supplied)
{
    (void)display;
    (void)window;
    (void)hints;
    *supplied = 0;
    return 0;
}

LIBC_FN void sysv_XSetWMName(XlDisplay *display, XlId window, const XlTextProperty *property)
{
    if (property != NULL && property->value != NULL) {
        sysv_XStoreName(display, window, (const char *)property->value);
    }
}

LIBC_FN void sysv_XSetWMProperties(XlDisplay *display, XlId window, const XlTextProperty *name, const XlTextProperty *icon,
                                   char **argv, int argc, void *normal_hints, void *wm_hints, void *class_hint)
{
    (void)icon;
    (void)argv;
    (void)argc;
    (void)normal_hints;
    (void)wm_hints;
    (void)class_hint;
    sysv_XSetWMName(display, window, name);
}

LIBC_FN void sysv_Xutf8SetWMProperties(XlDisplay *display, XlId window, const char *name, const char *icon, char **argv,
                                       int argc, void *normal_hints, void *wm_hints, void *class_hint)
{
    (void)icon;
    (void)argv;
    (void)argc;
    (void)normal_hints;
    (void)wm_hints;
    (void)class_hint;
    if (name != NULL) {
        sysv_XStoreName(display, window, name);
    }
}

LIBC_FN int sysv_XSetTransientForHint(XlDisplay *display, XlId window, XlId parent)
{
    (void)display;
    (void)window;
    (void)parent;
    return 1;
}

/* Unmapping is what withdrawing does here: it must also report UnmapNotify, which clients such as SDL2 wait for. */
LIBC_FN int sysv_XWithdrawWindow(XlDisplay *display, XlId window, int screen)
{
    (void)screen;
    return sysv_XUnmapWindow(display, window);
}

LIBC_FN XlId sysv_XCreateFontCursor(XlDisplay *display, unsigned shape)
{
    (void)shape;
    return xl_allocate_id(display);
}

LIBC_FN XlId sysv_XCreatePixmapCursor(XlDisplay *display, XlId source, XlId mask, const void *foreground,
                                      const void *background, unsigned x, unsigned y)
{
    (void)source;
    (void)mask;
    (void)foreground;
    (void)background;
    (void)x;
    (void)y;
    return xl_allocate_id(display);
}

LIBC_FN int sysv_XFreeCursor(XlDisplay *display, XlId cursor)
{
    (void)display;
    (void)cursor;
    return 1;
}

/* A one-bit pixmap from packed rows (least significant bit first, rows padded to whole bytes). */
LIBC_FN XlId sysv_XCreateBitmapFromData(XlDisplay *display, XlId drawable, const char *data, unsigned width, unsigned height)
{
    XlId pixmap = sysv_XCreatePixmap(display, drawable, width, height, 1);
    size_t stride = (width + 7) / 8;
    xl_lock();
    XlDrawable *record = pixmap != 0 ? xl_drawable_find(pixmap) : NULL;
    for (unsigned y = 0; record != NULL && y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            int is_set = ((const uint8_t *)data)[y * stride + x / 8] >> (x % 8) & 1;
            record->pixels[(size_t)y * width + x] = (uint32_t)is_set;
        }
    }
    xl_unlock();
    return pixmap;
}

LIBC_FN int sysv_XGetInputFocus(XlDisplay *display, XlId *focus, int *revert_to)
{
    (void)display;
    HWND handle = GetFocus();
    *focus = handle != NULL ? xcb_window_id(handle) : 0;
    *revert_to = 1;
    return 1;
}

LIBC_FN int sysv_XQueryPointer(XlDisplay *display, XlId window, XlId *root, XlId *child, int *root_x, int *root_y,
                               int *window_x, int *window_y, unsigned *mask)
{
    POINT screen_point = {0, 0};
    GetCursorPos(&screen_point);
    POINT local = screen_point;
    HWND handle = xcb_window_handle((XcbId)window);
    if (handle != NULL) {
        ScreenToClient(handle, &local);
    }
    *root = display->screens[0].root;
    *child = 0;
    *root_x = screen_point.x;
    *root_y = screen_point.y;
    *window_x = local.x;
    *window_y = local.y;
    *mask = xcb_input_state();
    return 1;
}

LIBC_FN int sysv_XTranslateCoordinates(XlDisplay *display, XlId source, XlId target, int x, int y, int *out_x, int *out_y,
                                       XlId *child)
{
    POINT point = {x, y};
    HWND from = source == display->screens[0].root ? NULL : xcb_window_handle((XcbId)source);
    HWND to = target == display->screens[0].root ? NULL : xcb_window_handle((XcbId)target);
    if (from != NULL) {
        ClientToScreen(from, &point);
    }
    if (to != NULL) {
        ScreenToClient(to, &point);
    }
    *out_x = point.x;
    *out_y = point.y;
    *child = 0;
    return 1;
}

LIBC_FN int sysv_XWarpPointer(XlDisplay *display, XlId source, XlId target, int sx, int sy, unsigned sw, unsigned sh, int dx,
                              int dy)
{
    (void)display;
    (void)source;
    (void)sx;
    (void)sy;
    (void)sw;
    (void)sh;
    POINT point = {dx, dy};
    HWND to = target != 0 ? xcb_window_handle((XcbId)target) : NULL;
    if (to != NULL) {
        ClientToScreen(to, &point);
        SetCursorPos(point.x, point.y);
    } else if (target == 0) {
        POINT current;
        GetCursorPos(&current);
        SetCursorPos(current.x + dx, current.y + dy);
    }
    return 1;
}

LIBC_FN int sysv_XGrabPointer(XlDisplay *display, XlId window, int owner_events, unsigned mask, int pointer_mode,
                              int keyboard_mode, XlId confine_to, XlId cursor, uint64_t time)
{
    (void)display;
    (void)owner_events;
    (void)mask;
    (void)pointer_mode;
    (void)keyboard_mode;
    (void)confine_to;
    (void)cursor;
    (void)time;
    HWND handle = xcb_window_handle((XcbId)window);
    if (handle != NULL) {
        SetCapture(handle);
    }
    return GRAB_SUCCESS;
}

LIBC_FN int sysv_XUngrabPointer(XlDisplay *display, uint64_t time)
{
    (void)display;
    (void)time;
    ReleaseCapture();
    return 1;
}

LIBC_FN int sysv_XGrabKeyboard(XlDisplay *display, XlId window, int owner_events, int pointer_mode, int keyboard_mode,
                               uint64_t time)
{
    (void)display;
    (void)window;
    (void)owner_events;
    (void)pointer_mode;
    (void)keyboard_mode;
    (void)time;
    return GRAB_SUCCESS;
}

LIBC_FN int sysv_XUngrabKeyboard(XlDisplay *display, uint64_t time)
{
    (void)display;
    (void)time;
    return 1;
}

LIBC_FN XlId sysv_XGetSelectionOwner(XlDisplay *display, XlId selection)
{
    (void)display;
    (void)selection;
    return 0;
}

LIBC_FN int sysv_XSetSelectionOwner(XlDisplay *display, XlId selection, XlId owner, uint64_t time)
{
    (void)display;
    (void)selection;
    (void)owner;
    (void)time;
    return 1;
}

LIBC_FN int sysv_XConvertSelection(XlDisplay *display, XlId selection, XlId target, XlId property, XlId requestor, uint64_t time)
{
    (void)display;
    (void)selection;
    (void)target;
    (void)property;
    (void)requestor;
    (void)time;
    return 1;
}

LIBC_FN int sysv_XFilterEvent(XlEvent *event, XlId window)
{
    (void)event;
    (void)window;
    return 0;
}

LIBC_FN int sysv_XGetPointerMapping(XlDisplay *display, uint8_t *map, int count)
{
    (void)display;
    for (int i = 0; i < count && i < POINTER_BUTTONS; i++) {
        map[i] = (uint8_t)(i + 1);
    }
    return count < POINTER_BUTTONS ? count : POINTER_BUTTONS;
}

LIBC_FN int sysv_XGetErrorText(XlDisplay *display, int code, char *buffer, int size)
{
    (void)display;
    (void)code;
    if (size > 0) {
        strncpy(buffer, "unknown error", (size_t)size - 1);
        buffer[size - 1] = '\0';
    }
    return 1;
}

LIBC_FN char *sysv_XDisplayString(XlDisplay *display)
{
    return display->display_name;
}

LIBC_FN int sysv_XGrabKey(XlDisplay *display, int keycode, unsigned modifiers, XlId window, int owner_events, int pointer_mode,
                          int keyboard_mode)
{
    (void)display;
    (void)keycode;
    (void)modifiers;
    (void)window;
    (void)owner_events;
    (void)pointer_mode;
    (void)keyboard_mode;
    return 1;
}

LIBC_FN int sysv_XUngrabKey(XlDisplay *display, int keycode, unsigned modifiers, XlId window)
{
    (void)display;
    (void)keycode;
    (void)modifiers;
    (void)window;
    return 1;
}
