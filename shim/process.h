/*
 * File: shim/process.h
 * Purpose: Child processes: spawning with redirected standard handles, and the table waitpid and kill read
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef CROSSPORT_PROCESS_H
#define CROSSPORT_PROCESS_H

#include "fd.h"

/* What a child's standard handles are: each is a descriptor number in this process, or -1 for the NUL device. */
typedef struct {
    int input;
    int output;
    int error;
} ChildStreams;

/* Streams that make the child share this process's own standard descriptors. */
#define CHILD_STREAMS_INHERIT ((ChildStreams){0, 1, 2})

/*
 * Starts a program. `application` is a wide Windows path (or NULL to take it from the command line),
 * `command_line` a complete Windows command line, `environment` a UTF-16 block or NULL to inherit.
 * Returns the child's id, or -1 with errno set.
 */
int shim_process_start(const wchar_t *application, wchar_t *command_line, const wchar_t *environment,
                       const ChildStreams *streams);

/* Builds a Windows command line from a NULL-terminated UTF-8 argument vector. NULL with errno on failure. */
wchar_t *shim_command_line(char *const argv[]);

/* The Windows environment block for a NULL-terminated "NAME=value" vector, or NULL for none. */
wchar_t *shim_environment_block(char *const envp[]);

/* Waits for a child (any child when `pid` is -1). Returns its id, 0 when `no_wait` and still running, -1 on error. */
int shim_process_wait(int pid, int *status, int no_wait);

/* Terminates a process as `signal_number` would. Returns 0, or -1 with errno set (ESRCH, EPERM). */
int shim_process_terminate(int pid, int signal_number);

#endif
