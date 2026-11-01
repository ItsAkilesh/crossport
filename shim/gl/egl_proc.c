/*
 * File: shim/gl/egl_proc.c
 * Purpose: eglGetProcAddress and the table of EGL entry points it answers with
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * With EGL_KHR_get_all_proc_addresses, eglGetProcAddress answers for every OpenGL command as well as for EGL
 * functions; anything else is NULL.
 */

#include <string.h>

#include "egl_internal.h"
#include "gl_loader.h"

LIBC_FN EGLint sysv_eglGetError(void);
LIBC_FN void *sysv_eglGetDisplay(void *native_display);
LIBC_FN void *sysv_eglGetPlatformDisplay(EGLenum platform, void *native, const void *attributes);
LIBC_FN EGLBoolean sysv_eglInitialize(void *handle, EGLint *major, EGLint *minor);
LIBC_FN EGLBoolean sysv_eglTerminate(void *handle);
LIBC_FN const char *sysv_eglQueryString(void *handle, EGLint name);
LIBC_FN EGLBoolean sysv_eglGetConfigAttrib(void *handle, void *config, EGLint attribute, EGLint *value);
LIBC_FN EGLBoolean sysv_eglGetConfigs(void *handle, void **configs, EGLint size, EGLint *count);
LIBC_FN EGLBoolean sysv_eglChooseConfig(void *handle, const EGLint *attributes, void **configs, EGLint size, EGLint *count);
LIBC_FN void *sysv_eglCreateWindowSurface(void *handle, void *config, XlId window, const EGLint *attributes);
LIBC_FN void *sysv_eglCreatePlatformWindowSurface(void *handle, void *config, const XlId *window, const EGLint *attributes);
LIBC_FN void *sysv_eglCreatePbufferSurface(void *handle, void *config, const EGLint *attributes);
LIBC_FN EGLBoolean sysv_eglDestroySurface(void *handle, void *surface);
LIBC_FN EGLBoolean sysv_eglQuerySurface(void *handle, void *surface, EGLint attribute, EGLint *value);
LIBC_FN EGLBoolean sysv_eglSwapBuffers(void *handle, void *surface);
LIBC_FN EGLBoolean sysv_eglSwapInterval(void *handle, EGLint interval);
LIBC_FN EGLBoolean sysv_eglBindAPI(EGLenum api);
LIBC_FN EGLenum sysv_eglQueryAPI(void);
LIBC_FN void *sysv_eglCreateContext(void *handle, void *config, void *share, const EGLint *attributes);
LIBC_FN EGLBoolean sysv_eglDestroyContext(void *handle, void *context);
LIBC_FN EGLBoolean sysv_eglMakeCurrent(void *handle, void *draw, void *read, void *context);
LIBC_FN void *sysv_eglGetCurrentContext(void);
LIBC_FN void *sysv_eglGetCurrentDisplay(void);
LIBC_FN void *sysv_eglGetCurrentSurface(EGLint which);
LIBC_FN EGLBoolean sysv_eglQueryContext(void *handle, void *context, EGLint attribute, EGLint *value);
LIBC_FN EGLBoolean sysv_eglReleaseThread(void);
LIBC_FN EGLBoolean sysv_eglWaitClient(void);
LIBC_FN EGLBoolean sysv_eglWaitGL(void);
LIBC_FN EGLBoolean sysv_eglWaitNative(EGLint engine);
LIBC_FN void *sysv_eglGetPlatformDisplayEXT(EGLenum platform, void *native, const EGLint *attributes);
LIBC_FN void *sysv_eglCreatePlatformWindowSurfaceEXT(void *handle, void *config, const XlId *window, const EGLint *attributes);

typedef struct {
    const char *name;
    const void *function;
} EglEntry;

#define ENTRY(name) {#name, (const void *)sysv_##name}

static const EglEntry g_entries[] = {
    ENTRY(eglGetError), ENTRY(eglGetDisplay), ENTRY(eglGetPlatformDisplay), ENTRY(eglInitialize),
    ENTRY(eglTerminate), ENTRY(eglQueryString), ENTRY(eglGetConfigAttrib), ENTRY(eglGetConfigs),
    ENTRY(eglChooseConfig), ENTRY(eglCreateWindowSurface), ENTRY(eglCreatePlatformWindowSurface),
    ENTRY(eglCreatePbufferSurface), ENTRY(eglDestroySurface), ENTRY(eglQuerySurface), ENTRY(eglSwapBuffers),
    ENTRY(eglSwapInterval), ENTRY(eglBindAPI), ENTRY(eglQueryAPI), ENTRY(eglCreateContext),
    ENTRY(eglDestroyContext), ENTRY(eglMakeCurrent), ENTRY(eglGetCurrentContext), ENTRY(eglGetCurrentDisplay),
    ENTRY(eglGetCurrentSurface), ENTRY(eglQueryContext), ENTRY(eglReleaseThread), ENTRY(eglWaitClient),
    ENTRY(eglWaitGL), ENTRY(eglWaitNative), ENTRY(eglGetPlatformDisplayEXT),
    ENTRY(eglCreatePlatformWindowSurfaceEXT),
};

const void *egl_function_named(const char *name)
{
    for (size_t i = 0; i < sizeof(g_entries) / sizeof(g_entries[0]); i++) {
        if (strcmp(g_entries[i].name, name) == 0) {
            return g_entries[i].function;
        }
    }
    return NULL;
}

LIBC_FN const void *sysv_eglGetProcAddress(const char *name)
{
    const void *function = egl_function_named(name);
    return function != NULL ? function : gl_thunk_named(name);
}

/* The EXT spellings of the platform functions that Mesa-aware programs look up by name. */
LIBC_FN void *sysv_eglGetPlatformDisplayEXT(EGLenum platform, void *native, const EGLint *attributes)
{
    return sysv_eglGetPlatformDisplay(platform, native, attributes);
}

LIBC_FN void *sysv_eglCreatePlatformWindowSurfaceEXT(void *handle, void *config, const XlId *window, const EGLint *attributes)
{
    return sysv_eglCreatePlatformWindowSurface(handle, config, window, attributes);
}
