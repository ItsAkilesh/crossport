/*
 * File: shim/xcb/xcb_abi.h
 * Purpose: The xcb wire-level structures and constants translated programs read directly (setup, screen, events, replies)
 * Author: Akilesh
 * Date: 2026-11-05
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
    XCB_EVENT_MOTION_NOTIFY = 6, XCB_EVENT_ENTER_NOTIFY = 7, XCB_EVENT_LEAVE_NOTIFY = 8, XCB_EVENT_FOCUS_IN = 9,
    XCB_EVENT_FOCUS_OUT = 10, XCB_EVENT_EXPOSE = 12, XCB_EVENT_DESTROY_NOTIFY = 17, XCB_EVENT_UNMAP_NOTIFY = 18, XCB_EVENT_MAP_NOTIFY = 19,
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

/* FocusIn and FocusOut (12 bytes; padded to 32 in the queue). */
typedef struct {
    uint8_t response_type;
    uint8_t detail;
    uint16_t sequence;
    XcbId event;
    uint8_t mode;
    uint8_t pad[3];
} XcbFocusEvent;

/* EnterNotify and LeaveNotify. */
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
    uint8_t mode;
    uint8_t same_screen_focus;
} XcbCrossingEvent;

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
_Static_assert(sizeof(XcbFocusEvent) == 12, "focus events are 12 bytes");
_Static_assert(sizeof(XcbCrossingEvent) == 32, "crossing events are 32 bytes");
_Static_assert(sizeof(XcbExposeEvent) == 20, "expose events are 20 bytes");
_Static_assert(sizeof(XcbConfigureEvent) == 28, "configure events are 28 bytes");
_Static_assert(sizeof(XcbInternAtomReply) == 12, "intern atom replies are 12 bytes");
_Static_assert(sizeof(XcbGeometryReply) == 24, "geometry replies are 24 bytes");

/* xcb_depth_t, xcb_visualtype_t and xcb_format_t, with their iterators: Qt walks them to pick a visual. */
typedef struct {
    uint8_t depth;
    uint8_t pad0;
    uint16_t visuals_len;
    uint8_t pad1[4];
} XcbDepth;

typedef struct {
    XcbId visual_id;
    uint8_t visual_class;
    uint8_t bits_per_rgb_value;
    uint16_t colormap_entries;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint8_t pad[4];
} XcbVisualType;

typedef struct {
    uint8_t depth;
    uint8_t bits_per_pixel;
    uint8_t scanline_pad;
    uint8_t pad[5];
} XcbFormat;

typedef struct {
    XcbDepth *data;
    int rem;
    int index;
} XcbDepthIterator;

typedef struct {
    XcbVisualType *data;
    int rem;
    int index;
} XcbVisualTypeIterator;

typedef struct {
    XcbFormat *data;
    int rem;
    int index;
} XcbFormatIterator;

/* The header every reply starts with (xcb_generic_reply_t). */
typedef struct {
    uint8_t response_type;
    uint8_t detail;
    uint16_t sequence;
    uint32_t length;
} XcbReplyHeader;

typedef struct {
    uint8_t response_type;
    uint8_t backing_store;
    uint16_t sequence;
    uint32_t length;
    XcbId visual;
    uint16_t window_class;
    uint8_t bit_gravity;
    uint8_t win_gravity;
    uint32_t backing_planes;
    uint32_t backing_pixel;
    uint8_t save_under;
    uint8_t map_is_installed;
    uint8_t map_state;
    uint8_t override_redirect;
    XcbId colormap;
    uint32_t all_event_masks;
    uint32_t your_event_mask;
    uint16_t do_not_propagate_mask;
    uint8_t pad[2];
} XcbWindowAttributesReply;

typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    uint32_t length;
    XcbId root;
    XcbId parent;
    uint16_t children_len;
    uint8_t pad1[14];
} XcbQueryTreeReply;

typedef struct {
    uint8_t response_type;
    uint8_t same_screen;
    uint16_t sequence;
    uint32_t length;
    XcbId child;
    int16_t dst_x;
    int16_t dst_y;
} XcbTranslateCoordinatesReply;

