/*
 * File: shim/path.c
 * Purpose: Map Linux paths onto Windows paths: /tmp, /dev/null, /proc/self/exe, drive paths and separators
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "fd.h"
#include "libc_abi.h"

#define SEPARATOR_LINUX '/'
#define SEPARATOR_WINDOWS '\\'

static int utf8_to_wide(const char *text, wchar_t *out, size_t out_count)
{
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, (int)out_count);
    return written > 0 ? 0 : shim_fail(GetLastError() == ERROR_INSUFFICIENT_BUFFER ? LINUX_ENAMETOOLONG : LINUX_EINVAL);
}

static int has_prefix(const char *path, const char *prefix)
{
    size_t length = (size_t)lstrlenA(prefix);
    return strncmp(path, prefix, length) == 0 && (path[length] == '\0' || path[length] == SEPARATOR_LINUX);
}

/* Replaces the leading virtual directory with its real Windows location. */
static int map_virtual(const char *path, wchar_t *out, size_t out_count)
{
    if (strcmp(path, "/dev/null") == 0) {
        return lstrcpynW(out, L"NUL", (int)out_count) != NULL ? 0 : shim_fail(LINUX_ENAMETOOLONG);
    }
    if (strcmp(path, "/dev/tty") == 0) {
        return lstrcpynW(out, L"CONIN$", (int)out_count) != NULL ? 0 : shim_fail(LINUX_ENAMETOOLONG);
    }
    if (strcmp(path, "/proc/self/exe") == 0) {
        return GetModuleFileNameW(NULL, out, (DWORD)out_count) > 0 ? 0 : shim_fail(LINUX_ENOENT);
    }
    if (has_prefix(path, "/tmp")) {
        wchar_t temp[MAX_PATH + 1];
        DWORD length = GetTempPathW(MAX_PATH, temp);
        if (length == 0 || length >= MAX_PATH) {
            return shim_fail(LINUX_ENOENT);
        }
        wchar_t rest[MAX_PATH];
        const char *tail = path + 4;
        if (*tail == SEPARATOR_LINUX) {
            tail++;
        }
        if (utf8_to_wide(tail, rest, MAX_PATH) != 0) {
            return -1;
        }
        return (size_t)length + (size_t)lstrlenW(rest) < out_count
                   ? (lstrcpyW(out, temp), lstrcatW(out, rest), 0)
                   : shim_fail(LINUX_ENAMETOOLONG);
    }
    return 1;
}

int shim_path_to_wide(const char *linux_path, wchar_t *out, size_t out_count)
{
    if (linux_path == NULL || *linux_path == '\0') {
        return shim_fail(LINUX_ENOENT);
    }
    int mapped = map_virtual(linux_path, out, out_count);
    if (mapped <= 0) {
        return mapped;
    }
    if (utf8_to_wide(linux_path, out, out_count) != 0) {
        return -1;
    }
    for (wchar_t *at = out; *at != L'\0'; at++) {
        if (*at == SEPARATOR_LINUX) {
            *at = SEPARATOR_WINDOWS;
        }
    }
    return 0;
}

int shim_path_to_windows(const char *linux_path, char *out, size_t out_size)
{
    wchar_t wide[SHIM_PATH_MAX];
    if (shim_path_to_wide(linux_path, wide, SHIM_PATH_MAX) != 0) {
        return -1;
    }
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)out_size, NULL, NULL) > 0 ? 0
                                                                                           : shim_fail(LINUX_ENAMETOOLONG);
}

int shim_wide_to_utf8(const wchar_t *wide, char *out, size_t out_size)
{
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)out_size, NULL, NULL) > 0 ? 0
                                                                                           : shim_fail(LINUX_ENAMETOOLONG);
}
