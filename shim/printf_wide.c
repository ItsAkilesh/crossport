/*
 * File: shim/printf_wide.c
 * Purpose: Turns the wide arguments of printf's %ls and %lc into UTF-8 text for the formatting engine
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Before this, %ls was read as a narrow string, so a wide string printed only its first character. A precision counts bytes, and a
 * character is never split: the conversion stops before the character that would not fit, as glibc does.
 */

#include "printf_internal.h"

#define MAX_UTF8_LENGTH 4
#define MAX_CODE_POINT 0x10FFFFu
#define SURROGATE_FIRST 0xD800u
#define SURROGATE_LAST 0xDFFFu

/* Encodes one wide character as UTF-8 into `out`. Returns the length, or 0 for a value that is not a character. */
size_t shim_wide_char_to_utf8(char out[MAX_UTF8_LENGTH], int32_t wide)
{
    uint32_t value = (uint32_t)wide;
    if (value > MAX_CODE_POINT || (value >= SURROGATE_FIRST && value <= SURROGATE_LAST)) {
        return 0;
    }
    if (value < 0x80) {
        out[0] = (char)value;
        return 1;
    }
    size_t length = value < 0x800 ? 2 : (value < 0x10000 ? 3 : 4);
    for (size_t i = length - 1; i > 0; i--) {
        out[i] = (char)(0x80u | (value & 0x3Fu));
        value >>= 6;
    }
    out[0] = (char)((0xF00u >> length) | value);
    return length;
}

/* The UTF-8 form of a wide string in a new heap block (free with HeapFree), cut to `precision` bytes when that is not negative. NULL on failure. */
char *shim_wide_string_to_utf8(const int32_t *text, int precision)
{
    size_t characters = 0;
    while (text[characters] != 0) {
        characters++;
    }
    char *result = (char *)HeapAlloc(GetProcessHeap(), 0, characters * MAX_UTF8_LENGTH + 1);
    if (result == NULL) {
        return NULL;
    }
    size_t used = 0;
    for (size_t i = 0; i < characters; i++) {
        char encoded[MAX_UTF8_LENGTH];
        size_t length = shim_wide_char_to_utf8(encoded, text[i]);
        if (length == 0 || (precision >= 0 && used + length > (size_t)precision)) {
            break;
        }
        for (size_t j = 0; j < length; j++) {
            result[used++] = encoded[j];
        }
    }
    result[used] = '\0';
    return result;
}
