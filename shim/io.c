/*
 * File: shim/io.c
 * Purpose: Console output (puts) and the fatal-error helpers the rest of the shim relies on
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <string.h>

#include "internal.h"

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

NORETURN void shim_die(const char *message)
{
    shim_write_all(GetStdHandle(STD_ERROR_HANDLE), message, strlen(message));
    ExitProcess(SHIM_STARTUP_FAILURE_EXIT_CODE);
    __builtin_unreachable();
}
