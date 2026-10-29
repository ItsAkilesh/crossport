/*
 * File: shim/env_internal.h
 * Purpose: Startup hook of the environment table, called before copy relocations read `environ`
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_ENV_INTERNAL_H
#define CROSSPORT_ENV_INTERNAL_H

#include <string.h>

/* Builds the UTF-8 environment from the Windows one and sets `environ`. */
void env_init(void);

#endif
