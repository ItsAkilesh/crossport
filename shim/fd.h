/*
 * File: shim/fd.h
 * Purpose: The file-descriptor table and path mapping that stdio and the POSIX file calls are built on
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef CROSSPORT_FD_H
#define CROSSPORT_FD_H

#include "libc_abi.h"

#define SHIM_PATH_MAX 4096
#define SHIM_MAX_FDS 1024

/* Linux x86-64 open(2) flags. */
enum {
    LINUX_O_ACCMODE = 3, LINUX_O_RDONLY = 0, LINUX_O_WRONLY = 1, LINUX_O_RDWR = 2, LINUX_O_CREAT = 0x40,
    LINUX_O_EXCL = 0x80, LINUX_O_TRUNC = 0x200, LINUX_O_APPEND = 0x400, LINUX_O_DIRECTORY = 0x10000
};

enum { LINUX_O_NONBLOCK = 0x800, LINUX_O_CLOEXEC = 0x80000 };

enum { LINUX_SEEK_SET = 0, LINUX_SEEK_CUR = 1, LINUX_SEEK_END = 2 };

/* What a descriptor is, which decides how it is read, written and waited on. */
typedef enum { FD_KIND_FILE = 0, FD_KIND_PIPE = 1, FD_KIND_EVENT = 2 } FdKind;

/* An eventfd: a 64-bit counter and a Win32 event that is signalled while the counter is not zero. */
typedef struct {
    SRWLOCK lock;
    uint64_t counter;
    int is_semaphore;
    HANDLE event;
} EventState;

/* Linux paths to wide Windows paths (see path.c). Returns 0, or -1 with errno set. */
int shim_path_to_wide(const char *linux_path, wchar_t *out, size_t out_count);
int shim_wide_to_utf8(const wchar_t *wide, char *out, size_t out_size);
/* Absolute Windows path in the form Linux programs expect: slashes, and the drive as the first component (/C:/dir). */
int shim_wide_to_linux_path(const wchar_t *wide, char *out, size_t out_size);

/* Makes the three standard descriptors; called once at startup. */
void fd_init(void);

/* Descriptor operations. All return -1 with errno set on failure, like their POSIX counterparts. */
int shim_fd_open(const char *path, int flags, int mode);
int shim_fd_adopt(HANDLE handle, int flags);
/* Like shim_fd_adopt for descriptors that are not plain files; `state` is owned by the descriptor. */
int shim_fd_adopt_kind(FdKind kind, HANDLE handle, void *state, int flags);
int shim_fd_adopt_at(int target, FdKind kind, HANDLE handle, void *state, int flags);
FdKind shim_fd_kind(int fd);
void *shim_fd_state(int fd);
/* The status flags (access mode, O_NONBLOCK, O_APPEND) of a descriptor, or -1 for a bad one. */
int shim_fd_flags(int fd);
int shim_fd_set_nonblocking(int fd, int is_nonblocking);

/* eventfd counter operations (fd_event.c). Both block unless the descriptor is non-blocking. */
int64_t shim_event_read(int fd, void *buffer, size_t count);
int64_t shim_event_write(int fd, const void *buffer, size_t count);
/* Poll readiness bits (POLLIN, POLLOUT, POLLHUP) of one descriptor; see poll.c. */
short shim_fd_ready(int fd, short wanted);
/* A Win32 handle that becomes signalled when the descriptor may be ready, or NULL when it must be polled. */
HANDLE shim_fd_wait_handle(int fd);
int shim_fd_close(int fd);
int64_t shim_fd_read(int fd, void *buffer, size_t count);
int64_t shim_fd_write(int fd, const void *buffer, size_t count);
int64_t shim_fd_seek(int fd, int64_t offset, int whence);
HANDLE shim_fd_handle(int fd);

/* Fills a Linux struct stat (144 bytes) for an open handle or a path. */
int shim_stat_handle(HANDLE handle, void *stat_buffer);
int shim_stat_path(const char *path, void *stat_buffer);

/* POSIX calls other units of the shim use; defined in posix_fd.c and posix_fs.c. */
LIBC_FN int sysv_isatty(int fd);
LIBC_FN int sysv_mkstemp64(char *pattern);
LIBC_FN int sysv_unlink(const char *path);
LIBC_FN int sysv_rmdir(const char *path);

#endif
