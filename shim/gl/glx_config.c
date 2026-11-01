/*
 * File: shim/gl/glx_config.c
 * Purpose: GLX visuals and framebuffer configurations: the single configuration the shim offers and its attributes
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * Choosing a visual or configuration always yields the one that exists; attributes a program asked for are
 * not negotiated. Programs that need multisampling or a different depth see the real values through the
 * attribute queries and can adapt.
 */

#include "glx_internal.h"

#define VISUAL_MASK_ID 0x1
#define CONFIG_ID 1
#define COLOR_BITS 32
#define CHANNEL_BITS 8
#define DEPTH_BITS 24
#define STENCIL_BITS 8

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN XlVisualInfo *sysv_XGetVisualInfo(XlDisplay *display, int64_t mask, const XlVisualInfo *wanted, int *count);

int glx_config_attribute(int attribute, int *value)
{
    int result = 0;
    switch (attribute) {
    case GLX_USE_GL: case GLX_RGBA: case GLX_DOUBLEBUFFER: case GLX_X_RENDERABLE:
        result = 1;
        break;
    case GLX_BUFFER_SIZE:
        result = COLOR_BITS;
        break;
    case GLX_RED_SIZE: case GLX_GREEN_SIZE: case GLX_BLUE_SIZE: case GLX_ALPHA_SIZE:
        result = CHANNEL_BITS;
        break;
    case GLX_DEPTH_SIZE:
        result = DEPTH_BITS;
        break;
    case GLX_STENCIL_SIZE:
        result = STENCIL_BITS;
        break;
    case GLX_LEVEL: case GLX_STEREO: case GLX_AUX_BUFFERS: case GLX_SAMPLE_BUFFERS: case GLX_SAMPLES:
    case GLX_FRAMEBUFFER_SRGB_CAPABLE: case GLX_MAX_PBUFFER_WIDTH: case GLX_MAX_PBUFFER_WIDTH + 1:
    case GLX_MAX_PBUFFER_PIXELS: case GLX_TRANSPARENT_ALPHA_VALUE:
        result = 0;
        break;
    case GLX_CONFIG_CAVEAT: case GLX_TRANSPARENT_TYPE:
        result = GLX_NONE;
        break;
    case GLX_X_VISUAL_TYPE:
        result = GLX_TRUE_COLOR;
        break;
    case GLX_DRAWABLE_TYPE:
        result = GLX_WINDOW_BIT;
        break;
    case GLX_RENDER_TYPE:
        result = GLX_RGBA_BIT;
        break;
    case GLX_FBCONFIG_ID:
        result = CONFIG_ID;
        break;
    default:
        if (attribute >= GLX_ACCUM_RED_SIZE && attribute <= GLX_ACCUM_ALPHA_SIZE) {
            break;
        }
        return GLX_BAD_ATTRIBUTE;
    }
    *value = result;
    return GLX_SUCCESS;
}

XlVisualInfo *glx_default_visual(XlDisplay *display)
{
    XlVisualInfo wanted = {0};
    wanted.visual_id = xl_display_data(display)->visual.visual_id;
    int count = 0;
    return sysv_XGetVisualInfo(display, VISUAL_MASK_ID, &wanted, &count);
}

LIBC_FN XlVisualInfo *sysv_glXChooseVisual(XlDisplay *display, int screen, const int *attributes)
{
    (void)attributes;
    return screen == 0 ? glx_default_visual(display) : NULL;
}

LIBC_FN int sysv_glXGetConfig(XlDisplay *display, const XlVisualInfo *visual, int attribute, int *value)
{
    if (attribute == GLX_VISUAL_ID) {
        *value = (int)visual->visual_id;
        return GLX_SUCCESS;
    }
    (void)display;
    return glx_config_attribute(attribute, value);
}

/* Configurations are an array of pointers to the one configuration marker. */
static int g_config_marker = CONFIG_ID;

static void **configuration_list(int *count)
{
    void **list = (void **)sysv_malloc(sizeof(void *));
    *count = 0;
    if (list != NULL) {
        list[0] = &g_config_marker;
        *count = 1;
    }
    return list;
}

LIBC_FN void **sysv_glXChooseFBConfig(XlDisplay *display, int screen, const int *attributes, int *count)
{
    (void)display;
    (void)attributes;
    *count = 0;
    return screen == 0 ? configuration_list(count) : NULL;
}

LIBC_FN void **sysv_glXGetFBConfigs(XlDisplay *display, int screen, int *count)
{
    return sysv_glXChooseFBConfig(display, screen, NULL, count);
}

LIBC_FN int sysv_glXGetFBConfigAttrib(XlDisplay *display, void *config, int attribute, int *value)
{
    if (config != &g_config_marker) {
        return GLX_BAD_ATTRIBUTE;
    }
    if (attribute == GLX_VISUAL_ID) {
        *value = (int)xl_display_data(display)->visual.visual_id;
        return GLX_SUCCESS;
    }
    return glx_config_attribute(attribute, value);
}

LIBC_FN XlVisualInfo *sysv_glXGetVisualFromFBConfig(XlDisplay *display, void *config)
{
    return config == &g_config_marker ? glx_default_visual(display) : NULL;
}
