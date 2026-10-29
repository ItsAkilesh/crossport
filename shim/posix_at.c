/*
 * File: shim/posix_at.c
 * Purpose: The *at path calls (openat, unlinkat, ...) and link, truncate, statvfs and sendfile
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * A path relative to a directory descriptor is resolved by joining it to the directory's own path and then
 * calling the ordinary path function, so the mapping of Linux paths onto Windows paths lives in one place.
 */

#include <string.h>

#include "fd.h"

#define AT_FDCWD (-100)
#define AT_REMOVEDIR 0x200
#define SENDFILE_CHUNK 65536
#define WIDE_BACKSLASH ((wchar_t)0x5c)
#define STATVFS_FILENAME_MAX 255

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
LIBC_FN int sysv_ftruncate64(int fd, int64_t length);

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

LIBC_FN int sysv_truncate(const char *path, int64_t length)
{
    int fd = shim_fd_open(path, LINUX_O_WRONLY, 0);
    if (fd < 0) {
        return -1;
    }
    int result = sysv_ftruncate64(fd, length);
    shim_fd_close(fd);
    return result;
}

LIBC_FN int sysv_truncate64(const char *path, int64_t length)
{
    return sysv_truncate(path, length);
}

static int fill_statvfs(const wchar_t *root, StatVfs *out)
{
    ULARGE_INTEGER available;
    ULARGE_INTEGER total;
    ULARGE_INTEGER free_bytes;
    DWORD sectors_per_cluster = 0;
    DWORD bytes_per_sector = 0;
    DWORD ignored_free = 0;
    DWORD ignored_total = 0;
    if (!GetDiskFreeSpaceExW(root, &available, &total, &free_bytes) ||
        !GetDiskFreeSpaceW(root, &sectors_per_cluster, &bytes_per_sector, &ignored_free, &ignored_total)) {
        return shim_fail_from_win();
    }
    uint64_t block = (uint64_t)sectors_per_cluster * bytes_per_sector;
    *out = (StatVfs){.f_bsize = block, .f_frsize = block, .f_blocks = total.QuadPart / block,
                     .f_bfree = free_bytes.QuadPart / block, .f_bavail = available.QuadPart / block,
                     .f_namemax = STATVFS_FILENAME_MAX};
    return 0;
}

LIBC_FN int sysv_statvfs(const char *path, StatVfs *out)
{
    wchar_t wide[SHIM_PATH_MAX];
    wchar_t volume[SHIM_PATH_MAX];
    if (shim_path_to_wide(path, wide, SHIM_PATH_MAX) != 0) {
        return -1;
    }
    if (!GetVolumePathNameW(wide, volume, SHIM_PATH_MAX)) {
        return shim_fail_from_win();
    }
    return fill_statvfs(volume, out);
}

LIBC_FN int sysv_statvfs64(const char *path, StatVfs *out)
{
    return sysv_statvfs(path, out);
}

LIBC_FN int sysv_fstatvfs(int fd, StatVfs *out)
{
    wchar_t wide[SHIM_PATH_MAX];
    wchar_t volume[SHIM_PATH_MAX];
    HANDLE handle = shim_fd_handle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    if (GetFinalPathNameByHandleW(handle, wide, SHIM_PATH_MAX, FILE_NAME_NORMALIZED) == 0 ||
        !GetVolumePathNameW(wide, volume, SHIM_PATH_MAX)) {
        return shim_fail_from_win();
    }
    return fill_statvfs(volume, out);
}

LIBC_FN int sysv_fstatvfs64(int fd, StatVfs *out)
{
    return sysv_fstatvfs(fd, out);
}

LIBC_FN int64_t sysv_sendfile(int out_fd, int in_fd, int64_t *offset, size_t count)
{
    static __thread uint8_t buffer[SENDFILE_CHUNK];
    int64_t restore = -1;
    int64_t total = 0;
    if (offset != NULL) {
        restore = shim_fd_seek(in_fd, 0, LINUX_SEEK_CUR);
        if (restore < 0 || shim_fd_seek(in_fd, *offset, LINUX_SEEK_SET) < 0) {
            return -1;
        }
    }
    while ((size_t)total < count) {
        size_t want = count - (size_t)total < sizeof(buffer) ? count - (size_t)total : sizeof(buffer);
        int64_t got = shim_fd_read(in_fd, buffer, want);
        if (got <= 0) {
            total = got < 0 && total == 0 ? -1 : total;
            break;
        }
        if (shim_fd_write(out_fd, buffer, (size_t)got) != got) {
            total = total == 0 ? -1 : total;
            break;
        }
        total += got;
    }
    if (offset != NULL) {
        *offset += total > 0 ? total : 0;
        shim_fd_seek(in_fd, restore, LINUX_SEEK_SET);
    }
    return total;
}

LIBC_FN int64_t sysv_sendfile64(int out_fd, int in_fd, int64_t *offset, size_t count)
{
    return sysv_sendfile(out_fd, in_fd, offset, count);
}

LIBC_FN int sysv_utimes(const char *path, const int64_t times[4]);

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
