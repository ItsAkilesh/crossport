/*
 * File: shim/string_names.c
 * Purpose: basename, strsignal and strerror_r
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define SIGNAL_NAME_COUNT 32
#define UNKNOWN_SIGNAL_BUFFER 32

LIBC_FN size_t sysv_strlcpy(char *to, const char *from, size_t size);

static const char *const SIGNAL_NAMES[SIGNAL_NAME_COUNT] = {
    NULL, "Hangup", "Interrupt", "Quit", "Illegal instruction", "Trace/breakpoint trap", "Aborted", "Bus error",
    "Floating point exception", "Killed", "User defined signal 1", "Segmentation fault", "User defined signal 2",
    "Broken pipe", "Alarm clock", "Terminated", "Stack fault", "Child exited", "Continued", "Stopped (signal)",
    "Stopped", "Stopped (tty input)", "Stopped (tty output)", "Urgent I/O condition", "CPU time limit exceeded",
    "File size limit exceeded", "Virtual timer expired", "Profiling timer expired", "Window changed", "I/O possible",
    "Power failure", "Bad system call"};

LIBC_FN size_t sysv_strlen(const char *text);

const char *shim_strerror(int value, char *scratch, size_t scratch_size);

/* POSIX basename: may modify its argument, strips trailing slashes. */
LIBC_FN char *sysv___xpg_basename(char *path)
{
    if (path == NULL || *path == '\0') {
        return (char *)".";
    }
    size_t end = sysv_strlen(path);
    while (end > 1 && path[end - 1] == '/') {
        path[--end] = '\0';
    }
    char *slash = NULL;
    for (char *at = path; *at != '\0'; at++) {
        slash = *at == '/' ? at : slash;
    }
    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/* GNU basename: never modifies its argument; "dir/" gives "". */
LIBC_FN char *sysv_basename(const char *path)
{
    const char *slash = NULL;
    for (const char *at = path; *at != '\0'; at++) {
        slash = *at == '/' ? at : slash;
    }
    return (char *)(slash != NULL ? slash + 1 : path);
}

LIBC_FN char *sysv_strsignal(int number)
{
    static char unknown[UNKNOWN_SIGNAL_BUFFER];
    if (number > 0 && number < SIGNAL_NAME_COUNT) {
        return (char *)SIGNAL_NAMES[number];
    }
    char digits[12];
    size_t count = 0;
    unsigned value = number < 0 ? (unsigned)-number : (unsigned)number;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0 && count < sizeof(digits));
    static const char prefix[] = "Unknown signal ";
    size_t length = sizeof(prefix) - 1;
    memcpy(unknown, prefix, length);
    if (number < 0) {
        unknown[length++] = '-';
    }
    for (size_t i = 0; i < count; i++) {
        unknown[length++] = digits[count - 1 - i];
    }
    unknown[length] = '\0';
    return unknown;
}

/* GNU strerror_r: returns the message, using the buffer only for unknown numbers. */
LIBC_FN char *sysv_strerror_r(int number, char *buffer, size_t size)
{
    char scratch[64];
    const char *message = shim_strerror(number, scratch, sizeof(scratch));
    if (message != scratch) {
        return (char *)message;
    }
    sysv_strlcpy(buffer, message, size);
    return buffer;
}

/* XSI strerror_r: copies the message and returns 0, or ERANGE when it does not fit. */
LIBC_FN int sysv___xpg_strerror_r(int number, char *buffer, size_t size)
{
    char scratch[64];
    const char *message = shim_strerror(number, scratch, sizeof(scratch));
    if (size == 0 || sysv_strlen(message) >= size) {
        if (size > 0) {
            sysv_strlcpy(buffer, message, size);
        }
        return LINUX_ERANGE;
    }
    sysv_strlcpy(buffer, message, size);
    return 0;
}
