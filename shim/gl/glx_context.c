/*
 * File: shim/gl/glx_context.c
 * Purpose: GLX contexts on WGL: create, bind to a window, swap buffers and destroy
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * A context is created on a hidden window with the shim's pixel format (glx has no drawable at that point),
 * so a driver that cannot make the requested version fails at creation, as it should. Binding it to a real
 * window works because every window has the same pixel format.
 */

#include "glx_internal.h"

#define MAX_CONTEXTS 64
#define ATTRIBUTE_PAIRS 4

LIBC_FN void *sysv_calloc(size_t count, size_t size);
LIBC_FN void sysv_free(void *block);

static GlxContext *g_contexts[MAX_CONTEXTS];
static SRWLOCK g_lock = SRWLOCK_INIT;

static int is_context_attribute(int key)
{
    return key == GLX_CONTEXT_MAJOR_VERSION || key == GLX_CONTEXT_MINOR_VERSION || key == GLX_CONTEXT_FLAGS ||
           key == GLX_CONTEXT_PROFILE_MASK;
}

/* Keeps the version, flags and profile pairs (the keys WGL shares with GLX); returns how many pairs were kept. */
static int copy_attributes(int *out, const int *attributes)
{
    int kept = 0;
    for (int i = 0; attributes != NULL && attributes[i] != 0 && kept < ATTRIBUTE_PAIRS; i += 2) {
        if (is_context_attribute(attributes[i])) {
            out[kept * 2] = attributes[i];
            out[kept * 2 + 1] = attributes[i + 1];
            kept++;
        }
    }
    out[kept * 2] = 0;
    return kept;
}

static HGLRC create_native(const GlxContext *context)
{
    const WglApi *api = gl_wgl();
    HDC dc = api != NULL ? gl_bootstrap_dc() : NULL;
    if (dc == NULL) {
        return NULL;
    }
    HGLRC share = context->share != NULL ? context->share->native : NULL;
    if (context->has_attributes) {
        return gl_wgl_create_with_attributes(dc, share, context->attributes);
    }
    HGLRC created = api->create_context(dc);
    if (created != NULL && share != NULL) {
        api->share_lists(share, created);
    }
    return created;
}

static int register_context(GlxContext *context)
{
    int is_registered = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_CONTEXTS && !is_registered; i++) {
        if (g_contexts[i] == NULL) {
            g_contexts[i] = context;
            is_registered = 1;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return is_registered;
}

static void unregister_context(const GlxContext *context)
{
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_CONTEXTS; i++) {
        g_contexts[i] = g_contexts[i] == context ? NULL : g_contexts[i];
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static SYSV GlxContext *new_context(XlDisplay *display, GlxContext *share, const int *attributes)
{
    GlxContext *context = (GlxContext *)sysv_calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->display = display;
    context->share = share;
    context->has_attributes = copy_attributes(context->attributes, attributes) > 0;
    context->native = create_native(context);
    if (context->native == NULL || !register_context(context)) {
        const WglApi *api = gl_wgl();
        if (context->native != NULL && api != NULL) {
            api->delete_context(context->native);
        }
        sysv_free(context);
        return NULL;
    }
    return context;
}

LIBC_FN GlxContext *sysv_glXCreateContext(XlDisplay *display, const XlVisualInfo *visual, GlxContext *share, int is_direct)
{
    (void)visual;
    (void)is_direct;
    return new_context(display, share, NULL);
}

LIBC_FN GlxContext *sysv_glXCreateNewContext(XlDisplay *display, void *config, int render_type, GlxContext *share,
                                             int is_direct)
{
    (void)config;
    (void)render_type;
    (void)is_direct;
    return new_context(display, share, NULL);
}

LIBC_FN GlxContext *sysv_glXCreateContextAttribsARB(XlDisplay *display, void *config, GlxContext *share, int is_direct,
                                                    const int *attributes)
{
    (void)config;
    (void)is_direct;
    return new_context(display, share, attributes);
}

LIBC_FN void sysv_glXDestroyContext(XlDisplay *display, GlxContext *context)
{
    (void)display;
    const WglApi *api = gl_wgl();
    if (context == NULL || api == NULL) {
        return;
    }
    if (api->get_current_context() == context->native) {
        api->make_current(NULL, NULL);
    }
    api->delete_context(context->native);
    unregister_context(context);
    sysv_free(context);
}

LIBC_FN int sysv_glXMakeCurrent(XlDisplay *display, XlId drawable, GlxContext *context)
{
    const WglApi *api = gl_wgl();
    if (api == NULL) {
        return 0;
    }
    if (context == NULL) {
        return api->make_current(NULL, NULL) != 0;
    }
    HDC dc = gl_window_dc(drawable);
    if (dc == NULL || !api->make_current(dc, context->native)) {
        return 0;
    }
    context->display = display;
    context->drawable = drawable;
    return 1;
}

LIBC_FN int sysv_glXMakeContextCurrent(XlDisplay *display, XlId draw, XlId read, GlxContext *context)
{
    (void)read;
    return sysv_glXMakeCurrent(display, draw, context);
}

LIBC_FN GlxContext *sysv_glXGetCurrentContext(void)
{
    const WglApi *api = gl_wgl();
    HGLRC current = api != NULL ? api->get_current_context() : NULL;
    GlxContext *found = NULL;
    AcquireSRWLockShared(&g_lock);
    for (size_t i = 0; i < MAX_CONTEXTS && current != NULL; i++) {
        found = (g_contexts[i] != NULL && g_contexts[i]->native == current) ? g_contexts[i] : found;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

LIBC_FN XlId sysv_glXGetCurrentDrawable(void)
{
    GlxContext *context = sysv_glXGetCurrentContext();
    return context != NULL ? context->drawable : 0;
}

LIBC_FN XlDisplay *sysv_glXGetCurrentDisplay(void)
{
    GlxContext *context = sysv_glXGetCurrentContext();
    return context != NULL ? context->display : NULL;
}

LIBC_FN void sysv_glXSwapBuffers(XlDisplay *display, XlId drawable)
{
    (void)display;
    HDC dc = gl_window_existing_dc(drawable);
    if (dc != NULL) {
        SwapBuffers(dc);
    }
}

LIBC_FN int sysv_glXIsDirect(XlDisplay *display, GlxContext *context)
{
    (void)display;
    (void)context;
    return 1;
}
