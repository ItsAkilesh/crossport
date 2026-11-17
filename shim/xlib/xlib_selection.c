/*
 * File: shim/xlib/xlib_selection.c
 * Purpose: X selections (the clipboard): owners, SelectionClear, SelectionRequest and SelectionNotify, bridged to the Windows clipboard for text
 * Author: Akilesh
 * Date: 2026-11-09
 *
 * One table of selection owners serves every display of the process. As in Xorg, the previous owner is told with
 * SelectionClear when the selection passes to a different client (display) or is released (owner None); a client
 * that only changes which of its own windows owns it is told nothing. Asking for the contents sends the owner a
 * SelectionRequest; the owner answers by writing a property and sending SelectionNotify to the requestor
 * (XSendEvent). A selection nobody owns is answered at once with a SelectionNotify whose property is None. These
 * events reach their window whether or not the program selected for them, as on a real server.
 *
 * CLIPBOARD text is bridged to the Windows clipboard (xlib_clipboard.c), so copying works between Windows programs
 * and translated ones. When a program takes CLIPBOARD, the shim asks it for UTF8_STRING in the same way any
 * requestor would (the requestor is a pseudo window) and puts the answer on the Windows clipboard. When nobody in
 * the process owns CLIPBOARD but Windows holds text, the shim answers requests from it and reports a pseudo owner.
 * When another program replaces what a translated one published, the owner is sent SelectionClear.
 */

#include <string.h>

#include "xlib_internal.h"

#define MAX_SELECTIONS 32
#define NO_PROPERTY 0
/* Not a real window: the requestor of the shim's own clipboard request, and the owner reported for Windows' text. */
#define BRIDGE_WINDOW 0x7f000001u
#define BRIDGE_DEPTH 24
#define ATOM_TYPE_ATOM 4
#define FORMAT_8 8
#define FORMAT_32 32
#define CURRENT_TIME 0

typedef struct {
    XlId selection;
    XlId owner;
    XlDisplay *display;
} Selection;

static Selection g_selections[MAX_SELECTIONS];
static SRWLOCK g_lock = SRWLOCK_INIT;

static XlId atom(const char *name)
{
    return xcb_atom_for(name, strlen(name));
}

static int is_clipboard(XlId selection)
{
    return selection == atom("CLIPBOARD");
}

/* The table entry for a selection, creating it when `is_new` allows; NULL when the table is full. Hold the lock. */
static Selection *entry_for(XlId selection, int is_new)
{
    Selection *free_slot = NULL;
    for (size_t i = 0; i < MAX_SELECTIONS; i++) {
        if (g_selections[i].selection == selection) {
            return &g_selections[i];
        }
        free_slot = (free_slot == NULL && g_selections[i].selection == 0) ? &g_selections[i] : free_slot;
    }
    if (is_new && free_slot != NULL) {
        free_slot->selection = selection;
    }
    return is_new ? free_slot : NULL;
}

static Selection copy_of(XlId selection)
{
    AcquireSRWLockShared(&g_lock);
    const Selection *found = entry_for(selection, 0);
    Selection copy = found != NULL ? *found : (Selection){0};
    ReleaseSRWLockShared(&g_lock);
    return copy;
}

static SYSV void post_clear(XlDisplay *display, XlId window, XlId selection, uint64_t time)
{
    XlEvent event = {0};
    event.selection_clear = (XlSelectionClearEvent){.type = XL_SELECTION_CLEAR, .serial = display->last_request_read,
                                                    .display = display, .window = window, .selection = selection,
                                                    .time = time};
    xl_events_push_back(display, &event);
}

static SYSV void post_notify(XlDisplay *display, XlId requestor, XlId selection, XlId target, XlId property, uint64_t time)
{
    XlEvent event = {0};
    event.selection = (XlSelectionEvent){.type = XL_SELECTION_NOTIFY, .serial = display->last_request_read,
                                         .display = display, .requestor = requestor, .selection = selection,
                                         .target = target, .property = property, .time = time};
    xl_events_push_back(display, &event);
}

static SYSV void post_request(XlDisplay *display, const Selection *owner, XlId requestor, XlId target, XlId property, uint64_t time)
{
    XlEvent event = {0};
    event.selection_request = (XlSelectionRequestEvent){.type = XL_SELECTION_REQUEST, .serial = display->last_request_read,
                                                        .display = owner->display, .owner = owner->owner,
                                                        .requestor = requestor, .selection = owner->selection,
                                                        .target = target, .property = property, .time = time};
    xl_events_push_back(owner->display, &event);
}

/* The requestor of the shim's own clipboard request has to exist as far as the owner can tell: GTK looks it up with
 * XGetWindowAttributes before it answers and refuses when the window is unknown. Registered once, never mapped. */
