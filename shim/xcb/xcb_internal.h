/*
 * File: shim/xcb/xcb_internal.h
 * Purpose: State and helpers shared by the xcb shim's source files: the connection, the window table and the event queue
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef CROSSPORT_XCB_INTERNAL_H
#define CROSSPORT_XCB_INTERNAL_H

#include "xcb_abi.h"

/* The shim's own allocator, so memory handed to programs can be released with their free(). */
LIBC_FN void *sysv_malloc(size_t size);

/* The xcb requests the layer above (Xlib) forwards to. */
LIBC_FN XcbCookie sysv_xcb_create_window(void *connection, uint8_t depth, XcbId window, XcbId parent, int16_t x, int16_t y,
                                         uint16_t width, uint16_t height, uint16_t border_width, uint16_t window_class,
                                         XcbId visual, uint32_t value_mask, const void *value_list);
LIBC_FN XcbCookie sysv_xcb_map_window(void *connection, XcbId window);
LIBC_FN XcbCookie sysv_xcb_unmap_window(void *connection, XcbId window);
LIBC_FN XcbCookie sysv_xcb_destroy_window(void *connection, XcbId window);
LIBC_FN XcbCookie sysv_xcb_configure_window(void *connection, XcbId window, uint16_t mask, const uint32_t *values);
LIBC_FN XcbCookie sysv_xcb_change_property(void *connection, uint8_t mode, XcbId window, XcbId property, XcbId type,
                                           uint8_t format, uint32_t length, const void *data);

#define XCB_WINDOW_CLASS L"CrossportX11"
/* The root window's id: windows created under it are top-level. */
#define XCB_ROOT_WINDOW_ID 0x00000100u

/* A connection hands out its setup block and screen as one piece of memory, like the X server's reply. */
typedef struct {
    XcbSetup setup;
    XcbScreen screens[1];
} XcbRoots;

/* The one connection of this process (a translated program talks to a single, local X server). */
XcbRoots *xcb_roots(void);
uint32_t xcb_next_sequence(void);

/* Number of an atom, created on first use; 0 when the table is full. */
XcbId xcb_atom_for(const char *name, size_t length);

/* Window table: maps the ids programs generate to the Win32 windows behind them. */
HWND xcb_window_handle(XcbId id);
XcbId xcb_window_id(HWND handle);
void xcb_window_forget(HWND handle);
/* The atom the program registered as its WM_DELETE_WINDOW protocol on a window, or 0 when it did not. */
void xcb_window_set_delete_atom(XcbId id, uint32_t delete_atom);
uint32_t xcb_window_delete_atom(XcbId id);

/* Event queue, filled by the window procedure and drained by xcb_poll_for_event / xcb_wait_for_event. */
void xcb_events_push(const XcbEvent *event);
void xcb_events_register_class(void);
/* Moves the oldest event into `out`; when none is waiting, `should_wait` blocks until one arrives. Returns 0 for none. */
int xcb_events_take(XcbEvent *out, int should_wait);
/* A layer above xcb (Xlib) that keeps pixel buffers for windows paints them and follows their size. */
typedef int (*XcbPaintHook)(HWND window);
typedef void (*XcbSizeHook)(HWND window, int width, int height);
void xcb_events_set_hooks(XcbPaintHook paint, XcbSizeHook size);
/* Keyboard map: the X keycode for a Windows scan code, and the keysym of a keycode (column 0 plain, 1 shifted). */
uint8_t xcb_keycode_from_scancode(uint8_t scancode, int is_extended);
uint32_t xcb_keysym_for(uint8_t keycode, int column);
/* Name of an atom, or NULL when the number is unknown. */
const char *xcb_atom_name(XcbId atom);

#endif
