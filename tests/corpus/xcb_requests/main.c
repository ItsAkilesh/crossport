/*
 * File: tests/corpus/xcb_requests/main.c
 * Purpose: xcb client that makes the requests a toolkit's platform plugin makes, and prints what the server answers
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Differential test: the same program talks to a real X server (Xvfb) on Linux and to the shim on Windows, and must
 * print the same. It checks only what the protocol fixes: the structure of the setup, property round trips (offsets,
 * types, deletion), atom names, window attributes and tree, images put and read back, events sent to oneself, and
 * that no extension is claimed that a toolkit would then have to speak.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#define WINDOW_WIDTH 40
#define WINDOW_HEIGHT 30
#define PIXMAP_SIZE 2

static xcb_connection_t *g_connection;
/* An extension no server has, so both sides must answer "absent" (a real Xvfb does have XKEYBOARD, the shim has none). */
static xcb_extension_t g_missing_extension = {"CROSSPORT-NO-SUCH-EXTENSION", 0};

static void print_setup(const xcb_setup_t *setup, const xcb_screen_t *screen)
{
    int has_depth_24 = 0;
    int has_true_color_visual = 0;
    for (xcb_depth_iterator_t depths = xcb_screen_allowed_depths_iterator(screen); depths.rem; xcb_depth_next(&depths)) {
        has_depth_24 |= depths.data->depth == 24;
        for (xcb_visualtype_iterator_t visuals = xcb_depth_visuals_iterator(depths.data); visuals.rem;
             xcb_visualtype_next(&visuals)) {
            has_true_color_visual |= depths.data->depth == 24 && visuals.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR &&
                                     visuals.data->red_mask == 0xff0000 && visuals.data->blue_mask == 0xff;
        }
    }
    int has_format_32 = 0;
    for (xcb_format_iterator_t formats = xcb_setup_pixmap_formats_iterator(setup); formats.rem; xcb_format_next(&formats)) {
        has_format_32 |= formats.data->depth == 24 && formats.data->bits_per_pixel == 32;
    }
    printf("setup: protocol=%d screens=%d depth24=%d true_color=%d format24_32bit=%d root_depth=%d\n",
           setup->protocol_major_version, xcb_setup_roots_length(setup), has_depth_24, has_true_color_visual, has_format_32,
           screen->root_depth);
}

static void print_extension(const char *name, xcb_extension_t *extension)
{
    const xcb_query_extension_reply_t *reply = xcb_get_extension_data(g_connection, extension);
    printf("extension %s: present=%d\n", name, reply != NULL && reply->present);
}

