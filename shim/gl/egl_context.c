/*
 * File: shim/gl/egl_context.c
 * Purpose: EGL contexts on WGL: the bound API, create, make current, destroy and the current-state queries
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * OpenGL contexts use WGL directly. OpenGL ES contexts use WGL's ES profile (WGL_EXT_create_context_es2_profile),
 * so a driver without it refuses them with EGL_BAD_MATCH. A context made with no version asks the driver for
 * its default (the compatibility profile, newest version); one that names version 3.2 or later and no profile
 * is a core profile context, as EGL_KHR_create_context specifies.
 */

#include "egl_internal.h"
#include "thread_cell.h"

#define MAX_CONTEXTS 64
#define WGL_CONTEXT_MAJOR_VERSION 0x2091
#define WGL_CONTEXT_MINOR_VERSION 0x2092
#define WGL_CONTEXT_FLAGS 0x2094
#define WGL_CONTEXT_PROFILE_MASK 0x9126
#define WGL_PROFILE_CORE 0x1
#define WGL_PROFILE_ES2 0x4
#define WGL_FLAG_DEBUG 0x1
#define WGL_FLAG_FORWARD_COMPATIBLE 0x2
#define FIRST_CORE_PROFILE_MAJOR 3
#define FIRST_CORE_PROFILE_MINOR 2
#define ATTRIBUTE_SLOTS 9

LIBC_FN void *sysv_calloc(size_t count, size_t size);
LIBC_FN void sysv_free(void *block);

typedef struct {
    int major;
    int minor;
    int profile;
    int flags;
    int has_version;
} ContextRequest;

static EglContext *g_contexts[MAX_CONTEXTS];
static SRWLOCK g_lock = SRWLOCK_INIT;
static ThreadCell g_api_cell = THREAD_CELL_INIT;

static int bound_api(void)
{
    uintptr_t api = (uintptr_t)thread_cell_get(&g_api_cell);
    return api != 0 ? (int)api : EGL_OPENGL_ES_API;
}

LIBC_FN EGLBoolean sysv_eglBindAPI(EGLenum api)
{
    if (api != EGL_OPENGL_API && api != EGL_OPENGL_ES_API) {
        return egl_fail(EGL_BAD_PARAMETER);
    }
    thread_cell_set(&g_api_cell, (void *)(uintptr_t)api);
    return egl_ok();
}

LIBC_FN EGLenum sysv_eglQueryAPI(void)
{
    egl_set_error(EGL_SUCCESS);
    return (EGLenum)bound_api();
}

