/*
 * File: shim/gl/egl_internal.h
 * Purpose: Constants, handle types and shared declarations of the EGL layer (libEGL.so.1 on WGL)
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * One display, one framebuffer configuration (double-buffered RGBA, 8 bits per channel, 24-bit depth,
 * 8-bit stencil, no multisampling) and window and pbuffer surfaces, all on the shim's one Win32 pixel format.
 * Contexts are OpenGL contexts, or OpenGL ES 2 and later through WGL's ES profile.
 */

#ifndef CROSSPORT_EGL_INTERNAL_H
#define CROSSPORT_EGL_INTERNAL_H

#include "gl_window.h"
#include "libc_abi.h"
#include "xlib_internal.h"

typedef int32_t EGLint;
typedef uint32_t EGLBoolean;
typedef uint32_t EGLenum;

#define EGL_TRUE 1
#define EGL_FALSE 0
#define EGL_NONE 0x3038

#define EGL_SUCCESS 0x3000
#define EGL_NOT_INITIALIZED 0x3001
#define EGL_BAD_ALLOC 0x3003
#define EGL_BAD_ATTRIBUTE 0x3004
#define EGL_BAD_CONFIG 0x3005
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_MATCH 0x3009
#define EGL_BAD_NATIVE_WINDOW 0x300B
#define EGL_BAD_PARAMETER 0x300C
#define EGL_BAD_SURFACE 0x300D

#define EGL_BUFFER_SIZE 0x3020
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_CONFIG_CAVEAT 0x3027
#define EGL_CONFIG_ID 0x3028
#define EGL_LEVEL 0x3029
#define EGL_MAX_PBUFFER_HEIGHT 0x302A
#define EGL_MAX_PBUFFER_PIXELS 0x302B
#define EGL_MAX_PBUFFER_WIDTH 0x302C
#define EGL_NATIVE_RENDERABLE 0x302D
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_NATIVE_VISUAL_TYPE 0x302F
#define EGL_SAMPLES 0x3031
#define EGL_SAMPLE_BUFFERS 0x3032
#define EGL_SURFACE_TYPE 0x3033
#define EGL_TRANSPARENT_TYPE 0x3034
#define EGL_BIND_TO_TEXTURE_RGB 0x3039
#define EGL_BIND_TO_TEXTURE_RGBA 0x303A
#define EGL_MIN_SWAP_INTERVAL 0x303B
#define EGL_MAX_SWAP_INTERVAL 0x303C
#define EGL_LUMINANCE_SIZE 0x303D
#define EGL_ALPHA_MASK_SIZE 0x303E
#define EGL_COLOR_BUFFER_TYPE 0x303F
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_CONFORMANT 0x3042
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_RGB_BUFFER 0x308E

#define EGL_PBUFFER_BIT 0x1
#define EGL_WINDOW_BIT 0x4
#define EGL_PIXMAP_BIT 0x2
#define EGL_OPENGL_ES2_BIT 0x4
#define EGL_OPENGL_BIT 0x8

#define EGL_OPENGL_ES_API 0x30A0
#define EGL_OPENGL_API 0x30A2
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055
#define EGL_CLIENT_APIS 0x308D
#define EGL_RENDER_BUFFER 0x3086
#define EGL_BACK_BUFFER 0x3084
#define EGL_CONTEXT_CLIENT_TYPE 0x3097
#define EGL_DRAW 0x3059
#define EGL_READ 0x305A

#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_DEBUG 0x31B0
#define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE 0x31B1
#define EGL_PLATFORM_X11 0x31D5

#define EGL_VERSION_MAJOR 1
#define EGL_VERSION_MINOR 4
#define EGL_VENDOR_NAME "Crossport"
#define EGL_VERSION_STRING "1.4 Crossport"
#define EGL_API_LIST "OpenGL OpenGL_ES"
#define EGL_CLIENT_EXTENSIONS "EGL_EXT_client_extensions EGL_EXT_platform_base EGL_KHR_platform_x11 EGL_EXT_platform_x11"
#define EGL_DISPLAY_EXTENSIONS "EGL_KHR_create_context EGL_KHR_surfaceless_context EGL_KHR_get_all_proc_addresses"

typedef struct {
    void *native_display;
    int is_initialized;
} EglDisplay;

typedef struct {
    XlId window;
    HWND offscreen;
    int32_t width;
    int32_t height;
} EglSurface;

typedef struct {
    HGLRC native;
    EglSurface *draw;
    EglDisplay *display;
    int api;
    int major;
} EglContext;

/* Records the calling thread's error; every entry point ends by recording EGL_SUCCESS or the failure. */
void egl_set_error(int code);
EGLBoolean egl_fail(int code);
EGLBoolean egl_ok(void);

/* The shim's one display, once it has been initialized; NULL (with the error recorded) for any other handle. */
EglDisplay *egl_display(void *handle);

/* The one configuration's value for an attribute, or EGL_BAD_ATTRIBUTE; EGL_SUCCESS stores `value`. */
int egl_config_attribute(const EglDisplay *display, int attribute, int *value);
int egl_is_config(const void *config);

/* The device context a surface draws to (the bootstrap window's when there is no surface). */
HDC egl_surface_dc(const EglSurface *surface);
int egl_is_surface(const void *surface);

/* The address of an EGL function by name, or NULL. */
const void *egl_function_named(const char *name);

#endif
