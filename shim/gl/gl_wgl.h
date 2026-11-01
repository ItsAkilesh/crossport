/*
 * File: shim/gl/gl_wgl.h
 * Purpose: The WGL entry points of opengl32.dll that the GLX and EGL layers use, resolved on first use
 * Author: Akilesh
 * Date: 2026-11-01
 */

#ifndef CROSSPORT_GL_WGL_H
#define CROSSPORT_GL_WGL_H

#include "gl_loader.h"

typedef struct {
    HGLRC(WINAPI *create_context)(HDC dc);
    BOOL(WINAPI *delete_context)(HGLRC context);
    BOOL(WINAPI *make_current)(HDC dc, HGLRC context);
    BOOL(WINAPI *share_lists)(HGLRC first, HGLRC second);
    HGLRC(WINAPI *get_current_context)(void);
    HDC(WINAPI *get_current_dc)(void);
} WglApi;

/* The table, or NULL when opengl32.dll is not available. */
const WglApi *gl_wgl(void);

/* A context created with wglCreateContextAttribsARB (OpenGL 3.0 and later, core profiles); NULL when unsupported.
 * `attributes` is the ARB list: pairs ending in 0. */
HGLRC gl_wgl_create_with_attributes(HDC dc, HGLRC share, const int *attributes);

#endif
