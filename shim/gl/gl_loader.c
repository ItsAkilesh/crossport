/*
 * File: shim/gl/gl_loader.c
 * Purpose: Load opengl32.dll on first use and resolve the real entry points behind the generated OpenGL thunks
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * opengl32.dll exports OpenGL 1.1 and the WGL functions directly. Everything newer comes from
 * wglGetProcAddress, which only answers while a context is current, so a thunk whose target is not found
 * is retried on its next call. Programs that never use OpenGL do not need the library.
 */

#include <stdlib.h>
#include <string.h>

#include "gl_loader.h"

#define OPENGL_LIBRARY "opengl32.dll"
#define WGL_LOOKUP "wglGetProcAddress"
/* Some drivers answer a failed wglGetProcAddress with 1, 2, 3 or -1 instead of NULL. */
#define WGL_FAILURE_MAX 3

typedef void *(WINAPI *WglGetProcAddressFn)(const char *name);

static INIT_ONCE g_load_once = INIT_ONCE_STATIC_INIT;
static HMODULE g_library;
static WglGetProcAddressFn g_wgl_lookup;

static BOOL CALLBACK load_library(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    g_library = LoadLibraryA(OPENGL_LIBRARY);
    if (g_library != NULL) {
        g_wgl_lookup = (WglGetProcAddressFn)(void *)GetProcAddress(g_library, WGL_LOOKUP);
    }
    return TRUE;
}

static int is_function(const void *address)
{
    uintptr_t value = (uintptr_t)address;
    return value > WGL_FAILURE_MAX && value != (uintptr_t)-1;
}

void *gl_real_function(const char *name)
{
    InitOnceExecuteOnce(&g_load_once, load_library, NULL, NULL);
    if (g_library == NULL) {
        return NULL;
    }
    void *exported = (void *)GetProcAddress(g_library, name);
    if (exported != NULL) {
        return exported;
    }
    void *provided = g_wgl_lookup != NULL ? g_wgl_lookup(name) : NULL;
    return is_function(provided) ? provided : NULL;
}

void *gl_resolve(size_t index)
{
    return gl_real_function(g_gl_command_names[index]);
}

static int compare_name(const void *key, const void *entry)
{
    return strcmp((const char *)key, *(const char *const *)entry);
}

const void *gl_thunk_named(const char *name)
{
    const char *const *found = (const char *const *)bsearch(name, g_gl_command_names, g_gl_command_count,
                                                             sizeof(g_gl_command_names[0]), compare_name);
    return found != NULL ? g_gl_thunks[found - g_gl_command_names] : NULL;
}
