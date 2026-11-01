/*
 * File: shim/gl/gl_window.h
 * Purpose: The device context of an X window with an OpenGL pixel format, for the GLX and EGL layers
 * Author: Akilesh
 * Date: 2026-11-01
 */

#ifndef CROSSPORT_GL_WINDOW_H
#define CROSSPORT_GL_WINDOW_H

#include "gl_wgl.h"

/* The window's device context with a double-buffered RGBA depth-and-stencil pixel format set, or NULL when the
 * id is not a window or no such format exists. The window keeps this one context (its class is CS_OWNDC), so
 * the result is never released. Also marks the window as drawn by OpenGL, so the software buffer never covers it. */
HDC gl_window_dc(uint64_t drawable);

/* A hidden window of the given size with the OpenGL pixel format, for pbuffer surfaces; NULL on failure. Its device
 * context is the window's own and stays valid until the window is destroyed. */
HWND gl_offscreen_create(int width, int height);
HDC gl_offscreen_dc(HWND window);
void gl_offscreen_destroy(HWND window);

/* The device context of a hidden window with the same pixel format, where contexts are created before any
 * drawable exists. NULL when no format is available. */
HDC gl_bootstrap_dc(void);

/* The window's device context as it is, without setting a pixel format; for swapping buffers. NULL when not a window. */
HDC gl_window_existing_dc(uint64_t drawable);

#endif
