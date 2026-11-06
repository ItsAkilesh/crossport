/*
 * File: shim/xlib/xlib_xkb.c
 * Purpose: The Xkb* functions of libX11 that toolkits call: extension query, keyboard state, group and modifier locks, event selection
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * The extension is the one the xcb layer serves (xcb_xkb.c), so Xlib reports it present with the same opcode and
 * event base. Locks and latches are accepted and change nothing: the keyboard is Windows', and its lock keys are
 * read from Windows. Selected events are accepted; Xlib clients do not receive XkbStateNotify, they read the state.
 * XkbGetMap answers NULL: the keymap is served through the xcb requests (as Qt reads it with libxkbcommon-x11),
 * not as Xlib's descriptor structures, so an Xlib client that wants the maps goes without them.
 */

#include <string.h>

#include "xcb_xkb.h"
#include "xlib_internal.h"

#define XKB_LIB_MAJOR 1
#define XKB_LIB_MINOR 0
#define BOOL_TRUE 1
#define BOOL_FALSE 0
#define STATUS_SUCCESS 0

/* XkbStateRec: the state of the keyboard, byte for byte as libX11 lays it out. */
typedef struct {
    uint8_t group;
    uint8_t locked_group;
    uint16_t base_group;
    uint16_t latched_group;
    uint8_t mods;
    uint8_t base_mods;
    uint8_t latched_mods;
    uint8_t locked_mods;
    uint8_t compat_state;
    uint8_t grab_mods;
    uint8_t compat_grab_mods;
    uint8_t lookup_mods;
    uint8_t compat_lookup_mods;
    uint16_t ptr_buttons;
} XlXkbState;

/* The extension's identity, for XQueryExtension; 0 when `name` is not XKEYBOARD. */
int xl_xkb_query_extension(const char *name, int *opcode, int *first_event, int *first_error)
{
    if (name == NULL || strcmp(name, XKB_EXTENSION_NAME) != 0) {
        return BOOL_FALSE;
    }
    *opcode = XKB_MAJOR_OPCODE;
    *first_event = XKB_FIRST_EVENT;
    *first_error = XKB_FIRST_ERROR;
    return BOOL_TRUE;
}

static int agree_on_version(int *major, int *minor)
{
    int is_compatible = (major == NULL || *major <= XKB_LIB_MAJOR) && (minor == NULL || *minor <= XKB_LIB_MINOR);
    if (major != NULL) {
        *major = XKB_SERVER_MAJOR;
    }
    if (minor != NULL) {
        *minor = XKB_SERVER_MINOR;
    }
    return is_compatible;
}

LIBC_FN int sysv_XkbQueryExtension(XlDisplay *display, int *opcode, int *event, int *error, int *major, int *minor)
{
    (void)display;
    if (opcode != NULL) {
        *opcode = XKB_MAJOR_OPCODE;
    }
    if (event != NULL) {
        *event = XKB_FIRST_EVENT;
    }
    if (error != NULL) {
        *error = XKB_FIRST_ERROR;
    }
    return agree_on_version(major, minor);
}

LIBC_FN int sysv_XkbUseExtension(XlDisplay *display, int *major, int *minor)
{
    (void)display;
    return agree_on_version(major, minor);
}

LIBC_FN int sysv_XkbLibraryVersion(int *major, int *minor)
{
    int is_compatible = (*major == XKB_LIB_MAJOR) && (*minor <= XKB_LIB_MINOR);
    *major = XKB_LIB_MAJOR;
    *minor = XKB_LIB_MINOR;
    return is_compatible;
}

LIBC_FN int sysv_XkbGetState(XlDisplay *display, unsigned device_spec, XlXkbState *state)
{
    (void)display;
    (void)device_spec;
    XcbXkbState now;
    xcb_xkb_state(&now);
    *state = (XlXkbState){.mods = now.mods, .base_mods = (uint8_t)(now.mods & ~now.locked_mods),
                          .locked_mods = now.locked_mods, .compat_state = now.mods, .lookup_mods = now.mods,
                          .compat_lookup_mods = now.mods, .ptr_buttons = now.buttons};
    return STATUS_SUCCESS;
}

LIBC_FN int sysv_XkbGetIndicatorState(XlDisplay *display, unsigned device_spec, unsigned *state)
{
    (void)display;
    (void)device_spec;
    *state = 0;
    return STATUS_SUCCESS;
}

LIBC_FN int sysv_XkbLockGroup(XlDisplay *display, unsigned device_spec, unsigned group)
{
    (void)display;
    (void)device_spec;
    (void)group;
    return BOOL_TRUE;
}

LIBC_FN int sysv_XkbLatchGroup(XlDisplay *display, unsigned device_spec, unsigned group)
{
    return sysv_XkbLockGroup(display, device_spec, group);
}

LIBC_FN int sysv_XkbLockModifiers(XlDisplay *display, unsigned device_spec, unsigned affect, unsigned values)
{
    (void)affect;
    (void)values;
    return sysv_XkbLockGroup(display, device_spec, 0);
}

LIBC_FN int sysv_XkbLatchModifiers(XlDisplay *display, unsigned device_spec, unsigned affect, unsigned latches)
{
    (void)affect;
    (void)latches;
    return sysv_XkbLockGroup(display, device_spec, 0);
}

LIBC_FN int sysv_XkbSelectEvents(XlDisplay *display, unsigned device_spec, unsigned changed, unsigned values)
{
    (void)changed;
    (void)values;
    return sysv_XkbLockGroup(display, device_spec, 0);
}

LIBC_FN int sysv_XkbSelectEventDetails(XlDisplay *display, unsigned device_spec, unsigned event_type, unsigned long changed,
                                       unsigned long values)
{
    (void)event_type;
    (void)changed;
    (void)values;
    return sysv_XkbLockGroup(display, device_spec, 0);
}

LIBC_FN int sysv_XkbBell(XlDisplay *display, XlId window, int percent, uint64_t name)
{
    (void)window;
    (void)percent;
    (void)name;
    return sysv_XkbLockGroup(display, 0, 0);
}

/* Maps are not handed out as Xlib structures: a client asking for one is told there is none and goes without. */
LIBC_FN void *sysv_XkbGetMap(XlDisplay *display, unsigned which, unsigned device_spec)
{
    (void)display;
    (void)which;
    (void)device_spec;
    return NULL;
}

LIBC_FN int sysv_XkbSetMap(XlDisplay *display, unsigned which, void *desc)
{
    (void)display;
    (void)which;
    (void)desc;
    return BOOL_FALSE;
}

LIBC_FN void sysv_XkbFreeKeyboard(void *desc, unsigned which, int is_free_all)
{
    (void)desc;
    (void)which;
    (void)is_free_all;
}

LIBC_FN void sysv_XkbFreeClientMap(void *desc, unsigned which, int is_free_all)
{
    (void)desc;
    (void)which;
    (void)is_free_all;
}
