/*
 * File: shim/xlib/xlib_selection.c
 * Purpose: X selections (the clipboard) between the windows of this process: owners, SelectionClear, SelectionRequest and SelectionNotify
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * One table of selection owners serves every display of the process. Taking a selection tells the previous
 * owner with SelectionClear. Asking for its contents sends the owner a SelectionRequest; the owner answers by
 * writing a property and sending SelectionNotify to the requestor (XSendEvent). A selection nobody owns is
 * answered at once with a SelectionNotify whose property is None. These events reach their window whether or
 * not the program selected for them, as on a real server. Windows' own clipboard is not connected yet, so the
 * selections exist only between windows of this process.
 */

#include "xlib_internal.h"

#define MAX_SELECTIONS 32
#define NO_PROPERTY 0

typedef struct {
    XlId selection;
    XlId owner;
    XlDisplay *display;
} Selection;

static Selection g_selections[MAX_SELECTIONS];
static SRWLOCK g_lock = SRWLOCK_INIT;

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

LIBC_FN XlId sysv_XGetSelectionOwner(XlDisplay *display, XlId selection)
{
    (void)display;
    AcquireSRWLockShared(&g_lock);
    const Selection *found = entry_for(selection, 0);
    XlId owner = found != NULL ? found->owner : 0;
    ReleaseSRWLockShared(&g_lock);
    return owner;
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
    int is_taken_over = previous.owner != 0 && (previous.owner != owner || previous.display != display);
    if (is_taken_over) {
        post_clear(previous.display, previous.owner, selection, time);
    }
    return 1;
}

LIBC_FN int sysv_XConvertSelection(XlDisplay *display, XlId selection, XlId target, XlId property, XlId requestor,
                                   uint64_t time)
{
    xl_next_request(display);
    AcquireSRWLockShared(&g_lock);
    const Selection *found = entry_for(selection, 0);
    Selection owner = found != NULL ? *found : (Selection){0};
    ReleaseSRWLockShared(&g_lock);
    if (owner.owner == 0) {
        post_notify(display, requestor, selection, target, NO_PROPERTY, time);
        return 1;
    }
    XlEvent event = {0};
    event.selection_request = (XlSelectionRequestEvent){.type = XL_SELECTION_REQUEST, .serial = display->last_request_read,
                                                        .display = owner.display, .owner = owner.owner,
                                                        .requestor = requestor, .selection = selection, .target = target,
                                                        .property = property, .time = time};
    xl_events_push_back(owner.display, &event);
    return 1;
}