static void ensure_bridge_window(void)
{
    xl_lock();
    if (xl_drawable_find(BRIDGE_WINDOW) == NULL) {
        xl_drawable_add(BRIDGE_WINDOW, 1, BRIDGE_DEPTH, 1, 1, 0);
    }
    xl_unlock();
}

/* Windows' clipboard has been replaced by another program: a translated owner of CLIPBOARD no longer owns it. */
static SYSV void drop_if_replaced(XlId selection)
{
    if (!is_clipboard(selection) || !xl_clipboard_was_replaced()) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    Selection *found = entry_for(selection, 0);
    Selection previous = found != NULL ? *found : (Selection){0};
    if (found != NULL) {
        found->owner = 0;
        found->display = NULL;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (previous.owner != 0) {
        post_clear(previous.display, previous.owner, selection, CURRENT_TIME);
    }
}

SYSV int xl_selection_take_bridge_reply(const XlEvent *event)
{
    if (event->type != XL_SELECTION_NOTIFY || event->selection.requestor != BRIDGE_WINDOW) {
        return 0;
    }
    if (event->selection.property != NO_PROPERTY) {
        size_t length = 0;
        uint8_t *text = xl_property_take(BRIDGE_WINDOW, event->selection.property, &length);
        if (text != NULL) {
            xl_clipboard_publish(text, length);
            HeapFree(GetProcessHeap(), 0, text);
        }
    }
    return 1;
}

LIBC_FN XlId sysv_XGetSelectionOwner(XlDisplay *display, XlId selection)
{
    (void)display;
    drop_if_replaced(selection);
    XlId owner = copy_of(selection).owner;
    return (owner == 0 && is_clipboard(selection) && xl_clipboard_has_foreign_text()) ? BRIDGE_WINDOW : owner;
}

LIBC_FN int sysv_XSetSelectionOwner(XlDisplay *display, XlId selection, XlId owner, uint64_t time)
{
    xl_next_request(display);
    AcquireSRWLockExclusive(&g_lock);
    Selection *found = entry_for(selection, 1);
    Selection previous = found != NULL ? *found : (Selection){0};
    if (found != NULL) {
        found->owner = owner;
        found->display = owner != 0 ? display : NULL;
    }
    ReleaseSRWLockExclusive(&g_lock);
    int is_taken_over = previous.owner != 0 && (owner == 0 || previous.display != display);
    if (is_taken_over) {
        post_clear(previous.display, previous.owner, selection, time);
    }
    if (owner != 0 && is_clipboard(selection)) {
        ensure_bridge_window();
        post_request(display, &(Selection){selection, owner, display}, BRIDGE_WINDOW, atom("UTF8_STRING"),
                     atom("CROSSPORT_CLIPBOARD"), time);
    }
    return 1;
}

/* Answers a request for CLIPBOARD from the text on Windows' clipboard: the text itself, or the list of formats. */
static SYSV void answer_from_windows(XlDisplay *display, XlId selection, XlId target, XlId property, XlId requestor,
                                     uint64_t time)
{
    XlId destination = property != NO_PROPERTY ? property : target;
    int is_stored = 0;
    if (target == atom("TARGETS")) {
        uint32_t formats[] = {(uint32_t)target, (uint32_t)atom("UTF8_STRING"), (uint32_t)atom("text/plain;charset=utf-8")};
        is_stored = xl_property_put(requestor, destination, ATOM_TYPE_ATOM, FORMAT_32, (const uint8_t *)formats, sizeof(formats));
    } else if (target == atom("UTF8_STRING") || target == atom("text/plain;charset=utf-8")) {
        size_t length = 0;
        uint8_t *text = xl_clipboard_read(&length);
        if (text != NULL) {
            is_stored = xl_property_put(requestor, destination, target, FORMAT_8, text, length);
            HeapFree(GetProcessHeap(), 0, text);
        }
    }
    post_notify(display, requestor, selection, target, is_stored ? destination : NO_PROPERTY, time);
}

LIBC_FN int sysv_XConvertSelection(XlDisplay *display, XlId selection, XlId target, XlId property, XlId requestor,
                                   uint64_t time)
{
    xl_next_request(display);
    drop_if_replaced(selection);
    Selection owner = copy_of(selection);
    if (owner.owner == 0 && is_clipboard(selection) && xl_clipboard_has_foreign_text()) {
        answer_from_windows(display, selection, target, property, requestor, time);
    } else if (owner.owner == 0) {
        post_notify(display, requestor, selection, target, NO_PROPERTY, time);
    } else {
        post_request(display, &owner, requestor, target, property, time);
    }
    return 1;
}
