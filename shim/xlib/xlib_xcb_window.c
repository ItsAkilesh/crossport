/*
 * File: shim/xlib/xlib_xcb_window.c
 * Purpose: xcb requests about windows, properties, atoms, selections, the pointer, focus, grabs and the keyboard
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * The xcb functions a toolkit's platform plugin calls, built on the same tables the Xlib layer keeps (properties,
 * drawables). Each request is carried out at once and its reply is kept for the program to collect (see
 * xlib_xcb_core.c). Selections are tracked by owner only: a conversion is answered as "no owner" unless this
 * process owns the selection, so a toolkit's clipboard stays usable but empty.
 */

#include <string.h>

#include "xlib_internal.h"

#define ANY_PROPERTY_TYPE 0
#define WORD_BYTES 4
#define MAX_SELECTIONS 16
#define BAD_WINDOW_ERROR 3
#define MAP_UNMAPPED 0
#define MAP_VIEWABLE 2
#define INPUT_OUTPUT_CLASS 1
#define NORTH_WEST_GRAVITY 1
#define GRAB_SUCCESS 0
#define SEND_EVENT_FLAG 0x80
#define PROPERTY_NOTIFY_EVENT 28
#define SELECTION_NOTIFY_EVENT 31
#define PROPERTY_NEW_VALUE 0
#define PROPERTY_DELETED 1
#define KEYSYMS_PER_KEYCODE 2
#define MODIFIER_ROWS 8
#define KEYCODES_PER_MODIFIER 2
#define ERROR_BYTES 32

LIBC_FN XcbCookie sysv_xcb_get_geometry(void *connection, XcbId drawable);

/* A generic error (xcb_generic_error_t, 32 bytes) for a request that named something that does not exist. */
static void report_error(void **error, uint8_t code, XcbId resource)
{
    if (error == NULL) {
        return;
    }
    uint8_t *block = (uint8_t *)sysv_malloc(ERROR_BYTES);
    if (block != NULL) {
        memset(block, 0, ERROR_BYTES);
        block[1] = code;
        memcpy(block + 4, &resource, sizeof(resource));
    }
    *error = block;
}

static XcbCookie keep_or_error(void *reply)
{
    return xcb_reply_keep(reply);
}

/* ------------------------------------------------------------------ window attributes */

LIBC_FN XcbCookie sysv_xcb_get_window_attributes(void *connection, XcbId window)
{
    (void)connection;
    XcbWindowAttributesReply *reply = (XcbWindowAttributesReply *)xcb_reply_allocate(sizeof(*reply), 0);
    if (reply == NULL) {
        return keep_or_error(NULL);
    }
    HWND handle = xcb_window_handle(window);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    int64_t mask = drawable != NULL ? drawable->event_mask : 0;
    xl_unlock();
    reply->visual = xcb_roots()->screens[0].root_visual;
    reply->window_class = INPUT_OUTPUT_CLASS;
    reply->bit_gravity = 0;
    reply->win_gravity = NORTH_WEST_GRAVITY;
    reply->map_state = (handle != NULL && IsWindowVisible(handle)) ? MAP_VIEWABLE : MAP_UNMAPPED;
    reply->colormap = xcb_roots()->screens[0].default_colormap;
    reply->all_event_masks = (uint32_t)mask;
    reply->your_event_mask = (uint32_t)mask;
    return keep_or_error(handle != NULL || window == XCB_ROOT_WINDOW_ID ? reply : (sysv_free(reply), NULL));
}

LIBC_FN XcbCookie sysv_xcb_get_window_attributes_unchecked(void *connection, XcbId window)
{
    return sysv_xcb_get_window_attributes(connection, window);
}

LIBC_FN XcbWindowAttributesReply *sysv_xcb_get_window_attributes_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    XcbWindowAttributesReply *reply = (XcbWindowAttributesReply *)xcb_reply_take(cookie, error);
    if (reply == NULL) {
        report_error(error, BAD_WINDOW_ERROR, 0);
    }
    return reply;
}

/* Only the event mask matters to the shim; the other values of the list are read past. */
static void apply_attribute_values(XcbId window, uint32_t mask, const uint32_t *values)
{
    const uint32_t event_mask_bit = 1u << 11;
    if ((mask & event_mask_bit) == 0) {
        return;
    }
    size_t position = 0;
    for (uint32_t bit = 0; bit < 11; bit++) {
        position += (mask >> bit) & 1u;
    }
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    if (drawable != NULL) {
        drawable->event_mask = values[position];
    }
    xl_unlock();
}

