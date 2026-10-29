/*
 * File: tests/corpus/xlib_draw/main.c
 * Purpose: Xlib client that opens a window, receives its first events, draws, copies, puts and reads back images, and prints what it saw
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Differential test for the in-process libX11: the same program runs against a real X server (Xvfb) on
 * Linux and against the shim on Windows, and the output must match. It draws only what every X server
 * draws identically (rectangles, horizontal, vertical and 45 degree lines, copies, images).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define WIDTH 200
#define HEIGHT 150
#define BACKGROUND 0x102030ul
#define PIXEL_MASK 0xfffffful
#define FNV_OFFSET 1469598103934665603ull
#define FNV_PRIME 1099511628211ull
#define PATTERN_WIDTH 20
#define PATTERN_HEIGHT 10

static const char *event_name(int type)
{
    switch (type) {
    case MapNotify: return "MapNotify";
    case Expose: return "Expose";
    case ConfigureNotify: return "ConfigureNotify";
    case ClientMessage: return "ClientMessage";
    default: return "other";
    }
}

static void print_display(Display *display)
{
    Screen *screen = DefaultScreenOfDisplay(display);
    Visual *visual = DefaultVisual(display, DefaultScreen(display));
    printf("screens=%d default=%d depth=%d class=%d\n", ScreenCount(display), DefaultScreen(display),
           DefaultDepth(display, DefaultScreen(display)), visual->class);
    printf("masks=%06lx/%06lx/%06lx white=%06lx black=%06lx\n", visual->red_mask, visual->green_mask, visual->blue_mask,
           WhitePixelOfScreen(screen) & PIXEL_MASK, BlackPixelOfScreen(screen) & PIXEL_MASK);
    printf("screen_positive=%d\n", DisplayWidth(display, 0) > 0 && DisplayHeight(display, 0) > 0);
}

static void print_atoms(Display *display)
{
    Atom protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    char *name = XGetAtomName(display, protocols);
    printf("atoms: WM_NAME=%lu STRING=%lu WM_CLASS=%lu dynamic=%d name=%s\n", XInternAtom(display, "WM_NAME", False),
           XInternAtom(display, "STRING", False), XInternAtom(display, "WM_CLASS", False), protocols > 68, name);
    XFree(name);
    printf("same_atom=%d\n", protocols == XInternAtom(display, "WM_PROTOCOLS", False));
}

static Window open_window(Display *display)
{
    XSetWindowAttributes attributes = {.background_pixel = BACKGROUND,
                                       .event_mask = ExposureMask | StructureNotifyMask | KeyPressMask};
    Window window = XCreateWindow(display, DefaultRootWindow(display), 40, 40, WIDTH, HEIGHT, 0, DefaultDepth(display, 0),
                                  InputOutput, DefaultVisual(display, 0), CWBackPixel | CWEventMask, &attributes);
    Atom delete_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XStoreName(display, window, "xlib_draw");
    XSetWMProtocols(display, window, &delete_atom, 1);
    return window;
}

static void wait_for_first_expose(Display *display)
{
    XEvent event;
    printf("events:");
    do {
        XNextEvent(display, &event);
        printf(" %s", event_name(event.type));
    } while (event.type != Expose);
    printf("\nexpose=%dx%d count=%d\n", event.xexpose.width, event.xexpose.height, event.xexpose.count);
}

static void draw_shapes(Display *display, Window window)
{
    GC gc = XCreateGC(display, window, 0, NULL);
    XSetForeground(display, gc, 0xff0000);
    XFillRectangle(display, window, gc, 10, 10, 50, 40);
    XSetForeground(display, gc, 0x00ff00);
    XDrawRectangle(display, window, gc, 70, 10, 30, 30);
    XDrawLine(display, window, gc, 0, 0, 100, 100);
    XDrawLine(display, window, gc, 120, 20, 190, 20);
    XDrawLine(display, window, gc, 120, 30, 120, 90);
    XSetForeground(display, gc, 0x0000ff);
    XDrawPoint(display, window, gc, 150, 60);
    XSetFunction(display, gc, GXxor);
    XSetForeground(display, gc, 0xffffff);
    XFillRectangle(display, window, gc, 10, 10, 20, 20);
    XSetFunction(display, gc, GXcopy);
    XFreeGC(display, gc);
}

static void copy_through_pixmap(Display *display, Window window)
{
    GC gc = XCreateGC(display, window, 0, NULL);
    Pixmap pixmap = XCreatePixmap(display, window, 30, 20, DefaultDepth(display, 0));
    XSetForeground(display, gc, 0xffff00);
    XFillRectangle(display, pixmap, gc, 0, 0, 30, 20);
    XSetForeground(display, gc, 0x00ffff);
    XFillRectangle(display, pixmap, gc, 5, 5, 10, 10);
    XCopyArea(display, pixmap, window, gc, 0, 0, 30, 20, 150, 100);
    XCopyArea(display, window, window, gc, 10, 10, 40, 30, 60, 60);
    XFreePixmap(display, pixmap);
    XFreeGC(display, gc);
}

static void put_pattern(Display *display, Window window)
{
    GC gc = XCreateGC(display, window, 0, NULL);
    uint32_t *data = malloc((size_t)PATTERN_WIDTH * PATTERN_HEIGHT * sizeof(uint32_t));
    for (int y = 0; y < PATTERN_HEIGHT; y++) {
        for (int x = 0; x < PATTERN_WIDTH; x++) {
            data[y * PATTERN_WIDTH + x] = (uint32_t)(x * 12) << 16 | (uint32_t)(y * 25) << 8 | (uint32_t)(x + y);
        }
    }
    XImage *image = XCreateImage(display, DefaultVisual(display, 0), 24, ZPixmap, 0, (char *)data, PATTERN_WIDTH,
                                 PATTERN_HEIGHT, 32, 0);
    XPutImage(display, window, gc, image, 0, 0, 100, 100, PATTERN_WIDTH, PATTERN_HEIGHT);
    XImage *back = XGetImage(display, window, 100, 100, PATTERN_WIDTH, PATTERN_HEIGHT, AllPlanes, ZPixmap);
    int is_equal = 1;
    for (int y = 0; y < PATTERN_HEIGHT; y++) {
        for (int x = 0; x < PATTERN_WIDTH; x++) {
            is_equal = is_equal && (XGetPixel(back, x, y) & PIXEL_MASK) == data[y * PATTERN_WIDTH + x];
        }
    }
    printf("image: bpp=%d depth=%d bytes_per_line=%d round_trip=%d\n", back->bits_per_pixel, back->depth,
           back->bytes_per_line, is_equal);
    XDestroyImage(back);
    XDestroyImage(image);
    XFreeGC(display, gc);
}

static void print_pixels(Display *display, Window window)
{
    XImage *image = XGetImage(display, window, 0, 0, WIDTH, HEIGHT, AllPlanes, ZPixmap);
    uint64_t hash = FNV_OFFSET;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            hash = (hash ^ (XGetPixel(image, x, y) & PIXEL_MASK)) * FNV_PRIME;
        }
    }
    printf("pixels: bg=%06lx rect=%06lx outline=%06lx diagonal=%06lx point=%06lx xor=%06lx copy=%06lx\n",
           XGetPixel(image, 190, 140) & PIXEL_MASK, XGetPixel(image, 40, 45) & PIXEL_MASK,
           XGetPixel(image, 70, 25) & PIXEL_MASK, XGetPixel(image, 50, 50) & PIXEL_MASK,
           XGetPixel(image, 150, 60) & PIXEL_MASK, XGetPixel(image, 15, 15) & PIXEL_MASK,
           XGetPixel(image, 160, 110) & PIXEL_MASK);
    printf("hash=%016llx\n", (unsigned long long)hash);
    XDestroyImage(image);
}

static void print_key(Display *display, Window window, unsigned keycode, unsigned state)
{
    XKeyEvent key = {.type = KeyPress, .display = display, .window = window, .keycode = keycode, .state = state};
    char text[8] = {0};
    KeySym symbol = NoSymbol;
    int length = XLookupString(&key, text, (int)sizeof(text), &symbol, NULL);
    printf("key %u state=%u: sym=%lx len=%d first=%d lookup=%lx\n", keycode, state, symbol, length,
           length > 0 ? text[0] : 0, XLookupKeysym(&key, 0));
}

static void print_keys(Display *display, Window window)
{
    print_key(display, window, 38, 0);
    print_key(display, window, 38, ShiftMask);
    print_key(display, window, 38, LockMask);
    print_key(display, window, 38, ControlMask);
    print_key(display, window, 10, ShiftMask);
    print_key(display, window, 36, 0);
    print_key(display, window, 111, 0);
}

static void send_to_self(Display *display, Window window)
{
    XEvent sent = {.xclient = {.type = ClientMessage, .window = window, .format = 32,
                               .message_type = XInternAtom(display, "CROSSPORT_TEST", False)}};
    sent.xclient.data.l[0] = 1234;
    XSendEvent(display, window, False, NoEventMask, &sent);
    XSync(display, False);
    XEvent received;
    do {
        XNextEvent(display, &received);
    } while (received.type != ClientMessage);
    char *name = XGetAtomName(display, received.xclient.message_type);
    printf("message: %s %s l0=%ld format=%d\n", event_name(received.type), name, received.xclient.data.l[0],
           received.xclient.format);
    XFree(name);
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        puts("no display");
        return 1;
    }
    print_display(display);
    print_atoms(display);
    Window window = open_window(display);
    XMapWindow(display, window);
    wait_for_first_expose(display);
    draw_shapes(display, window);
    copy_through_pixmap(display, window);
    put_pattern(display, window);
    XSync(display, False);
    print_pixels(display, window);
    print_keys(display, window);
    send_to_self(display, window);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    puts("done");
    return 0;
}
