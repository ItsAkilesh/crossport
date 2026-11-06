/*
 * File: tests/corpus/xkb_requests/main.c
 * Purpose: xcb client that speaks the XKEYBOARD extension the way libxkbcommon-x11 does, and prints what the server answers
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * Differential test: the same program talks to a real X server (Xvfb) on Linux and to the shim on Windows, and must
 * print the same. It sends the extension's requests through xcb's generic path (so it needs no libxcb-xkb): query the
 * extension, UseExtension, PerClientFlags, GetState, GetMap (the key types and the symbols of one key) and GetNames
 * (the name of one key), and prints only what the protocol fixes, not what a particular server's keymap or lock
 * keys happen to be.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#define MINOR_USE_EXTENSION 0
#define MINOR_GET_STATE 4
#define MINOR_GET_MAP 8
#define MINOR_GET_NAMES 17
#define MINOR_PER_CLIENT_FLAGS 21
#define USE_CORE_KBD 0x100
#define HEADER_BYTES 4
#define REQUEST_BYTES 28
#define FLAGS_GRABS_AND_LOOKUP 0x0a
#define MAP_PARTS_ALL 0xff
#define NAMES_WANTED 0x1ff5
#define PROBE_KEYCODE 38
#define MAP_HEADER 40
#define NAMES_HEADER 32
#define TYPE_HEADER 8
#define ENTRY_BYTES 8
#define PRESERVE_BYTES 4

static xcb_extension_t g_xkb = {"XKEYBOARD", 0};
static xcb_connection_t *g_connection;

static uint16_t get16(const uint8_t *at)
{
    uint16_t value;
    memcpy(&value, at, sizeof(value));
    return value;
}

static uint32_t get32(const uint8_t *at)
{
    uint32_t value;
    memcpy(&value, at, sizeof(value));
    return value;
}

static void put16(uint8_t *at, uint16_t value)
{
    memcpy(at, &value, sizeof(value));
}

static void put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof(value));
}

/* Sends one XKB request (`length` bytes, header included) and returns its reply, or NULL. */
static uint8_t *call(uint8_t minor, uint8_t *request, size_t length)
{
    struct iovec parts[4] = {{0}, {0}, {request, length}, {NULL, 0}};
    xcb_protocol_request_t description = {.count = 2, .ext = &g_xkb, .opcode = minor, .isvoid = 0};
    unsigned sequence = xcb_send_request(g_connection, XCB_REQUEST_CHECKED, parts + 2, &description);
    return xcb_wait_for_reply(g_connection, sequence, NULL);
}

static void check_extension(void)
{
    const xcb_query_extension_reply_t *data = xcb_get_extension_data(g_connection, &g_xkb);
    printf("extension XKEYBOARD: present=%d\n", data != NULL && data->present);
}

static void check_use_extension(void)
{
    uint8_t request[8] = {0};
    put16(request + 4, 1);
    uint8_t *reply = call(MINOR_USE_EXTENSION, request, sizeof(request));
    printf("use_extension: supported=%d major=%d\n", reply != NULL ? reply[1] : -1, reply != NULL ? get16(reply + 8) : -1);
    free(reply);
}

static void check_per_client_flags(void)
{
    uint8_t request[REQUEST_BYTES] = {0};
    put16(request + 4, USE_CORE_KBD);
    put32(request + 8, FLAGS_GRABS_AND_LOOKUP);
    put32(request + 12, FLAGS_GRABS_AND_LOOKUP);
    uint8_t *reply = call(MINOR_PER_CLIENT_FLAGS, request, sizeof(request));
    printf("per_client_flags: supported=%d value=%d\n",
           reply != NULL && (get32(reply + 8) & FLAGS_GRABS_AND_LOOKUP) == FLAGS_GRABS_AND_LOOKUP,
           reply != NULL && (get32(reply + 12) & FLAGS_GRABS_AND_LOOKUP) == FLAGS_GRABS_AND_LOOKUP);
    free(reply);
}