LIBC_FN XcbCookie sysv_xcb_change_window_attributes(void *connection, XcbId window, uint32_t mask, const void *values)
{
    (void)connection;
    apply_attribute_values(window, mask, (const uint32_t *)values);
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_change_window_attributes_checked(void *connection, XcbId window, uint32_t mask,
                                                            const void *values)
{
    return sysv_xcb_change_window_attributes(connection, window, mask, values);
}

/* The window a toolkit asks for gets its pixel buffer here; Xlib's XCreateWindow does the same itself. */
LIBC_FN XcbCookie sysv_xcb_create_window(void *connection, uint8_t depth, XcbId window, XcbId parent, int16_t x, int16_t y,
                                         uint16_t width, uint16_t height, uint16_t border_width, uint16_t window_class,
                                         XcbId visual, uint32_t value_mask, const void *value_list)
{
    XcbCookie cookie = xcb_window_create(connection, depth, window, parent, x, y, width, height, border_width,
                                         window_class, visual, value_mask, value_list);
    if (xcb_window_handle(window) != NULL) {
        xl_lock();
        if (xl_drawable_find(window) == NULL) {
            xl_drawable_add(window, 1, depth > 0 ? depth : XL_ROOT_DEPTH, width > 0 ? width : 1, height > 0 ? height : 1, 0);
        }
        xl_unlock();
        apply_attribute_values(window, value_mask, (const uint32_t *)value_list);
    }
    return cookie;
}

LIBC_FN XcbCookie sysv_xcb_create_window_checked(void *connection, uint8_t depth, XcbId window, XcbId parent, int16_t x,
                                                 int16_t y, uint16_t width, uint16_t height, uint16_t border_width,
                                                 uint16_t window_class, XcbId visual, uint32_t value_mask,
                                                 const void *value_list)
{
    return sysv_xcb_create_window(connection, depth, window, parent, x, y, width, height, border_width, window_class,
                                  visual, value_mask, value_list);
}

LIBC_FN XcbCookie sysv_xcb_reparent_window(void *connection, XcbId window, XcbId parent, int16_t x, int16_t y)
{
    (void)connection;
    (void)window;
    (void)parent;
    (void)x;
    (void)y;
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

/* ------------------------------------------------------------------ the tree, the pointer, coordinates */

LIBC_FN XcbCookie sysv_xcb_query_tree(void *connection, XcbId window)
{
    (void)connection;
    XcbQueryTreeReply *reply = (XcbQueryTreeReply *)xcb_reply_allocate(sizeof(*reply), 0);
    if (reply != NULL) {
        reply->root = XCB_ROOT_WINDOW_ID;
        reply->parent = window == XCB_ROOT_WINDOW_ID ? 0 : XCB_ROOT_WINDOW_ID;
    }
    return xcb_reply_keep(reply);
}

LIBC_FN XcbCookie sysv_xcb_query_tree_unchecked(void *connection, XcbId window)
{
    return sysv_xcb_query_tree(connection, window);
}

LIBC_FN XcbQueryTreeReply *sysv_xcb_query_tree_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbQueryTreeReply *)xcb_reply_take(cookie, error);
}

LIBC_FN XcbId *sysv_xcb_query_tree_children(const XcbQueryTreeReply *reply)
{
    return (XcbId *)(reply + 1);
}

LIBC_FN int sysv_xcb_query_tree_children_length(const XcbQueryTreeReply *reply)
{
    return reply->children_len;
}

LIBC_FN XcbCookie sysv_xcb_query_pointer(void *connection, XcbId window)
{
    (void)connection;
    XcbQueryPointerReply *reply = (XcbQueryPointerReply *)xcb_reply_allocate(sizeof(*reply), 0);
    if (reply != NULL) {
        POINT screen_point = {0, 0};
        GetCursorPos(&screen_point);
        POINT local = screen_point;
        HWND handle = xcb_window_handle(window);
        if (handle != NULL) {
            ScreenToClient(handle, &local);
        }
        reply->same_screen = 1;
        reply->root = XCB_ROOT_WINDOW_ID;
        reply->root_x = (int16_t)screen_point.x;
        reply->root_y = (int16_t)screen_point.y;
        reply->win_x = (int16_t)local.x;
        reply->win_y = (int16_t)local.y;
        reply->mask = xcb_input_state();
    }
    return xcb_reply_keep(reply);
}

