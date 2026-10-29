/*
 * File: shim/xlib/xlib_keyboard.c
 * Purpose: libX11 keyboard functions: keysym lookup, XLookupString, the keyboard and modifier mappings
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The keymap is a fixed US layout (shim/xcb/xcb_keys.c). Input methods are not provided: XOpenIM answers
 * NULL, which toolkits treat as "no input method" and fall back to XLookupString.
 */

#include <string.h>

#include "xlib_internal.h"

#define STATE_SHIFT 0x1u
#define STATE_LOCK 0x2u
#define STATE_CONTROL 0x4u
#define KEYSYM_RETURN 0xff0d
#define KEYSYM_KP_ENTER 0xff8d
#define KEYSYM_BACKSPACE 0xff08
#define KEYSYM_TAB 0xff09
#define KEYSYM_ESCAPE 0xff1b
#define KEYSYM_DELETE 0xffff
#define KEYSYM_FIRST_PRINTABLE 0x20
#define KEYSYM_LAST_PRINTABLE 0x7e
#define CONTROL_MASK_BITS 0x1f
#define SYMBOLS_PER_KEYCODE 2
#define MODIFIER_COUNT 8
#define KEYS_PER_MODIFIER 2
#define KEYMAP_BYTES 32
#define MAX_KEYCODE 255

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);

typedef struct {
    int32_t max_keypermod;
    uint8_t *modifiermap;
} XlModifierKeymap;

