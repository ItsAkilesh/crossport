/*
 * File: shim/xcb/xcb_xkb_names.c
 * Purpose: XKB GetNames reply: the names of the keymap's parts, key types, levels, groups and keys, as atoms
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * Names travel as atoms, which the client turns back into text with GetAtomName, so each name is interned here.
 * The lists follow the header in the order of the protocol: the four component names, the type names, the level
 * count of each type, the level names, the group names and the four-letter key names.
 */

#include <string.h>

#include "xcb_xkb.h"

#define NAMES_REPLY_MAX 4096
#define NAMES_HEADER_BYTES 32
#define REQUEST_WHICH 8
#define WHICH_KEYCODES 0x001
#define WHICH_SYMBOLS 0x004
#define WHICH_TYPES 0x010
#define WHICH_COMPAT 0x020
#define WHICH_KEY_TYPE_NAMES 0x040
#define WHICH_KT_LEVEL_NAMES 0x080
#define WHICH_INDICATOR_NAMES 0x100
#define WHICH_KEY_NAMES 0x200
#define WHICH_KEY_ALIASES 0x400
#define WHICH_VIRTUAL_MOD_NAMES 0x800
#define WHICH_GROUP_NAMES 0x1000
#define WHICH_SUPPORTED (WHICH_KEYCODES | WHICH_SYMBOLS | WHICH_TYPES | WHICH_COMPAT | WHICH_KEY_TYPE_NAMES | \
                         WHICH_KT_LEVEL_NAMES | WHICH_INDICATOR_NAMES | WHICH_KEY_NAMES | WHICH_KEY_ALIASES | \
                         WHICH_VIRTUAL_MOD_NAMES | WHICH_GROUP_NAMES)
#define KEYCODES_NAME "evdev+aliases(qwerty)"
#define SYMBOLS_NAME "pc+us+inet(evdev)"
#define TYPES_NAME "complete"
#define COMPAT_NAME "complete"
#define LAYOUT_NAME "English (US)"
#define GROUP_MASK_ONE 0x01
#define KEYCODE_COUNT (XKB_LAST_KEYCODE - XKB_FIRST_KEYCODE + 1)
#define ALIGN 4

typedef struct {
    uint8_t *data;
    size_t used;
} Cursor;

static void put_atom(Cursor *cursor, const char *name)
{
    xkb_put32(cursor->data + cursor->used, xcb_atom_for(name, strlen(name)));
    cursor->used += sizeof(uint32_t);
}

static uint16_t level_name_count(void)
{
    uint16_t total = 0;
    for (int i = 0; i < XKB_TYPE_COUNT; i++) {
        total = (uint16_t)(total + XKB_TYPES[i].levels);
    }
    return total;
}

static void put_component_names(Cursor *cursor, uint32_t which)
{
    if (which & WHICH_KEYCODES) {
        put_atom(cursor, KEYCODES_NAME);
    }
    if (which & WHICH_SYMBOLS) {
        put_atom(cursor, SYMBOLS_NAME);
    }
    if (which & WHICH_TYPES) {
        put_atom(cursor, TYPES_NAME);
    }
    if (which & WHICH_COMPAT) {
        put_atom(cursor, COMPAT_NAME);
    }
}

static void put_type_names(Cursor *cursor, uint32_t which)
{
    if (which & WHICH_KEY_TYPE_NAMES) {
        for (int i = 0; i < XKB_TYPE_COUNT; i++) {
            put_atom(cursor, XKB_TYPES[i].name);
        }
        for (int i = 0; i < XKB_TYPE_COUNT; i++) {
            cursor->data[cursor->used++] = (which & WHICH_KT_LEVEL_NAMES) ? XKB_TYPES[i].levels : 0;
        }
        while (cursor->used % ALIGN != 0) {
            cursor->used++;
        }
    }
    if (which & WHICH_KT_LEVEL_NAMES) {
        for (int i = 0; i < XKB_TYPE_COUNT; i++) {
            for (int level = 0; level < XKB_TYPES[i].levels; level++) {
                put_atom(cursor, XKB_TYPES[i].level_names[level]);
            }
        }
    }
}

static void put_key_names(Cursor *cursor, uint32_t which)
{
    if (which & WHICH_GROUP_NAMES) {
        put_atom(cursor, LAYOUT_NAME);
    }
    if (which & WHICH_KEY_NAMES) {
        for (unsigned keycode = XKB_FIRST_KEYCODE; keycode <= XKB_LAST_KEYCODE; keycode++) {
            xcb_xkb_key_name((uint8_t)keycode, (char *)cursor->data + cursor->used);
            cursor->used += 4;
        }
    }
}

static void put_header(uint8_t *reply, uint32_t which)
{
    reply[0] = 1;
    reply[1] = XKB_DEVICE_ID;
    xkb_put32(reply + 8, which);
    reply[12] = XKB_FIRST_KEYCODE;
    reply[13] = XKB_LAST_KEYCODE;
    reply[14] = (which & WHICH_KEY_TYPE_NAMES) ? XKB_TYPE_COUNT : 0;
    reply[15] = (which & WHICH_GROUP_NAMES) ? GROUP_MASK_ONE : 0;
    reply[18] = XKB_FIRST_KEYCODE;
    reply[19] = (which & WHICH_KEY_NAMES) ? KEYCODE_COUNT : 0;
    xkb_put16(reply + 26, (which & WHICH_KT_LEVEL_NAMES) ? level_name_count() : 0);
}

void *xcb_xkb_get_names(const uint8_t *request, size_t length)
{
    if (length < REQUEST_WHICH + sizeof(uint32_t)) {
        return NULL;
    }
    uint32_t wanted;
    memcpy(&wanted, request + REQUEST_WHICH, sizeof(wanted));
    uint32_t which = wanted & WHICH_SUPPORTED;
    uint8_t *reply = (uint8_t *)xcb_reply_allocate(XKB_REPLY_FIXED, NAMES_REPLY_MAX);
    if (reply == NULL) {
        return NULL;
    }
    put_header(reply, which);
    Cursor cursor = {.data = reply, .used = NAMES_HEADER_BYTES};
    put_component_names(&cursor, which);
    put_type_names(&cursor, which);
    put_key_names(&cursor, which);
    xkb_put32(reply + 4, (uint32_t)((cursor.used - XKB_REPLY_FIXED + ALIGN - 1) / ALIGN));
    return reply;
}
