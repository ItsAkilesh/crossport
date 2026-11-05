/*
 * File: shim/xlib/xlib_text.c
 * Purpose: XTextProperty conversion and storage: XmbTextListToTextProperty, Xutf8TextListToTextProperty, XStringListToTextProperty, XSetTextProperty
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Window titles and similar strings travel as a type (STRING or UTF8_STRING) plus bytes. A list of strings
 * becomes one block with a NUL after each string, as the ICCCM says. The shim has no locale conversion: strings
 * are taken as they are, and the type says UTF8_STRING when the caller asked for UTF-8 and STRING otherwise.
 */

#include <string.h>

#include "xlib_internal.h"

#define STATUS_SUCCESS 0
#define STATUS_ERROR 0
#define STATUS_OK 1
#define FORMAT_BYTES 8
#define MODE_REPLACE 0
#define UTF8_STRING_STYLE 4
#define COMPOUND_TEXT_STYLE 1
#define STRING_STYLE 0
#define ASCII_LIMIT 0x80

LIBC_FN int sysv_XChangeProperty(XlDisplay *display, XlId window, XlId property, XlId type, int format, int mode,
                                 const unsigned char *data, int count);

static XlId atom(const char *name)
{
    return xcb_atom_for(name, strlen(name));
}

/* Joins the strings with a NUL after each into a new malloc block; `property` is left untouched when it fails. */
static int join_strings(char **list, int count, XlId encoding, XlTextProperty *property)
{
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        total += strlen(list[i]) + 1;
    }
    unsigned char *block = (unsigned char *)sysv_malloc(total + 1);
    if (block == NULL) {
        return 0;
    }
    size_t offset = 0;
    for (int i = 0; i < count; i++) {
        size_t length = strlen(list[i]) + 1;
        memcpy(block + offset, list[i], length);
        offset += length;
    }
    block[total] = 0;
    *property = (XlTextProperty){.value = block, .encoding = encoding, .format = FORMAT_BYTES,
                                 .item_count = total > 0 ? total - 1 : 0};
    return 1;
}

static int is_invalid_list(char **list, int count)
{
    return list == NULL || count < 0;
}

LIBC_FN int sysv_XStringListToTextProperty(char **list, int count, XlTextProperty *property)
{
    return !is_invalid_list(list, count) && join_strings(list, count, atom("STRING"), property) ? STATUS_OK : STATUS_ERROR;
}

/* STRING for plain ASCII or when asked, UTF8_STRING for UTF-8 style or non-ASCII text, COMPOUND_TEXT when asked. */
static XlId encoding_for(char **list, int count, int style)
{
    int is_ascii = 1;
    for (int i = 0; i < count && list != NULL; i++) {
        for (const unsigned char *c = (const unsigned char *)list[i]; *c != 0; c++) {
            is_ascii = is_ascii && *c < ASCII_LIMIT;
        }
    }
    if (style == UTF8_STRING_STYLE || (!is_ascii && style != STRING_STYLE && style != COMPOUND_TEXT_STYLE)) {
        return atom("UTF8_STRING");
    }
    return style == COMPOUND_TEXT_STYLE ? atom("COMPOUND_TEXT") : atom("STRING");
}

static int convert_list(char **list, int count, int style, XlTextProperty *property)
{
    if (is_invalid_list(list, count)) {
        return -1;
    }
    return join_strings(list, count, encoding_for(list, count, style), property) ? STATUS_SUCCESS : -1;
}

LIBC_FN int sysv_XmbTextListToTextProperty(XlDisplay *display, char **list, int count, int style, XlTextProperty *property)
{
    (void)display;
    return convert_list(list, count, style, property);
}

LIBC_FN int sysv_Xutf8TextListToTextProperty(XlDisplay *display, char **list, int count, int style, XlTextProperty *property)
{
    (void)display;
    return convert_list(list, count, style, property);
}

LIBC_FN void sysv_XSetTextProperty(XlDisplay *display, XlId window, const XlTextProperty *property, XlId name)
{
    if (property->format == FORMAT_BYTES && property->value != NULL) {
        sysv_XChangeProperty(display, window, name, property->encoding, FORMAT_BYTES, MODE_REPLACE, property->value,
                             (int)property->item_count);
    }
}
