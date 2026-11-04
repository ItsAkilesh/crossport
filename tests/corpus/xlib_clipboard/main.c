/*
 * File: tests/corpus/xlib_clipboard/main.c
 * Purpose: Xlib client that reads the system clipboard as UTF-8 text, then takes it over and serves a clipboard manager's request
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Windows-only check of the clipboard bridge (tools/ci/check_clipboard.ps1 sets the Windows clipboard before the run
 * and reads it after). The program prints what CLIPBOARD held (line ends shown as '|'), then owns CLIPBOARD and answers
 * the request the shim's bridge sends, as any clipboard manager's request would be answered. Against a real X server
 * nothing is on the clipboard and nobody asks, so the run just times out quietly and exits.
 */

#include <poll.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define OFFERED_TEXT "from crossport\nsecond line"
#define PROPERTY_LONGS 256
#define WAIT_MILLISECONDS 1500

static void print_clipboard(Display *display, Window window, Atom clipboard, Atom utf8, Atom property)
{
    XEvent event;
    XConvertSelection(display, clipboard, utf8, property, window, CurrentTime);
    XFlush(display);
    do {
        XNextEvent(display, &event);
    } while (event.type != SelectionNotify);
    printf("windows_text=");
    if (event.xselection.property != None) {
        Atom type = None;
        int format = 0;
        unsigned long count = 0;
        unsigned long remaining = 0;
        unsigned char *data = NULL;
        XGetWindowProperty(display, window, property, 0, PROPERTY_LONGS, True, AnyPropertyType, &type, &format, &count,
                           &remaining, &data);
        for (unsigned long i = 0; data != NULL && i < count; i++) {
            putchar(data[i] == '\n' ? '|' : data[i]);
        }
        XFree(data);
    } else {
        printf("none");
    }
    putchar('\n');
}

static void answer(Display *display, const XSelectionRequestEvent *request, Atom utf8)
{
    XEvent reply = {0};
    int is_supported = request->target == utf8;
    if (is_supported) {
        XChangeProperty(display, request->requestor, request->property, utf8, 8, PropModeReplace,
                        (const unsigned char *)OFFERED_TEXT, (int)strlen(OFFERED_TEXT));
    }
    reply.xselection.type = SelectionNotify;
    reply.xselection.requestor = request->requestor;
    reply.xselection.selection = request->selection;
    reply.xselection.target = request->target;
    reply.xselection.property = is_supported ? request->property : None;
    reply.xselection.time = request->time;
    XSendEvent(display, request->requestor, False, NoEventMask, &reply);
    XFlush(display);
}

/* Serves requests until one has been answered or the wait runs out; returns whether one was. */
static int serve_one_request(Display *display, Atom utf8)
{
    struct pollfd descriptor = {.fd = ConnectionNumber(display), .events = POLLIN};
    for (int rounds = 0; rounds < WAIT_MILLISECONDS / 10; rounds++) {
        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == SelectionRequest) {
                answer(display, &event.xselectionrequest, utf8);
                return 1;
            }
        }
        poll(&descriptor, 1, 10);
    }
    return 0;
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        puts("no display");
        return 1;
    }
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 10, 10, 0, 0, 0);
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    Atom property = XInternAtom(display, "XSEL_DATA", False);

    print_clipboard(display, window, clipboard, utf8, property);
    XSetSelectionOwner(display, clipboard, window, CurrentTime);
    printf("owner_is_us=%d\n", XGetSelectionOwner(display, clipboard) == window);
    printf("served=%d\n", serve_one_request(display, utf8));
    XCloseDisplay(display);
    puts("done");
    return 0;
}
