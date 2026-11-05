/*
 * File: tests/corpus/xlib_tree/main.c
 * Purpose: Xlib client that walks up and down the window tree with XQueryTree
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Differential test: without a window manager every top-level window is a child of the root and has no
 * children, on a real server (Xvfb) and in the shim. The program prints what XQueryTree reports, and frees
 * the children list the way toolkits do, which crashed a real SDL2 program when the call did nothing.
 */

#include <stdio.h>

#include <X11/Xlib.h>

static void print_tree(Display *display, const char *label, Window window, Window root)
{
    Window reported_root = None;
    Window parent = None;
    Window *children = NULL;
    unsigned count = 0;
    int is_ok = XQueryTree(display, window, &reported_root, &parent, &children, &count);
    printf("%s: ok=%d root_is_root=%d parent=%s children=%u list=%s\n", label, is_ok, reported_root == root,
           parent == None ? "none" : parent == root ? "root" : "other", count, children == NULL ? "null" : "set");
    if (children != NULL) {
        XFree(children);
    }
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        puts("no display");
        return 1;
    }
    Window root = DefaultRootWindow(display);
    Window window = XCreateSimpleWindow(display, root, 10, 10, 50, 40, 0, 0, 0);
    XMapWindow(display, window);
    XSync(display, False);
    print_tree(display, "window", window, root);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    puts("done");
    return 0;
}
