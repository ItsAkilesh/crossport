/*
 * File: shim/fs_more.c
 * Purpose: rlimits, load average, fsync variants, fallocate, fchdir, statfs, futimens and utime
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "fd.h"

#define RLIM_INFINITY_VALUE UINT64_MAX
#define STACK_LIMIT_BYTES (8u * 1024 * 1024)
#define OPEN_FILES_LIMIT 1024u

LIBC_FN int sysv_access(const char *path, int mode);
LIBC_FN int sysv_fsync(int fd);
LIBC_FN int sysv_open(const char *path, int flags, int mode);
LIBC_FN int sysv_ftruncate64(int fd, int64_t length);
LIBC_FN int sysv_utimes(const char *path, const int64_t times[4]);

/* struct rlimit: soft and hard limit. */
LIBC_FN int sysv_getrlimit(int resource, uint64_t *limit)
{
    enum { RLIMIT_STACK_ID = 3, RLIMIT_NOFILE_ID = 7 };
    uint64_t value = resource == RLIMIT_STACK_ID ? STACK_LIMIT_BYTES : (resource == RLIMIT_NOFILE_ID ? OPEN_FILES_LIMIT : RLIM_INFINITY_VALUE);
    limit[0] = limit[1] = value;
    return 0;
}

LIBC_FN int sysv_getrlimit64(int resource, uint64_t *limit)
{
    return sysv_getrlimit(resource, limit);
}

LIBC_FN int sysv_getloadavg(double *averages, int count)
{
    for (int i = 0; i < count && i < 3; i++) {
        averages[i] = 0.0;
    }
    return count < 3 ? count : 3;
}

LIBC_FN int sysv_fdatasync(int fd)
{
    return sysv_fsync(fd);
}

LIBC_FN void sysv_sync(void)
{
}

LIBC_FN int sysv_creat64(const char *path, unsigned mode)
{
    enum { WRITE_ONLY = 1, CREATE = 0x40, TRUNCATE = 0x200 };
    return sysv_open(path, WRITE_ONLY | CREATE | TRUNCATE, (int)mode);
}

LIBC_FN int sysv_eaccess(const char *path, int mode)
{
    return sysv_access(path, mode);
}

LIBC_FN int sysv_posix_fallocate64(int fd, int64_t offset, int64_t length)
{
    HANDLE handle = shim_fd_handle(fd);
    LARGE_INTEGER size;
    if (handle == INVALID_HANDLE_VALUE) {
        return LINUX_EBADF;
    }
    if (GetFileSizeEx(handle, &size) && size.QuadPart < offset + length) {
        return sysv_ftruncate64(fd, offset + length) == 0 ? 0 : *shim_errno_ptr();
    }
    return 0;
}

LIBC_FN int sysv_fallocate64(int fd, int mode, int64_t offset, int64_t length)
{
    (void)mode;
    int error = sysv_posix_fallocate64(fd, offset, length);
    return error == 0 ? 0 : shim_fail(error);
}

LIBC_FN int sysv_fchdir(int fd)
{
    wchar_t path[SHIM_PATH_MAX];
    HANDLE handle = shim_fd_handle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    DWORD length = GetFinalPathNameByHandleW(handle, path, SHIM_PATH_MAX, FILE_NAME_NORMALIZED);
    const wchar_t *plain = (length > 4 && path[0] == (wchar_t)0x5c && path[1] == (wchar_t)0x5c && path[2] == L'?') ? path + 4 : path;
    return length > 0 && length < SHIM_PATH_MAX && SetCurrentDirectoryW(plain) ? 0 : shim_fail(LINUX_ENOTDIR);
}

/* struct statfs on x86-64 (120 bytes), filled from the volume's free-space figures. */
typedef struct {
    int64_t type;
    int64_t block_size;
    uint64_t blocks;
    uint64_t blocks_free;
    uint64_t blocks_available;
    uint64_t files;
    uint64_t files_free;
    int32_t fsid[2];
    int64_t name_length;
    int64_t fragment_size;
    int64_t flags;
    int64_t spare[4];
} StatFs;

_Static_assert(sizeof(StatFs) == 120, "struct statfs is 120 bytes");

static int fill_statfs(const wchar_t *root, StatFs *out)
{
    ULARGE_INTEGER available;
    ULARGE_INTEGER total;
    ULARGE_INTEGER free_bytes;
    if (!GetDiskFreeSpaceExW(root, &available, &total, &free_bytes)) {
        return shim_fail_from_win();
    }
    enum { BLOCK = 4096, EXT4_MAGIC = 0xef53, NAME_MAX_BYTES = 255 };
    memset(out, 0, sizeof(*out));
    out->type = EXT4_MAGIC;
    out->block_size = out->fragment_size = BLOCK;
    out->blocks = total.QuadPart / BLOCK;
    out->blocks_free = free_bytes.QuadPart / BLOCK;
    out->blocks_available = available.QuadPart / BLOCK;
    out->name_length = NAME_MAX_BYTES;
    return 0;
}

LIBC_FN int sysv_statfs(const char *path, StatFs *out)
{
    wchar_t wide[SHIM_PATH_MAX];
    wchar_t volume[SHIM_PATH_MAX];
    if (shim_path_to_wide(path, wide, SHIM_PATH_MAX) != 0) {
        return -1;
    }
    return GetVolumePathNameW(wide, volume, SHIM_PATH_MAX) ? fill_statfs(volume, out) : shim_fail_from_win();
}

LIBC_FN int sysv_statfs64(const char *path, StatFs *out)
{
    return sysv_statfs(path, out);
}

LIBC_FN int sysv_fstatfs(int fd, StatFs *out)
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
    return fill_statfs(volume, out);
}

LIBC_FN int sysv_fstatfs64(int fd, StatFs *out)
{
    return sysv_fstatfs(fd, out);
}

/* Changes a file's times through its descriptor; NULL means now. times is two {seconds, nanoseconds} pairs. */
LIBC_FN int sysv_futimens(int fd, const int64_t times[4])
{
    HANDLE handle = shim_fd_handle(fd);
    FILETIME access;
    FILETIME modified;
    const uint64_t epoch = 116444736000000000ull;
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    if (times == NULL) {
        GetSystemTimePreciseAsFileTime(&access);
        modified = access;
    } else {
        uint64_t a = epoch + (uint64_t)times[0] * 10000000ull + (uint64_t)times[1] / 100;
        uint64_t m = epoch + (uint64_t)times[2] * 10000000ull + (uint64_t)times[3] / 100;
        access = (FILETIME){(DWORD)a, (DWORD)(a >> 32)};
        modified = (FILETIME){(DWORD)m, (DWORD)(m >> 32)};
    }
    return SetFileTime(handle, NULL, &access, &modified) ? 0 : shim_fail_from_win();
}

LIBC_FN int sysv_utime(const char *path, const int64_t *times)
{
    int64_t converted[4];
    if (times != NULL) {
        converted[0] = times[0];
        converted[1] = 0;
        converted[2] = times[1];
        converted[3] = 0;
    }
    return sysv_utimes(path, times != NULL ? converted : NULL);
}
