/*
 * File: shim/gl/gl_handwritten.c
 * Purpose: OpenGL commands the generator cannot forward: those that take or return function pointers
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * A callback the program registers is a System V function, but the driver would call it with the Microsoft
 * convention, so the debug-message callbacks are accepted and never called: a program gets no debug output
 * but runs. glGetVkProcAddrNV returns NULL, which is how a driver without Vulkan interop answers.
 */

#include <string.h>

#include "gl_loader.h"

LIBC_FN void sysv_glDebugMessageCallback(const void *callback, const void *user)
{
    (void)callback;
    (void)user;
}

LIBC_FN void sysv_glDebugMessageCallbackARB(const void *callback, const void *user)
{
    (void)callback;
    (void)user;
}

LIBC_FN void sysv_glDebugMessageCallbackKHR(const void *callback, const void *user)
{
    (void)callback;
    (void)user;
}

LIBC_FN void sysv_glDebugMessageCallbackAMD(const void *callback, const void *user)
{
    (void)callback;
    (void)user;
}

LIBC_FN const void *sysv_glGetVkProcAddrNV(const char *name)
{
    (void)name;
    return NULL;
}

typedef struct {
    const char *name;
    const void *function;
} Handwritten;

static const Handwritten g_handwritten[] = {
    {"glDebugMessageCallback", (const void *)sysv_glDebugMessageCallback},
    {"glDebugMessageCallbackARB", (const void *)sysv_glDebugMessageCallbackARB},
    {"glDebugMessageCallbackKHR", (const void *)sysv_glDebugMessageCallbackKHR},
    {"glDebugMessageCallbackAMD", (const void *)sysv_glDebugMessageCallbackAMD},
    {"glGetVkProcAddrNV", (const void *)sysv_glGetVkProcAddrNV},
};

const void *gl_handwritten_thunk(const char *name)
{
    for (size_t i = 0; i < sizeof(g_handwritten) / sizeof(g_handwritten[0]); i++) {
        if (strcmp(g_handwritten[i].name, name) == 0) {
            return g_handwritten[i].function;
        }
    }
    return NULL;
}
