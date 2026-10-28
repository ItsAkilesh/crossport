/*
 * File: shim/io.c
 * Purpose: Console output (puts) and the fatal-error helpers the rest of the shim relies on
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <string.h>

#include "internal.h"

#define PUTS_FAILURE (-1)
#define PUTS_SUCCESS 1

BOOL shim_write_all(HANDLE handle, const char *data, size_t length)
{
    while (length > 0) {
        DWORD written = 0;
        if (!WriteFile(handle, data, (DWORD)length, &written, NULL) || written == 0) {
            return FALSE;
        }
        data += written;
        length -= written;
    }
    return TRUE;
}

NORETURN void shim_fail(const char *message)
{
    shim_write_all(GetStdHandle(STD_ERROR_HANDLE), message, strlen(message));
    ExitProcess(SHIM_STARTUP_FAILURE_EXIT_CODE);
    __builtin_unreachable();
}

/* Writes bytes directly, so Windows text mode never rewrites "\n" as "\r\n". */
EXPORT SYSV int puts(const char *text)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!shim_write_all(out, text, strlen(text)) || !shim_write_all(out, "\n", 1)) {
        return PUTS_FAILURE;
    }
    return PUTS_SUCCESS;
}
