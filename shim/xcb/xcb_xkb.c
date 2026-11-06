/*
 * File: shim/xcb/xcb_xkb.c
 * Purpose: The XKEYBOARD extension on the in-process X server: extension data, request dispatch, state and StateNotify
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * libxcb-xkb and libxkbcommon-x11, which Qt uses, send XKB requests through xcb_send_request and read the replies
 * with xcb_wait_for_reply, so the requests are answered here at the wire level: each reply is built in the
 * protocol's layout and kept under its sequence number like every other reply. The keymap itself is in
 * xcb_xkb_map.c and xcb_xkb_names.c. A client that selects StateNotify events gets one whenever the modifiers
 * change, which is how a toolkit's own keymap state follows the keyboard.
 */

#include <string.h>

#include "xcb_xkb.h"

#define MINOR_USE_EXTENSION 0
#define MINOR_SELECT_EVENTS 1
#define MINOR_BELL 3
#define MINOR_GET_STATE 4
#define MINOR_LATCH_LOCK_STATE 5
#define MINOR_GET_CONTROLS 6
#define MINOR_SET_CONTROLS 7
#define MINOR_GET_MAP 8
#define MINOR_SET_MAP 9
#define MINOR_GET_COMPAT_MAP 10
#define MINOR_SET_COMPAT_MAP 11
#define MINOR_GET_INDICATOR_STATE 12
#define MINOR_GET_INDICATOR_MAP 13
#define MINOR_SET_INDICATOR_MAP 14
#define MINOR_SET_NAMED_INDICATOR 16
#define MINOR_GET_NAMES 17
#define MINOR_SET_NAMES 18
#define MINOR_SET_GEOMETRY 20
#define MINOR_PER_CLIENT_FLAGS 21
#define MINOR_GET_DEVICE_INFO 24
#define MINOR_SET_DEVICE_INFO 25
#define EVENT_STATE_NOTIFY 2
#define EVENT_BIT_STATE_NOTIFY 0x0002
#define SELECT_AFFECT 6
#define SELECT_CLEAR 8
#define FLAGS_SUPPORTED 0x1f
#define FLAGS_CHANGE 8
#define FLAGS_VALUE 12
#define CONTROLS_REPLY_EXTRA 60
#define DEVICE_INFO_REPLY_EXTRA 4
#define CONTROLS_NUM_GROUPS 9
#define CONTROLS_REPEAT_DELAY 20
#define CONTROLS_REPEAT_INTERVAL 22
#define CONTROLS_ENABLED 56
#define CONTROLS_PER_KEY_REPEAT 60
#define PER_KEY_REPEAT_BYTES 32
#define DEFAULT_REPEAT_DELAY_MS 660
#define DEFAULT_REPEAT_INTERVAL_MS 40
#define CONTROL_REPEAT_KEYS 0x01
#define MODS_MASK 0xff
#define MODS_LOCK 0x02
#define STATE_BUTTONS_SHIFT 8
#define STATE_BUTTONS_MASK 0x1f
#define STATE_CHANGED_PARTS 0x180f
#define EVENT_BYTES 32

static volatile LONG g_selected_events;
static volatile LONG g_client_flags;
static volatile LONG g_last_mods;

int xcb_xkb_is_extension(const void *extension)
{
    const char *name = extension != NULL ? *(const char *const *)extension : NULL;
    return name != NULL && strcmp(name, XKB_EXTENSION_NAME) == 0;
}

const XcbExtensionReply *xcb_xkb_extension_data(void)
{
    static const XcbExtensionReply present = {.response_type = 1, .present = 1, .major_opcode = XKB_MAJOR_OPCODE,
                                              .first_event = XKB_FIRST_EVENT, .first_error = XKB_FIRST_ERROR};
    return &present;
}

void xcb_xkb_state(XcbXkbState *out)
{
    uint16_t state = xcb_input_state();
    out->mods = (uint8_t)(state & MODS_MASK);
    out->locked_mods = (uint8_t)(state & MODS_LOCK);
    out->buttons = (uint16_t)((state >> STATE_BUTTONS_SHIFT) & STATE_BUTTONS_MASK);
}

static void *new_reply(size_t extra)
{
    uint8_t *reply = (uint8_t *)xcb_reply_allocate(XKB_REPLY_FIXED, extra);
    if (reply != NULL) {
        reply[1] = XKB_DEVICE_ID;
    }
    return reply;
}

static void *reply_use_extension(void)
{
    uint8_t *reply = (uint8_t *)new_reply(0);
    if (reply != NULL) {
        reply[1] = 1;
        xkb_put16(reply + 8, XKB_SERVER_MAJOR);
        xkb_put16(reply + 10, XKB_SERVER_MINOR);
    }
    return reply;
}

/* The flags the client changed take their new value; the answer says what is set now. */
static void *reply_per_client_flags(const uint8_t *request, size_t length)
{
    uint8_t *reply = (uint8_t *)new_reply(0);
    if (reply == NULL || length < FLAGS_VALUE + sizeof(uint32_t)) {
        return reply;
    }
    uint32_t change;
    uint32_t value;
    memcpy(&change, request + FLAGS_CHANGE, sizeof(change));
    memcpy(&value, request + FLAGS_VALUE, sizeof(value));
    change &= FLAGS_SUPPORTED;
    uint32_t flags = ((uint32_t)g_client_flags & ~change) | (value & change);
    InterlockedExchange(&g_client_flags, (LONG)flags);
    xkb_put32(reply + 8, FLAGS_SUPPORTED);
    xkb_put32(reply + 12, flags);
    return reply;
}

