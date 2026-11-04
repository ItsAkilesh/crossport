/*
 * File: tests/corpus/xlib_selection/main.c
 * Purpose: Xlib client with two windows that hold the whole clipboard conversation: take ownership, request, answer, refuse, lose it
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Differential test for selections: the same program runs against a real X server (Xvfb) on Linux and against
 * the shim on Windows, and the output must match. One window owns the CLIPBOARD selection and the other asks
 * for it; the program prints every selection event with its fields, and the data that arrives.
 */

#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define COPIED_TEXT "hello clipboard"
#define PROPERTY_LONGS 64

static Display *g_display;
static Window g_owner;
static Window g_requestor;

static const char *window_name(Window window)
{
    return window == g_owner ? "owner" : window == g_requestor ? "requestor" : window == None ? "none" : "other";
}

static void print_atom(const char *label, Atom atom)
{
    char *name = atom == None ? NULL : XGetAtomName(g_display, atom);
    printf(" %s=%s", label, name != NULL ? name : "None");
    if (name != NULL) {
        XFree(name);
    }
}

static void next_selection_event(XEvent *event)
{
    do {
        XNextEvent(g_display, event);
    } while (event->type != SelectionRequest && event->type != SelectionNotify && event->type != SelectionClear);
}

static void print_request(const XSelectionRequestEvent *request)
{
    printf("SelectionRequest owner=%s requestor=%s", window_name(request->owner), window_name(request->requestor));
    print_atom("selection", request->selection);
    print_atom("target", request->target);
    print_atom("property", request->property);
    putchar('\n');
}

static void print_notify(const XSelectionEvent *notify)
{
    printf("SelectionNotify requestor=%s", window_name(notify->requestor));
    print_atom("selection", notify->selection);
    print_atom("target", notify->target);
    print_atom("property", notify->property);
    putchar('\n');
}

static void print_clear(const XSelectionClearEvent *clear)
{
    printf("SelectionClear window=%s", window_name(clear->window));
    print_atom("selection", clear->selection);
    putchar('\n');
}

/* The owner's answer: the text in the requested property, or None for a target it cannot convert. */
static void answer(const XSelectionRequestEvent *request, Atom utf8)
{
    XEvent reply = {0};
    int is_supported = request->target == utf8;
    if (is_supported) {
        XChangeProperty(g_display, request->requestor, request->property, utf8, 8, PropModeReplace,
                        (const unsigned char *)COPIED_TEXT, (int)strlen(COPIED_TEXT));
    }
    reply.xselection.type = SelectionNotify;
    reply.xselection.requestor = request->requestor;
    reply.xselection.selection = request->selection;
    reply.xselection.target = request->target;
    reply.xselection.property = is_supported ? request->property : None;
    reply.xselection.time = request->time;
    XSendEvent(g_display, request->requestor, False, NoEventMask, &reply);
    XFlush(g_display);
}

static void print_received_data(Atom property)
{
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *data = NULL;
    XGetWindowProperty(g_display, g_requestor, property, 0, PROPERTY_LONGS, True, AnyPropertyType, &type, &format, &count,
                       &remaining, &data);
    printf("data:");
    print_atom("type", type);
    printf(" format=%d count=%lu remaining=%lu text=%s\n", format, count, remaining, data != NULL ? (char *)data : "-");
    if (data != NULL) {
        XFree(data);
    }
}

/* Asks for the selection and handles the conversation until the requestor has its answer. */
static void request(Atom clipboard, Atom target, Atom property, Atom utf8)
{
    XEvent event;
    XConvertSelection(g_display, clipboard, target, property, g_requestor, CurrentTime);
    XFlush(g_display);
    do {
        next_selection_event(&event);
        if (event.type == SelectionRequest) {
            print_request(&event.xselectionrequest);
            answer(&event.xselectionrequest, utf8);
        }
    } while (event.type != SelectionNotify);
    print_notify(&event.xselection);
    if (event.xselection.property != None) {
        print_received_data(event.xselection.property);
    }
}

int main(void)
{
    g_display = XOpenDisplay(NULL);
    if (g_display == NULL) {
        puts("no display");
        return 1;
    }
    Window root = DefaultRootWindow(g_display);
    g_owner = XCreateSimpleWindow(g_display, root, 0, 0, 10, 10, 0, 0, 0);
    g_requestor = XCreateSimpleWindow(g_display, root, 20, 0, 10, 10, 0, 0, 0);
    Atom clipboard = XInternAtom(g_display, "CLIPBOARD", False);
    Atom utf8 = XInternAtom(g_display, "UTF8_STRING", False);
    Atom property = XInternAtom(g_display, "XSEL_DATA", False);
    Atom targets = XInternAtom(g_display, "TARGETS", False);
    XEvent event;

    printf("owner_at_start=%s\n", window_name(XGetSelectionOwner(g_display, clipboard)));
    request(clipboard, utf8, property, utf8);
    XSetSelectionOwner(g_display, clipboard, g_owner, CurrentTime);
    printf("owner_after_set=%s\n", window_name(XGetSelectionOwner(g_display, clipboard)));
    request(clipboard, utf8, property, utf8);
    request(clipboard, targets, property, utf8);
    /* Passing it to another window of the same client sends no SelectionClear; releasing it does. */
    XSetSelectionOwner(g_display, clipboard, g_requestor, CurrentTime);
    printf("owner_after_takeover=%s\n", window_name(XGetSelectionOwner(g_display, clipboard)));
    XSetSelectionOwner(g_display, clipboard, None, CurrentTime);
    next_selection_event(&event);
    print_clear(&event.xselectionclear);
    printf("owner_after_release=%s\n", window_name(XGetSelectionOwner(g_display, clipboard)));
    request(clipboard, utf8, property, utf8);
    XCloseDisplay(g_display);
    puts("done");
    return 0;
}
