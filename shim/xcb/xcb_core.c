/*
 * File: shim/xcb/xcb_core.c
 * Purpose: xcb connection setup, screen and id helpers, and atoms: the parts of libxcb that need no window
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "xcb_internal.h"

#define RESOURCE_ID_BASE 0x00200000u
#define RESOURCE_ID_MASK 0x001fffffu
#define ROOT_WINDOW_ID XCB_ROOT_WINDOW_ID
#define DEFAULT_COLORMAP_ID 0x00000020u
#define ROOT_VISUAL_ID 0x00000021u
#define ROOT_DEPTH 24
#define MILLIMETERS_PER_INCH_X10 254u
#define ASSUMED_DPI 96u
#define MIN_KEYCODE 8
#define MAX_KEYCODE 255
#define MAX_ATOMS 512
#define MAX_ATOM_NAME 128

static XcbRoots g_roots;
static INIT_ONCE g_init_once = INIT_ONCE_STATIC_INIT;
static volatile LONG g_sequence;
static volatile LONG g_next_id;
static SRWLOCK g_atom_lock = SRWLOCK_INIT;
static char g_atom_names[MAX_ATOMS][MAX_ATOM_NAME];
static uint32_t g_atom_count;

/* The atoms the X protocol predefines, numbered from 1 in this order. */
static const char *const PREDEFINED_ATOMS[] = {
    "PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP", "CARDINAL", "COLORMAP", "CURSOR", "CUT_BUFFER0",
    "CUT_BUFFER1", "CUT_BUFFER2", "CUT_BUFFER3", "CUT_BUFFER4", "CUT_BUFFER5", "CUT_BUFFER6", "CUT_BUFFER7",
    "DRAWABLE", "FONT", "INTEGER", "PIXMAP", "POINT", "RECTANGLE", "RESOURCE_MANAGER", "RGB_COLOR_MAP",
    "RGB_BEST_MAP", "RGB_BLUE_MAP", "RGB_DEFAULT_MAP", "RGB_GRAY_MAP", "RGB_GREEN_MAP", "RGB_RED_MAP", "STRING",
    "VISUALID", "WINDOW", "WM_COMMAND", "WM_HINTS", "WM_CLIENT_MACHINE", "WM_ICON_NAME", "WM_ICON_SIZE",
    "WM_NAME", "WM_NORMAL_HINTS", "WM_SIZE_HINTS", "WM_ZOOM_HINTS", "MIN_SPACE", "NORM_SPACE", "MAX_SPACE",
    "END_SPACE", "SUPERSCRIPT_X", "SUPERSCRIPT_Y", "SUBSCRIPT_X", "SUBSCRIPT_Y", "UNDERLINE_POSITION",
    "UNDERLINE_THICKNESS", "STRIKEOUT_ASCENT", "STRIKEOUT_DESCENT", "ITALIC_ANGLE", "X_HEIGHT", "QUAD_WIDTH",
    "WEIGHT", "POINT_SIZE", "RESOLUTION", "COPYRIGHT", "NOTICE", "FONT_NAME", "FAMILY_NAME", "FULL_NAME",
    "CAP_HEIGHT", "WM_CLASS", "WM_TRANSIENT_FOR",
};
#define PREDEFINED_ATOM_COUNT (sizeof(PREDEFINED_ATOMS) / sizeof(PREDEFINED_ATOMS[0]))

static uint16_t millimeters(int pixels)
{
    return (uint16_t)((uint32_t)pixels * MILLIMETERS_PER_INCH_X10 / (ASSUMED_DPI * 10u));
}

static BOOL CALLBACK build_roots(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    g_roots.setup = (XcbSetup){.status = 1, .protocol_major_version = 11, .release_number = 1,
                               .resource_id_base = RESOURCE_ID_BASE, .resource_id_mask = RESOURCE_ID_MASK,
                               .maximum_request_length = UINT16_MAX, .roots_len = 1, .min_keycode = MIN_KEYCODE,
                               .max_keycode = MAX_KEYCODE};
    g_roots.screens[0] = (XcbScreen){.root = ROOT_WINDOW_ID, .default_colormap = DEFAULT_COLORMAP_ID,
                                     .white_pixel = 0x00ffffffu, .width_in_pixels = (uint16_t)width,
                                     .height_in_pixels = (uint16_t)height,
                                     .width_in_millimeters = millimeters(width),
                                     .height_in_millimeters = millimeters(height), .min_installed_maps = 1,
                                     .max_installed_maps = 1, .root_visual = ROOT_VISUAL_ID,
                                     .root_depth = ROOT_DEPTH};
    g_atom_count = XCB_FIRST_DYNAMIC_ATOM;
    return TRUE;
}

