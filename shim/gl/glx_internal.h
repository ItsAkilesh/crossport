/*
 * File: shim/gl/glx_internal.h
 * Purpose: Constants and shared declarations of the GLX layer (libGL.so.1 and libGLX.so.0 on WGL)
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * There is one framebuffer configuration and one visual, because a window gets one Win32 pixel format:
 * double-buffered RGBA, 8 bits per channel, 24-bit depth, 8-bit stencil, no multisampling.
 */

#ifndef CROSSPORT_GLX_INTERNAL_H
#define CROSSPORT_GLX_INTERNAL_H

#include "gl_window.h"
#include "libc_abi.h"
#include "xlib_internal.h"

#define GLX_SUCCESS 0
#define GLX_BAD_ATTRIBUTE 2
#define GLX_VERSION_MAJOR 1
#define GLX_VERSION_MINOR 4
#define GLX_VENDOR_NAME "Crossport"
#define GLX_EXTENSIONS "GLX_ARB_create_context GLX_ARB_create_context_profile GLX_ARB_get_proc_address GLX_EXT_swap_control GLX_SGI_swap_control"

#define GLX_USE_GL 1
#define GLX_BUFFER_SIZE 2
#define GLX_LEVEL 3
#define GLX_RGBA 4
#define GLX_DOUBLEBUFFER 5
#define GLX_STEREO 6
#define GLX_AUX_BUFFERS 7
#define GLX_RED_SIZE 8
#define GLX_GREEN_SIZE 9
#define GLX_BLUE_SIZE 10
#define GLX_ALPHA_SIZE 11
#define GLX_DEPTH_SIZE 12
#define GLX_STENCIL_SIZE 13
#define GLX_ACCUM_RED_SIZE 14
#define GLX_ACCUM_ALPHA_SIZE 17
#define GLX_CONFIG_CAVEAT 0x20
#define GLX_X_VISUAL_TYPE 0x22
#define GLX_TRANSPARENT_TYPE 0x23
#define GLX_TRANSPARENT_ALPHA_VALUE 0x28
#define GLX_VISUAL_ID 0x800B
#define GLX_DRAWABLE_TYPE 0x8010
#define GLX_RENDER_TYPE 0x8011
#define GLX_X_RENDERABLE 0x8012
#define GLX_FBCONFIG_ID 0x8013
#define GLX_MAX_PBUFFER_WIDTH 0x8016
#define GLX_MAX_PBUFFER_PIXELS 0x8018
#define GLX_SAMPLE_BUFFERS 100000
#define GLX_SAMPLES 100001
#define GLX_FRAMEBUFFER_SRGB_CAPABLE 0x20B2
#define GLX_NONE 0x8000
#define GLX_TRUE_COLOR 0x8002
#define GLX_WINDOW_BIT 1
#define GLX_RGBA_BIT 1
#define GLX_WIDTH 0x801D
#define GLX_HEIGHT 0x801E

#define GLX_CONTEXT_MAJOR_VERSION 0x2091
#define GLX_CONTEXT_MINOR_VERSION 0x2092
#define GLX_CONTEXT_FLAGS 0x2094
#define GLX_CONTEXT_PROFILE_MASK 0x9126

#define GLX_STRING_VENDOR 1
#define GLX_STRING_VERSION 2
#define GLX_STRING_EXTENSIONS 3

typedef struct GlxContext {
    HGLRC native;
    struct GlxContext *share;
    int attributes[9];
    int has_attributes;
    XlDisplay *display;
    XlId drawable;
} GlxContext;

/* The one configuration's value for an attribute: GLX_SUCCESS, or GLX_BAD_ATTRIBUTE for an unknown one. */
int glx_config_attribute(int attribute, int *value);

/* The shim's one visual as a malloc'd XVisualInfo (freed with XFree), or NULL. */
XlVisualInfo *glx_default_visual(XlDisplay *display);

/* The address of a GLX function by name, or NULL. */
const void *glx_function_named(const char *name);

/* glx_config.c */
LIBC_FN XlVisualInfo *sysv_glXChooseVisual(XlDisplay *display, int screen, const int *attributes);
LIBC_FN int sysv_glXGetConfig(XlDisplay *display, const XlVisualInfo *visual, int attribute, int *value);
LIBC_FN void **sysv_glXChooseFBConfig(XlDisplay *display, int screen, const int *attributes, int *count);
LIBC_FN void **sysv_glXGetFBConfigs(XlDisplay *display, int screen, int *count);
LIBC_FN int sysv_glXGetFBConfigAttrib(XlDisplay *display, void *config, int attribute, int *value);
LIBC_FN XlVisualInfo *sysv_glXGetVisualFromFBConfig(XlDisplay *display, void *config);

/* glx_context.c */
LIBC_FN GlxContext *sysv_glXCreateContext(XlDisplay *display, const XlVisualInfo *visual, GlxContext *share, int is_direct);
LIBC_FN GlxContext *sysv_glXCreateNewContext(XlDisplay *display, void *config, int render_type, GlxContext *share,
                                             int is_direct);
LIBC_FN GlxContext *sysv_glXCreateContextAttribsARB(XlDisplay *display, void *config, GlxContext *share, int is_direct,
                                                    const int *attributes);
LIBC_FN void sysv_glXDestroyContext(XlDisplay *display, GlxContext *context);
LIBC_FN int sysv_glXMakeCurrent(XlDisplay *display, XlId drawable, GlxContext *context);
LIBC_FN int sysv_glXMakeContextCurrent(XlDisplay *display, XlId draw, XlId read, GlxContext *context);
LIBC_FN GlxContext *sysv_glXGetCurrentContext(void);
LIBC_FN XlId sysv_glXGetCurrentDrawable(void);
LIBC_FN XlDisplay *sysv_glXGetCurrentDisplay(void);
LIBC_FN void sysv_glXSwapBuffers(XlDisplay *display, XlId drawable);
LIBC_FN int sysv_glXIsDirect(XlDisplay *display, GlxContext *context);

#endif