LIBC_FN XcbQueryPointerReply *sysv_xcb_query_pointer_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbQueryPointerReply *)xcb_reply_take(cookie, error);
}

LIBC_FN XcbCookie sysv_xcb_translate_coordinates(void *connection, XcbId source, XcbId target, int16_t x, int16_t y)
{
    (void)connection;
    XcbTranslateCoordinatesReply *reply = (XcbTranslateCoordinatesReply *)xcb_reply_allocate(sizeof(*reply), 0);
    if (reply != NULL) {
        POINT point = {x, y};
        HWND from = source == XCB_ROOT_WINDOW_ID ? NULL : xcb_window_handle(source);
        HWND to = target == XCB_ROOT_WINDOW_ID ? NULL : xcb_window_handle(target);
        if (from != NULL) {
            ClientToScreen(from, &point);
        }
        if (to != NULL) {
            ScreenToClient(to, &point);
        }
        reply->same_screen = 1;
        reply->dst_x = (int16_t)point.x;
        reply->dst_y = (int16_t)point.y;
    }
    return xcb_reply_keep(reply);
}

LIBC_FN XcbCookie sysv_xcb_translate_coordinates_unchecked(void *connection, XcbId source, XcbId target, int16_t x,
                                                           int16_t y)
{
    return sysv_xcb_translate_coordinates(connection, source, target, x, y);
}

LIBC_FN XcbTranslateCoordinatesReply *sysv_xcb_translate_coordinates_reply(void *connection, XcbCookie cookie,
                                                                           void **error)
{
    (void)connection;
    return (XcbTranslateCoordinatesReply *)xcb_reply_take(cookie, error);
}

LIBC_FN XcbCookie sysv_xcb_get_geometry_unchecked(void *connection, XcbId drawable)
{
    return sysv_xcb_get_geometry(connection, drawable);
}

LIBC_FN XcbCookie sysv_xcb_warp_pointer(void *connection, XcbId source, XcbId target, int16_t sx, int16_t sy,
                                        uint16_t sw, uint16_t sh, int16_t dx, int16_t dy)
{
    (void)connection;
    (void)source;
    (void)sx;
    (void)sy;
    (void)sw;
    (void)sh;
    POINT point = {dx, dy};
    HWND to = target != 0 ? xcb_window_handle(target) : NULL;
    if (to != NULL) {
        ClientToScreen(to, &point);
        SetCursorPos(point.x, point.y);
    } else if (target == 0) {
        POINT current;
        GetCursorPos(&current);
        SetCursorPos(current.x + dx, current.y + dy);
    }
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

/* ------------------------------------------------------------------ focus and grabs */

LIBC_FN XcbCookie sysv_xcb_set_input_focus(void *connection, uint8_t revert_to, XcbId focus, uint32_t time)
{
    (void)connection;
    (void)revert_to;
    (void)time;
    HWND handle = xcb_window_handle(focus);
    if (handle != NULL) {
        xcb_ui_set_focus(handle);
    }
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_get_input_focus(void *connection)
{
    (void)connection;
    XcbInputFocusReply *reply = (XcbInputFocusReply *)xcb_reply_allocate(sizeof(*reply), 0);
    if (reply != NULL) {
        reply->revert_to = 1;
        reply->focus = xcb_window_id(GetForegroundWindow());
    }
    return xcb_reply_keep(reply);
}

LIBC_FN XcbInputFocusReply *sysv_xcb_get_input_focus_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbInputFocusReply *)xcb_reply_take(cookie, error);
}

static XcbCookie keep_grab_success(void)
{
    return xcb_reply_keep(xcb_reply_allocate(sizeof(XcbGrabReply), 0));
}

LIBC_FN XcbCookie sysv_xcb_grab_pointer(void *connection, uint8_t owner_events, XcbId window, uint16_t mask,
                                        uint8_t pointer_mode, uint8_t keyboard_mode, XcbId confine_to, XcbId cursor,
                                        uint32_t time)
{
    (void)connection;
    (void)owner_events;
    (void)mask;
    (void)pointer_mode;
    (void)keyboard_mode;
    (void)confine_to;
    (void)cursor;
    (void)time;
    HWND handle = xcb_window_handle(window);
    if (handle != NULL) {
        xcb_ui_set_capture(handle);
    }
    return keep_grab_success();
}