XcbRoots *xcb_roots(void)
{
    InitOnceExecuteOnce(&g_init_once, build_roots, NULL, NULL);
    return &g_roots;
}

uint32_t xcb_next_sequence(void)
{
    return (uint32_t)InterlockedIncrement(&g_sequence);
}

LIBC_FN void *sysv_xcb_connect(const char *display, int *screen)
{
    (void)display;
    if (screen != NULL) {
        *screen = 0;
    }
    return xcb_roots();
}

LIBC_FN void sysv_xcb_disconnect(void *connection)
{
    (void)connection;
}

LIBC_FN int sysv_xcb_connection_has_error(void *connection)
{
    return connection == NULL;
}

LIBC_FN int sysv_xcb_flush(void *connection)
{
    (void)connection;
    return 1;
}

LIBC_FN const XcbSetup *sysv_xcb_get_setup(void *connection)
{
    (void)connection;
    return &xcb_roots()->setup;
}

LIBC_FN XcbScreenIterator sysv_xcb_setup_roots_iterator(const XcbSetup *setup)
{
    return (XcbScreenIterator){.data = (XcbScreen *)((const uint8_t *)setup + sizeof(XcbSetup)),
                               .rem = setup->roots_len, .index = 0};
}

LIBC_FN void sysv_xcb_screen_next(XcbScreenIterator *iterator)
{
    iterator->rem--;
    iterator->index++;
    iterator->data++;
}

LIBC_FN uint32_t sysv_xcb_generate_id(void *connection)
{
    (void)connection;
    return RESOURCE_ID_BASE | ((uint32_t)InterlockedIncrement(&g_next_id) & RESOURCE_ID_MASK);
}

/* Number of an atom, creating it on first use; 0 when the table is full. */
XcbId xcb_atom_for(const char *name, size_t length)
{
    for (size_t i = 0; i < PREDEFINED_ATOM_COUNT; i++) {
        if (strlen(PREDEFINED_ATOMS[i]) == length && strncmp(PREDEFINED_ATOMS[i], name, length) == 0) {
            return (XcbId)(i + 1);
        }
    }
    XcbId found = 0;
    AcquireSRWLockExclusive(&g_atom_lock);
    xcb_roots();
    for (uint32_t i = XCB_FIRST_DYNAMIC_ATOM; i < g_atom_count && found == 0; i++) {
        if (strlen(g_atom_names[i]) == length && strncmp(g_atom_names[i], name, length) == 0) {
            found = i;
        }
    }
    if (found == 0 && g_atom_count < MAX_ATOMS && length < MAX_ATOM_NAME) {
        memcpy(g_atom_names[g_atom_count], name, length);
        g_atom_names[g_atom_count][length] = '\0';
        found = g_atom_count++;
    }
    ReleaseSRWLockExclusive(&g_atom_lock);
    return found;
}

const char *xcb_atom_name(XcbId atom)
{
    if (atom >= 1 && atom <= PREDEFINED_ATOM_COUNT) {
        return PREDEFINED_ATOMS[atom - 1];
    }
    const char *name = NULL;
    AcquireSRWLockShared(&g_atom_lock);
    if (atom >= XCB_FIRST_DYNAMIC_ATOM && atom < g_atom_count) {
        name = g_atom_names[atom];
    }
    ReleaseSRWLockShared(&g_atom_lock);
    return name;
}

LIBC_FN XcbCookie sysv_xcb_intern_atom(void *connection, uint8_t only_if_exists, uint16_t name_length, const char *name)
{
    (void)connection;
    (void)only_if_exists;
    XcbId atom = xcb_atom_for(name, name_length);
    return (XcbCookie){.sequence = atom};
}

/* The cookie of an intern_atom request carries the atom itself: the answer is known when the request is made. */
LIBC_FN XcbInternAtomReply *sysv_xcb_intern_atom_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    if (error != NULL) {
        *error = NULL;
    }
    XcbInternAtomReply *reply = (XcbInternAtomReply *)sysv_malloc(sizeof(*reply));
    if (reply != NULL) {
        *reply = (XcbInternAtomReply){.response_type = 1, .sequence = (uint16_t)xcb_next_sequence(), .atom = cookie.sequence};
    }
    return reply;
}
