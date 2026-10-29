/*
 * File: shim/posix_at.c
 * Purpose: The *at path calls (openat, unlinkat, fstatat, ...), link and utimensat
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "fd.h"

#define AT_FDCWD (-100)
#define AT_REMOVEDIR 0x200
#define WIDE_BACKSLASH ((wchar_t)0x5c)

/* struct statvfs for x86-64 (112 bytes). */
typedef struct {
    uint64_t f_bsize;
    uint64_t f_frsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint64_t f_favail;
    uint64_t f_fsid;
    uint64_t f_flag;
    uint64_t f_namemax;
    int32_t spare[6];
} StatVfs;

_Static_assert(sizeof(StatVfs) == 112, "struct statvfs is 112 bytes");

LIBC_FN int sysv_mkdir(const char *path, unsigned mode);
LIBC_FN int sysv_access(const char *path, int mode);
LIBC_FN int sysv_chmod(const char *path, unsigned mode);
LIBC_FN int64_t sysv_readlink(const char *path, char *buffer, size_t size);
LIBC_FN int sysv_rename(const char *from, const char *to);
LIBC_FN int sysv_utimes(const char *path, const int64_t times[4]);

/* The Linux-style absolute path of the directory open as `dirfd`. */
static int directory_path(int dirfd, char *out, size_t size)
{
    wchar_t wide[SHIM_PATH_MAX];
    HANDLE handle = shim_fd_handle(dirfd);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    DWORD length = GetFinalPathNameByHandleW(handle, wide, SHIM_PATH_MAX, FILE_NAME_NORMALIZED);
    if (length == 0 || length >= SHIM_PATH_MAX) {
        return shim_fail(LINUX_ENOTDIR);
    }
    const wchar_t *plain = (length > 4 && wide[0] == WIDE_BACKSLASH && wide[1] == WIDE_BACKSLASH && wide[2] == L'?') ? wide + 4 : wide;
    return shim_wide_to_linux_path(plain, out, size);
}

/* `path` made absolute against `dirfd` unless it already is, or the directory is the current one. */
static int resolve(int dirfd, const char *path, char *out, size_t size)
{
    if (path == NULL) {
        return shim_fail(LINUX_EFAULT);
    }
    if (path[0] == '/' || dirfd == AT_FDCWD) {
        if (strlen(path) + 1 > size) {
            return shim_fail(LINUX_ENAMETOOLONG);
        }
        strcpy(out, path);
        return 0;
    }
    if (directory_path(dirfd, out, size) != 0) {
        return -1;
    }
    size_t base = strlen(out);
    if (base + 1 + strlen(path) + 1 > size) {
        return shim_fail(LINUX_ENAMETOOLONG);
    }
    out[base] = '/';
    strcpy(out + base + 1, path);
    return 0;
}

#define WITH_PATH(dirfd, path, name) \
    char name[SHIM_PATH_MAX]; \
    if (resolve((dirfd), (path), name, sizeof(name)) != 0) { \
        return -1; \
    }

LIBC_FN int sysv_openat(int dirfd, const char *path, int flags, int mode)
{
    WITH_PATH(dirfd, path, full)
    return shim_fd_open(full, flags, mode);
}

LIBC_FN int sysv_openat64(int dirfd, const char *path, int flags, int mode)
{
    return sysv_openat(dirfd, path, flags, mode);
}

LIBC_FN int sysv___openat_2(int dirfd, const char *path, int flags)
{
    return sysv_openat(dirfd, path, flags, 0);
}

LIBC_FN int sysv___openat64_2(int dirfd, const char *path, int flags)
{
    return sysv_openat(dirfd, path, flags, 0);
}

LIBC_FN int sysv_unlinkat(int dirfd, const char *path, int flags)
{
    WITH_PATH(dirfd, path, full)
    return (flags & AT_REMOVEDIR) ? sysv_rmdir(full) : sysv_unlink(full);
}

LIBC_FN int sysv_mkdirat(int dirfd, const char *path, unsigned mode)
{
    WITH_PATH(dirfd, path, full)
    return sysv_mkdir(full, mode);
}

LIBC_FN int sysv_fchmodat(int dirfd, const char *path, unsigned mode, int flags)
{
    (void)flags;
    WITH_PATH(dirfd, path, full)
    return sysv_chmod(full, mode);
}

LIBC_FN int sysv_faccessat(int dirfd, const char *path, int mode, int flags)
{
    (void)flags;
    WITH_PATH(dirfd, path, full)
    return sysv_access(full, mode);
}

LIBC_FN int sysv_fstatat(int dirfd, const char *path, void *stat_buffer, int flags)
{
    (void)flags;
    WITH_PATH(dirfd, path, full)
    return shim_stat_path(full, stat_buffer);
}

LIBC_FN int sysv_fstatat64(int dirfd, const char *path, void *stat_buffer, int flags)
{
    return sysv_fstatat(dirfd, path, stat_buffer, flags);
}

LIBC_FN int sysv_newfstatat(int dirfd, const char *path, void *stat_buffer, int flags)
{
    return sysv_fstatat(dirfd, path, stat_buffer, flags);
}

LIBC_FN int64_t sysv_readlinkat(int dirfd, const char *path, char *buffer, size_t size)
{
    WITH_PATH(dirfd, path, full)
    return sysv_readlink(full, buffer, size);
}

LIBC_FN int sysv_renameat(int old_dirfd, const char *old_path, int new_dirfd, const char *new_path)
{
    char from[SHIM_PATH_MAX];
    char to[SHIM_PATH_MAX];
    if (resolve(old_dirfd, old_path, from, sizeof(from)) != 0 || resolve(new_dirfd, new_path, to, sizeof(to)) != 0) {
        return -1;
    }
    return sysv_rename(from, to);
}

LIBC_FN int sysv_link(const char *existing, const char *added)
{
    wchar_t from[SHIM_PATH_MAX];
    wchar_t to[SHIM_PATH_MAX];
    if (shim_path_to_wide(existing, from, SHIM_PATH_MAX) != 0 || shim_path_to_wide(added, to, SHIM_PATH_MAX) != 0) {
        return -1;
    }
    return CreateHardLinkW(to, from, NULL) ? 0 : shim_fail_from_win();
}

LIBC_FN int sysv_linkat(int old_dirfd, const char *old_path, int new_dirfd, const char *new_path, int flags)
{
    (void)flags;
    char from[SHIM_PATH_MAX];
    char to[SHIM_PATH_MAX];
    if (resolve(old_dirfd, old_path, from, sizeof(from)) != 0 || resolve(new_dirfd, new_path, to, sizeof(to)) != 0) {
        return -1;
    }
    return sysv_link(from, to);
}

#define UTIME_NOW_NANOSECONDS ((1l << 30) - 1)
#define UTIME_OMIT_NANOSECONDS ((1l << 30) - 2)
#define NANOSECONDS_PER_MICROSECOND 1000

/* utimensat for plain timestamps: UTIME_NOW sets the current time, UTIME_OMIT leaves a time unchanged (as now). */
LIBC_FN int sysv_utimensat(int dirfd, const char *path, const int64_t times[4], int flags)
{
    (void)flags;
    WITH_PATH(dirfd, path, full)
    if (times == NULL || times[1] == UTIME_NOW_NANOSECONDS || times[3] == UTIME_NOW_NANOSECONDS) {
        return sysv_utimes(full, NULL);
    }
    int64_t converted[4] = {times[0], times[1] / NANOSECONDS_PER_MICROSECOND, times[2], times[3] / NANOSECONDS_PER_MICROSECOND};
    return sysv_utimes(full, converted);
}