LIBC_FN XcbCookie sysv_xcb_ungrab_pointer(void *connection, uint32_t time)
{
    (void)connection;
    (void)time;
    xcb_ui_release_capture();
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_grab_keyboard(void *connection, uint8_t owner_events, XcbId window, uint32_t time,
                                         uint8_t pointer_mode, uint8_t keyboard_mode)
{
    (void)connection;
    (void)owner_events;
    (void)window;
    (void)time;
    (void)pointer_mode;
    (void)keyboard_mode;
    return keep_grab_success();
}

LIBC_FN XcbGrabReply *sysv_xcb_grab_pointer_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbGrabReply *)xcb_reply_take(cookie, error);
}

LIBC_FN XcbGrabReply *sysv_xcb_grab_keyboard_reply(void *connection, XcbCookie cookie, void **error)
{
    return sysv_xcb_grab_pointer_reply(connection, cookie, error);
}

LIBC_FN XcbCookie sysv_xcb_ungrab_keyboard(void *connection, uint32_t time)
{
    (void)connection;
    (void)time;
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_grab_server(void *connection)
{
    return sysv_xcb_no_operation(connection);
}

LIBC_FN XcbCookie sysv_xcb_ungrab_server(void *connection)
{
    return sysv_xcb_no_operation(connection);
}

LIBC_FN XcbCookie sysv_xcb_bell(void *connection, int8_t percent)
{
    (void)percent;
    MessageBeep(MB_OK);
    return sysv_xcb_no_operation(connection);
}

LIBC_FN XcbCookie sysv_xcb_change_keyboard_control(void *connection, uint32_t mask, const void *values)
{
    (void)mask;
    (void)values;
    return sysv_xcb_no_operation(connection);
}

/* ------------------------------------------------------------------ keyboard maps */

LIBC_FN XcbCookie sysv_xcb_get_keyboard_mapping(void *connection, uint8_t first_keycode, uint8_t count)
{
    (void)connection;
    size_t bytes = (size_t)count * KEYSYMS_PER_KEYCODE * sizeof(uint32_t);
    XcbKeyboardMappingReply *reply = (XcbKeyboardMappingReply *)xcb_reply_allocate(sizeof(*reply), bytes);
    if (reply != NULL) {
        reply->keysyms_per_keycode = KEYSYMS_PER_KEYCODE;
        uint32_t *symbols = (uint32_t *)(reply + 1);
        for (uint32_t i = 0; i < count; i++) {
            for (int column = 0; column < KEYSYMS_PER_KEYCODE; column++) {
                symbols[i * KEYSYMS_PER_KEYCODE + column] = xcb_keysym_for((uint8_t)(first_keycode + i), column);
            }
        }
    }
    return xcb_reply_keep(reply);
}

LIBC_FN XcbKeyboardMappingReply *sysv_xcb_get_keyboard_mapping_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbKeyboardMappingReply *)xcb_reply_take(cookie, error);
}

LIBC_FN uint32_t *sysv_xcb_get_keyboard_mapping_keysyms(const XcbKeyboardMappingReply *reply)
{
    return (uint32_t *)(reply + 1);
}

LIBC_FN int sysv_xcb_get_keyboard_mapping_keysyms_length(const XcbKeyboardMappingReply *reply)
{
    return (int)reply->length;
}

LIBC_FN XcbCookie sysv_xcb_get_modifier_mapping(void *connection)
{
    (void)connection;
    size_t bytes = (size_t)MODIFIER_ROWS * KEYCODES_PER_MODIFIER;
    XcbModifierMappingReply *reply = (XcbModifierMappingReply *)xcb_reply_allocate(sizeof(*reply), bytes);
    if (reply != NULL) {
        reply->keycodes_per_modifier = KEYCODES_PER_MODIFIER;
        memcpy(reply + 1, xl_modifier_keys(), bytes);
    }
    return xcb_reply_keep(reply);
}

LIBC_FN XcbModifierMappingReply *sysv_xcb_get_modifier_mapping_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbModifierMappingReply *)xcb_reply_take(cookie, error);
}

