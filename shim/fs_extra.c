/*
 * File: shim/fs_extra.c
 * Purpose: truncate, statvfs and sendfile
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "fd.h"

#define SENDFILE_CHUNK 65536
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

LIBC_FN int sysv_ftruncate64(int fd, int64_t length);

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
    uint8_t *buffer = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, SENDFILE_CHUNK);
    if (buffer == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    int64_t restore = -1;
    int64_t total = 0;
    if (offset != NULL) {
        restore = shim_fd_seek(in_fd, 0, LINUX_SEEK_CUR);
        if (restore < 0 || shim_fd_seek(in_fd, *offset, LINUX_SEEK_SET) < 0) {
            HeapFree(GetProcessHeap(), 0, buffer);
            return -1;
        }
    }
    while ((size_t)total < count) {
        size_t want = count - (size_t)total < SENDFILE_CHUNK ? count - (size_t)total : SENDFILE_CHUNK;
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
    HeapFree(GetProcessHeap(), 0, buffer);
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
