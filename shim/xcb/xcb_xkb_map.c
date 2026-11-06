/*
 * File: shim/xcb/xcb_xkb_map.c
 * Purpose: XKB GetMap, GetCompatMap and GetIndicatorMap replies: the key types, symbols and modifier map in wire format
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * libxkbcommon-x11 builds a whole keymap from these replies, so the layout follows the protocol's: a 40-byte
 * header, then the lists in the order the present mask names them (types, symbols, action counts and actions,
 * behaviors, virtual modifiers, explicit components, modifier map, virtual modifier map). This keyboard has no
 * actions, behaviors, explicit components or virtual modifiers, so those lists are empty.
 */

#include <string.h>

#include "xcb_xkb.h"

#define MAP_REPLY_MAX 16384
#define MAP_HEADER_BYTES 40
#define PART_KEY_TYPES 0x01
#define PART_KEY_SYMS 0x02
#define PART_MODIFIER_MAP 0x04
#define PART_KEY_ACTIONS 0x10
#define KEY_TYPE_BYTES 8
#define MAP_ENTRY_BYTES 8
#define SYM_MAP_BYTES 8
#define GROUP_COUNT_ONE 1
#define ALIGN 4
#define REQUEST_FULL 6
#define REQUEST_PARTIAL 8
#define REQUEST_FIRST_TYPE 10
#define REQUEST_FIRST_SYM 12
#define REQUEST_FIRST_ACTION 14
#define REQUEST_FIRST_MODMAP 22
#define KEYCODE_COUNT (XKB_LAST_KEYCODE - XKB_FIRST_KEYCODE + 1)

typedef struct {
    uint8_t *data;
    size_t used;
} Cursor;

typedef struct {
    uint8_t first;
    uint8_t count;
} Range;

static void put_byte(Cursor *cursor, uint8_t value)
{
    cursor->data[cursor->used++] = value;
}

static void put_word(Cursor *cursor, uint16_t value)
{
    xkb_put16(cursor->data + cursor->used, value);
    cursor->used += sizeof(value);
}

static void put_long(Cursor *cursor, uint32_t value)
{
    xkb_put32(cursor->data + cursor->used, value);
    cursor->used += sizeof(value);
}

static void pad_to_word(Cursor *cursor)
{
    while (cursor->used % ALIGN != 0) {
        cursor->used++;
    }
}

/* The keys a request covers: all of them when the part is in `full`, the asked-for range when in `partial`. */
static Range key_range(uint16_t full, uint16_t partial, uint16_t part, uint8_t first, uint8_t count)
{
    if (full & part) {
        return (Range){XKB_FIRST_KEYCODE, KEYCODE_COUNT};
    }
    if (!(partial & part) || first < XKB_FIRST_KEYCODE) {
        return (Range){0, 0};
    }
    unsigned last = (unsigned)first + count;
    return (Range){first, (uint8_t)(last > XKB_LAST_KEYCODE + 1 ? XKB_LAST_KEYCODE + 1 - first : count)};
}

static Range type_range(uint16_t full, uint16_t partial, uint8_t first, uint8_t count)
{
    if (full & PART_KEY_TYPES) {
        return (Range){0, XKB_TYPE_COUNT};
    }
    if (!(partial & PART_KEY_TYPES) || first >= XKB_TYPE_COUNT) {
        return (Range){0, 0};
    }
    return (Range){first, (uint8_t)(first + count > XKB_TYPE_COUNT ? XKB_TYPE_COUNT - first : count)};
}

static void put_type(Cursor *cursor, const XkbTypeDef *type)
{
    put_byte(cursor, type->mods);
    put_byte(cursor, type->mods);
    put_word(cursor, 0);
    put_byte(cursor, type->levels);
    put_byte(cursor, type->entry_count);
    put_byte(cursor, 0);
    put_byte(cursor, 0);
    for (int i = 0; i < type->entry_count; i++) {
        put_byte(cursor, 1);
        put_byte(cursor, type->entry_mods[i]);
        put_byte(cursor, type->entry_level[i]);
        put_byte(cursor, type->entry_mods[i]);
        put_word(cursor, 0);
        put_word(cursor, 0);
    }
}

static void put_key_syms(Cursor *cursor, uint8_t keycode)
{
    uint8_t levels = 0;
    uint8_t type = xcb_xkb_key_type(keycode, &levels);
    put_byte(cursor, levels == 0 ? 0 : type);
    for (int i = 1; i < 4; i++) {
        put_byte(cursor, 0);
    }
    put_byte(cursor, levels == 0 ? 0 : GROUP_COUNT_ONE);
    put_byte(cursor, levels);
    put_word(cursor, levels);
    for (uint8_t level = 0; level < levels; level++) {
        put_long(cursor, xcb_keysym_for(keycode, level));
    }
}

