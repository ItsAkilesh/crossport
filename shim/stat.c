/*
 * File: shim/stat.c
 * Purpose: Fill Linux struct stat (x86-64, 144 bytes) from Win32 file information
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "fd.h"

#define S_IFREG 0100000u
#define S_IFDIR 0040000u
#define S_IFCHR 0020000u
#define S_IFIFO 0010000u
#define UNIX_EPOCH_AS_FILETIME 116444736000000000ull
#define TICKS_PER_SECOND 10000000ull
#define NANOSECONDS_PER_TICK 100ull
#define BLOCK_BYTES 512
#define PREFERRED_IO_BYTES 4096

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t pad;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    int64_t st_atime_nsec;
    int64_t st_mtime;
    int64_t st_mtime_nsec;
    int64_t st_ctime;
    int64_t st_ctime_nsec;
    int64_t unused[3];
} LinuxStat;

_Static_assert(sizeof(LinuxStat) == 144, "struct stat must match the Linux x86-64 layout");

static void set_time(FILETIME file_time, int64_t *seconds, int64_t *nanoseconds)
{
    uint64_t ticks = ((uint64_t)file_time.dwHighDateTime << 32) | file_time.dwLowDateTime;
    uint64_t since_epoch = ticks >= UNIX_EPOCH_AS_FILETIME ? ticks - UNIX_EPOCH_AS_FILETIME : 0;
    *seconds = (int64_t)(since_epoch / TICKS_PER_SECOND);
    *nanoseconds = (int64_t)((since_epoch % TICKS_PER_SECOND) * NANOSECONDS_PER_TICK);
}

static void fill_special(LinuxStat *out, uint32_t mode)
{
    out->st_mode = mode;
    out->st_nlink = 1;
    out->st_blksize = PREFERRED_IO_BYTES;
}

int shim_stat_handle(HANDLE handle, void *stat_buffer)
{
    LinuxStat *out = (LinuxStat *)stat_buffer;
    ZeroMemory(out, sizeof(*out));
    DWORD type = GetFileType(handle);
    if (type == FILE_TYPE_CHAR) {
        fill_special(out, S_IFCHR | 0620);
        return 0;
    }
    if (type == FILE_TYPE_PIPE) {
        fill_special(out, S_IFIFO | 0600);
        return 0;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (type != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info)) {
        return shim_fail(type == FILE_TYPE_UNKNOWN ? LINUX_EBADF : shim_errno_from_win(GetLastError()));
    }
    int is_directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    int is_read_only = (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    out->st_mode = is_directory ? (S_IFDIR | 0755) : (S_IFREG | (is_read_only ? 0444 : 0644));
    out->st_dev = info.dwVolumeSerialNumber;
    out->st_ino = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    out->st_nlink = info.nNumberOfLinks;
    out->st_size = (int64_t)(((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow);
    out->st_blksize = PREFERRED_IO_BYTES;
    out->st_blocks = (out->st_size + BLOCK_BYTES - 1) / BLOCK_BYTES;
    set_time(info.ftLastAccessTime, &out->st_atime, &out->st_atime_nsec);
    set_time(info.ftLastWriteTime, &out->st_mtime, &out->st_mtime_nsec);
    set_time(info.ftCreationTime, &out->st_ctime, &out->st_ctime_nsec);
    return 0;
}

int shim_stat_path(const char *path, void *stat_buffer)
{
    wchar_t wide[SHIM_PATH_MAX];
    if (shim_path_to_wide(path, wide, SHIM_PATH_MAX) != 0) {
        return -1;
    }
    HANDLE handle = CreateFileW(wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return shim_fail_from_win();
    }
    int result = shim_stat_handle(handle, stat_buffer);
    CloseHandle(handle);
    return result;
}
