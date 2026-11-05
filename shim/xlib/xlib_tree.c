/*
 * File: shim/xlib/xlib_tree.c
 * Purpose: XQueryTree: the window hierarchy as the shim keeps it
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Every window of the shim is a child of the root window, and no window has children of its own (there is no
 * reparenting window manager). So the parent of any window is the root, the root has no parent, and the children
 * list is empty. Toolkits walk up with XQueryTree to find the frame a window manager put around a window; here the
 * first step reaches the root and they stop.
 */

#include "xlib_internal.h"

LIBC_FN int sysv_XQueryTree(XlDisplay *display, XlId window, XlId *root, XlId *parent, XlId **children, unsigned *count)
{
    xl_next_request(display);
    XlId root_window = display->screens[display->default_screen].root;
    *root = root_window;
    *parent = window == root_window ? 0 : root_window;
    *children = NULL;
    *count = 0;
    return 1;
}