LIBC_FN uint8_t *sysv_xcb_get_modifier_mapping_keycodes(const XcbModifierMappingReply *reply)
{
    return (uint8_t *)(reply + 1);
}

LIBC_FN int sysv_xcb_get_modifier_mapping_keycodes_length(const XcbModifierMappingReply *reply)
{
    return reply->keycodes_per_modifier * MODIFIER_ROWS;
}

/* ------------------------------------------------------------------ atoms */

LIBC_FN XcbCookie sysv_xcb_get_atom_name(void *connection, XcbId atom)
{
    (void)connection;
    const char *name = xcb_atom_name(atom);
    size_t length = name != NULL ? strlen(name) : 0;
    XcbAtomNameReply *reply = (XcbAtomNameReply *)xcb_reply_allocate(sizeof(*reply), length);
    if (reply != NULL) {
        reply->name_len = (uint16_t)length;
        memcpy(reply + 1, name, length);
    }
    return xcb_reply_keep(name != NULL ? reply : (sysv_free(reply), NULL));
}

LIBC_FN XcbAtomNameReply *sysv_xcb_get_atom_name_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbAtomNameReply *)xcb_reply_take(cookie, error);
}

LIBC_FN char *sysv_xcb_get_atom_name_name(const XcbAtomNameReply *reply)
{
    return (char *)(reply + 1);
}

LIBC_FN int sysv_xcb_get_atom_name_name_length(const XcbAtomNameReply *reply)
{
    return reply->name_len;
}

/* ------------------------------------------------------------------ properties */

static void send_property_notify(XcbId window, XcbId atom, uint8_t state)
{
    XcbEvent event = {0};
    event.response_type = PROPERTY_NOTIFY_EVENT;
    memcpy(event.body, &window, sizeof(window));
    memcpy(event.body + 4, &atom, sizeof(atom));
    uint32_t time = GetTickCount();
    memcpy(event.body + 8, &time, sizeof(time));
    event.body[12] = state;
    xcb_events_push(&event);
}

/* Store, then act on the title and protocols: the form a toolkit uses, which the Xlib layer reaches differently. */
LIBC_FN XcbCookie sysv_xcb_change_property(void *connection, uint8_t mode, XcbId window, XcbId property, XcbId type,
                                           uint8_t format, uint32_t length, const void *data)
{
    (void)connection;
    size_t bytes = (size_t)length * (format / 8u);
    if (xl_property_store(window, property, type, format, mode, (const uint8_t *)data, bytes)) {
        send_property_notify(window, property, PROPERTY_NEW_VALUE);
    }
    xcb_property_effects(window, property, type, format, length, data);
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_change_property_checked(void *connection, uint8_t mode, XcbId window, XcbId property,
                                                   XcbId type, uint8_t format, uint32_t length, const void *data)
{
    return sysv_xcb_change_property(connection, mode, window, property, type, format, length, data);
}

LIBC_FN XcbCookie sysv_xcb_delete_property(void *connection, XcbId window, XcbId property)
{
    (void)connection;
    if (xl_property_delete(window, property)) {
        send_property_notify(window, property, PROPERTY_DELETED);
    }
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

/* The reply of GetProperty: the slice asked for, as the wire protocol defines it (offsets and lengths in 4-byte units). */
LIBC_FN XcbCookie sysv_xcb_get_property(void *connection, uint8_t should_delete, XcbId window, XcbId property,
                                        XcbId type, uint32_t long_offset, uint32_t long_length)
{
    (void)connection;
    XlPropertyCopy copy;
    int is_found = xl_property_snapshot(window, property, &copy);
    size_t start = (size_t)long_offset * WORD_BYTES;
    size_t available = is_found && start < copy.size ? copy.size - start : 0;
    int is_wrong_type = is_found && type != ANY_PROPERTY_TYPE && type != copy.type;
    size_t taken = (is_found && !is_wrong_type) ? ((size_t)long_length * WORD_BYTES < available ? (size_t)long_length * WORD_BYTES : available) : 0;
    XcbPropertyReply *reply = (XcbPropertyReply *)xcb_reply_allocate(sizeof(*reply), taken);
    if (reply != NULL && is_found) {
        reply->format = (uint8_t)copy.format;
        reply->type = copy.type;
        /* Xorg reports the whole property in items, not bytes, when the requested type does not match. */
        reply->bytes_after = (uint32_t)(is_wrong_type ? copy.size / (copy.format / 8u) : available - taken);
        reply->value_len = (uint32_t)(taken / (copy.format / 8u));
        memcpy(reply + 1, copy.data + start, taken);
    }
    int should_remove = should_delete && is_found && !is_wrong_type && available == taken;
    sysv_free(copy.data);
    if (should_remove && xl_property_delete(window, property)) {
        send_property_notify(window, property, PROPERTY_DELETED);
    }
    return xcb_reply_keep(reply);
}

LIBC_FN XcbCookie sysv_xcb_get_property_unchecked(void *connection, uint8_t should_delete, XcbId window, XcbId property,
                                                  XcbId type, uint32_t long_offset, uint32_t long_length)
{
    return sysv_xcb_get_property(connection, should_delete, window, property, type, long_offset, long_length);
}

LIBC_FN XcbPropertyReply *sysv_xcb_get_property_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbPropertyReply *)xcb_reply_take(cookie, error);
}

