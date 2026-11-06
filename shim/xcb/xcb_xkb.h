/*
 * File: shim/xcb/xcb_xkb.h
 * Purpose: The XKEYBOARD extension of the in-process X server: identity, wire-level request entry and the keymap tables
 * Author: Akilesh
 * Date: 2026-11-06
 */

#ifndef CROSSPORT_XCB_XKB_H
#define CROSSPORT_XCB_XKB_H

#include "xcb_internal.h"

#define XKB_EXTENSION_NAME "XKEYBOARD"
#define XKB_MAJOR_OPCODE 135
#define XKB_FIRST_EVENT 85
#define XKB_FIRST_ERROR 137
#define XKB_SERVER_MAJOR 1
#define XKB_SERVER_MINOR 0
#define XKB_DEVICE_ID 3
#define XKB_FIRST_KEYCODE 8
#define XKB_LAST_KEYCODE 255
#define XKB_REPLY_FIXED 32
#define XKB_MAX_REQUEST 128

/* xcb_protocol_request_t and the iovec libxcb's generic send path takes; extension libraries (libxcb-xkb) call it. */
typedef struct {
    size_t count;
    const void *extension;
    uint8_t opcode;
    uint8_t is_void;
} XcbProtocolRequest;

typedef struct {
    const uint8_t *base;
    size_t length;
} XcbVector;

/* The keyboard state as XKB reports it: the effective modifiers, the locked ones and the pointer buttons. */
typedef struct {
    uint8_t mods;
    uint8_t locked_mods;
    uint16_t buttons;
} XcbXkbState;

/* Whether `extension` (an xcb_extension_t) is XKEYBOARD, and the extension's answer to xcb_get_extension_data. */
int xcb_xkb_is_extension(const void *extension);
const XcbExtensionReply *xcb_xkb_extension_data(void);
/* Carries out one request of the extension and returns its sequence number; 0 when it is not an XKB request. */
uint32_t xcb_xkb_request(const XcbProtocolRequest *request, const XcbVector *vector);
void xcb_xkb_state(XcbXkbState *out);
/* Called when the modifier state may have changed: queues an XkbStateNotify for a client that asked for them. */
void xcb_xkb_note_modifiers(uint16_t state);

/* Replies the keymap requests build (xcb_xkb_map.c, xcb_xkb_names.c); each is the program's to free. */
void *xcb_xkb_get_map(const uint8_t *request, size_t length);
void *xcb_xkb_get_names(const uint8_t *request, size_t length);
void *xcb_xkb_get_compat_map(void);
void *xcb_xkb_get_indicator_map(void);

/* The keymap tables (xcb_xkb_keys.c): the key types, the modifier map and the four-letter key names. */
#define XKB_TYPE_COUNT 4
#define XKB_MAX_MAP_ENTRIES 2
#define XKB_MAX_LEVELS 2
typedef struct {
    uint8_t mods;
    uint8_t levels;
    uint8_t entry_count;
    uint8_t entry_mods[XKB_MAX_MAP_ENTRIES];
    uint8_t entry_level[XKB_MAX_MAP_ENTRIES];
    const char *name;
    const char *level_names[XKB_MAX_LEVELS];
} XkbTypeDef;
extern const XkbTypeDef XKB_TYPES[XKB_TYPE_COUNT];

/* The type index and the number of levels (0 for a key without symbols) of a keycode. */
uint8_t xcb_xkb_key_type(uint8_t keycode, uint8_t *levels);
/* The real modifier mask a keycode sets (0 for none). */
uint8_t xcb_xkb_key_modifiers(uint8_t keycode);
/* The four characters naming a keycode, as <AE01>. */
void xcb_xkb_key_name(uint8_t keycode, char name[4]);

/* Little-endian stores into a reply. */
void xkb_put16(uint8_t *at, uint16_t value);
void xkb_put32(uint8_t *at, uint32_t value);

/* Replies and reply memory (xlib_xcb_core.c). */
XcbCookie xcb_reply_keep(void *reply);
void *xcb_reply_allocate(size_t fixed_size, size_t extra);

#endif
