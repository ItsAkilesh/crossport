/*
 * File: shim/xlib/xlib_queue.c
 * Purpose: A display's queue of Xlib events: conversion from the xcb-style events the window procedure produces, and queue operations
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Win32 messages become 32-byte wire events (xcb's format) in one process-wide queue. Each display converts
 * them to XEvents when the program asks (XPending, XNextEvent, ...), drops those the window did not select
 * and those that change nothing (a ConfigureNotify with the size already reported), and keeps the result in
 * its own ring until the program takes them.
 */

#include <string.h>

#include "xlib_internal.h"

#define INITIAL_QUEUE_CAPACITY 64
#define EVENT_TYPE_MASK 0x7f
#define CLIENT_MESSAGE_WORDS 5
#define BUTTON_STATE_MASK 0x1f00u

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);

int64_t xl_event_mask_for(int type, uint32_t state)
{
    switch (type) {
    case XL_KEY_PRESS:
        return XL_MASK_KEY_PRESS;
    case XL_KEY_RELEASE:
        return XL_MASK_KEY_RELEASE;
    case XL_BUTTON_PRESS:
        return XL_MASK_BUTTON_PRESS;
    case XL_BUTTON_RELEASE:
        return XL_MASK_BUTTON_RELEASE;
    case XL_MOTION_NOTIFY:
        return XL_MASK_POINTER_MOTION | ((state & BUTTON_STATE_MASK) != 0 ? XL_MASK_BUTTON_MOTION : 0);
    case XL_ENTER_NOTIFY:
        return XL_MASK_ENTER;
    case XL_LEAVE_NOTIFY:
        return XL_MASK_LEAVE;
    case XL_FOCUS_IN:
    case XL_FOCUS_OUT:
        return XL_MASK_FOCUS;
    case XL_EXPOSE:
        return XL_MASK_EXPOSURE;
    case XL_DESTROY_NOTIFY:
    case XL_UNMAP_NOTIFY:
    case XL_MAP_NOTIFY:
    case XL_CONFIGURE_NOTIFY:
        return XL_MASK_STRUCTURE;
    case XL_PROPERTY_NOTIFY:
        return XL_MASK_PROPERTY;
    default:
        return 0;
    }
}

/* What the program selected on a window, or -1 when the window is not one of Xlib's (its events pass). */
static int64_t selected_mask(XlId window)
{
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    int64_t mask = drawable != NULL ? drawable->event_mask : -1;
    xl_unlock();
    return mask;
}

/* Records a configure size and tells whether it differs from what the program was last told. */
static int is_new_size(XlId window, int width, int height)
{
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(window);
    int is_new = drawable == NULL || drawable->reported_width != width || drawable->reported_height != height;
    if (drawable != NULL) {
        drawable->reported_width = width;
        drawable->reported_height = height;
    }
    xl_unlock();
    return is_new;
}

static void convert_input(XlDisplay *display, const XcbEvent *wire, int type, XlEvent *out)
{
    XcbInputEvent input;
    memcpy(&input, wire, sizeof(input));
    out->input = (XlInputEvent){.type = type, .serial = display->last_request_read, .display = display,
                                .window = input.event, .root = input.root, .subwindow = input.child,
                                .time = input.time, .x = input.event_x, .y = input.event_y, .x_root = input.root_x,
                                .y_root = input.root_y, .state = input.state, .detail = input.detail,
                                .same_screen = input.same_screen};
}

static void convert_client_message(XlDisplay *display, const XcbEvent *wire, XlEvent *out)
{
    XcbClientMessageEvent message;
    memcpy(&message, wire, sizeof(message));
    out->client = (XlClientMessageEvent){.type = XL_CLIENT_MESSAGE, .serial = display->last_request_read,
                                         .display = display, .window = message.window, .message_type = message.type,
                                         .format = message.format};
    for (int i = 0; i < CLIENT_MESSAGE_WORDS; i++) {
        out->client.data[i] = message.data32[i];
    }
}