LIBC_FN void *sysv_xcb_get_property_value(const XcbPropertyReply *reply)
{
    return (void *)(reply + 1);
}

LIBC_FN int sysv_xcb_get_property_value_length(const XcbPropertyReply *reply)
{
    return (int)(reply->value_len * (reply->format / 8u));
}

/* ------------------------------------------------------------------ selections and events */

typedef struct {
    XcbId selection;
    XcbId owner;
} SelectionOwner;

static SelectionOwner g_selections[MAX_SELECTIONS];
static SRWLOCK g_selection_lock = SRWLOCK_INIT;

LIBC_FN XcbCookie sysv_xcb_set_selection_owner(void *connection, XcbId owner, XcbId selection, uint32_t time)
{
    (void)connection;
    (void)time;
    AcquireSRWLockExclusive(&g_selection_lock);
    SelectionOwner *slot = NULL;
    for (size_t i = 0; i < MAX_SELECTIONS; i++) {
        if (g_selections[i].selection == selection) {
            slot = &g_selections[i];
        } else if (slot == NULL && g_selections[i].selection == 0) {
            slot = &g_selections[i];
        }
    }
    if (slot != NULL) {
        *slot = (SelectionOwner){.selection = selection, .owner = owner};
    }
    ReleaseSRWLockExclusive(&g_selection_lock);
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_get_selection_owner(void *connection, XcbId selection)
{
    (void)connection;
    XcbSelectionOwnerReply *reply = (XcbSelectionOwnerReply *)xcb_reply_allocate(sizeof(*reply), 0);
    if (reply != NULL) {
        AcquireSRWLockShared(&g_selection_lock);
        for (size_t i = 0; i < MAX_SELECTIONS; i++) {
            reply->owner = g_selections[i].selection == selection ? g_selections[i].owner : reply->owner;
        }
        ReleaseSRWLockShared(&g_selection_lock);
    }
    return xcb_reply_keep(reply);
}

LIBC_FN XcbSelectionOwnerReply *sysv_xcb_get_selection_owner_reply(void *connection, XcbCookie cookie, void **error)
{
    (void)connection;
    return (XcbSelectionOwnerReply *)xcb_reply_take(cookie, error);
}

/* Nobody here can answer, so the requester is told at once that the selection has no content for it. */
LIBC_FN XcbCookie sysv_xcb_convert_selection(void *connection, XcbId requestor, XcbId selection, XcbId target,
                                             XcbId property, uint32_t time)
{
    (void)connection;
    XcbEvent event = {0};
    event.response_type = SELECTION_NOTIFY_EVENT;
    memcpy(event.body, &time, sizeof(time));
    memcpy(event.body + 4, &requestor, sizeof(requestor));
    memcpy(event.body + 8, &selection, sizeof(selection));
    memcpy(event.body + 12, &target, sizeof(target));
    (void)property;
    xcb_events_push(&event);
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_send_event(void *connection, uint8_t propagate, XcbId destination, uint32_t mask,
                                      const char *event)
{
    (void)connection;
    (void)propagate;
    (void)destination;
    (void)mask;
    XcbEvent copy;
    memcpy(&copy, event, sizeof(copy));
    copy.response_type |= SEND_EVENT_FLAG;
    xcb_events_push(&copy);
    return (XcbCookie){.sequence = xcb_next_sequence()};
}