static void check_state(void)
{
    uint8_t request[8] = {0};
    put16(request + 4, USE_CORE_KBD);
    uint8_t *reply = call(MINOR_GET_STATE, request, sizeof(request));
    printf("state: device_found=%d group=%d locked_group=%d\n", reply != NULL && reply[1] != 0,
           reply != NULL ? reply[12] : -1, reply != NULL ? reply[13] : -1);
    free(reply);
}

/* The sym map of `keycode` in a GetMap reply: skips the types, then the maps of the keys before it. */
static const uint8_t *find_sym_map(const uint8_t *reply, uint8_t keycode)
{
    const uint8_t *at = reply + MAP_HEADER;
    for (int i = 0; i < reply[15]; i++) {
        uint8_t entries = at[5];
        at += TYPE_HEADER + entries * ENTRY_BYTES + (at[6] ? entries * PRESERVE_BYTES : 0);
    }
    for (int key = reply[17]; key < keycode && key < reply[17] + reply[20]; key++) {
        at += 8 + get16(at + 6) * 4;
    }
    return at;
}

static void check_map(void)
{
    uint8_t request[REQUEST_BYTES] = {0};
    put16(request + 4, USE_CORE_KBD);
    put16(request + 6, MAP_PARTS_ALL);
    uint8_t *reply = call(MINOR_GET_MAP, request, sizeof(request));
    if (reply == NULL) {
        puts("map: no reply");
        return;
    }
    printf("map: min=%d max=%d present_all=%d types_ok=%d\n", reply[10], reply[11], get16(reply + 12) == MAP_PARTS_ALL,
           reply[15] >= 4);
    const uint8_t *keys = find_sym_map(reply, PROBE_KEYCODE);
    printf("map: key%d width=%d level0=%x level1=%x\n", PROBE_KEYCODE, keys[5], get32(keys + 8), get32(keys + 12));
    free(reply);
}

/* The four-letter name of `keycode` in a GetNames reply, found by skipping the lists in front of the key names. */
static void print_key_name(const uint8_t *reply, uint8_t keycode)
{
    uint32_t which = get32(reply + 8);
    unsigned types = reply[14];
    const uint8_t *at = reply + NAMES_HEADER;
    at += 4 * (unsigned)__builtin_popcount(which & 0x3f);
    at += 4 * types + ((types + 3u) & ~3u);
    at += 4 * (unsigned)get16(reply + 26) + 4 * (unsigned)__builtin_popcount(get32(reply + 20));
    at += 4 * (unsigned)__builtin_popcount(get16(reply + 16)) + 4 * (unsigned)__builtin_popcount(reply[15]);
    const uint8_t *name = at + 4 * (keycode - reply[18]);
    printf("names: key%d=%.4s\n", keycode, (const char *)name);
}

static void check_names(void)
{
    uint8_t request[12] = {0};
    put16(request + 4, USE_CORE_KBD);
    put32(request + 8, NAMES_WANTED);
    uint8_t *reply = call(MINOR_GET_NAMES, request, sizeof(request));
    if (reply == NULL) {
        puts("names: no reply");
        return;
    }
    printf("names: which_all=%d types_ok=%d keys_ok=%d\n", (get32(reply + 8) & NAMES_WANTED) == NAMES_WANTED,
           reply[14] >= 4, reply[19] >= PROBE_KEYCODE - reply[18]);
    print_key_name(reply, PROBE_KEYCODE);
    free(reply);
}

int main(void)
{
    int screen_number = 0;
    g_connection = xcb_connect(NULL, &screen_number);
    if (xcb_connection_has_error(g_connection)) {
        puts("no display");
        return 1;
    }
    check_extension();
    check_use_extension();
    check_per_client_flags();
    check_state();
    check_map();
    check_names();
    xcb_disconnect(g_connection);
    puts("done");
    return 0;
}
