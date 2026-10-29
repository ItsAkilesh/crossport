/*
 * File: shim/posix_fd.c
 * Purpose: Descriptor-based POSIX calls: open, close, read, write, pread, pwrite, lseek, fstat, isatty and friends
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "fd.h"

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

/* struct iovec. */
typedef struct {
    void *base;
    size_t length;
} IoVec;

LIBC_FN int64_t sysv_readv(int fd, const IoVec *vectors, int count)
{
    int64_t total = 0;
    for (int i = 0; i < count; i++) {
        int64_t got = shim_fd_read(fd, vectors[i].base, vectors[i].length);
        if (got < 0) {
            return total > 0 ? total : -1;
        }
        total += got;
        if ((size_t)got < vectors[i].length) {
            break;
        }
    }
    return total;
}

LIBC_FN int64_t sysv_writev(int fd, const IoVec *vectors, int count)
{
    int64_t total = 0;
    for (int i = 0; i < count; i++) {
        int64_t wrote = shim_fd_write(fd, vectors[i].base, vectors[i].length);
        if (wrote < 0) {
            return total > 0 ? total : -1;
        }
        total += wrote;
    }
    return total;
}
