/*
 * File: tests/corpus/vulkan_window/window.h
 * Purpose: A plain xcb window the Vulkan sample presents into
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef VULKAN_WINDOW_WINDOW_H
#define VULKAN_WINDOW_WINDOW_H

#include <xcb/xcb.h>

#define WINDOW_WIDTH 160
#define WINDOW_HEIGHT 120

typedef struct {
    xcb_connection_t *connection;
    xcb_window_t id;
    xcb_atom_t delete_atom;
} Window;

/* Connects to the display, creates the window, asks for close notifications and maps it. Returns 0 on success. */
int window_create(Window *window, const char *title);
void window_destroy(Window *window);

/* Drains pending events. Returns 1 once the user asked to close the window. */
int window_should_close(Window *window);

#endif
