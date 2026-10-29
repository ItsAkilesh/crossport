/*
 * File: shim/fd_ext.c
 * Purpose: pipe, pipe2, dup, dup2, dup3, fcntl and ioctl on the descriptor table
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "fd.h"

#define PIPE_BUFFER_BYTES 65536

enum {
    F_DUPFD = 0, F_GETFD = 1, F_SETFD = 2, F_GETFL = 3, F_SETFL = 4, F_GETLK = 5, F_SETLK = 6, F_SETLKW = 7,
    F_DUPFD_CLOEXEC = 1030
};
enum { FD_CLOEXEC_BIT = 1, LOCK_UNLOCKED = 2 }; /* F_UNLCK */
enum { IOCTL_FIONREAD = 0x541b, IOCTL_FIONBIO = 0x5421, IOCTL_TIOCGWINSZ = 0x5413 };

typedef struct {
    int16_t l_type;
} FlockHead;

/* struct winsize. */
typedef struct {
    uint16_t rows;
    uint16_t columns;
    uint16_t x_pixels;
    uint16_t y_pixels;
} WindowSize;

LIBC_FN int sysv_pipe2(int fds[2], int flags)
{
    HANDLE read_end;
    HANDLE write_end;
    if ((flags & ~(LINUX_O_NONBLOCK | LINUX_O_CLOEXEC)) != 0) {
        return shim_fail(LINUX_EINVAL);
    }
    if (!CreatePipe(&read_end, &write_end, NULL, PIPE_BUFFER_BYTES)) {
        return shim_fail_from_win();
    }
    int first = shim_fd_adopt_kind(FD_KIND_PIPE, read_end, NULL, LINUX_O_RDONLY | (flags & LINUX_O_NONBLOCK));
    int second = first < 0 ? -1 : shim_fd_adopt_kind(FD_KIND_PIPE, write_end, NULL, LINUX_O_WRONLY | (flags & LINUX_O_NONBLOCK));
    if (second < 0) {
        int error = *shim_errno_ptr();
        if (first >= 0) {
            shim_fd_close(first);
        } else {
            CloseHandle(read_end);
        }
        CloseHandle(write_end);
        shim_set_errno(error);
        return -1;
    }
    fds[0] = first;
    fds[1] = second;
    return 0;
}

LIBC_FN int sysv_pipe(int fds[2])
{
    return sysv_pipe2(fds, 0);
}

/* Duplicates the handle behind a descriptor; the copy shares the file offset, like POSIX dup. */
static int duplicate_into(int from, int target, int minimum)
{
    FdKind kind = shim_fd_kind(from);
    HANDLE handle = shim_fd_handle(from);
    int flags = shim_fd_flags(from);
    if (handle == INVALID_HANDLE_VALUE || flags < 0) {
        return -1;
    }
    if (kind == FD_KIND_EVENT) {
        return shim_fail(LINUX_EINVAL); /* the counter would have to be shared; not supported yet */
    }
    HANDLE copy;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return shim_fail_from_win();
    }
    if (target >= 0) {
        return shim_fd_adopt_at(target, kind, copy, NULL, flags);
    }
    for (int candidate = minimum; candidate < SHIM_MAX_FDS; candidate++) {
        if (shim_fd_handle(candidate) == INVALID_HANDLE_VALUE && shim_fd_adopt_at(candidate, kind, copy, NULL, flags) >= 0) {
            return candidate;
        }
    }
    CloseHandle(copy);
    return shim_fail(LINUX_EMFILE);
}

LIBC_FN int sysv_dup(int fd)
{
    return duplicate_into(fd, -1, 0);
}

LIBC_FN int sysv_dup2(int from, int to)
{
    if (shim_fd_handle(from) == INVALID_HANDLE_VALUE) {
        return -1;
    }
    return from == to ? to : duplicate_into(from, to, 0);
}

LIBC_FN int sysv_dup3(int from, int to, int flags)
{
    (void)flags;
    return from == to ? shim_fail(LINUX_EINVAL) : sysv_dup2(from, to);
}

/*
 * Advisory record locks (F_SETLK and friends) are accepted and always succeed: a translated program
 * runs as one process here, so there is no other locker to exclude.
 */
LIBC_FN int sysv_fcntl64(int fd, int command, uint64_t argument)
{
    int flags = shim_fd_flags(fd);
    if (flags < 0) {
        return -1;
    }
    switch (command) {
    case F_GETFD:
    case F_SETFD:
        return 0;
    case F_GETFL:
        return flags;
    case F_SETFL:
        return shim_fd_set_nonblocking(fd, (argument & LINUX_O_NONBLOCK) != 0);
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
        return duplicate_into(fd, -1, (int)argument);
    case F_GETLK:
        ((FlockHead *)(uintptr_t)argument)->l_type = LOCK_UNLOCKED;
        return 0;
    case F_SETLK:
    case F_SETLKW:
        return 0;
    default:
        return shim_fail(LINUX_EINVAL);
    }
}

/* glibc exports fcntl as well as fcntl64; large-file builds use the latter. */
LIBC_FN int sysv_fcntl(int fd, int command, uint64_t argument)
{
    return sysv_fcntl64(fd, command, argument);
}

static int ioctl_available(int fd, int *count)
{
    DWORD available = 0;
    HANDLE handle = shim_fd_handle(fd);
    if (shim_fd_kind(fd) == FD_KIND_PIPE && PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) {
        *count = (int)available;
        return 0;
    }
    LARGE_INTEGER size;
    LARGE_INTEGER position;
    LARGE_INTEGER zero = {0};
    if (GetFileSizeEx(handle, &size) && SetFilePointerEx(handle, zero, &position, FILE_CURRENT)) {
        *count = (int)(size.QuadPart > position.QuadPart ? size.QuadPart - position.QuadPart : 0);
        return 0;
    }
    return shim_fail(LINUX_ENOTTY);
}

LIBC_FN int sysv_ioctl(int fd, uint64_t request, uint64_t argument)
{
    if (shim_fd_flags(fd) < 0) {
        return -1;
    }
    switch (request) {
    case IOCTL_FIONREAD:
        return ioctl_available(fd, (int *)(uintptr_t)argument);
    case IOCTL_FIONBIO:
        return shim_fd_set_nonblocking(fd, *(int *)(uintptr_t)argument != 0);
    case IOCTL_TIOCGWINSZ: {
        CONSOLE_SCREEN_BUFFER_INFO info;
        WindowSize *size = (WindowSize *)(uintptr_t)argument;
        int is_console = GetConsoleScreenBufferInfo(shim_fd_handle(fd), &info);
        if (!is_console) {
            return shim_fail(LINUX_ENOTTY);
        }
        *size = (WindowSize){.rows = (uint16_t)(info.srWindow.Bottom - info.srWindow.Top + 1),
                             .columns = (uint16_t)(info.srWindow.Right - info.srWindow.Left + 1)};
        return 0;
    }
    default:
        return shim_fail(LINUX_ENOTTY);
    }
}
