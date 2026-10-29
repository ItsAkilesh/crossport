/*
 * File: shim/xcb/xcb_keys.c
 * Purpose: The keyboard map: Windows scan codes to X keycodes, and X keycodes to keysyms for a US layout
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * X keycodes on Linux are evdev codes plus 8. The main block of a PC keyboard has the same numbers in
 * Windows scan codes and evdev; the keys Windows reports with the "extended" flag need a table. The
 * keysym columns are the unshifted and shifted symbols of a US layout, which is what the server reports
 * until XKB keymaps (libxkbcommon) are provided.
 */

#include "xcb_internal.h"

#define KEYCODE_OFFSET 8
#define KEY_COUNT 128

typedef struct {
    uint8_t scancode;
    uint8_t evdev;
} ExtendedKey;

static const ExtendedKey EXTENDED_KEYS[] = {
    {0x1C, 96}, {0x1D, 97}, {0x35, 98}, {0x37, 99}, {0x38, 100}, {0x47, 102}, {0x48, 103}, {0x49, 104},
    {0x4B, 105}, {0x4D, 106}, {0x4F, 107}, {0x50, 108}, {0x51, 109}, {0x52, 110}, {0x53, 111}, {0x5B, 125},
    {0x5C, 126}, {0x5D, 127},
};

uint8_t xcb_keycode_from_scancode(uint8_t scancode, int is_extended)
{
    if (is_extended) {
        for (size_t i = 0; i < sizeof(EXTENDED_KEYS) / sizeof(EXTENDED_KEYS[0]); i++) {
            if (EXTENDED_KEYS[i].scancode == scancode) {
                return (uint8_t)(EXTENDED_KEYS[i].evdev + KEYCODE_OFFSET);
            }
        }
    }
    return (uint8_t)(scancode + KEYCODE_OFFSET);
}

/* Keysyms by evdev code: { unshifted, shifted }. Zero means no symbol. */
static const uint32_t SYMBOLS[KEY_COUNT][2] = {
    [1] = {0xff1b, 0xff1b},
    [2] = {'1', '!'}, [3] = {'2', '@'}, [4] = {'3', '#'}, [5] = {'4', '$'}, [6] = {'5', '%'}, [7] = {'6', '^'},
    [8] = {'7', '&'}, [9] = {'8', '*'}, [10] = {'9', '('}, [11] = {'0', ')'}, [12] = {'-', '_'}, [13] = {'=', '+'},
    [14] = {0xff08, 0xff08}, [15] = {0xff09, 0xfe20},
    [16] = {'q', 'Q'}, [17] = {'w', 'W'}, [18] = {'e', 'E'}, [19] = {'r', 'R'}, [20] = {'t', 'T'}, [21] = {'y', 'Y'},
    [22] = {'u', 'U'}, [23] = {'i', 'I'}, [24] = {'o', 'O'}, [25] = {'p', 'P'}, [26] = {'[', '{'}, [27] = {']', '}'},
    [28] = {0xff0d, 0xff0d}, [29] = {0xffe3, 0xffe3},
    [30] = {'a', 'A'}, [31] = {'s', 'S'}, [32] = {'d', 'D'}, [33] = {'f', 'F'}, [34] = {'g', 'G'}, [35] = {'h', 'H'},
    [36] = {'j', 'J'}, [37] = {'k', 'K'}, [38] = {'l', 'L'}, [39] = {';', ':'}, [40] = {'\'', '"'}, [41] = {'`', '~'},
    [42] = {0xffe1, 0xffe1}, [43] = {'\\', '|'},
    [44] = {'z', 'Z'}, [45] = {'x', 'X'}, [46] = {'c', 'C'}, [47] = {'v', 'V'}, [48] = {'b', 'B'}, [49] = {'n', 'N'},
    [50] = {'m', 'M'}, [51] = {',', '<'}, [52] = {'.', '>'}, [53] = {'/', '?'}, [54] = {0xffe2, 0xffe2},
    [55] = {0xffaa, 0xffaa}, [56] = {0xffe9, 0xffe9}, [57] = {' ', ' '}, [58] = {0xffe5, 0xffe5},
    [59] = {0xffbe, 0xffbe}, [60] = {0xffbf, 0xffbf}, [61] = {0xffc0, 0xffc0}, [62] = {0xffc1, 0xffc1},
    [63] = {0xffc2, 0xffc2}, [64] = {0xffc3, 0xffc3}, [65] = {0xffc4, 0xffc4}, [66] = {0xffc5, 0xffc5},
    [67] = {0xffc6, 0xffc6}, [68] = {0xffc7, 0xffc7}, [69] = {0xff7f, 0xff7f}, [70] = {0xff14, 0xff14},
    [71] = {0xffb7, 0xffb7}, [72] = {0xffb8, 0xffb8}, [73] = {0xffb9, 0xffb9}, [74] = {0xffad, 0xffad},
    [75] = {0xffb4, 0xffb4}, [76] = {0xffb5, 0xffb5}, [77] = {0xffb6, 0xffb6}, [78] = {0xffab, 0xffab},
    [79] = {0xffb1, 0xffb1}, [80] = {0xffb2, 0xffb2}, [81] = {0xffb3, 0xffb3}, [82] = {0xffb0, 0xffb0},
    [83] = {0xffae, 0xffae}, [87] = {0xffc8, 0xffc8}, [88] = {0xffc9, 0xffc9},
    [96] = {0xff8d, 0xff8d}, [97] = {0xffe4, 0xffe4}, [98] = {0xffaf, 0xffaf}, [100] = {0xffea, 0xffea},
    [102] = {0xff50, 0xff50}, [103] = {0xff52, 0xff52}, [104] = {0xff55, 0xff55}, [105] = {0xff51, 0xff51},
    [106] = {0xff53, 0xff53}, [107] = {0xff57, 0xff57}, [108] = {0xff54, 0xff54}, [109] = {0xff56, 0xff56},
    [110] = {0xff63, 0xff63}, [111] = {0xffff, 0xffff}, [125] = {0xffeb, 0xffeb}, [126] = {0xffec, 0xffec},
    [127] = {0xff67, 0xff67},
};

uint32_t xcb_keysym_for(uint8_t keycode, int column)
{
    int evdev = keycode - KEYCODE_OFFSET;
    if (evdev < 0 || evdev >= KEY_COUNT || column < 0 || column > 1) {
        return 0;
    }
    return SYMBOLS[evdev][column];
}
