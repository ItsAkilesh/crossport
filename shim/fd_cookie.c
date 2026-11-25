/*
 * File: shim/fd_cookie.c
 * Purpose: fopencookie and open_memstream-style custom streams: a stdio stream whose bytes come from, and go to, functions the program supplies
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the compatibility survey (fopencookie blocks 11 programs, among them gzip and many that wrap compression). A cookie stream is a
 * stdio stream over a descriptor of its own kind: the descriptor layer hands reads, writes, seeks and the final close to the program's functions
 * (System V convention, so they are called through pointers of that type), and the stream's buffering is the ordinary one.
 */

#include "libc_abi.h"
#include "stdio_internal.h"

typedef int64_t (SYSV *ReadFunction)(void *cookie, char *buffer, size_t count);
typedef int64_t (SYSV *WriteFunction)(void *cookie, const char *buffer, size_t count);
typedef int (SYSV *SeekFunction)(void *cookie, int64_t *offset, int whence);
typedef int (SYSV *CloseFunction)(void *cookie);

/* cookie_io_functions_t: four optional function pointers. */
typedef struct {
    ReadFunction read;
    WriteFunction write;
    SeekFunction seek;
    CloseFunction close;
} CookieFunctions;

typedef struct {
    void *cookie;
    CookieFunctions functions;
} CookieState;

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);

int shim_cookie_open(void *cookie, const void *functions, int flags)
{
    CookieState *state = sysv_malloc(sizeof(*state));
    HANDLE handle = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (state == NULL || handle == INVALID_HANDLE_VALUE) {
        sysv_free(state);
        return shim_fail(LINUX_ENOMEM);
    }
    state->cookie = cookie;
    state->functions = *(const CookieFunctions *)functions;
    int fd = shim_fd_adopt_kind(FD_KIND_COOKIE, handle, state, flags);
    if (fd < 0) {
        CloseHandle(handle);
        sysv_free(state);
    }
    return fd;
}

/* A missing read function is end of file; a missing write function discards the data, as glibc documents. */
int64_t shim_cookie_read(void *state, void *buffer, size_t count)
{
    CookieState *cookie = state;
    return cookie->functions.read != NULL ? cookie->functions.read(cookie->cookie, buffer, count) : 0;
}

int64_t shim_cookie_write(void *state, const void *buffer, size_t count)
{
    CookieState *cookie = state;
    return cookie->functions.write != NULL ? cookie->functions.write(cookie->cookie, buffer, count) : (int64_t)count;
}

int64_t shim_cookie_seek(void *state, int64_t offset, int whence)
{
    CookieState *cookie = state;
    if (cookie->functions.seek == NULL) {
        return shim_fail(LINUX_ESPIPE);
    }
    int64_t position = offset;
    return cookie->functions.seek(cookie->cookie, &position, whence) == 0 ? position : -1;
}

int shim_cookie_close(void *state)
{
    CookieState *cookie = state;
    int result = cookie->functions.close != NULL ? cookie->functions.close(cookie->cookie) : 0;
    sysv_free(cookie);
    return result;
}

static int has_letter(const char *mode, char letter)
{
    for (; *mode != '\0'; mode++) {
        if (*mode == letter) {
            return 1;
        }
    }
    return 0;
}

/* fopencookie(3): the mode picks readable, writable or both, as for fopen. */
LIBC_FN ShimFile *sysv_fopencookie(void *cookie, const char *mode, CookieFunctions functions)
{
    int is_plus = has_letter(mode, '+');
    int is_readable = mode[0] == 'r' || is_plus;
    int is_writable = mode[0] == 'w' || mode[0] == 'a' || is_plus;
    int access = is_readable && is_writable ? LINUX_O_RDWR : (is_writable ? LINUX_O_WRONLY : LINUX_O_RDONLY);
    int fd = shim_cookie_open(cookie, &functions, access | (mode[0] == 'a' ? LINUX_O_APPEND : 0));
    if (fd < 0) {
        return NULL;
    }
    ShimFile *file = file_new(fd, is_readable, is_writable);
    if (file == NULL) {
        shim_fd_close(fd);
    }
    return file;
}