static unsigned count_syms(Range keys)
{
    unsigned total = 0;
    for (unsigned i = 0; i < keys.count; i++) {
        uint8_t levels = 0;
        xcb_xkb_key_type((uint8_t)(keys.first + i), &levels);
        total += levels;
    }
    return total;
}

static unsigned count_modifier_keys(Range keys)
{
    unsigned total = 0;
    for (unsigned i = 0; i < keys.count; i++) {
        total += xcb_xkb_key_modifiers((uint8_t)(keys.first + i)) != 0;
    }
    return total;
}

static void put_modifier_map(Cursor *cursor, Range keys)
{
    for (unsigned i = 0; i < keys.count; i++) {
        uint8_t keycode = (uint8_t)(keys.first + i);
        if (xcb_xkb_key_modifiers(keycode) != 0) {
            put_byte(cursor, keycode);
            put_byte(cursor, xcb_xkb_key_modifiers(keycode));
        }
    }
    pad_to_word(cursor);
}

typedef struct {
    Range types;
    Range syms;
    Range actions;
    Range modmap;
} MapRanges;

static void put_header(uint8_t *reply, uint16_t present, const MapRanges *ranges)
{
    reply[0] = 1;
    reply[1] = XKB_DEVICE_ID;
    reply[10] = XKB_FIRST_KEYCODE;
    reply[11] = XKB_LAST_KEYCODE;
    xkb_put16(reply + 12, present);
    reply[14] = ranges->types.first;
    reply[15] = ranges->types.count;
    reply[16] = ranges->types.count;
    reply[17] = ranges->syms.first;
    xkb_put16(reply + 18, (uint16_t)count_syms(ranges->syms));
    reply[20] = ranges->syms.count;
    reply[21] = ranges->actions.first;
    reply[24] = ranges->actions.count;
    reply[31] = ranges->modmap.first;
    reply[32] = ranges->modmap.count;
    reply[33] = (uint8_t)count_modifier_keys(ranges->modmap);
}

static void put_lists(Cursor *cursor, const MapRanges *ranges)
{
    for (unsigned i = 0; i < ranges->types.count; i++) {
        put_type(cursor, &XKB_TYPES[ranges->types.first + i]);
    }
    for (unsigned i = 0; i < ranges->syms.count; i++) {
        put_key_syms(cursor, (uint8_t)(ranges->syms.first + i));
    }
    cursor->used += ranges->actions.count;
    pad_to_word(cursor);
    put_modifier_map(cursor, ranges->modmap);
}

void *xcb_xkb_get_map(const uint8_t *request, size_t length)
{
    if (length < REQUEST_FIRST_MODMAP + 2) {
        return NULL;
    }
    uint16_t full = (uint16_t)(request[REQUEST_FULL] | request[REQUEST_FULL + 1] << 8);
    uint16_t partial = (uint16_t)(request[REQUEST_PARTIAL] | request[REQUEST_PARTIAL + 1] << 8);
    MapRanges ranges = {
        .types = type_range(full, partial, request[REQUEST_FIRST_TYPE], request[REQUEST_FIRST_TYPE + 1]),
        .syms = key_range(full, partial, PART_KEY_SYMS, request[REQUEST_FIRST_SYM], request[REQUEST_FIRST_SYM + 1]),
        .actions = key_range(full, partial, PART_KEY_ACTIONS, request[REQUEST_FIRST_ACTION],
                             request[REQUEST_FIRST_ACTION + 1]),
        .modmap = key_range(full, partial, PART_MODIFIER_MAP, request[REQUEST_FIRST_MODMAP],
                            request[REQUEST_FIRST_MODMAP + 1]),
    };
    uint8_t *reply = (uint8_t *)xcb_reply_allocate(XKB_REPLY_FIXED, MAP_REPLY_MAX);
    if (reply == NULL) {
        return NULL;
    }
    put_header(reply, (uint16_t)(full | partial), &ranges);
    Cursor cursor = {.data = reply, .used = MAP_HEADER_BYTES};
    put_lists(&cursor, &ranges);
    xkb_put32(reply + 4, (uint32_t)((cursor.used - XKB_REPLY_FIXED + ALIGN - 1) / ALIGN));
    return reply;
}

/* No symbol interpretations and no per-group modifier compatibility. */
void *xcb_xkb_get_compat_map(void)
{
    uint8_t *reply = (uint8_t *)xcb_reply_allocate(XKB_REPLY_FIXED, 0);
    if (reply != NULL) {
        reply[1] = XKB_DEVICE_ID;
    }
    return reply;
}

/* No indicators are described: the keyboard has lights nobody can read. */
void *xcb_xkb_get_indicator_map(void)
{
    uint8_t *reply = (uint8_t *)xcb_reply_allocate(XKB_REPLY_FIXED, 0);
    if (reply != NULL) {
        reply[1] = XKB_DEVICE_ID;
    }
    return reply;
}
