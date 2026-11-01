/*
 * File: shim/gl/egl_display.c
 * Purpose: EGL errors, the display, version and strings, and the one framebuffer configuration
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * Choosing a configuration filters on the attributes a program can ask for: sizes are minimums, and a request
 * for multisampling, pixmaps or an unsupported client API finds no configuration, as it would on a driver
 * without them.
 */

#include "egl_internal.h"
#include "thread_cell.h"

#define CONFIG_ID 1
#define COLOR_BITS 32
#define CHANNEL_BITS 8
#define DEPTH_BITS 24
#define STENCIL_BITS 8
#define MAX_PBUFFER_SIZE 16384
#define MAX_PBUFFER_PIXELS (16384 * 16384)
#define SUPPORTED_SURFACES (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)
#define SUPPORTED_APIS (EGL_OPENGL_BIT | EGL_OPENGL_ES2_BIT)
#define X_TRUE_COLOR 4

static ThreadCell g_error_cell = THREAD_CELL_INIT;
static EglDisplay g_display;
static int g_config_marker = CONFIG_ID;

void egl_set_error(int code)
{
    thread_cell_set(&g_error_cell, (void *)(uintptr_t)code);
}

EGLBoolean egl_fail(int code)
{
    egl_set_error(code);
    return EGL_FALSE;
}

