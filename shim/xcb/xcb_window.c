/*
 * File: shim/xcb/xcb_window.c
 * Purpose: xcb window requests (create, map, destroy, configure, properties, geometry) backed by Win32 windows
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * A window is created on the calling thread, and its messages arrive on that thread, so the program must
 * poll for events from the thread that created the window (the normal single-threaded X client).
 */

#include <string.h>

#include "xcb_internal.h"

#define MAX_WINDOWS 64
#define WINDOW_STYLE (WS_OVERLAPPEDWINDOW)
#define CONFIGURE_X 0x0001
#define CONFIGURE_Y 0x0002
#define CONFIGURE_WIDTH 0x0004
#define CONFIGURE_HEIGHT 0x0008
#define PROPERTY_FORMAT_32 32
#define MAX_TITLE_CHARS 255

typedef struct {
    XcbId id;
    HWND handle;
    uint32_t delete_atom;
} WindowSlot;

static WindowSlot g_windows[MAX_WINDOWS];
static SRWLOCK g_window_lock = SRWLOCK_INIT;

static WindowSlot *find_slot(XcbId id)
{
    for (size_t i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == id && id != 0) {
            return &g_windows[i];
        }
    }
    return NULL;
}

HWND xcb_window_handle(XcbId id)
{
    AcquireSRWLockShared(&g_window_lock);
    WindowSlot *slot = find_slot(id);
    HWND handle = slot != NULL ? slot->handle : NULL;
    ReleaseSRWLockShared(&g_window_lock);
    return handle;
}

XcbId xcb_window_id(HWND handle)
{
    XcbId id = 0;
    AcquireSRWLockShared(&g_window_lock);
    for (size_t i = 0; i < MAX_WINDOWS && id == 0; i++) {
        id = g_windows[i].handle == handle ? g_windows[i].id : 0;
    }
    ReleaseSRWLockShared(&g_window_lock);
    return id;
}

void xcb_window_forget(HWND handle)
{
    AcquireSRWLockExclusive(&g_window_lock);
    for (size_t i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].handle == handle) {
            memset(&g_windows[i], 0, sizeof(g_windows[i]));
        }
    }
    ReleaseSRWLockExclusive(&g_window_lock);
}

void xcb_window_set_delete_atom(XcbId id, uint32_t delete_atom)
{
    AcquireSRWLockExclusive(&g_window_lock);
    WindowSlot *slot = find_slot(id);
    if (slot != NULL) {
        slot->delete_atom = delete_atom;
    }
    ReleaseSRWLockExclusive(&g_window_lock);
}

uint32_t xcb_window_delete_atom(XcbId id)
{
    AcquireSRWLockShared(&g_window_lock);
    WindowSlot *slot = find_slot(id);
    uint32_t atom = slot != NULL ? slot->delete_atom : 0;
    ReleaseSRWLockShared(&g_window_lock);
    return atom;
}

static int store_window(XcbId id, HWND handle)
{
    int is_stored = 0;
    AcquireSRWLockExclusive(&g_window_lock);
    for (size_t i = 0; i < MAX_WINDOWS && !is_stored; i++) {
        if (g_windows[i].id == 0) {
            g_windows[i] = (WindowSlot){.id = id, .handle = handle};
            is_stored = 1;
        }
    }
    ReleaseSRWLockExclusive(&g_window_lock);
    return is_stored;
}