typedef struct {
    uint8_t response_type;
    uint8_t same_screen;
    uint16_t sequence;
    uint32_t length;
    XcbId root;
    XcbId child;
    int16_t root_x;
    int16_t root_y;
    int16_t win_x;
    int16_t win_y;
    uint16_t mask;
    uint8_t pad[2];
} XcbQueryPointerReply;

typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    uint32_t length;
    uint16_t name_len;
    uint8_t pad1[22];
} XcbAtomNameReply;

typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    uint32_t length;
    XcbId owner;
} XcbSelectionOwnerReply;

typedef struct {
    uint8_t response_type;
    uint8_t revert_to;
    uint16_t sequence;
    uint32_t length;
    XcbId focus;
} XcbInputFocusReply;

typedef struct {
    uint8_t response_type;
    uint8_t status;
    uint16_t sequence;
    uint32_t length;
} XcbGrabReply;

typedef struct {
    uint8_t response_type;
    uint8_t format;
    uint16_t sequence;
    uint32_t length;
    XcbId type;
    uint32_t bytes_after;
    uint32_t value_len;
    uint8_t pad[12];
} XcbPropertyReply;

typedef struct {
    uint8_t response_type;
    uint8_t depth;
    uint16_t sequence;
    uint32_t length;
    XcbId visual;
    uint8_t pad[20];
} XcbImageReply;

typedef struct {
    uint8_t response_type;
    uint8_t keysyms_per_keycode;
    uint16_t sequence;
    uint32_t length;
    uint8_t pad[24];
} XcbKeyboardMappingReply;

typedef struct {
    uint8_t response_type;
    uint8_t keycodes_per_modifier;
    uint16_t sequence;
    uint32_t length;
    uint8_t pad[24];
} XcbModifierMappingReply;

/* xcb_query_extension_reply_t, which xcb_get_extension_data returns. */
typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    uint32_t length;
    uint8_t present;
    uint8_t major_opcode;
    uint8_t first_event;
    uint8_t first_error;
} XcbExtensionReply;

_Static_assert(sizeof(XcbDepth) == 8 && sizeof(XcbVisualType) == 24 && sizeof(XcbFormat) == 8, "depth, visual, format");
_Static_assert(sizeof(XcbReplyHeader) == 8, "replies start with 8 bytes");
_Static_assert(sizeof(XcbWindowAttributesReply) == 44, "xcb_get_window_attributes_reply_t is 44 bytes");
_Static_assert(sizeof(XcbQueryTreeReply) == 32, "xcb_query_tree_reply_t is 32 bytes");
_Static_assert(sizeof(XcbTranslateCoordinatesReply) == 16, "xcb_translate_coordinates_reply_t is 16 bytes");
_Static_assert(sizeof(XcbQueryPointerReply) == 28, "xcb_query_pointer_reply_t is 28 bytes");
_Static_assert(sizeof(XcbAtomNameReply) == 32, "xcb_get_atom_name_reply_t is 32 bytes");
_Static_assert(sizeof(XcbSelectionOwnerReply) == 12, "xcb_get_selection_owner_reply_t is 12 bytes");
_Static_assert(sizeof(XcbInputFocusReply) == 12, "xcb_get_input_focus_reply_t is 12 bytes");
_Static_assert(sizeof(XcbGrabReply) == 8, "grab replies are 8 bytes");
_Static_assert(sizeof(XcbPropertyReply) == 32, "xcb_get_property_reply_t is 32 bytes");
_Static_assert(sizeof(XcbImageReply) == 32, "xcb_get_image_reply_t is 32 bytes");
_Static_assert(sizeof(XcbKeyboardMappingReply) == 32, "xcb_get_keyboard_mapping_reply_t is 32 bytes");
_Static_assert(sizeof(XcbModifierMappingReply) == 32, "xcb_get_modifier_mapping_reply_t is 32 bytes");
_Static_assert(sizeof(XcbExtensionReply) == 12, "xcb_query_extension_reply_t is 12 bytes");

#endif
