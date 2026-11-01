/*
 * File: shim/gl/glx_query.c
 * Purpose: GLX queries, glXGetProcAddress, swap interval and the small window calls
 * Author: Akilesh
 * Date: 2026-11-01
 */

#include <string.h>

#include "gl_loader.h"
#include "glx_internal.h"

#define SWAP_INTERVAL_FUNCTION "wglSwapIntervalEXT"

typedef BOOL(WINAPI *SwapIntervalFn)(int interval);

LIBC_FN int sysv_glXQueryExtension(XlDisplay *display, int *error_base, int *event_base)
{
    (void)display;
    *error_base = 0;
    *event_base = 0;
    return 1;
}

LIBC_FN int sysv_glXQueryVersion(XlDisplay *display, int *major, int *minor)
{
    (void)display;
    *major = GLX_VERSION_MAJOR;
    *minor = GLX_VERSION_MINOR;
    return 1;
}

LIBC_FN const char *sysv_glXQueryExtensionsString(XlDisplay *display, int screen)
{
    (void)display;
    (void)screen;
    return GLX_EXTENSIONS;
}

static const char *server_string(int name)
{
    switch (name) {
    case GLX_STRING_VENDOR:
        return GLX_VENDOR_NAME;
    case GLX_STRING_VERSION:
        return "1.4";
    case GLX_STRING_EXTENSIONS:
        return GLX_EXTENSIONS;
    default:
        return NULL;
    }
}

LIBC_FN const char *sysv_glXGetClientString(XlDisplay *display, int name)
{
    (void)display;
    return server_string(name);
}

LIBC_FN const char *sysv_glXQueryServerString(XlDisplay *display, int screen, int name)
{
    (void)display;
    (void)screen;
    return server_string(name);
}

LIBC_FN int sysv_glXSwapIntervalEXT(XlDisplay *display, XlId drawable, int interval)
{
    (void)display;
    (void)drawable;
    SwapIntervalFn set = (SwapIntervalFn)gl_real_function(SWAP_INTERVAL_FUNCTION);
    return set != NULL && set(interval) ? 0 : 1;
}

LIBC_FN int sysv_glXSwapIntervalSGI(int interval)
{
    return sysv_glXSwapIntervalEXT(NULL, 0, interval);
}

LIBC_FN int sysv_glXQueryDrawable(XlDisplay *display, XlId drawable, int attribute, unsigned *value)
{
    (void)display;
    HWND window = xcb_window_handle((XcbId)drawable);
    RECT client = {0};
    if (window == NULL || !GetClientRect(window, &client)) {
        return 0;
    }
    *value = (unsigned)(attribute == GLX_WIDTH ? client.right : attribute == GLX_HEIGHT ? client.bottom : 0);
    return attribute == GLX_WIDTH || attribute == GLX_HEIGHT;
}

/* A GLX window is the X window itself. */
LIBC_FN XlId sysv_glXCreateWindow(XlDisplay *display, void *config, XlId window, const int *attributes)
{
    (void)display;
    (void)config;
    (void)attributes;
    return window;
}

LIBC_FN void sysv_glXDestroyWindow(XlDisplay *display, XlId window)
{
    (void)display;
    (void)window;
}

/* X and GL share one queue here, so there is nothing to wait for. */
LIBC_FN void sysv_glXWaitGL(void)
{
}

LIBC_FN void sysv_glXWaitX(void)
{
}

typedef struct {
    const char *name;
    const void *function;
} GlxEntry;

static const GlxEntry g_entries[] = {
    {"glXChooseVisual", (const void *)sysv_glXChooseVisual},
    {"glXGetConfig", (const void *)sysv_glXGetConfig},
    {"glXChooseFBConfig", (const void *)sysv_glXChooseFBConfig},
    {"glXGetFBConfigs", (const void *)sysv_glXGetFBConfigs},
    {"glXGetFBConfigAttrib", (const void *)sysv_glXGetFBConfigAttrib},
    {"glXGetVisualFromFBConfig", (const void *)sysv_glXGetVisualFromFBConfig},
    {"glXCreateContext", (const void *)sysv_glXCreateContext},
    {"glXCreateNewContext", (const void *)sysv_glXCreateNewContext},
    {"glXCreateContextAttribsARB", (const void *)sysv_glXCreateContextAttribsARB},
    {"glXDestroyContext", (const void *)sysv_glXDestroyContext},
    {"glXMakeCurrent", (const void *)sysv_glXMakeCurrent},
    {"glXMakeContextCurrent", (const void *)sysv_glXMakeContextCurrent},
    {"glXGetCurrentContext", (const void *)sysv_glXGetCurrentContext},
    {"glXGetCurrentDrawable", (const void *)sysv_glXGetCurrentDrawable},
    {"glXGetCurrentDisplay", (const void *)sysv_glXGetCurrentDisplay},
    {"glXSwapBuffers", (const void *)sysv_glXSwapBuffers},
    {"glXIsDirect", (const void *)sysv_glXIsDirect},
    {"glXQueryExtension", (const void *)sysv_glXQueryExtension},
    {"glXQueryVersion", (const void *)sysv_glXQueryVersion},
    {"glXQueryExtensionsString", (const void *)sysv_glXQueryExtensionsString},
    {"glXGetClientString", (const void *)sysv_glXGetClientString},
    {"glXQueryServerString", (const void *)sysv_glXQueryServerString},
    {"glXSwapIntervalEXT", (const void *)sysv_glXSwapIntervalEXT},
    {"glXSwapIntervalSGI", (const void *)sysv_glXSwapIntervalSGI},
    {"glXQueryDrawable", (const void *)sysv_glXQueryDrawable},
    {"glXCreateWindow", (const void *)sysv_glXCreateWindow},
    {"glXDestroyWindow", (const void *)sysv_glXDestroyWindow},
    {"glXWaitGL", (const void *)sysv_glXWaitGL},
    {"glXWaitX", (const void *)sysv_glXWaitX},
};

const void *glx_function_named(const char *name)
{
    for (size_t i = 0; i < sizeof(g_entries) / sizeof(g_entries[0]); i++) {
        if (strcmp(g_entries[i].name, name) == 0) {
            return g_entries[i].function;
        }
    }
    return NULL;
}

/* glXGetProcAddress answers for GLX functions and every OpenGL command; anything else is NULL, as in Mesa. */
LIBC_FN const void *sysv_glXGetProcAddress(const unsigned char *name)
{
    const char *text = (const char *)name;
    const void *function = glx_function_named(text);
    return function != NULL ? function : gl_thunk_named(text);
}

LIBC_FN const void *sysv_glXGetProcAddressARB(const unsigned char *name)
{
    return sysv_glXGetProcAddress(name);
}
