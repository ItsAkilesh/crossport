/*
 * File: shim/xlib/xlib_extension.c
 * Purpose: The Xlib extension hooks that libXext, libXrandr, libXi and the other extension libraries call into
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * The extension libraries are translated with the application and sit on top of Xlib's private interface. The
 * in-process X server offers no extensions beyond core (XQueryExtension says no), so XInitExtension reports the
 * same and every extension library concludes "not present". XAddExtension still returns a record: cairo and other
 * libraries register their own hooks with it and treat a NULL as out of memory. The mutex hook variables must exist and stay NULL:
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

/* XExtCodes: what XAddExtension hands a library that registers hooks of its own (cairo does, for display close). */
typedef struct {
    int extension;
    int major_opcode;
    int first_event;
    int first_error;
} ExtensionCodes;

/* Registers a client-side extension. The server has none to give it, so only the running number is real. */
LIBC_FN void *sysv_XAddExtension(XlDisplay *display)
{
    static volatile LONG next_number = 128;
    (void)display;
    ExtensionCodes *codes = (ExtensionCodes *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*codes));
    if (codes != NULL) {
        codes->extension = (int)InterlockedIncrement(&next_number);
    }
    return codes;
}