static int track(EglContext *context)
{
    int is_tracked = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_CONTEXTS && !is_tracked; i++) {
        if (g_contexts[i] == NULL) {
            g_contexts[i] = context;
            is_tracked = 1;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return is_tracked;
}

static int is_context(const void *context)
{
    int is_known = 0;
    AcquireSRWLockShared(&g_lock);
    for (size_t i = 0; i < MAX_CONTEXTS; i++) {
        is_known = is_known || (context != NULL && g_contexts[i] == context);
    }
    ReleaseSRWLockShared(&g_lock);
    return is_known;
}

static void untrack(const EglContext *context)
{
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_CONTEXTS; i++) {
        g_contexts[i] = g_contexts[i] == context ? NULL : g_contexts[i];
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static ContextRequest read_request(const EGLint *attributes)
{
    ContextRequest request = {.major = 1, .minor = 0};
    for (int i = 0; attributes != NULL && attributes[i] != EGL_NONE; i += 2) {
        int value = attributes[i + 1];
        switch (attributes[i]) {
        case EGL_CONTEXT_CLIENT_VERSION:
            request.major = value;
            request.has_version = 1;
            break;
        case EGL_CONTEXT_MINOR_VERSION:
            request.minor = value;
            break;
        case EGL_CONTEXT_OPENGL_PROFILE_MASK:
            request.profile = value;
            break;
        case EGL_CONTEXT_OPENGL_DEBUG:
            request.flags |= value ? WGL_FLAG_DEBUG : 0;
            break;
        case EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE:
            request.flags |= value ? WGL_FLAG_FORWARD_COMPATIBLE : 0;
            break;
        default:
            break;
        }
    }
    return request;
}

/* The WGL attribute list for a request, or NULL (a legacy context) when the program asked for nothing specific. */
static const int *wgl_attributes(const ContextRequest *request, int is_es, int *slots)
{
    int is_modern = request->major > FIRST_CORE_PROFILE_MAJOR ||
                    (request->major == FIRST_CORE_PROFILE_MAJOR && request->minor >= FIRST_CORE_PROFILE_MINOR);
    int profile = is_es ? WGL_PROFILE_ES2 : (request->profile != 0 ? request->profile : (is_modern ? WGL_PROFILE_CORE : 0));
    if (!is_es && !request->has_version && request->flags == 0 && profile == 0) {
        return NULL;
    }
    int next = 0;
    slots[next++] = WGL_CONTEXT_MAJOR_VERSION;
    slots[next++] = request->major;
    slots[next++] = WGL_CONTEXT_MINOR_VERSION;
    slots[next++] = request->minor;
    if (request->flags != 0) {
        slots[next++] = WGL_CONTEXT_FLAGS;
        slots[next++] = request->flags;
    }
    if (profile != 0) {
        slots[next++] = WGL_CONTEXT_PROFILE_MASK;
        slots[next++] = profile;
    }
    slots[next] = 0;
    return slots;
}

LIBC_FN void *sysv_eglCreateContext(void *handle, void *config, void *share, const EGLint *attributes)
{
    EglDisplay *display = egl_display(handle);
    if (display == NULL) {
        return NULL;
    }
    if (!egl_is_config(config)) {
        egl_set_error(EGL_BAD_CONFIG);
        return NULL;
    }
    if (share != NULL && !is_context(share)) {
        egl_set_error(EGL_BAD_CONTEXT);
        return NULL;
    }
    int is_es = bound_api() == EGL_OPENGL_ES_API;
    ContextRequest request = read_request(attributes);
    if (is_es && request.major < 2) {
        egl_set_error(EGL_BAD_MATCH);
        return NULL;
    }
    int slots[ATTRIBUTE_SLOTS];
    EglContext *context = (EglContext *)sysv_calloc(1, sizeof(*context));
    if (context == NULL) {
        egl_set_error(EGL_BAD_ALLOC);
        return NULL;
    }
    context->display = display;
    context->api = is_es ? EGL_OPENGL_ES_API : EGL_OPENGL_API;
    context->major = request.major;
    context->native = gl_create_native_context(share != NULL ? ((EglContext *)share)->native : NULL,
                                               wgl_attributes(&request, is_es, slots));
    if (context->native == NULL || !track(context)) {
        const WglApi *api = gl_wgl();
        if (context->native != NULL && api != NULL) {
            api->delete_context(context->native);
        }
        sysv_free(context);
        egl_set_error(EGL_BAD_MATCH);
        return NULL;
    }
    egl_set_error(EGL_SUCCESS);
    return context;
}

LIBC_FN EGLBoolean sysv_eglDestroyContext(void *handle, void *context)
{
    if (egl_display(handle) == NULL) {
        return EGL_FALSE;
    }
    const WglApi *api = gl_wgl();
    if (!is_context(context) || api == NULL) {
        return egl_fail(EGL_BAD_CONTEXT);
    }
    EglContext *found = (EglContext *)context;
    if (api->get_current_context() == found->native) {
        api->make_current(NULL, NULL);
    }
    api->delete_context(found->native);
    untrack(found);
    sysv_free(found);
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglMakeCurrent(void *handle, void *draw, void *read, void *context)
{
    (void)read;
    const WglApi *api = gl_wgl();
    if (egl_display(handle) == NULL) {
        return EGL_FALSE;
    }
    if (api == NULL) {
        return egl_fail(EGL_BAD_CONTEXT);
    }
    if (context == NULL) {
        return api->make_current(NULL, NULL) ? egl_ok() : egl_fail(EGL_BAD_CONTEXT);
    }
    if (!is_context(context)) {
        return egl_fail(EGL_BAD_CONTEXT);
    }
    if (draw != NULL && !egl_is_surface(draw)) {
        return egl_fail(EGL_BAD_SURFACE);
    }
    EglContext *found = (EglContext *)context;
    HDC dc = egl_surface_dc((const EglSurface *)draw);
    if (dc == NULL || !api->make_current(dc, found->native)) {
        return egl_fail(EGL_BAD_MATCH);
    }
    found->draw = (EglSurface *)draw;
    return egl_ok();
}

static EglContext *current_context(void)
{
    const WglApi *api = gl_wgl();
    HGLRC current = api != NULL ? api->get_current_context() : NULL;
    EglContext *found = NULL;
    AcquireSRWLockShared(&g_lock);
    for (size_t i = 0; i < MAX_CONTEXTS && current != NULL; i++) {
        found = (g_contexts[i] != NULL && g_contexts[i]->native == current) ? g_contexts[i] : found;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

LIBC_FN void *sysv_eglGetCurrentContext(void)
{
    egl_set_error(EGL_SUCCESS);
    return current_context();
}

LIBC_FN void *sysv_eglGetCurrentDisplay(void)
{
    EglContext *context = current_context();
    egl_set_error(EGL_SUCCESS);
    return context != NULL ? context->display : NULL;
}

LIBC_FN void *sysv_eglGetCurrentSurface(EGLint which)
{
    EglContext *context = current_context();
    egl_set_error(EGL_SUCCESS);
    return (context != NULL && (which == EGL_DRAW || which == EGL_READ)) ? context->draw : NULL;
}

LIBC_FN EGLBoolean sysv_eglQueryContext(void *handle, void *context, EGLint attribute, EGLint *value)
{
    if (egl_display(handle) == NULL) {
        return EGL_FALSE;
    }
    if (!is_context(context)) {
        return egl_fail(EGL_BAD_CONTEXT);
    }
    const EglContext *found = (const EglContext *)context;
    switch (attribute) {
    case EGL_CONFIG_ID:
        *value = 1;
        break;
    case EGL_CONTEXT_CLIENT_TYPE:
        *value = found->api;
        break;
    case EGL_CONTEXT_CLIENT_VERSION:
        *value = found->major;
        break;
    case EGL_RENDER_BUFFER:
        *value = EGL_BACK_BUFFER;
        break;
    default:
        return egl_fail(EGL_BAD_ATTRIBUTE);
    }
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglReleaseThread(void)
{
    const WglApi *api = gl_wgl();
    if (api != NULL) {
        api->make_current(NULL, NULL);
    }
    thread_cell_set(&g_api_cell, NULL);
    return egl_ok();
}

/* X, GL and EGL share one queue here, so there is nothing to wait for. */
LIBC_FN EGLBoolean sysv_eglWaitClient(void)
{
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglWaitGL(void)
{
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglWaitNative(EGLint engine)
{
    (void)engine;
    return egl_ok();
}
