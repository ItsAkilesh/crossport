/*
 * File: shim/gl/gl_window.c
 * Purpose: Give an X window a device context with an OpenGL pixel format
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * A window's pixel format can be set once, so every window and every context shares this one format:
 * 32-bit colour with alpha, 24-bit depth, 8-bit stencil, double buffered. GLX creates contexts with no
 * drawable, so they are made on a small hidden window with the same format (EGL pbuffers are hidden windows
 * of the requested size); WGL lets a context draw to any
 * device context of a matching format.
 */

#include "gl_window.h"
#include "xlib_internal.h"

#define COLOR_BITS 32
#define ALPHA_BITS 8
#define DEPTH_BITS 24
#define STENCIL_BITS 8
#define DESCRIPTOR_VERSION 1
#define OFFSCREEN_CLASS "crossport_gl_offscreen"

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

static INIT_ONCE g_class_once = INIT_ONCE_STATIC_INIT;
static ATOM g_class;

static BOOL CALLBACK register_class(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    WNDCLASSEXA window_class = {.cbSize = sizeof(window_class), .style = CS_OWNDC, .lpfnWndProc = DefWindowProcA,
                                .hInstance = GetModuleHandleA(NULL), .lpszClassName = OFFSCREEN_CLASS};
    g_class = RegisterClassExA(&window_class);
    return TRUE;
}

HWND gl_offscreen_create(int width, int height)
{
    InitOnceExecuteOnce(&g_class_once, register_class, NULL, NULL);
    if (g_class == 0) {
        return NULL;
    }
    HWND window = CreateWindowExA(0, OFFSCREEN_CLASS, "", WS_POPUP, 0, 0, width, height, NULL, NULL,
                                  GetModuleHandleA(NULL), NULL);
    HDC dc = window != NULL ? GetDC(window) : NULL;
    if (dc == NULL || !ensure_pixel_format(dc)) {
        if (window != NULL) {
            DestroyWindow(window);
        }
        return NULL;
    }
    return window;
}

HDC gl_offscreen_dc(HWND window)
{
    return GetDC(window);
}

void gl_offscreen_destroy(HWND window)
{
    DestroyWindow(window);
}

static INIT_ONCE g_bootstrap_once = INIT_ONCE_STATIC_INIT;
static HWND g_bootstrap_window;

static BOOL CALLBACK create_bootstrap(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    g_bootstrap_window = gl_offscreen_create(1, 1);
    return TRUE;
}

HDC gl_bootstrap_dc(void)
{
    InitOnceExecuteOnce(&g_bootstrap_once, create_bootstrap, NULL, NULL);
    return g_bootstrap_window != NULL ? gl_offscreen_dc(g_bootstrap_window) : NULL;
}

HDC gl_window_existing_dc(uint64_t drawable)
{
    HWND window = xcb_window_handle((XcbId)drawable);
    return window != NULL ? GetDC(window) : NULL;
}
