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

/* Applies crossport.env from the executable's directory (envfile.c); call after env_init. */
void shim_apply_env_file(void);

/* Finds or starts the user's D-Bus session bus and sets DBUS_SESSION_BUS_ADDRESS (dbus_session.c); call after env_init. */
void shim_start_session_bus(void);

/* setenv for the shim's own (Microsoft-convention) code. */
int shim_env_set(const char *name, const char *value, int overwrite);

#endif
