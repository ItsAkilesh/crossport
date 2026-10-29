/*
 * File: shim/fd.h
 * Purpose: The file-descriptor table and path mapping that stdio and the POSIX file calls are built on
 * Author: Akilesh
 * Date: 2026-10-28
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

enum { LINUX_SEEK_SET = 0, LINUX_SEEK_CUR = 1, LINUX_SEEK_END = 2 };

/* Linux paths to wide Windows paths (see path.c). Returns 0, or -1 with errno set. */
int shim_path_to_wide(const char *linux_path, wchar_t *out, size_t out_count);
int shim_wide_to_utf8(const wchar_t *wide, char *out, size_t out_size);

/* Makes the three standard descriptors; called once at startup. */
void fd_init(void);

/* Descriptor operations. All return -1 with errno set on failure, like their POSIX counterparts. */
int shim_fd_open(const char *path, int flags, int mode);
int shim_fd_adopt(HANDLE handle, int flags);
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
