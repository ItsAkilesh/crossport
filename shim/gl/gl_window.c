/*
 * File: shim/gl/gl_window.c
 * Purpose: Give an X window a device context with an OpenGL pixel format
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * A window's pixel format can be set once, so every window and every context shares this one format:
 * 32-bit colour with alpha, 24-bit depth, 8-bit stencil, double buffered. GLX creates contexts with no
 * drawable, so they are made on a small hidden window with the same format; WGL lets a context draw to any
 * device context of a matching format.
 */

#include "gl_window.h"
#include "xlib_internal.h"

#define COLOR_BITS 32
#define ALPHA_BITS 8
#define DEPTH_BITS 24
#define STENCIL_BITS 8
#define DESCRIPTOR_VERSION 1
#define BOOTSTRAP_CLASS "STATIC"

/* Sets the shim's one pixel format on a device context that has none. */
static int ensure_pixel_format(HDC dc)
{
    if (GetPixelFormat(dc) != 0) {
        return 1;
    }
    PIXELFORMATDESCRIPTOR descriptor = {.nSize = sizeof(descriptor), .nVersion = DESCRIPTOR_VERSION,
                                        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                        .iPixelType = PFD_TYPE_RGBA, .cColorBits = COLOR_BITS,
                                        .cAlphaBits = ALPHA_BITS, .cDepthBits = DEPTH_BITS,
                                        .cStencilBits = STENCIL_BITS, .iLayerType = PFD_MAIN_PLANE};
    int format = ChoosePixelFormat(dc, &descriptor);
    return format != 0 && SetPixelFormat(dc, format, &descriptor);
}

HDC gl_window_dc(uint64_t drawable)
{
    HWND window = xcb_window_handle((XcbId)drawable);
    HDC dc = window != NULL ? GetDC(window) : NULL;
    if (dc == NULL || !ensure_pixel_format(dc)) {
        return NULL;
    }
    xl_window_mark_gl_owned((XlId)drawable);
    return dc;
}

static INIT_ONCE g_bootstrap_once = INIT_ONCE_STATIC_INIT;
static HDC g_bootstrap_dc;

static BOOL CALLBACK create_bootstrap(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    HWND window = CreateWindowExA(0, BOOTSTRAP_CLASS, "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    HDC dc = window != NULL ? GetDC(window) : NULL;
    if (dc != NULL && ensure_pixel_format(dc)) {
        g_bootstrap_dc = dc;
    }
    return TRUE;
}

HDC gl_bootstrap_dc(void)
{
    InitOnceExecuteOnce(&g_bootstrap_once, create_bootstrap, NULL, NULL);
    return g_bootstrap_dc;
}

HDC gl_window_existing_dc(uint64_t drawable)
{
    HWND window = xcb_window_handle((XcbId)drawable);
    return window != NULL ? GetDC(window) : NULL;
}