static XcbCookie next_cookie(void)
{
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_create_window(void *connection, uint8_t depth, XcbId window, XcbId parent, int16_t x, int16_t y,
                                         uint16_t width, uint16_t height, uint16_t border_width, uint16_t window_class,
                                         XcbId visual, uint32_t value_mask, const void *value_list)
{
    (void)connection;
    (void)depth;
    (void)parent;
    (void)border_width;
    (void)window_class;
    (void)visual;
    (void)value_mask;
    (void)value_list;
    xcb_events_register_class();
    RECT frame = {0, 0, width, height};
    AdjustWindowRect(&frame, WINDOW_STYLE, FALSE);
    int left = (x == 0 && y == 0) ? CW_USEDEFAULT : x;
    int top = (x == 0 && y == 0) ? CW_USEDEFAULT : y;
    HWND handle = CreateWindowExW(0, XCB_WINDOW_CLASS, L"", WINDOW_STYLE, left, top, frame.right - frame.left,
                                  frame.bottom - frame.top, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (handle != NULL && !store_window(window, handle)) {
        DestroyWindow(handle);
    }
    return next_cookie();
}

LIBC_FN XcbCookie sysv_xcb_map_window(void *connection, XcbId window)
{
    (void)connection;
    HWND handle = xcb_window_handle(window);
    if (handle != NULL) {
        ShowWindow(handle, SW_SHOW);
        UpdateWindow(handle);
    }
    return next_cookie();
}

LIBC_FN XcbCookie sysv_xcb_unmap_window(void *connection, XcbId window)
{
    (void)connection;
    HWND handle = xcb_window_handle(window);
    if (handle != NULL) {
        ShowWindow(handle, SW_HIDE);
    }
    return next_cookie();
}

LIBC_FN XcbCookie sysv_xcb_destroy_window(void *connection, XcbId window)
{
    (void)connection;
    HWND handle = xcb_window_handle(window);
    if (handle != NULL) {
        DestroyWindow(handle);
    }
    return next_cookie();
}

static void set_title(HWND handle, const char *text, uint32_t length)
{
    wchar_t title[MAX_TITLE_CHARS + 1];
    int count = MultiByteToWideChar(CP_UTF8, 0, text, (int)length, title, MAX_TITLE_CHARS);
    title[count > 0 ? count : 0] = L'\0';
    SetWindowTextW(handle, title);
}

/* Only the properties with a visible effect are acted on: the title and the WM_DELETE_WINDOW protocol. */
LIBC_FN XcbCookie sysv_xcb_change_property(void *connection, uint8_t mode, XcbId window, XcbId property, XcbId type,
                                           uint8_t format, uint32_t length, const void *data)
{
    (void)connection;
    (void)mode;
    HWND handle = xcb_window_handle(window);
    if (handle != NULL && property == XCB_ATOM_WM_NAME && format == 8) {
        set_title(handle, (const char *)data, length);
    } else if (handle != NULL && type == XCB_ATOM_ATOM && format == PROPERTY_FORMAT_32 && length > 0 &&
               property == xcb_atom_for("WM_PROTOCOLS", 12)) {
        xcb_window_set_delete_atom(window, ((const uint32_t *)data)[0]);
    }
    return next_cookie();
}

LIBC_FN XcbCookie sysv_xcb_configure_window(void *connection, XcbId window, uint16_t mask, const uint32_t *values)
{
    (void)connection;
    HWND handle = xcb_window_handle(window);
    RECT rect;
    if (handle != NULL && GetWindowRect(handle, &rect)) {
        int x = rect.left;
        int y = rect.top;
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        size_t next = 0;
        x = (mask & CONFIGURE_X) ? (int)values[next++] : x;
        y = (mask & CONFIGURE_Y) ? (int)values[next++] : y;
        width = (mask & CONFIGURE_WIDTH) ? (int)values[next++] : width;
        height = (mask & CONFIGURE_HEIGHT) ? (int)values[next++] : height;
        SetWindowPos(handle, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return next_cookie();
}

/* The cookie carries the drawable: the geometry is read when the reply is asked for. */
LIBC_FN XcbCookie sysv_xcb_get_geometry(void *connection, XcbId drawable)
{
    (void)connection;
    return (XcbCookie){.sequence = drawable};
}

LIBC_FN XcbGeometryReply *sysv_xcb_get_geometry_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    if (error != NULL) {
        *error = NULL;
    }
    RECT client;
    HWND handle = xcb_window_handle(cookie.sequence);
    XcbGeometryReply *reply = (XcbGeometryReply *)sysv_malloc(sizeof(*reply));
    if (reply == NULL) {
        return NULL;
    }
    *reply = (XcbGeometryReply){.response_type = 1, .depth = 24, .sequence = (uint16_t)xcb_next_sequence(),
                                .root = xcb_roots()->screens[0].root};
    if (handle != NULL && GetClientRect(handle, &client)) {
        reply->width = (uint16_t)client.right;
        reply->height = (uint16_t)client.bottom;
    }
    return reply;
}
