/*
 * File: shim/internal.h
 * Purpose: Declarations shared by the shim's translation units: calling-convention macros and fatal-error helpers
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_INTERNAL_H
#define CROSSPORT_INTERNAL_H

#include <stddef.h>
#include <windows.h>

/* Translated code keeps Linux (System V) conventions, so everything it calls uses sysv_abi. */
#define SYSV __attribute__((sysv_abi))
#define EXPORT __declspec(dllexport)
#define NORETURN __attribute__((noreturn))

/* Exit code used when startup cannot continue (mirrors a shell's "command not executable"). */
#define SHIM_STARTUP_FAILURE_EXIT_CODE 127

/* Writes a message to stderr and ends the process. Never returns. */
NORETURN void shim_die(const char *message);

/* Writes all of `data`, retrying on short writes. */
BOOL shim_write_all(HANDLE handle, const char *data, size_t length);

/* Installs the unhandled-exception filter that reports where a crash happened (crash.c). */
void shim_crash_init(void);

#endif
