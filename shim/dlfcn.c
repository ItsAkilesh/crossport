/*
 * File: shim/dlfcn.c
 * Purpose: dlopen, dlsym, dlclose, dlerror and dladdr for the libraries the shim itself provides
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Programs look up functions at run time to probe for optional features (for example a C++ runtime asking
 * for a pthread function, or SDL looking for the X11 libraries). The shim answers for its own libraries
 * and for the default namespace; other libraries are reported as not found, which every such program must
 * already handle. Loading translated bundled libraries at run time is not supported yet.
 */

#include <string.h>

#include "libc_abi.h"
#include "thread_cell.h"

#define RTLD_NEXT_HANDLE ((void *)-1)
#define ERROR_TEXT_MAX 256
#define ERROR_MISSING "cannot open shared object file: No such file or directory"
#define ERROR_SYMBOL "undefined symbol"
#define ERROR_HANDLE "invalid handle"

/* The libraries whose symbols the shim exports, by the names programs use. */
static const char *const PROVIDED[] = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
                                       "libvulkan.so.1", "libxcb.so.1", "ld-linux-x86-64.so.2"};

/* The handle every provided library shares: the shim's own module. */
static HMODULE g_self;
static ThreadCell g_error_cell = THREAD_CELL_INIT;

static void set_error(const char *message)
{
    thread_cell_set(&g_error_cell, (void *)message);
}

static HMODULE self_module(void)
{
    if (g_self == NULL) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void *)self_module, &g_self);
    }
    return g_self;
}

static int is_provided(const char *name)
{
    const char *base = strrchr(name, '/');
    base = base != NULL ? base + 1 : name;
    for (size_t i = 0; i < sizeof(PROVIDED) / sizeof(PROVIDED[0]); i++) {
        if (strcmp(base, PROVIDED[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

LIBC_FN void *sysv_dlopen(const char *name, int flags)
{
    (void)flags;
    set_error(NULL);
    if (name == NULL || is_provided(name)) {
        return self_module();
    }
    set_error(ERROR_MISSING);
    return NULL;
}

LIBC_FN void *sysv_dlsym(void *handle, const char *name)
{
    set_error(NULL);
    if (handle != NULL && handle != RTLD_NEXT_HANDLE && handle != self_module()) {
        set_error(ERROR_HANDLE);
        return NULL;
    }
    void *address = (void *)GetProcAddress(self_module(), name);
    if (address == NULL) {
        set_error(ERROR_SYMBOL);
    }
    return address;
}

LIBC_FN int sysv_dlclose(void *handle)
{
    (void)handle;
    return 0;
}

/* The text of the last failure on this thread, once: like glibc, a second call returns NULL. */
LIBC_FN char *sysv_dlerror(void)
{
    const char *message = (const char *)thread_cell_get(&g_error_cell);
    set_error(NULL);
    return (char *)message;
}

/* No symbol-from-address lookup: reporting "not found" is valid and callers handle it. */
LIBC_FN int sysv_dladdr(const void *address, void *info)
{
    (void)address;
    (void)info;
    return 0;
}
