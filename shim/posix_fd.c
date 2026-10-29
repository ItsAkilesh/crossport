/*
 * File: shim/posix_fd.c
 * Purpose: Descriptor-based POSIX calls: open, close, read, write, pread, pwrite, lseek, fstat, isatty, fcntl and friends
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "fd.h"

enum { F_DUPFD = 0, F_GETFD = 1, F_SETFD = 2, F_GETFL = 3, F_SETFL = 4, F_GETLK = 5, F_SETLK = 6, F_SETLKW = 7 };
enum { LOCK_UNLOCKED = 2 }; /* F_UNLCK */

typedef struct {
    int16_t l_type;
} FlockHead;

LIBC_FN int sysv_open(const char *path, int flags, int mode)
{
    return shim_fd_open(path, flags, mode);
}

LIBC_FN int sysv_open64(const char *path, int flags, int mode)
{
    return shim_fd_open(path, flags, mode);
}

LIBC_FN int sysv_close(int fd)
{
    return shim_fd_close(fd);
}

LIBC_FN int64_t sysv_read(int fd, void *buffer, size_t count)
{
    return shim_fd_read(fd, buffer, count);
}

LIBC_FN int64_t sysv_write(int fd, const void *buffer, size_t count)
{
    return shim_fd_write(fd, buffer, count);
}

LIBC_FN int64_t sysv_lseek(int fd, int64_t offset, int whence)
{
    return shim_fd_seek(fd, offset, whence);
}

LIBC_FN int64_t sysv_lseek64(int fd, int64_t offset, int whence)
{
    return shim_fd_seek(fd, offset, whence);
}

/* Positioned I/O must not disturb the shared file offset, so it is saved and restored around the call. */
static int64_t positioned(int fd, void *buffer, size_t count, int64_t offset, int is_write)
{
    int64_t saved = shim_fd_seek(fd, 0, LINUX_SEEK_CUR);
    if (saved < 0 || shim_fd_seek(fd, offset, LINUX_SEEK_SET) < 0) {
        return -1;
    }
    int64_t moved = is_write ? shim_fd_write(fd, buffer, count) : shim_fd_read(fd, buffer, count);
    shim_fd_seek(fd, saved, LINUX_SEEK_SET);
    return moved;
}

LIBC_FN int64_t sysv_pread64(int fd, void *buffer, size_t count, int64_t offset)
{
    return positioned(fd, buffer, count, offset, 0);
}

LIBC_FN int64_t sysv_pwrite64(int fd, const void *buffer, size_t count, int64_t offset)
{
    return positioned(fd, (void *)buffer, count, offset, 1);
}

LIBC_FN int sysv_fstat(int fd, void *stat_buffer)
{
    HANDLE handle = shim_fd_handle(fd);
    return handle == INVALID_HANDLE_VALUE ? -1 : shim_stat_handle(handle, stat_buffer);
}

LIBC_FN int sysv_fstat64(int fd, void *stat_buffer)
{
    return sysv_fstat(fd, stat_buffer);
}

LIBC_FN int sysv_isatty(int fd)
{
    HANDLE handle = shim_fd_handle(fd);
    DWORD mode;
    if (handle != INVALID_HANDLE_VALUE && GetFileType(handle) == FILE_TYPE_CHAR && GetConsoleMode(handle, &mode)) {
        return 1;
    }
    shim_set_errno(handle == INVALID_HANDLE_VALUE ? LINUX_EBADF : LINUX_ENOTTY);
    return 0;
}

LIBC_FN int sysv_fsync(int fd)
{
    HANDLE handle = shim_fd_handle(fd);
    return handle == INVALID_HANDLE_VALUE ? -1 : (FlushFileBuffers(handle) ? 0 : shim_fail_from_win());
}

LIBC_FN int sysv_ftruncate64(int fd, int64_t length)
{
    HANDLE handle = shim_fd_handle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    int64_t saved = shim_fd_seek(fd, 0, LINUX_SEEK_CUR);
    if (saved < 0 || shim_fd_seek(fd, length, LINUX_SEEK_SET) < 0) {
        return -1;
    }
    int ok = SetEndOfFile(handle);
    shim_fd_seek(fd, saved, LINUX_SEEK_SET);
    return ok ? 0 : shim_fail_from_win();
}

/*
 * Advisory record locks (F_SETLK and friends) are accepted and always succeed: a translated program
 * runs as one process here, so there is no other locker to exclude.
 */
LIBC_FN int sysv_fcntl64(int fd, int command, uint64_t argument)
{
    HANDLE handle = shim_fd_handle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    switch (command) {
    case F_GETFD:
    case F_SETFD:
    case F_SETFL:
        return 0;
    case F_GETFL:
        return LINUX_O_RDWR;
    case F_DUPFD: {
        HANDLE copy;
        return DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)
                   ? shim_fd_adopt(copy, 0)
                   : shim_fail_from_win();
    }
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

LIBC_FN int sysv_fchmod(int fd, unsigned mode)
{
    (void)mode;
    return shim_fd_handle(fd) == INVALID_HANDLE_VALUE ? -1 : 0;
}

LIBC_FN int sysv_fchown(int fd, unsigned owner, unsigned group)
{
    (void)owner;
    (void)group;
    return shim_fd_handle(fd) == INVALID_HANDLE_VALUE ? -1 : 0;
}
