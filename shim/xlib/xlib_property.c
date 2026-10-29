/*
 * File: shim/xlib/xlib_property.c
 * Purpose: Window properties: XChangeProperty, XGetWindowProperty, XDeleteProperty and the PropertyNotify events they cause
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Properties are kept in one table keyed by window and atom, in the form the wire protocol uses (format-32
 * items are 4 bytes). Programs see Xlib's form: format-32 items are C longs of 8 bytes. A property written
 * with the title or protocols atoms is also passed to the xcb layer, which acts on it (window title,
 * WM_DELETE_WINDOW).
 */

#include <string.h>

#include "xlib_internal.h"

#define MODE_REPLACE 0
#define MODE_PREPEND 1
#define FORMAT_8 8
#define FORMAT_16 16
#define FORMAT_32 32
#define ANY_PROPERTY_TYPE 0
#define SUCCESS 0
#define WORD_BYTES 4
#define PROPERTY_NEW_VALUE 0
#define PROPERTY_DELETE 1
#define BITS_PER_BYTE 8

LIBC_FN void *sysv_malloc(size_t size);

typedef struct {
    XlId window;
    XlId atom;
    XlId type;
    int32_t format;
    size_t size;
    uint8_t *data;
} Property;

static Property *g_properties;
static size_t g_count;
static size_t g_capacity;

static Property *find_property(XlId window, XlId atom)
{
    for (size_t i = 0; i < g_count; i++) {
        if (g_properties[i].window == window && g_properties[i].atom == atom) {
            return &g_properties[i];
        }
    }
    return NULL;
}

static Property *add_property(XlId window, XlId atom)
{
    if (g_count == g_capacity) {
        size_t capacity = g_capacity == 0 ? 32 : g_capacity * 2;
        Property *grown = (Property *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(Property));
        if (grown == NULL) {
            return NULL;
        }
        if (g_properties != NULL) {
            memcpy(grown, g_properties, g_count * sizeof(Property));
            HeapFree(GetProcessHeap(), 0, g_properties);
        }
        g_properties = grown;
        g_capacity = capacity;
    }
    g_properties[g_count] = (Property){.window = window, .atom = atom};
    return &g_properties[g_count++];
}

static void remove_property(Property *property)
{
    HeapFree(GetProcessHeap(), 0, property->data);
    *property = g_properties[--g_count];
}

/* Items as they are stored: format-32 data arrives as 8-byte longs and is narrowed to 4 bytes. */
static uint8_t *wire_form(const unsigned char *data, int format, int count, size_t *size)
{
    size_t item_bytes = (size_t)format / BITS_PER_BYTE;
    *size = (size_t)count * item_bytes;
    uint8_t *wire = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, *size + 1);
    if (wire == NULL) {
        return NULL;
    }
    if (format == FORMAT_32) {
        for (int i = 0; i < count; i++) {
            uint32_t value = (uint32_t)((const uint64_t *)data)[i];
            memcpy(wire + (size_t)i * WORD_BYTES, &value, WORD_BYTES);
        }
    } else {
        memcpy(wire, data, *size);
    }
    return wire;
}

static SYSV void notify(XlDisplay *display, XlId window, XlId atom, int state)
{
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    int is_selected = drawable != NULL && (drawable->event_mask & XL_MASK_PROPERTY) != 0;
    xl_unlock();
    if (is_selected) {
        XlEvent event = {0};
        event.property = (XlPropertyEvent){.type = XL_PROPERTY_NOTIFY, .serial = display->last_request_read,
                                           .display = display, .window = window, .atom = atom,
                                           .time = GetTickCount(), .state = state};
        xl_events_push_back(display, &event);
    }
}

/* Stores `wire` into the property under the mode; returns 0 when the new data does not fit the old property. */
static int store(XlId window, XlId property, XlId type, int format, int mode, uint8_t *wire, size_t size)
{
    Property *entry = find_property(window, property);
    if (entry == NULL) {
        entry = add_property(window, property);
        mode = MODE_REPLACE;
    } else if (mode != MODE_REPLACE && (entry->type != type || entry->format != format)) {
        return 0;
    }
    if (entry == NULL) {
        return 0;
    }
    size_t old_size = mode == MODE_REPLACE ? 0 : entry->size;
    uint8_t *merged = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, old_size + size + 1);
    if (merged == NULL) {
        return 0;
    }
    if (mode == MODE_PREPEND) {
        memcpy(merged, wire, size);
        memcpy(merged + size, entry->data, old_size);
    } else {
        if (old_size > 0) {
            memcpy(merged, entry->data, old_size);
        }
        memcpy(merged + old_size, wire, size);
    }
    HeapFree(GetProcessHeap(), 0, entry->data);
    *entry = (Property){.window = window, .atom = property, .type = type, .format = format, .size = old_size + size,
                        .data = merged};
    return 1;
}