/* Converts one wire event; returns 0 when the program should not see it. */
static int convert(XlDisplay *display, const XcbEvent *wire, XlEvent *out)
{
    int type = wire->response_type & EVENT_TYPE_MASK;
    memset(out, 0, sizeof(*out));
    XlId window = 0;
    switch (type) {
    case XL_KEY_PRESS:
    case XL_KEY_RELEASE:
    case XL_BUTTON_PRESS:
    case XL_BUTTON_RELEASE:
    case XL_MOTION_NOTIFY:
        convert_input(display, wire, type, out);
        window = out->input.window;
        break;
    case XL_FOCUS_IN:
    case XL_FOCUS_OUT: {
        XcbFocusEvent focus;
        memcpy(&focus, wire, sizeof(focus));
        out->focus = (XlFocusEvent){.type = type, .serial = display->last_request_read, .display = display,
                                    .window = focus.event, .mode = focus.mode, .detail = focus.detail};
        window = focus.event;
        break;
    }
    case XL_ENTER_NOTIFY:
    case XL_LEAVE_NOTIFY: {
        XcbCrossingEvent crossing;
        memcpy(&crossing, wire, sizeof(crossing));
        out->crossing = (XlCrossingEvent){.type = type, .serial = display->last_request_read, .display = display,
                                          .window = crossing.event, .root = crossing.root, .time = crossing.time,
                                          .x = crossing.event_x, .y = crossing.event_y, .x_root = crossing.root_x,
                                          .y_root = crossing.root_y, .mode = crossing.mode, .detail = crossing.detail,
                                          .same_screen = crossing.same_screen_focus & 1,
                                          .focus = (crossing.same_screen_focus >> 1) & 1, .state = crossing.state};
        window = crossing.event;
        break;
    }
    case XL_EXPOSE: {
        XcbExposeEvent expose;
        memcpy(&expose, wire, sizeof(expose));
        out->expose = (XlExposeEvent){.type = type, .serial = display->last_request_read, .display = display,
                                      .window = expose.window, .x = expose.x, .y = expose.y, .width = expose.width,
                                      .height = expose.height, .count = expose.count};
        window = expose.window;
        break;
    }
    case XL_CONFIGURE_NOTIFY: {
        XcbConfigureEvent configure;
        memcpy(&configure, wire, sizeof(configure));
        out->configure = (XlConfigureEvent){.type = type, .serial = display->last_request_read, .display = display,
                                            .event = configure.event, .window = configure.window, .x = configure.x,
                                            .y = configure.y, .width = configure.width, .height = configure.height};
        window = configure.window;
        if (!is_new_size(window, configure.width, configure.height)) {
            return 0;
        }
        break;
    }
    case XL_CLIENT_MESSAGE:
        convert_client_message(display, wire, out);
        return 1;
    default:
        return 0;
    }
    int64_t selected = selected_mask(window);
    return selected < 0 || (selected & xl_event_mask_for(type, type <= XL_MOTION_NOTIFY ? out->input.state : 0)) != 0;
}

static SYSV int grow(XlDisplayData *data)
{
    if (data->queue_count < data->queue_capacity) {
        return 1;
    }
    size_t capacity = data->queue_capacity == 0 ? INITIAL_QUEUE_CAPACITY : data->queue_capacity * 2;
    XlEvent *queue = (XlEvent *)sysv_malloc(capacity * sizeof(XlEvent));
    if (queue == NULL) {
        return 0;
    }
    for (size_t i = 0; i < data->queue_count; i++) {
        queue[i] = data->queue[(data->queue_head + i) % data->queue_capacity];
    }
    sysv_free(data->queue);
    data->queue = queue;
    data->queue_head = 0;
    data->queue_capacity = capacity;
    return 1;
}

SYSV int xl_events_push_back(XlDisplay *display, const XlEvent *event)
{
    XlDisplayData *data = xl_display_data(display);
    if (!grow(data)) {
        return 0;
    }
    data->queue[(data->queue_head + data->queue_count++) % data->queue_capacity] = *event;
    display->queue_length = (int32_t)data->queue_count;
    return 1;
}

SYSV int xl_events_push_front(XlDisplay *display, const XlEvent *event)
{
    XlDisplayData *data = xl_display_data(display);
    if (!grow(data)) {
        return 0;
    }
    data->queue_head = (data->queue_head + data->queue_capacity - 1) % data->queue_capacity;
    data->queue[data->queue_head] = *event;
    data->queue_count++;
    display->queue_length = (int32_t)data->queue_count;
    return 1;
}

SYSV void xl_events_fill(XlDisplay *display, int should_wait)
{
    XlDisplayData *data = xl_display_data(display);
    int should_block = should_wait && data->queue_count == 0;
    XcbEvent wire;
    XlEvent event;
    while (xcb_events_take(&wire, should_block)) {
        if (convert(display, &wire, &event) && xl_events_push_back(display, &event)) {
            should_block = 0;
        }
    }
}

SYSV int xl_events_take(XlDisplay *display, XlEventMatch matches, void *context, XlEvent *out, int should_remove)
{
    XlDisplayData *data = xl_display_data(display);
    for (size_t i = 0; i < data->queue_count; i++) {
        size_t slot = (data->queue_head + i) % data->queue_capacity;
        if (matches != NULL && !matches(&data->queue[slot], context)) {
            continue;
        }
        *out = data->queue[slot];
        if (should_remove) {
            for (size_t j = i; j + 1 < data->queue_count; j++) {
                data->queue[(data->queue_head + j) % data->queue_capacity] =
                    data->queue[(data->queue_head + j + 1) % data->queue_capacity];
            }
            data->queue_count--;
            display->queue_length = (int32_t)data->queue_count;
        }
        return 1;
    }
    return 0;
}

SYSV void xl_events_post_structure(XlDisplay *display, int type, XlId window)
{
    int64_t selected = selected_mask(window);
    if (selected >= 0 && (selected & XL_MASK_STRUCTURE) == 0) {
        return;
    }
    XlEvent event = {0};
    event.structure = (XlStructureEvent){.type = type, .serial = display->last_request_read, .display = display,
                                         .event = window, .window = window};
    xl_events_push_back(display, &event);
}
