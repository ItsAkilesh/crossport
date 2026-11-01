/*
 * File: shim/gl/gl_loader.h
 * Purpose: Declarations shared by the generated OpenGL thunks and the loader that finds the real entry points
 * Author: Akilesh
 * Date: 2026-11-01
 */

#ifndef CROSSPORT_GL_LOADER_H
#define CROSSPORT_GL_LOADER_H

#include "libc_abi.h"

/* Sorted by name; the thunk at the same index in g_gl_thunks calls the command of that name. */
extern const size_t g_gl_command_count;
extern const char *const g_gl_command_names[];
extern const void *const g_gl_thunks[];

/* The real (Microsoft convention) function for a command or a WGL entry, or NULL when the driver lacks it
 * or, for functions past OpenGL 1.1, when no context is current yet. */
void *gl_real_function(const char *name);

/* The real function behind the generated thunk with this index. */
void *gl_resolve(size_t index);

/* The hand-written SysV function for a command the generator skips, or NULL (gl_handwritten.c). */
const void *gl_handwritten_thunk(const char *name);

/* The shim's SysV thunk for a command name, or NULL for a name that is not an OpenGL command. */
const void *gl_thunk_named(const char *name);

#endif
