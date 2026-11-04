/*
 * File: tests/corpus/xlib_input/main.c
 * Purpose: Xlib client that drives its own pointer and keyboard through XTest and prints the events its window receives
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * Differential test for input: the program injects a pointer move, Shift+a, Control+a, a button click and a wheel
 * notch with XTest, and prints each event (kind, keysym, text, button, window and root coordinates, modifier
 * and button state). On Linux the events come from Xvfb. On Windows the shim turns the calls into real input, and
 * its window procedure turns the messages back into X events. The output must match.
 */

#include <stdio.h>
#include <string.h>
#include <sys/select.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#define WIDTH 200
#define HEIGHT 150
#define ORIGIN 40
#define FIRST_X 100
#define FIRST_Y 90
#define SECOND_X 120
#define SECOND_Y 100
#define PARK_X 500
#define PARK_Y 400
#define EXPECTED_EVENTS 14
#define WAIT_STEP_MICROSECONDS 100000
#define WAIT_STEPS 100
#define TEXT_BYTES 8
#define REPORTED_STATE (ShiftMask | ControlMask | Mod1Mask | Button1Mask | Button2Mask | Button3Mask)
#define WHEEL_UP 4

static int g_last_x = -1;
static int g_last_y = -1;

static void wait_for_expose(Display *display)
{
    XEvent event;
    do {
        XNextEvent(display, &event);
    } while (event.type != Expose);
}

static void print_key(const XEvent *event)
{
    XKeyEvent key = event->xkey;
    char text[TEXT_BYTES] = {0};
    KeySym keysym = 0;
    int length = XLookupString(&key, text, sizeof(text) - 1, &keysym, NULL);
    printf("%s keysym=%lx text=", event->type == KeyPress ? "KeyPress" : "KeyRelease", (unsigned long)keysym);
    for (int i = 0; i < length; i++) {
        printf("%02x", (unsigned char)text[i]);
    }
    printf("%s state=%x\n", length == 0 ? "-" : "", key.state & REPORTED_STATE);
}

/* Pointer moves are reported once per distinct position: Windows can post two messages for one move. */
static int print_motion(const XEvent *event)
{
    const XMotionEvent *motion = &event->xmotion;
    if (motion->x == g_last_x && motion->y == g_last_y) {
        return 0;
    }
    g_last_x = motion->x;
    g_last_y = motion->y;
    printf("MotionNotify x=%d y=%d root=%d,%d state=%x\n", motion->x, motion->y, motion->x_root, motion->y_root,
           motion->state & REPORTED_STATE);
    return 1;
}

static void print_button(const XEvent *event)
{
    const XButtonEvent *button = &event->xbutton;
    printf("%s button=%u x=%d y=%d root=%d,%d state=%x\n", event->type == ButtonPress ? "ButtonPress" : "ButtonRelease",
           button->button, button->x, button->y, button->x_root, button->y_root, button->state & REPORTED_STATE);
}

/* Prints the events that carry input and returns how many were printed. */
static int print_event(const XEvent *event)
{
    switch (event->type) {
    case KeyPress:
    case KeyRelease:
        print_key(event);
        return 1;
    case ButtonPress:
    case ButtonRelease:
        print_button(event);
        return 1;
    case MotionNotify:
        return print_motion(event);
    default:
        return 0;
    }
}

static void tap(Display *display, KeySym keysym, int is_press)
{
    XTestFakeKeyEvent(display, XKeysymToKeycode(display, keysym), is_press, 0);
}

static void type_with_modifier(Display *display, KeySym modifier)
{
    tap(display, modifier, True);
    tap(display, XK_a, True);
    tap(display, XK_a, False);
    tap(display, modifier, False);
}

static void inject_input(Display *display)
{
    XTestFakeMotionEvent(display, DefaultScreen(display), FIRST_X, FIRST_Y, 0);
    type_with_modifier(display, XK_Shift_L);
    type_with_modifier(display, XK_Control_L);
    XTestFakeButtonEvent(display, Button1, True, 0);
    XTestFakeButtonEvent(display, Button1, False, 0);
    XTestFakeButtonEvent(display, WHEEL_UP, True, 0);
    XTestFakeButtonEvent(display, WHEEL_UP, False, 0);
    XTestFakeMotionEvent(display, DefaultScreen(display), SECOND_X, SECOND_Y, 0);
    XFlush(display);
}

/* Reads events until the expected number of input events arrived, or the wait runs out. */
static int collect(Display *display)
{
    int printed = 0;
    for (int step = 0; step < WAIT_STEPS && printed < EXPECTED_EVENTS; step++) {
        while (XPending(display) > 0 && printed < EXPECTED_EVENTS) {
            XEvent event;
            XNextEvent(display, &event);
            printed += print_event(&event);
        }
        if (printed < EXPECTED_EVENTS) {
            fd_set readable;
            struct timeval wait = {0, WAIT_STEP_MICROSECONDS};
            FD_ZERO(&readable);
            FD_SET(ConnectionNumber(display), &readable);
            select(ConnectionNumber(display) + 1, &readable, NULL, NULL, &wait);
        }
    }
    return printed;
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        puts("no display");
        return 1;
    }
    int event_base = 0;
    int error_base = 0;
    int major = 0;
    int minor = 0;
    printf("xtest=%d\n", XTestQueryExtension(display, &event_base, &error_base, &major, &minor));
    /* The pointer keeps its position between runs on Windows; start with it away from where the window will be. */
    XTestFakeMotionEvent(display, DefaultScreen(display), PARK_X, PARK_Y, 0);
    XSync(display, False);
    Window root = DefaultRootWindow(display);
    XSetWindowAttributes attributes = {0};
    attributes.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                            PointerMotionMask | StructureNotifyMask;
    Window window = XCreateWindow(display, root, ORIGIN, ORIGIN, WIDTH, HEIGHT, 0, CopyFromParent, InputOutput, CopyFromParent,
                                  CWEventMask, &attributes);
    XMapWindow(display, window);
    wait_for_expose(display);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XSync(display, False);
    XWindowAttributes geometry;
    XGetWindowAttributes(display, window, &geometry);
    printf("window at %d,%d size %dx%d\n", geometry.x, geometry.y, geometry.width, geometry.height);
    inject_input(display);
    int printed = collect(display);
    printf("received=%d of %d\n", printed, EXPECTED_EVENTS);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    puts("done");
    return 0;
}
