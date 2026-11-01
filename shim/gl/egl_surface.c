/*
 * File: shim/gl/egl_surface.c
 * Purpose: EGL window and pbuffer surfaces: create, destroy, query, swap and swap interval
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * A window surface is an X window and draws through its own device context; a pbuffer is a hidden window of
 * the requested size. Pixmap surfaces are not offered.
 */

#include "egl_internal.h"

#define MAX_SURFACES 64
#define SWAP_INTERVAL_FUNCTION "wglSwapIntervalEXT"

typedef BOOL(WINAPI *SwapIntervalFn)(int interval);

LIBC_FN void *sysv_calloc(size_t count, size_t size);
LIBC_FN void sysv_free(void *block);

static EglSurface *g_surfaces[MAX_SURFACES];
static SRWLOCK g_lock = SRWLOCK_INIT;

int egl_is_surface(const void *surface)
{
    int is_known = 0;
    AcquireSRWLockShared(&g_lock);
    for (size_t i = 0; i < MAX_SURFACES; i++) {
        is_known = is_known || (surface != NULL && g_surfaces[i] == surface);
    }
    ReleaseSRWLockShared(&g_lock);
    return is_known;
}

static int track(EglSurface *surface)
{
    int is_tracked = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_SURFACES && !is_tracked; i++) {
        if (g_surfaces[i] == NULL) {
            g_surfaces[i] = surface;
            is_tracked = 1;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return is_tracked;
}

static void untrack(const EglSurface *surface)
{
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_SURFACES; i++) {
        g_surfaces[i] = g_surfaces[i] == surface ? NULL : g_surfaces[i];
    }
    ReleaseSRWLockExclusive(&g_lock);
}

HDC egl_surface_dc(const EglSurface *surface)
{
    if (surface == NULL) {
        return gl_bootstrap_dc();
    }
    return surface->offscreen != NULL ? gl_offscreen_dc(surface->offscreen) : gl_window_dc(surface->window);
}

static SYSV void *store(EglSurface *surface)
{
    if (surface == NULL) {
        egl_set_error(EGL_BAD_ALLOC);
        return NULL;
    }
    if (!track(surface)) {
        if (surface->offscreen != NULL) {
            gl_offscreen_destroy(surface->offscreen);
        }
        sysv_free(surface);
        egl_set_error(EGL_BAD_ALLOC);
        return NULL;
    }
    egl_set_error(EGL_SUCCESS);
    return surface;
}

static SYSV void *create_window_surface(void *handle, void *config, XlId window)
{
    if (egl_display(handle) == NULL) {
        return NULL;
    }
    HWND native = xcb_window_handle((XcbId)window);
    RECT client = {0};
    if (!egl_is_config(config)) {
        egl_set_error(EGL_BAD_CONFIG);
        return NULL;
    }
    if (native == NULL || !GetClientRect(native, &client)) {
        egl_set_error(EGL_BAD_NATIVE_WINDOW);
        return NULL;
    }
    EglSurface *surface = (EglSurface *)sysv_calloc(1, sizeof(*surface));
    if (surface != NULL) {
        surface->window = window;
        surface->width = client.right;
        surface->height = client.bottom;
    }
    return store(surface);
}

LIBC_FN void *sysv_eglCreateWindowSurface(void *handle, void *config, XlId window, const EGLint *attributes)
{
    (void)attributes;
    return create_window_surface(handle, config, window);
}

/* The platform variant passes a pointer to the native window. */
LIBC_FN void *sysv_eglCreatePlatformWindowSurface(void *handle, void *config, const XlId *window, const EGLint *attributes)
{
    (void)attributes;
    if (window == NULL) {
        egl_set_error(EGL_BAD_NATIVE_WINDOW);
        return NULL;
    }
    return create_window_surface(handle, config, *window);
}

LIBC_FN void *sysv_eglCreatePbufferSurface(void *handle, void *config, const EGLint *attributes)
{
    if (egl_display(handle) == NULL) {
        return NULL;
    }
    if (!egl_is_config(config)) {
        egl_set_error(EGL_BAD_CONFIG);
        return NULL;
    }
    int32_t width = 1;
    int32_t height = 1;
    for (int i = 0; attributes != NULL && attributes[i] != EGL_NONE; i += 2) {
        width = attributes[i] == EGL_WIDTH && attributes[i + 1] > 0 ? attributes[i + 1] : width;
        height = attributes[i] == EGL_HEIGHT && attributes[i + 1] > 0 ? attributes[i + 1] : height;
    }
    EglSurface *surface = (EglSurface *)sysv_calloc(1, sizeof(*surface));
    if (surface != NULL) {
        surface->offscreen = gl_offscreen_create(width, height);
        surface->width = width;
        surface->height = height;
        if (surface->offscreen == NULL) {
            sysv_free(surface);
            surface = NULL;
        }
    }
    return store(surface);
}

LIBC_FN EGLBoolean sysv_eglDestroySurface(void *handle, void *surface)
{
    if (egl_display(handle) == NULL) {
        return EGL_FALSE;
    }
    if (!egl_is_surface(surface)) {
        return egl_fail(EGL_BAD_SURFACE);
    }
    EglSurface *found = (EglSurface *)surface;
    untrack(found);
    if (found->offscreen != NULL) {
        gl_offscreen_destroy(found->offscreen);
    }
    sysv_free(found);
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglQuerySurface(void *handle, void *surface, EGLint attribute, EGLint *value)
{
    if (egl_display(handle) == NULL) {
        return EGL_FALSE;
    }
    if (!egl_is_surface(surface)) {
        return egl_fail(EGL_BAD_SURFACE);
    }
    const EglSurface *found = (const EglSurface *)surface;
    RECT client = {0};
    HWND window = found->offscreen != NULL ? found->offscreen : xcb_window_handle((XcbId)found->window);
    if (attribute == EGL_CONFIG_ID) {
        *value = 1;
    } else if ((attribute == EGL_WIDTH || attribute == EGL_HEIGHT) && window != NULL && GetClientRect(window, &client)) {
        *value = attribute == EGL_WIDTH ? client.right : client.bottom;
    } else {
        return egl_fail(EGL_BAD_ATTRIBUTE);
    }
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglSwapBuffers(void *handle, void *surface)
{
    if (egl_display(handle) == NULL) {
        return EGL_FALSE;
    }
    if (!egl_is_surface(surface)) {
        return egl_fail(EGL_BAD_SURFACE);
    }
    const EglSurface *found = (const EglSurface *)surface;
    if (found->offscreen == NULL) {
        HDC dc = gl_window_existing_dc(found->window);
        if (dc == NULL || !SwapBuffers(dc)) {
            return egl_fail(EGL_BAD_SURFACE);
        }
    }
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglSwapInterval(void *handle, EGLint interval)
{
    if (egl_display(handle) == NULL) {
        return EGL_FALSE;
    }
    SwapIntervalFn set = (SwapIntervalFn)gl_real_function(SWAP_INTERVAL_FUNCTION);
    return set != NULL && set(interval) ? egl_ok() : egl_fail(EGL_BAD_SURFACE);
}
