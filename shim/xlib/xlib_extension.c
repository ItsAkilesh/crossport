/*
 * File: shim/xlib/xlib_extension.c
 * Purpose: The Xlib extension hooks that libXext, libXrandr, libXi and the other extension libraries call into
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * The extension libraries are translated with the application and sit on top of Xlib's private interface. The
 * in-process X server offers no extensions beyond core (XQueryExtension says no), so XInitExtension reports the
 * same and every extension library concludes "not present". The mutex hook variables must exist and stay NULL:
 * libXext reads them to decide whether to lock, and an unprovided data symbol would be a function address.
 */

#include "xlib_internal.h"

LIBC_DATA(void *, _XLockMutex_fn);
LIBC_DATA(void *, _XUnlockMutex_fn);
LIBC_DATA(char, _Xglobal_lock)[64];

LIBC_FN void *sysv_XInitExtension(XlDisplay *display, const char *name)
{
    (void)display;
    (void)name;
    return NULL;
}

LIBC_FN void *sysv_XAddExtension(XlDisplay *display)
{
    (void)display;
    return NULL;
}
