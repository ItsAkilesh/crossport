/*
 * File: shim/xcb/xcb_abi.h
 * Purpose: The xcb wire-level structures and constants translated programs read directly (setup, screen, events, replies)
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * xcb hands programs pointers into its own memory and the programs read the fields, so these layouts are
 * part of the ABI and must match libxcb's headers exactly.
 */

#ifndef CROSSPORT_XCB_ABI_H
#define CROSSPORT_XCB_ABI_H

#include "libc_abi.h"

typedef uint32_t XcbId;

/* Every request returns a cookie: the sequence number its reply will carry. */
typedef struct {
    uint32_t sequence;
} XcbCookie;

/* xcb_setup_t: 40 bytes, followed (in our layout) directly by the screens, with no vendor string or formats. */
typedef struct {
    uint8_t status;
    uint8_t pad0;
    uint16_t protocol_major_version;
    uint16_t protocol_minor_version;
    uint16_t length;
    uint32_t release_number;
    uint32_t resource_id_base;
    uint32_t resource_id_mask;
    uint32_t motion_buffer_size;
    uint16_t vendor_len;
    uint16_t maximum_request_length;
    uint8_t roots_len;
    uint8_t pixmap_formats_len;
    uint8_t image_byte_order;
    uint8_t bitmap_format_bit_order;
    uint8_t bitmap_format_scanline_unit;
    uint8_t bitmap_format_scanline_pad;
    uint8_t min_keycode;
    uint8_t max_keycode;
    uint8_t pad1[4];
} XcbSetup;

/* xcb_screen_t: 40 bytes. */
typedef struct {
    XcbId root;
    XcbId default_colormap;
    uint32_t white_pixel;
    uint32_t black_pixel;
    uint32_t current_input_masks;
    uint16_t width_in_pixels;
    uint16_t height_in_pixels;
    uint16_t width_in_millimeters;
    uint16_t height_in_millimeters;
    uint16_t min_installed_maps;
    uint16_t max_installed_maps;
    XcbId root_visual;
    uint8_t backing_stores;
    uint8_t save_unders;
    uint8_t root_depth;
    uint8_t allowed_depths_len;
} XcbScreen;

/* xcb_screen_iterator_t. */
typedef struct {
    XcbScreen *data;
    int rem;
    int index;
} XcbScreenIterator;

/* xcb_generic_event_t: 32 bytes; the sequence number is the high half of the first word. */
typedef struct {
    uint8_t response_type;
    uint8_t detail;
    uint16_t sequence;
    uint8_t body[28];
} XcbEvent;

/* Event numbers (xcb's XCB_KEY_PRESS and friends). */
enum {
    XCB_EVENT_KEY_PRESS = 2, XCB_EVENT_KEY_RELEASE = 3, XCB_EVENT_BUTTON_PRESS = 4, XCB_EVENT_BUTTON_RELEASE = 5,
    XCB_EVENT_MOTION_NOTIFY = 6, XCB_EVENT_EXPOSE = 12, XCB_EVENT_DESTROY_NOTIFY = 17, XCB_EVENT_MAP_NOTIFY = 19,
    XCB_EVENT_CONFIGURE_NOTIFY = 22, XCB_EVENT_CLIENT_MESSAGE = 33
};

/* The input events (key, button, motion) share one layout after the first four bytes. */
typedef struct {
    uint8_t response_type;
    uint8_t detail;
    uint16_t sequence;
    uint32_t time;
    XcbId root;
    XcbId event;
    XcbId child;
    int16_t root_x;
    int16_t root_y;
    int16_t event_x;
    int16_t event_y;
    uint16_t state;
    uint8_t same_screen;
    uint8_t pad;
} XcbInputEvent;

typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    XcbId window;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t count;
    uint8_t pad1[2];
} XcbExposeEvent;

typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    XcbId event;
    XcbId window;
    XcbId above_sibling;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t border_width;
    uint8_t override_redirect;
    uint8_t pad1;
} XcbConfigureEvent;

typedef struct {
    uint8_t response_type;
    uint8_t format;
    uint16_t sequence;
    XcbId window;
    XcbId type;
    uint32_t data32[5];
} XcbClientMessageEvent;

/* Replies are heap blocks the program frees with free(). */
typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    uint32_t length;
    XcbId atom;
} XcbInternAtomReply;

typedef struct {
    uint8_t response_type;
    uint8_t depth;
    uint16_t sequence;
    uint32_t length;
    XcbId root;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t border_width;
    uint8_t pad[2];
} XcbGeometryReply;

/* Atoms the X server predefines, and the first number given to atoms programs create. */
enum { XCB_ATOM_ATOM = 4, XCB_ATOM_CARDINAL = 6, XCB_ATOM_STRING = 31, XCB_ATOM_WM_NAME = 39, XCB_FIRST_DYNAMIC_ATOM = 69 };

/* The sizes libxcb's own headers give these structures; a mismatch would corrupt what programs read. */
_Static_assert(sizeof(XcbSetup) == 40, "xcb_setup_t is 40 bytes");
_Static_assert(sizeof(XcbScreen) == 40, "xcb_screen_t is 40 bytes");
_Static_assert(sizeof(XcbScreenIterator) == 16, "xcb_screen_iterator_t is 16 bytes");
_Static_assert(sizeof(XcbEvent) == 32, "xcb events are 32 bytes");
_Static_assert(sizeof(XcbInputEvent) == 32, "input events are 32 bytes");
_Static_assert(sizeof(XcbClientMessageEvent) == 32, "client messages are 32 bytes");
_Static_assert(sizeof(XcbExposeEvent) == 20, "expose events are 20 bytes");
_Static_assert(sizeof(XcbConfigureEvent) == 28, "configure events are 28 bytes");
_Static_assert(sizeof(XcbInternAtomReply) == 12, "intern atom replies are 12 bytes");
_Static_assert(sizeof(XcbGeometryReply) == 24, "geometry replies are 24 bytes");

#endif