static int is_letter(uint32_t symbol)
{
    return (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z');
}

/* The keysym a key produces with the given modifier state (Shift, and Caps Lock for letters). */
static uint32_t symbol_for_state(uint32_t keycode, uint32_t state)
{
    uint32_t plain = xcb_keysym_for((uint8_t)keycode, 0);
    int is_shifted = (state & STATE_SHIFT) != 0;
    if (is_letter(plain) && (state & STATE_LOCK)) {
        is_shifted = !is_shifted;
    }
    uint32_t symbol = xcb_keysym_for((uint8_t)keycode, is_shifted ? 1 : 0);
    return symbol != 0 ? symbol : plain;
}

LIBC_FN uint64_t sysv_XLookupKeysym(const XlInputEvent *event, int index)
{
    return xcb_keysym_for((uint8_t)event->detail, index);
}

LIBC_FN uint64_t sysv_XKeycodeToKeysym(XlDisplay *display, uint8_t keycode, int index)
{
    (void)display;
    return xcb_keysym_for(keycode, index);
}

LIBC_FN uint64_t sysv_XkbKeycodeToKeysym(XlDisplay *display, uint8_t keycode, uint32_t group, uint32_t level)
{
    (void)display;
    return group == 0 ? xcb_keysym_for(keycode, (int)level) : 0;
}

LIBC_FN uint8_t sysv_XKeysymToKeycode(XlDisplay *display, uint64_t keysym)
{
    (void)display;
    for (int keycode = 1; keycode <= MAX_KEYCODE; keycode++) {
        if (xcb_keysym_for((uint8_t)keycode, 0) == keysym || xcb_keysym_for((uint8_t)keycode, 1) == keysym) {
            return (uint8_t)keycode;
        }
    }
    return 0;
}

/* The characters a key press produces: printable keysyms, control characters, and the few control keys. */
static int string_for(uint32_t symbol, uint32_t state, char *buffer, int size)
{
    int character = -1;
    if (symbol >= KEYSYM_FIRST_PRINTABLE && symbol <= KEYSYM_LAST_PRINTABLE) {
        character = (int)symbol;
        if ((state & STATE_CONTROL) && (is_letter(symbol) || symbol == '[' || symbol == '\\' || symbol == ']' || symbol == ' ')) {
            character = (int)symbol & CONTROL_MASK_BITS;
        }
    } else if (symbol == KEYSYM_RETURN || symbol == KEYSYM_KP_ENTER) {
        character = '\r';
    } else if (symbol == KEYSYM_BACKSPACE) {
        character = '\b';
    } else if (symbol == KEYSYM_TAB) {
        character = '\t';
    } else if (symbol == KEYSYM_ESCAPE) {
        character = 0x1b;
    } else if (symbol == KEYSYM_DELETE) {
        character = 0x7f;
    }
    if (character < 0 || size < 1) {
        return 0;
    }
    buffer[0] = (char)character;
    return 1;
}

LIBC_FN int sysv_XLookupString(const XlInputEvent *event, char *buffer, int size, uint64_t *keysym, void *status)
{
    (void)status;
    uint32_t symbol = symbol_for_state(event->detail, event->state);
    if (keysym != NULL) {
        *keysym = symbol;
    }
    return string_for(symbol, event->state, buffer, size);
}

LIBC_FN uint64_t *sysv_XGetKeyboardMapping(XlDisplay *display, uint8_t first, int count, int *per_keycode)
{
    (void)display;
    *per_keycode = SYMBOLS_PER_KEYCODE;
    uint64_t *symbols = (uint64_t *)sysv_malloc((size_t)(count > 0 ? count : 1) * SYMBOLS_PER_KEYCODE * sizeof(uint64_t));
    for (int i = 0; symbols != NULL && i < count; i++) {
        for (int column = 0; column < SYMBOLS_PER_KEYCODE; column++) {
            symbols[i * SYMBOLS_PER_KEYCODE + column] = xcb_keysym_for((uint8_t)(first + i), column);
        }
    }
    return symbols;
}

LIBC_FN int sysv_XDisplayKeycodes(XlDisplay *display, int *minimum, int *maximum)
{
    *minimum = display->min_keycode;
    *maximum = display->max_keycode;
    return 1;
}

/* Modifier rows (Shift, Lock, Control, Mod1 to Mod5) with their X keycodes: evdev code plus 8. */
static const uint8_t MODIFIER_KEYS[MODIFIER_COUNT][KEYS_PER_MODIFIER] = {
    {50, 62}, {66, 0}, {37, 105}, {64, 108}, {77, 0}, {0, 0}, {133, 134}, {0, 0},
};

LIBC_FN XlModifierKeymap *sysv_XGetModifierMapping(XlDisplay *display)
{
    (void)display;
    XlModifierKeymap *map = (XlModifierKeymap *)sysv_malloc(sizeof(*map));
    uint8_t *keys = (uint8_t *)sysv_malloc(sizeof(MODIFIER_KEYS));
    if (map == NULL || keys == NULL) {
        sysv_free(map);
        sysv_free(keys);
        return NULL;
    }
    memcpy(keys, MODIFIER_KEYS, sizeof(MODIFIER_KEYS));
    *map = (XlModifierKeymap){.max_keypermod = KEYS_PER_MODIFIER, .modifiermap = keys};
    return map;
}

LIBC_FN int sysv_XFreeModifiermap(XlModifierKeymap *map)
{
    if (map != NULL) {
        sysv_free(map->modifiermap);
        sysv_free(map);
    }
    return 1;
}

LIBC_FN int sysv_XRefreshKeyboardMapping(void *event)
{
    (void)event;
    return 1;
}

LIBC_FN int sysv_XQueryKeymap(XlDisplay *display, char keys[KEYMAP_BYTES])
{
    (void)display;
    memset(keys, 0, KEYMAP_BYTES);
    return 1;
}

LIBC_FN char *sysv_XSetLocaleModifiers(const char *modifiers)
{
    (void)modifiers;
    return (char *)"";
}

LIBC_FN int sysv_XSupportsLocale(void)
{
    return 1;
}

LIBC_FN void *sysv_XOpenIM(XlDisplay *display, void *database, char *name, char *class_name)
{
    (void)display;
    (void)database;
    (void)name;
    (void)class_name;
    return NULL;
}

LIBC_FN int sysv_XkbSetDetectableAutoRepeat(XlDisplay *display, int is_detectable, int *supported)
{
    (void)display;
    (void)is_detectable;
    if (supported != NULL) {
        *supported = 1;
    }
    return 1;
}