EGLBoolean egl_ok(void)
{
    egl_set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

LIBC_FN EGLint sysv_eglGetError(void)
{
    EGLint code = (EGLint)(uintptr_t)thread_cell_get(&g_error_cell);
    egl_set_error(EGL_SUCCESS);
    return code != 0 ? code : EGL_SUCCESS;
}

EglDisplay *egl_display(void *handle)
{
    if (handle != &g_display) {
        egl_set_error(EGL_BAD_DISPLAY);
        return NULL;
    }
    if (!g_display.is_initialized) {
        egl_set_error(EGL_NOT_INITIALIZED);
        return NULL;
    }
    return &g_display;
}

LIBC_FN void *sysv_eglGetDisplay(void *native_display)
{
    g_display.native_display = native_display;
    egl_set_error(EGL_SUCCESS);
    return &g_display;
}

/* Only the X11 platform exists; `native` is the Display pointer. */
LIBC_FN void *sysv_eglGetPlatformDisplay(EGLenum platform, void *native, const void *attributes)
{
    (void)attributes;
    if (platform != EGL_PLATFORM_X11) {
        egl_set_error(EGL_BAD_PARAMETER);
        return NULL;
    }
    return sysv_eglGetDisplay(native);
}

LIBC_FN EGLBoolean sysv_eglInitialize(void *handle, EGLint *major, EGLint *minor)
{
    if (handle != &g_display) {
        return egl_fail(EGL_BAD_DISPLAY);
    }
    g_display.is_initialized = 1;
    if (major != NULL) {
        *major = EGL_VERSION_MAJOR;
    }
    if (minor != NULL) {
        *minor = EGL_VERSION_MINOR;
    }
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglTerminate(void *handle)
{
    if (handle != &g_display) {
        return egl_fail(EGL_BAD_DISPLAY);
    }
    g_display.is_initialized = 0;
    return egl_ok();
}

LIBC_FN const char *sysv_eglQueryString(void *handle, EGLint name)
{
    if (handle == NULL && name == EGL_EXTENSIONS) {
        egl_set_error(EGL_SUCCESS);
        return EGL_CLIENT_EXTENSIONS;
    }
    if (egl_display(handle) == NULL) {
        return NULL;
    }
    const char *text = NULL;
    switch (name) {
    case EGL_VENDOR:
        text = EGL_VENDOR_NAME;
        break;
    case EGL_VERSION:
        text = EGL_VERSION_STRING;
        break;
    case EGL_CLIENT_APIS:
        text = EGL_API_LIST;
        break;
    case EGL_EXTENSIONS:
        text = EGL_DISPLAY_EXTENSIONS;
        break;
    default:
        egl_set_error(EGL_BAD_PARAMETER);
        return NULL;
    }
    egl_set_error(EGL_SUCCESS);
    return text;
}

int egl_is_config(const void *config)
{
    return config == &g_config_marker;
}

static int visual_id_of(const EglDisplay *display)
{
    XlDisplay *native = (XlDisplay *)display->native_display;
    return native != NULL ? (int)xl_display_data(native)->visual.visual_id : 0;
}

int egl_config_attribute(const EglDisplay *display, int attribute, int *value)
{
    int result = 0;
    switch (attribute) {
    case EGL_BUFFER_SIZE:
        result = COLOR_BITS;
        break;
    case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE: case EGL_ALPHA_SIZE:
        result = CHANNEL_BITS;
        break;
    case EGL_DEPTH_SIZE:
        result = DEPTH_BITS;
        break;
    case EGL_STENCIL_SIZE:
        result = STENCIL_BITS;
        break;
    case EGL_CONFIG_ID:
        result = CONFIG_ID;
        break;
    case EGL_CONFIG_CAVEAT: case EGL_TRANSPARENT_TYPE:
        result = EGL_NONE;
        break;
    case EGL_MAX_PBUFFER_WIDTH: case EGL_MAX_PBUFFER_HEIGHT:
        result = MAX_PBUFFER_SIZE;
        break;
    case EGL_MAX_PBUFFER_PIXELS:
        result = MAX_PBUFFER_PIXELS;
        break;
    case EGL_NATIVE_RENDERABLE:
        result = EGL_TRUE;
        break;
    case EGL_NATIVE_VISUAL_ID:
        result = visual_id_of(display);
        break;
    case EGL_NATIVE_VISUAL_TYPE:
        result = X_TRUE_COLOR;
        break;
    case EGL_SURFACE_TYPE:
        result = SUPPORTED_SURFACES;
        break;
    case EGL_RENDERABLE_TYPE: case EGL_CONFORMANT:
        result = SUPPORTED_APIS;
        break;
    case EGL_MAX_SWAP_INTERVAL:
        result = 1;
        break;
    case EGL_COLOR_BUFFER_TYPE:
        result = EGL_RGB_BUFFER;
        break;
    case EGL_LEVEL: case EGL_SAMPLES: case EGL_SAMPLE_BUFFERS: case EGL_BIND_TO_TEXTURE_RGB:
    case EGL_BIND_TO_TEXTURE_RGBA: case EGL_MIN_SWAP_INTERVAL: case EGL_LUMINANCE_SIZE: case EGL_ALPHA_MASK_SIZE:
        result = 0;
        break;
    default:
        return EGL_BAD_ATTRIBUTE;
    }
    *value = result;
    return EGL_SUCCESS;
}

LIBC_FN EGLBoolean sysv_eglGetConfigAttrib(void *handle, void *config, EGLint attribute, EGLint *value)
{
    const EglDisplay *display = egl_display(handle);
    if (display == NULL) {
        return EGL_FALSE;
    }
    if (!egl_is_config(config)) {
        return egl_fail(EGL_BAD_CONFIG);
    }
    int code = egl_config_attribute(display, attribute, value);
    return code == EGL_SUCCESS ? egl_ok() : egl_fail(code);
}

static EGLBoolean list_configs(void **configs, EGLint size, EGLint *count)
{
    if (count == NULL) {
        return egl_fail(EGL_BAD_PARAMETER);
    }
    if (configs != NULL && size > 0) {
        configs[0] = &g_config_marker;
    }
    *count = (configs == NULL || size > 0) ? 1 : 0;
    return egl_ok();
}

LIBC_FN EGLBoolean sysv_eglGetConfigs(void *handle, void **configs, EGLint size, EGLint *count)
{
    return egl_display(handle) != NULL ? list_configs(configs, size, count) : EGL_FALSE;
}

/* True when the one configuration satisfies a requested attribute. */
static int is_satisfied(const EglDisplay *display, int attribute, int requested)
{
    int actual = 0;
    switch (attribute) {
    case EGL_BUFFER_SIZE: case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE: case EGL_ALPHA_SIZE:
    case EGL_DEPTH_SIZE: case EGL_STENCIL_SIZE: case EGL_SAMPLES: case EGL_SAMPLE_BUFFERS:
        return egl_config_attribute(display, attribute, &actual) == EGL_SUCCESS && actual >= requested;
    case EGL_SURFACE_TYPE: case EGL_RENDERABLE_TYPE: case EGL_CONFORMANT:
        return egl_config_attribute(display, attribute, &actual) == EGL_SUCCESS && (requested & ~actual) == 0;
    case EGL_CONFIG_ID:
        return requested == CONFIG_ID;
    default:
        return 1;
    }
}

LIBC_FN EGLBoolean sysv_eglChooseConfig(void *handle, const EGLint *attributes, void **configs, EGLint size, EGLint *count)
{
    const EglDisplay *display = egl_display(handle);
    if (display == NULL) {
        return EGL_FALSE;
    }
    for (int i = 0; attributes != NULL && attributes[i] != EGL_NONE; i += 2) {
        if (!is_satisfied(display, attributes[i], attributes[i + 1])) {
            if (count == NULL) {
                return egl_fail(EGL_BAD_PARAMETER);
            }
            *count = 0;
            return egl_ok();
        }
    }
    return list_configs(configs, size, count);
}
