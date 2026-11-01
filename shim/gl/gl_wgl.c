/*
 * File: shim/gl/gl_wgl.c
 * Purpose: Resolve the WGL functions the GLX and EGL layers need, and create OpenGL 3.0+ contexts
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * wglCreateContextAttribsARB is only available through wglGetProcAddress while some context is current, so
 * creating a modern context first makes a throwaway legacy context on the same device context, as every
 * Windows OpenGL loader does.
 */

#include "gl_wgl.h"

typedef HGLRC(WINAPI *CreateContextAttribsFn)(HDC dc, HGLRC share, const int *attributes);

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static WglApi g_api;
static int g_is_available;

static BOOL CALLBACK resolve_api(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    g_api.create_context = (HGLRC(WINAPI *)(HDC))gl_real_function("wglCreateContext");
    g_api.delete_context = (BOOL(WINAPI *)(HGLRC))gl_real_function("wglDeleteContext");
    g_api.make_current = (BOOL(WINAPI *)(HDC, HGLRC))gl_real_function("wglMakeCurrent");
    g_api.share_lists = (BOOL(WINAPI *)(HGLRC, HGLRC))gl_real_function("wglShareLists");
    g_api.get_current_context = (HGLRC(WINAPI *)(void))gl_real_function("wglGetCurrentContext");
    g_api.get_current_dc = (HDC(WINAPI *)(void))gl_real_function("wglGetCurrentDC");
    g_is_available = g_api.create_context != NULL && g_api.delete_context != NULL && g_api.make_current != NULL &&
                     g_api.share_lists != NULL && g_api.get_current_context != NULL && g_api.get_current_dc != NULL;
    return TRUE;
}

const WglApi *gl_wgl(void)
{
    InitOnceExecuteOnce(&g_once, resolve_api, NULL, NULL);
    return g_is_available ? &g_api : NULL;
}

HGLRC gl_wgl_create_with_attributes(HDC dc, HGLRC share, const int *attributes)
{
    const WglApi *api = gl_wgl();
    HGLRC temporary = api != NULL ? api->create_context(dc) : NULL;
    if (temporary == NULL) {
        return NULL;
    }
    HGLRC previous = api->get_current_context();
    HDC previous_dc = api->get_current_dc();
    HGLRC created = NULL;
    if (api->make_current(dc, temporary)) {
        CreateContextAttribsFn create = (CreateContextAttribsFn)gl_real_function("wglCreateContextAttribsARB");
        created = create != NULL ? create(dc, share, attributes) : NULL;
    }
    api->make_current(previous_dc, previous);
    api->delete_context(temporary);
    return created;
}
