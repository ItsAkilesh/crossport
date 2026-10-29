/*
 * File: tests/corpus/vulkan_window/window.c
 * Purpose: Create, map and poll an xcb window, and notice the close request
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stdlib.h>
#include <string.h>

#include "window.h"

static xcb_atom_t intern(xcb_connection_t *connection, const char *name)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, NULL);
    xcb_atom_t atom = reply != NULL ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

static void register_close_request(Window *window)
{
    xcb_atom_t protocols = intern(window->connection, "WM_PROTOCOLS");
    window->delete_atom = intern(window->connection, "WM_DELETE_WINDOW");
    xcb_change_property(window->connection, XCB_PROP_MODE_REPLACE, window->id, protocols, XCB_ATOM_ATOM, 32, 1,
                        &window->delete_atom);
}

int window_create(Window *window, const char *title)
{
    int screen_number = 0;
    window->connection = xcb_connect(NULL, &screen_number);
    if (xcb_connection_has_error(window->connection)) {
        return -1;
    }
    const xcb_setup_t *setup = xcb_get_setup(window->connection);
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_number; i++) {
        xcb_screen_next(&screens);
    }
    window->id = xcb_generate_id(window->connection);
    uint32_t events = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS;
    xcb_create_window(window->connection, XCB_COPY_FROM_PARENT, window->id, screens.data->root, 0, 0, WINDOW_WIDTH,
                      WINDOW_HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screens.data->root_visual, XCB_CW_EVENT_MASK,
                      &events);
    xcb_change_property(window->connection, XCB_PROP_MODE_REPLACE, window->id, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        (uint32_t)strlen(title), title);
    register_close_request(window);
    xcb_map_window(window->connection, window->id);
    xcb_flush(window->connection);
    return 0;
}

void window_destroy(Window *window)
{
    xcb_destroy_window(window->connection, window->id);
    xcb_flush(window->connection);
    xcb_disconnect(window->connection);
}

static int is_close_request(const Window *window, const xcb_generic_event_t *event)
{
    if ((event->response_type & 0x7f) != XCB_CLIENT_MESSAGE) {
        return 0;
    }
    const xcb_client_message_event_t *message = (const xcb_client_message_event_t *)event;
    return message->data.data32[0] == window->delete_atom;
}

int window_should_close(Window *window)
{
    int should_close = 0;
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(window->connection)) != NULL) {
        should_close |= is_close_request(window, event);
        free(event);
    }
    return should_close;
}
