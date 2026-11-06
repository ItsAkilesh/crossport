/*
 * File: shim/xcb/xcb_xkb_keys.c
 * Purpose: The XKB keymap as data: key types, which keys are modifiers, key names, and byte stores for replies
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * The keymap is the US layout of xcb_keys.c seen through XKB: one group, the four canonical key types, and
 * the key names of the evdev keycode set (<AE01> and so on) that libxkbcommon and xkeyboard-config use.
 */

#include <string.h>

#include "xcb_xkb.h"

#define KEYCODE_OFFSET 8
#define NAMED_KEYS 128
#define LOWERCASE_FIRST 'a'
#define LOWERCASE_LAST 'z'
#define CASE_DISTANCE ('a' - 'A')
#define KEYMOD_SHIFT 0x01
#define KEYMOD_LOCK 0x02
#define KEYMOD_CONTROL 0x04
#define KEYMOD_ALT 0x08
#define KEYMOD_NUM 0x10
#define KEYMOD_SUPER 0x40
#define TYPE_ONE_LEVEL 0
#define TYPE_TWO_LEVEL 1
#define TYPE_ALPHABETIC 2

const XkbTypeDef XKB_TYPES[XKB_TYPE_COUNT] = {
    {.mods = 0, .levels = 1, .entry_count = 0, .name = "ONE_LEVEL", .level_names = {"Any"}},
    {.mods = KEYMOD_SHIFT, .levels = 2, .entry_count = 1, .entry_mods = {KEYMOD_SHIFT}, .entry_level = {1},
     .name = "TWO_LEVEL", .level_names = {"Any", "Shift"}},
    {.mods = KEYMOD_SHIFT | KEYMOD_LOCK, .levels = 2, .entry_count = 2, .entry_mods = {KEYMOD_SHIFT, KEYMOD_LOCK},
     .entry_level = {1, 1}, .name = "ALPHABETIC", .level_names = {"Base", "Caps"}},
    {.mods = KEYMOD_SHIFT | KEYMOD_NUM, .levels = 2, .entry_count = 2, .entry_mods = {KEYMOD_SHIFT, KEYMOD_NUM},
     .entry_level = {1, 1}, .name = "KEYPAD", .level_names = {"Base", "Number"}},
};

typedef struct {
    uint8_t keycode;
    uint8_t mods;
} ModifierKey;

/* The same keys as the core modifier map (xlib_keyboard.c). */
static const ModifierKey MODIFIER_KEYS[] = {
    {50, KEYMOD_SHIFT}, {62, KEYMOD_SHIFT}, {66, KEYMOD_LOCK}, {37, KEYMOD_CONTROL}, {105, KEYMOD_CONTROL}, {64, KEYMOD_ALT},
    {108, KEYMOD_ALT}, {77, KEYMOD_NUM}, {133, KEYMOD_SUPER}, {134, KEYMOD_SUPER},
};

/* Key names by evdev code; a code with no entry is named after its keycode. */
static const char *const KEY_NAMES[NAMED_KEYS] = {
    [1] = "ESC", [2] = "AE01", [3] = "AE02", [4] = "AE03", [5] = "AE04", [6] = "AE05", [7] = "AE06", [8] = "AE07",
    [9] = "AE08", [10] = "AE09", [11] = "AE10", [12] = "AE11", [13] = "AE12", [14] = "BKSP", [15] = "TAB",
    [16] = "AD01", [17] = "AD02", [18] = "AD03", [19] = "AD04", [20] = "AD05", [21] = "AD06", [22] = "AD07",
    [23] = "AD08", [24] = "AD09", [25] = "AD10", [26] = "AD11", [27] = "AD12", [28] = "RTRN", [29] = "LCTL",
    [30] = "AC01", [31] = "AC02", [32] = "AC03", [33] = "AC04", [34] = "AC05", [35] = "AC06", [36] = "AC07",
    [37] = "AC08", [38] = "AC09", [39] = "AC10", [40] = "AC11", [41] = "TLDE", [42] = "LFSH", [43] = "BKSL",
    [44] = "AB01", [45] = "AB02", [46] = "AB03", [47] = "AB04", [48] = "AB05", [49] = "AB06", [50] = "AB07",
    [51] = "AB08", [52] = "AB09", [53] = "AB10", [54] = "RTSH", [55] = "KPMU", [56] = "LALT", [57] = "SPCE",
    [58] = "CAPS", [59] = "FK01", [60] = "FK02", [61] = "FK03", [62] = "FK04", [63] = "FK05", [64] = "FK06",
    [65] = "FK07", [66] = "FK08", [67] = "FK09", [68] = "FK10", [69] = "NMLK", [70] = "SCLK", [71] = "KP7",
    [72] = "KP8", [73] = "KP9", [74] = "KPSU", [75] = "KP4", [76] = "KP5", [77] = "KP6", [78] = "KPAD",
    [79] = "KP1", [80] = "KP2", [81] = "KP3", [82] = "KP0", [83] = "KPDL", [87] = "FK11", [88] = "FK12",
    [96] = "KPEN", [97] = "RCTL", [98] = "KPDV", [100] = "RALT", [102] = "HOME", [103] = "UP", [104] = "PGUP",
    [105] = "LEFT", [106] = "RGHT", [107] = "END", [108] = "DOWN", [109] = "PGDN", [110] = "INS", [111] = "DELE",
    [125] = "LWIN", [126] = "RWIN", [127] = "COMP",
};

void xkb_put16(uint8_t *at, uint16_t value)
{
    memcpy(at, &value, sizeof(value));
}

void xkb_put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof(value));
}

static int is_lowercase_pair(uint32_t plain, uint32_t shifted)
{
    return plain >= LOWERCASE_FIRST && plain <= LOWERCASE_LAST && shifted == plain - CASE_DISTANCE;
}

uint8_t xcb_xkb_key_type(uint8_t keycode, uint8_t *levels)
{
    uint32_t plain = xcb_keysym_for(keycode, 0);
    uint32_t shifted = xcb_keysym_for(keycode, 1);
    if (plain == 0) {
        *levels = 0;
        return TYPE_ONE_LEVEL;
    }
    *levels = plain == shifted ? 1 : 2;
    if (plain == shifted) {
        return TYPE_ONE_LEVEL;
    }
    return is_lowercase_pair(plain, shifted) ? TYPE_ALPHABETIC : TYPE_TWO_LEVEL;
}

uint8_t xcb_xkb_key_modifiers(uint8_t keycode)
{
    for (size_t i = 0; i < sizeof(MODIFIER_KEYS) / sizeof(MODIFIER_KEYS[0]); i++) {
        if (MODIFIER_KEYS[i].keycode == keycode) {
            return MODIFIER_KEYS[i].mods;
        }
    }
    return 0;
}

void xcb_xkb_key_name(uint8_t keycode, char name[4])
{
    int evdev = keycode - KEYCODE_OFFSET;
    memset(name, 0, 4);
    if (evdev >= 0 && evdev < NAMED_KEYS && KEY_NAMES[evdev] != NULL) {
        memcpy(name, KEY_NAMES[evdev], strlen(KEY_NAMES[evdev]));
        return;
    }
    name[0] = 'K';
    name[1] = (char)('0' + keycode / 100);
    name[2] = (char)('0' + keycode / 10 % 10);
    name[3] = (char)('0' + keycode % 10);
}
