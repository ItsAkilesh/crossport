/*
 * File: shim/xlib/xlib_events.c
 * Purpose: libX11 event functions: XNextEvent, XPending, XCheck*Event, XIfEvent, XPutBackEvent, XSendEvent
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The functions that wait pump Win32 messages for the calling thread, so a program must read events on
 * the thread that created its windows (as single-threaded X clients do).
 */

#include <string.h>

#include "xlib_internal.h"

#define QUEUED_ALREADY 0

typedef struct {
    int64_t mask;
    XlId window;
    int has_window;
    int type;
    int has_type;
} EventFilter;

typedef struct {
    XlDisplay *display;
    int (SYSV *predicate)(XlDisplay *, XlEvent *, char *);
    char *argument;
} PredicateContext;

static SYSV int matches_filter(const XlEvent *event, void *context)
{
    const EventFilter *filter = (const EventFilter *)context;
    if (filter->has_window && event->any.window != filter->window) {
        return 0;
    }
    if (filter->has_type) {
        return event->type == filter->type;
    }
    return (xl_event_mask_for(event->type, event->type <= XL_MOTION_NOTIFY ? event->input.state : 0) & filter->mask) != 0;
}

static SYSV int matches_predicate(const XlEvent *event, void *context)
{
    PredicateContext *predicate = (PredicateContext *)context;
    XlEvent copy = *event;
    return predicate->predicate(predicate->display, &copy, predicate->argument) != 0;
}

/* Waits (when asked) until an event the filter accepts is queued, then takes it. */
static SYSV int take_matching(XlDisplay *display, XlEventMatch matches, void *context, XlEvent *out, int should_wait,
                              int should_remove)
{
    for (;;) {
        xl_events_fill(display, 0);
        if (xl_events_take(display, matches, context, out, should_remove)) {
            return 1;
        }
        if (!should_wait) {
            return 0;
        }
        xl_events_fill(display, 1);
    }
}

LIBC_FN int sysv_XNextEvent(XlDisplay *display, XlEvent *event)
{
    xl_present_all();
    take_matching(display, NULL, NULL, event, 1, 1);
    return 0;
}

LIBC_FN int sysv_XPeekEvent(XlDisplay *display, XlEvent *event)
{
    take_matching(display, NULL, NULL, event, 1, 0);
    return 0;
}

LIBC_FN int sysv_XPending(XlDisplay *display)
{
    xl_present_all();
    xl_events_fill(display, 0);
    return display->queue_length;
}

LIBC_FN int sysv_XEventsQueued(XlDisplay *display, int mode)
{
    if (mode != QUEUED_ALREADY) {
        xl_present_all();
        xl_events_fill(display, 0);
    }
    return display->queue_length;
}

LIBC_FN int sysv_XCheckMaskEvent(XlDisplay *display, int64_t mask, XlEvent *event)
{
    EventFilter filter = {.mask = mask};
    return take_matching(display, matches_filter, &filter, event, 0, 1);
}

LIBC_FN int sysv_XMaskEvent(XlDisplay *display, int64_t mask, XlEvent *event)
{
    EventFilter filter = {.mask = mask};
    take_matching(display, matches_filter, &filter, event, 1, 1);
    return 0;
}

LIBC_FN int sysv_XCheckWindowEvent(XlDisplay *display, XlId window, int64_t mask, XlEvent *event)
{
    EventFilter filter = {.mask = mask, .window = window, .has_window = 1};
    return take_matching(display, matches_filter, &filter, event, 0, 1);
}

LIBC_FN int sysv_XWindowEvent(XlDisplay *display, XlId window, int64_t mask, XlEvent *event)
{
    EventFilter filter = {.mask = mask, .window = window, .has_window = 1};
    take_matching(display, matches_filter, &filter, event, 1, 1);
    return 0;
}

LIBC_FN int sysv_XCheckTypedEvent(XlDisplay *display, int type, XlEvent *event)
{
    EventFilter filter = {.type = type, .has_type = 1};
    return take_matching(display, matches_filter, &filter, event, 0, 1);
}

LIBC_FN int sysv_XCheckTypedWindowEvent(XlDisplay *display, XlId window, int type, XlEvent *event)
{
    EventFilter filter = {.type = type, .has_type = 1, .window = window, .has_window = 1};
    return take_matching(display, matches_filter, &filter, event, 0, 1);
}

LIBC_FN int sysv_XIfEvent(XlDisplay *display, XlEvent *event, int (SYSV *predicate)(XlDisplay *, XlEvent *, char *), char *argument)
{
    PredicateContext context = {display, predicate, argument};
    take_matching(display, matches_predicate, &context, event, 1, 1);
    return 0;
}

LIBC_FN int sysv_XCheckIfEvent(XlDisplay *display, XlEvent *event, int (SYSV *predicate)(XlDisplay *, XlEvent *, char *), char *argument)
{
    PredicateContext context = {display, predicate, argument};
    return take_matching(display, matches_predicate, &context, event, 0, 1);
}

LIBC_FN int sysv_XPeekIfEvent(XlDisplay *display, XlEvent *event, int (SYSV *predicate)(XlDisplay *, XlEvent *, char *), char *argument)
{
    PredicateContext context = {display, predicate, argument};
    take_matching(display, matches_predicate, &context, event, 1, 0);
    return 0;
}

LIBC_FN int sysv_XPutBackEvent(XlDisplay *display, const XlEvent *event)
{
    return xl_events_push_front(display, event) ? 1 : 0;
}

/* Delivers an event to a window of this process; messages for other windows (the root, a window manager) go nowhere. */
LIBC_FN int sysv_XSendEvent(XlDisplay *display, XlId window, int should_propagate, int64_t mask, const XlEvent *event)
{
    (void)should_propagate;
    xl_lock();
    XlDrawable *target = xl_drawable_find(window);
    int is_delivered = target != NULL && (event->type == XL_CLIENT_MESSAGE || (target->event_mask & mask) != 0);
    xl_unlock();
    if (is_delivered) {
        XlEvent sent = *event;
        sent.any.send_event = 1;
        sent.any.display = display;
        sent.any.window = event->type == XL_CLIENT_MESSAGE ? window : event->any.window;
        xl_events_push_back(display, &sent);
    }
    return 1;
}

LIBC_FN int sysv_XGetEventData(XlDisplay *display, void *cookie)
{
    (void)display;
    (void)cookie;
    return 0;
}

LIBC_FN void sysv_XFreeEventData(XlDisplay *display, void *cookie)
{
    (void)display;
    (void)cookie;
}
