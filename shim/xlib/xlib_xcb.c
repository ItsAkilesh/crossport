/*
 * File: shim/xlib/xlib_xcb.c
 * Purpose: libX11-xcb: the xcb connection behind an Xlib display, which Qt's xcb platform plugin asks for
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Xlib and xcb here are two faces of one in-process server, so the connection behind every display is the same
 * one. Qt opens a display with Xlib, asks for its xcb connection, and from then on speaks xcb only; the shim has
 * a single queue of events, so it does not matter who owns it.
 */

#include "xlib_internal.h"

LIBC_FN void *sysv_XGetXCBConnection(XlDisplay *display)
{
    (void)display;
    return xcb_roots();
}

LIBC_FN void sysv_XSetEventQueueOwner(XlDisplay *display, int owner)
{
    (void)display;
    (void)owner;
}