LIBC_FN int sysv_XChangeProperty(XlDisplay *display, XlId window, XlId property, XlId type, int format, int mode,
                                 const unsigned char *data, int count)
{
    xl_next_request(display);
    size_t size = 0;
    uint8_t *wire = (format == FORMAT_8 || format == FORMAT_16 || format == FORMAT_32) && count >= 0
                        ? wire_form(data, format, count, &size) : NULL;
    if (wire == NULL) {
        return 1;
    }
    xl_lock();
    int is_stored = store(window, property, type, format, mode, wire, size);
    xl_unlock();
    if (is_stored) {
        sysv_xcb_change_property(NULL, (uint8_t)mode, (XcbId)window, (XcbId)property, (XcbId)type, (uint8_t)format,
                                 (uint32_t)count, wire);
        notify(display, window, property, PROPERTY_NEW_VALUE);
    }
    HeapFree(GetProcessHeap(), 0, wire);
    return 1;
}

/* The bytes handed to the program: the wire form, with format-32 items widened to longs, and a trailing NUL. */
static SYSV unsigned char *client_form(const Property *entry, size_t offset, size_t length, size_t *item_count)
{
    size_t item_bytes = (size_t)entry->format / BITS_PER_BYTE;
    *item_count = length / item_bytes;
    size_t out_item = entry->format == FORMAT_32 ? sizeof(uint64_t) : item_bytes;
    unsigned char *out = (unsigned char *)sysv_malloc(*item_count * out_item + 1);
    if (out == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < *item_count; i++) {
        if (entry->format == FORMAT_32) {
            uint32_t value;
            memcpy(&value, entry->data + offset + i * WORD_BYTES, WORD_BYTES);
            uint64_t widened = value;
            memcpy(out + i * sizeof(widened), &widened, sizeof(widened));
        } else {
            memcpy(out + i * item_bytes, entry->data + offset + i * item_bytes, item_bytes);
        }
    }
    out[*item_count * out_item] = '\0';
    return out;
}

LIBC_FN int sysv_XGetWindowProperty(XlDisplay *display, XlId window, XlId property, int64_t offset, int64_t length,
                                    int should_delete, XlId required_type, XlId *actual_type, int *actual_format,
                                    uint64_t *item_count, uint64_t *bytes_after, unsigned char **data)
{
    xl_next_request(display);
    *actual_type = 0;
    *actual_format = 0;
    *item_count = 0;
    *bytes_after = 0;
    *data = NULL;
    int has_deleted = 0;
    xl_lock();
    Property *entry = find_property(window, property);
    if (entry != NULL) {
        *actual_type = entry->type;
        *actual_format = entry->format;
        size_t start = (size_t)offset * WORD_BYTES;
        size_t available = start < entry->size ? entry->size - start : 0;
        int is_wrong_type = required_type != ANY_PROPERTY_TYPE && required_type != entry->type;
        size_t wanted = (size_t)length * WORD_BYTES;
        size_t taken = is_wrong_type ? 0 : (wanted < available ? wanted : available);
        taken -= taken % ((size_t)entry->format / BITS_PER_BYTE);
        /* Xorg reports the property's size in items, not bytes, when the requested type does not match. */
        *bytes_after = is_wrong_type ? entry->size / ((size_t)entry->format / BITS_PER_BYTE) : available - taken;
        if (!is_wrong_type) {
            size_t count = 0;
            *data = client_form(entry, start < entry->size ? start : entry->size, taken, &count);
            *item_count = count;
            if (should_delete && *bytes_after == 0) {
                remove_property(entry);
                has_deleted = 1;
            }
        }
    }
    xl_unlock();
    if (has_deleted) {
        notify(display, window, property, PROPERTY_DELETE);
    }
    return SUCCESS;
}

LIBC_FN int sysv_XDeleteProperty(XlDisplay *display, XlId window, XlId property)
{
    xl_next_request(display);
    xl_lock();
    Property *entry = find_property(window, property);
    if (entry != NULL) {
        remove_property(entry);
    }
    int is_deleted = entry != NULL;
    xl_unlock();
    if (is_deleted) {
        notify(display, window, property, PROPERTY_DELETE);
    }
    return 1;
}
