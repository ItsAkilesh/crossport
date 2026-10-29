/*
 * File: shim/posix_dir.c
 * Purpose: Directory streams: opendir, fdopendir, dirfd, readdir and closedir over the Win32 find functions
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "fd.h"

#define DT_DIR 4
#define DT_REG 8
#define WIDE_BACKSLASH ((wchar_t)0x5c)

static const wchar_t PATTERN_SUFFIX[] = {WIDE_BACKSLASH, L'*', 0};

static int with_wide(const char *path, wchar_t *wide)
{
    return shim_path_to_wide(path, wide, SHIM_PATH_MAX);
}

/* Linux struct dirent64: inode, offset, record length, type, then the name. */
typedef struct {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
} Dirent64;

typedef struct {
    HANDLE find;
    WIN32_FIND_DATAW data;
    int has_pending;
    Dirent64 entry;
    wchar_t *path; /* the directory itself, for dirfd and the *at calls */
    int fd;        /* descriptor made by dirfd, or -1 */
} Directory;

/* Starts enumerating the directory named by a wide Windows path. Returns NULL with errno set. */
void *shim_directory_open(const wchar_t *directory_path)
{
    wchar_t pattern[SHIM_PATH_MAX];
    size_t length = (size_t)lstrlenW(directory_path);
    if (length + 3 >= SHIM_PATH_MAX) {
        shim_set_errno(LINUX_ENAMETOOLONG);
        return NULL;
    }
    lstrcpyW(pattern, directory_path);
    lstrcatW(pattern, PATTERN_SUFFIX);
    Directory *directory = (Directory *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Directory));
    wchar_t *copy = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(wchar_t));
    if (directory == NULL || copy == NULL) {
        HeapFree(GetProcessHeap(), 0, directory);
        HeapFree(GetProcessHeap(), 0, copy);
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    directory->find = FindFirstFileW(pattern, &directory->data);
    if (directory->find == INVALID_HANDLE_VALUE) {
        shim_fail_from_win();
        HeapFree(GetProcessHeap(), 0, directory);
        HeapFree(GetProcessHeap(), 0, copy);
        return NULL;
    }
    lstrcpyW(copy, directory_path);
    directory->path = copy;
    directory->fd = -1;
    directory->has_pending = 1;
    return directory;
}

LIBC_FN void *sysv_opendir(const char *path)
{
    wchar_t wide[SHIM_PATH_MAX];
    return with_wide(path, wide) == 0 ? shim_directory_open(wide) : NULL;
}

/* The directory behind an open descriptor, enumerated from its start. */
LIBC_FN void *sysv_fdopendir(int fd)
{
    wchar_t path[SHIM_PATH_MAX];
    HANDLE handle = shim_fd_handle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    DWORD length = GetFinalPathNameByHandleW(handle, path, SHIM_PATH_MAX, FILE_NAME_NORMALIZED);
    if (length == 0 || length >= SHIM_PATH_MAX) {
        shim_set_errno(LINUX_ENOTDIR);
        return NULL;
    }
    const wchar_t *plain = (length > 4 && path[0] == WIDE_BACKSLASH && path[1] == WIDE_BACKSLASH && path[2] == L'?') ? path + 4 : path;
    return shim_directory_open(plain);
}

/* A descriptor for the directory, made on first request, for use as the base of the *at calls. */
LIBC_FN int sysv_dirfd(void *handle)
{
    Directory *directory = (Directory *)handle;
    if (directory->fd < 0) {
        HANDLE dir_handle = CreateFileW(directory->path, GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (dir_handle == INVALID_HANDLE_VALUE) {
            return shim_fail_from_win();
        }
        directory->fd = shim_fd_adopt(dir_handle, LINUX_O_RDONLY | LINUX_O_DIRECTORY);
        if (directory->fd < 0) {
            CloseHandle(dir_handle);
        }
    }
    return directory->fd;
}

LIBC_FN Dirent64 *sysv_readdir64(void *handle)
{
    Directory *directory = (Directory *)handle;
    if (!directory->has_pending && !FindNextFileW(directory->find, &directory->data)) {
        return NULL;
    }
    directory->has_pending = 0;
    Dirent64 *entry = &directory->entry;
    entry->d_ino = 1;
    entry->d_off = 0;
    entry->d_reclen = sizeof(Dirent64);
    entry->d_type = (directory->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return shim_wide_to_utf8(directory->data.cFileName, entry->d_name, sizeof(entry->d_name)) == 0 ? entry : NULL;
}

LIBC_FN Dirent64 *sysv_readdir(void *handle)
{
    return sysv_readdir64(handle);
}

LIBC_FN int sysv_closedir(void *handle)
{
    Directory *directory = (Directory *)handle;
    FindClose(directory->find);
    if (directory->fd >= 0) {
        shim_fd_close(directory->fd);
    }
    HeapFree(GetProcessHeap(), 0, directory->path);
    HeapFree(GetProcessHeap(), 0, directory);
    return 0;
}