static void *reply_get_state(void)
{
    uint8_t *reply = (uint8_t *)new_reply(0);
    XcbXkbState state;
    xcb_xkb_state(&state);
    if (reply != NULL) {
        reply[8] = state.mods;
        reply[9] = (uint8_t)(state.mods & ~state.locked_mods);
        reply[11] = state.locked_mods;
        reply[18] = state.mods;
        reply[21] = state.mods;
        reply[22] = state.mods;
        xkb_put16(reply + 24, state.buttons);
    }
    return reply;
}

static void *reply_get_controls(void)
{
    uint8_t *reply = (uint8_t *)new_reply(CONTROLS_REPLY_EXTRA);
    if (reply != NULL) {
        reply[CONTROLS_NUM_GROUPS] = 1;
        xkb_put16(reply + CONTROLS_REPEAT_DELAY, DEFAULT_REPEAT_DELAY_MS);
        xkb_put16(reply + CONTROLS_REPEAT_INTERVAL, DEFAULT_REPEAT_INTERVAL_MS);
        xkb_put32(reply + CONTROLS_ENABLED, CONTROL_REPEAT_KEYS);
        memset(reply + CONTROLS_PER_KEY_REPEAT, 0xff, PER_KEY_REPEAT_BYTES);
    }
    return reply;
}

static void *reply_get_device_info(void)
{
    return new_reply(DEVICE_INFO_REPLY_EXTRA);
}

static void select_events(const uint8_t *request, size_t length)
{
    if (length < SELECT_CLEAR + sizeof(uint16_t)) {
        return;
    }
    uint16_t affect = (uint16_t)(request[SELECT_AFFECT] | request[SELECT_AFFECT + 1] << 8);
    uint16_t clear = (uint16_t)(request[SELECT_CLEAR] | request[SELECT_CLEAR + 1] << 8);
    if (affect & EVENT_BIT_STATE_NOTIFY) {
        InterlockedExchange(&g_selected_events, (clear & EVENT_BIT_STATE_NOTIFY) ? 0 : 1);
    }
}

static int is_void_request(uint8_t minor)
{
    switch (minor) {
    case MINOR_SELECT_EVENTS: case MINOR_BELL: case MINOR_LATCH_LOCK_STATE: case MINOR_SET_CONTROLS:
    case MINOR_SET_MAP: case MINOR_SET_COMPAT_MAP: case MINOR_SET_INDICATOR_MAP: case MINOR_SET_NAMED_INDICATOR:
    case MINOR_SET_NAMES: case MINOR_SET_GEOMETRY: case MINOR_SET_DEVICE_INFO:
        return 1;
    default:
        return 0;
    }
}

/* The reply to a request that has one; an answer of all zeros for requests this keyboard has nothing to say to. */
static void *build_reply(uint8_t minor, const uint8_t *request, size_t length)
{
    switch (minor) {
    case MINOR_USE_EXTENSION: return reply_use_extension();
    case MINOR_GET_STATE: return reply_get_state();
    case MINOR_GET_CONTROLS: return reply_get_controls();
    case MINOR_GET_MAP: return xcb_xkb_get_map(request, length);
    case MINOR_GET_COMPAT_MAP: return xcb_xkb_get_compat_map();
    case MINOR_GET_INDICATOR_MAP: return xcb_xkb_get_indicator_map();
    case MINOR_GET_NAMES: return xcb_xkb_get_names(request, length);
    case MINOR_PER_CLIENT_FLAGS: return reply_per_client_flags(request, length);
    case MINOR_GET_DEVICE_INFO: return reply_get_device_info();
    case MINOR_GET_INDICATOR_STATE:
    default: return new_reply(0);
    }
}

static size_t gather(const XcbVector *vector, size_t count, uint8_t *out, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < count && vector != NULL; i++) {
        size_t take = vector[i].length < capacity - used ? vector[i].length : capacity - used;
        if (vector[i].base != NULL) {
            memcpy(out + used, vector[i].base, take);
        }
        used += take;
    }
    return used;
}

uint32_t xcb_xkb_request(const XcbProtocolRequest *request, const XcbVector *vector)
{
    if (request == NULL || !xcb_xkb_is_extension(request->extension)) {
        return 0;
    }
    uint8_t body[XKB_MAX_REQUEST] = {0};
    size_t length = gather(vector, request->count, body, sizeof(body));
    if (request->opcode == MINOR_SELECT_EVENTS) {
        select_events(body, length);
    }
    if (request->is_void || is_void_request(request->opcode)) {
        return xcb_next_sequence();
    }
    return xcb_reply_keep(build_reply(request->opcode, body, length)).sequence;
}

/* The event a client that selected StateNotify receives; the keyboard has one group and no latched modifiers. */
static void push_state_notify(uint16_t state)
{
    uint8_t wire[EVENT_BYTES] = {0};
    uint8_t mods = (uint8_t)(state & MODS_MASK);
    uint8_t locked = (uint8_t)(state & MODS_LOCK);
    wire[0] = XKB_FIRST_EVENT;
    wire[1] = EVENT_STATE_NOTIFY;
    xkb_put32(wire + 4, GetTickCount());
    wire[8] = XKB_DEVICE_ID;
    wire[9] = mods;
    wire[10] = (uint8_t)(mods & ~locked);
    wire[12] = locked;
    wire[19] = mods;
    wire[22] = mods;
    wire[23] = mods;
    xkb_put16(wire + 26, STATE_CHANGED_PARTS);
    XcbEvent event;
    memcpy(&event, wire, sizeof(event));
    xcb_events_push(&event);
}

void xcb_xkb_note_modifiers(uint16_t state)
{
    LONG mods = state & MODS_MASK;
    if (InterlockedExchange(&g_last_mods, mods) != mods && g_selected_events) {
        push_state_notify(state);
    }
}