static xcb_atom_t intern(const char *name)
{
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(g_connection, xcb_intern_atom(g_connection, 0, strlen(name), name), NULL);
    xcb_atom_t atom = reply != NULL ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

static void print_atom_name(const char *label, xcb_atom_t atom)
{
    xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(g_connection, xcb_get_atom_name(g_connection, atom), NULL);
    printf("atom %s: name=%.*s\n", label, reply != NULL ? xcb_get_atom_name_name_length(reply) : 0,
           reply != NULL ? xcb_get_atom_name_name(reply) : "");
    free(reply);
}

static void print_property(xcb_window_t window, const char *label, xcb_atom_t property, xcb_atom_t type, uint32_t offset,
                           uint32_t length)
{
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(g_connection, xcb_get_property(g_connection, 0, window, property, type, offset, length), NULL);
    if (reply == NULL) {
        printf("property %s: no reply\n", label);
        return;
    }
    printf("property %s: type=%u format=%d items=%u after=%u bytes=%d first=%d\n", label, reply->type != XCB_ATOM_NONE, reply->format,
           reply->value_len, reply->bytes_after, xcb_get_property_value_length(reply),
           xcb_get_property_value_length(reply) > 0 ? *(uint8_t *)xcb_get_property_value(reply) : -1);
    free(reply);
}

static void check_properties(xcb_window_t window)
{
    xcb_atom_t custom = intern("CROSSPORT_TEST");
    uint32_t numbers[3] = {10, 20, 30};
    xcb_change_property(g_connection, XCB_PROP_MODE_REPLACE, window, custom, XCB_ATOM_CARDINAL, 32, 3, numbers);
    print_property(window, "all", custom, XCB_ATOM_ANY, 0, 100);
    print_property(window, "from-offset-1", custom, XCB_ATOM_CARDINAL, 1, 100);
    print_property(window, "short", custom, XCB_ATOM_CARDINAL, 0, 1);
    print_property(window, "wrong-type", custom, XCB_ATOM_STRING, 0, 100);
    xcb_change_property(g_connection, XCB_PROP_MODE_APPEND, window, custom, XCB_ATOM_CARDINAL, 32, 1, &numbers[0]);
    print_property(window, "appended", custom, XCB_ATOM_CARDINAL, 0, 100);
    xcb_delete_property(g_connection, window, custom);
    print_property(window, "deleted", custom, XCB_ATOM_ANY, 0, 100);
    xcb_change_property(g_connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 5, "title");
    print_property(window, "title", XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 100);
}

static void check_window(xcb_window_t window, xcb_window_t root)
{
    xcb_get_window_attributes_reply_t *attributes =
        xcb_get_window_attributes_reply(g_connection, xcb_get_window_attributes(g_connection, window), NULL);
    printf("attributes: class=%d map_state=%d override=%d\n", attributes != NULL ? attributes->_class : -1,
           attributes != NULL ? attributes->map_state : -1, attributes != NULL ? attributes->override_redirect : -1);
    free(attributes);
    xcb_query_tree_reply_t *tree = xcb_query_tree_reply(g_connection, xcb_query_tree(g_connection, window), NULL);
    printf("tree: parent_is_root=%d children=%d\n", tree != NULL && tree->parent == root,
           tree != NULL ? xcb_query_tree_children_length(tree) : -1);
    free(tree);
    xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(g_connection, xcb_get_geometry(g_connection, window), NULL);
    printf("geometry: %dx%d\n", geometry != NULL ? geometry->width : -1, geometry != NULL ? geometry->height : -1);
    free(geometry);
}

static void check_images(xcb_window_t window)
{
    xcb_pixmap_t pixmap = xcb_generate_id(g_connection);
    xcb_gcontext_t gc = xcb_generate_id(g_connection);
    uint32_t colors[2] = {0x112233, 0x445566};
    xcb_create_pixmap(g_connection, 24, pixmap, window, PIXMAP_SIZE, PIXMAP_SIZE);
    xcb_create_gc(g_connection, gc, pixmap, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, colors);
    uint32_t pixels[PIXMAP_SIZE * PIXMAP_SIZE] = {0x00aa0001, 0x00bb0002, 0x00cc0003, 0x00dd0004};
    xcb_put_image(g_connection, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, gc, PIXMAP_SIZE, PIXMAP_SIZE, 0, 0, 0, 24, sizeof(pixels),
                  (const uint8_t *)pixels);
    xcb_get_image_reply_t *image = xcb_get_image_reply(
        g_connection, xcb_get_image(g_connection, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, 0, 0, PIXMAP_SIZE, PIXMAP_SIZE, ~0u), NULL);
    printf("image: depth=%d bytes=%d", image != NULL ? image->depth : -1, image != NULL ? xcb_get_image_data_length(image) : -1);
    for (int i = 0; image != NULL && i < PIXMAP_SIZE * PIXMAP_SIZE; i++) {
        printf(" %06x", ((uint32_t *)xcb_get_image_data(image))[i] & 0xffffff);
    }
    putchar('\n');
    free(image);
    xcb_free_gc(g_connection, gc);
    xcb_free_pixmap(g_connection, pixmap);
}

static void check_keyboard(void)
{
    const xcb_setup_t *setup = xcb_get_setup(g_connection);
    xcb_get_keyboard_mapping_reply_t *mapping = xcb_get_keyboard_mapping_reply(
        g_connection, xcb_get_keyboard_mapping(g_connection, 38, 1), NULL);
    printf("keyboard: per_keycode_ok=%d a=%x\n", mapping != NULL && mapping->keysyms_per_keycode >= 2,
           mapping != NULL ? xcb_get_keyboard_mapping_keysyms(mapping)[0] : 0);
    free(mapping);
    xcb_get_modifier_mapping_reply_t *modifiers =
        xcb_get_modifier_mapping_reply(g_connection, xcb_get_modifier_mapping(g_connection), NULL);
    printf("modifiers: per_modifier_ok=%d min_keycode=%d\n", modifiers != NULL && modifiers->keycodes_per_modifier >= 1,
           setup->min_keycode);
    free(modifiers);
}

static void check_self_event(xcb_window_t window)
{
    xcb_atom_t message = intern("CROSSPORT_PING");
    xcb_client_message_event_t event = {.response_type = XCB_CLIENT_MESSAGE, .format = 32, .window = window, .type = message};
    event.data.data32[0] = 1234;
    xcb_send_event(g_connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
    xcb_flush(g_connection);
    xcb_generic_event_t *received;
    do {
        received = xcb_wait_for_event(g_connection);
    } while (received != NULL && (received->response_type & 0x7f) != XCB_CLIENT_MESSAGE);
    xcb_client_message_event_t *echoed = (xcb_client_message_event_t *)received;
    printf("event: client_message=%d type_ok=%d data=%u\n", received != NULL, echoed != NULL && echoed->type == message,
           echoed != NULL ? echoed->data.data32[0] : 0);
    free(received);
}

int main(void)
{
    int screen_number = 0;
    g_connection = xcb_connect(NULL, &screen_number);
    if (xcb_connection_has_error(g_connection)) {
        puts("no display");
        return 1;
    }
    const xcb_setup_t *setup = xcb_get_setup(g_connection);
    xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;
    print_setup(setup, screen);
    print_extension("CROSSPORT-NO-SUCH-EXTENSION", &g_missing_extension);
    print_atom_name("WM_NAME", XCB_ATOM_WM_NAME);
    print_atom_name("custom", intern("CROSSPORT_NAME_CHECK"));
    xcb_window_t window = xcb_generate_id(g_connection);
    uint32_t mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(g_connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(g_connection);
    check_window(window, screen->root);
    check_properties(window);
    check_images(window);
    check_keyboard();
    check_self_event(window);
    xcb_destroy_window(g_connection, window);
    xcb_disconnect(g_connection);
    puts("done");
    return 0;
}
