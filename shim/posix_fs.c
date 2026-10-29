/*
 * File: shim/posix_fs.c
 * Purpose: Path-based POSIX calls: stat, unlink, rename, mkdir, access, getcwd, realpath, directories and temp files
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "fd.h"

#define DT_DIR 4
#define DT_REG 8
#define DT_UNKNOWN 0
#define TEMP_SUFFIX_LENGTH 6
#define TEMP_RETRIES 100
#define MS_UNIX_EPOCH 116444736000000000ull

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
} Directory;

LIBC_FN int sysv_stat(const char *path, void *stat_buffer)
{
    return shim_stat_path(path, stat_buffer);
}

LIBC_FN int sysv_stat64(const char *path, void *stat_buffer)
{
    return shim_stat_path(path, stat_buffer);
}

/* Symbolic links are not created by this layer, so lstat is stat. */
LIBC_FN int sysv_lstat64(const char *path, void *stat_buffer)
{
    return shim_stat_path(path, stat_buffer);
}

LIBC_FN int sysv_lstat(const char *path, void *stat_buffer)
{
    return shim_stat_path(path, stat_buffer);
}

static int with_wide(const char *path, wchar_t *wide)
{
    return shim_path_to_wide(path, wide, SHIM_PATH_MAX);
}

LIBC_FN int sysv_unlink(const char *path)
{
    wchar_t wide[SHIM_PATH_MAX];
    if (with_wide(path, wide) != 0) {
        return -1;
    }
    DWORD attributes = GetFileAttributesW(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return shim_fail(LINUX_EISDIR);
    }
    return DeleteFileW(wide) ? 0 : shim_fail_from_win();
}

LIBC_FN int sysv_rename(const char *from, const char *to)
{
    wchar_t wide_from[SHIM_PATH_MAX];
    wchar_t wide_to[SHIM_PATH_MAX];
    if (with_wide(from, wide_from) != 0 || with_wide(to, wide_to) != 0) {
        return -1;
    }
    return MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING) ? 0 : shim_fail_from_win();
}

LIBC_FN int sysv_mkdir(const char *path, unsigned mode)
{
    (void)mode;
    wchar_t wide[SHIM_PATH_MAX];
    return (with_wide(path, wide) == 0 && CreateDirectoryW(wide, NULL)) ? 0 : (GetLastError() ? shim_fail_from_win() : -1);
}

LIBC_FN int sysv_rmdir(const char *path)
{
    wchar_t wide[SHIM_PATH_MAX];
    return (with_wide(path, wide) == 0 && RemoveDirectoryW(wide)) ? 0 : (GetLastError() ? shim_fail_from_win() : -1);
}

LIBC_FN int sysv_access(const char *path, int mode)
{
    enum { W_OK = 2 };
    wchar_t wide[SHIM_PATH_MAX];
    if (with_wide(path, wide) != 0) {
        return -1;
    }
    DWORD attributes = GetFileAttributesW(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return shim_fail_from_win();
    }
    int is_read_only = (attributes & FILE_ATTRIBUTE_READONLY) != 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    return ((mode & W_OK) && is_read_only) ? shim_fail(LINUX_EACCES) : 0;
}

/* Windows has no permission bits to set; the request is accepted. */
LIBC_FN int sysv_chmod(const char *path, unsigned mode)
{
    (void)mode;
    wchar_t wide[SHIM_PATH_MAX];
    return (with_wide(path, wide) == 0 && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES) ? 0 : shim_fail_from_win();
}

LIBC_FN char *sysv_getcwd(char *buffer, size_t size)
{
    wchar_t wide[SHIM_PATH_MAX];
    char text[SHIM_PATH_MAX];
    if (GetCurrentDirectoryW(SHIM_PATH_MAX, wide) == 0 || shim_wide_to_linux_path(wide, text, sizeof(text)) != 0) {
        return NULL;
    }
    size_t length = strlen(text) + 1;
    if (buffer == NULL) {
        buffer = (char *)HeapAlloc(GetProcessHeap(), 0, size > length ? size : length);
    } else if (size < length) {
        shim_set_errno(LINUX_ERANGE);
        return NULL;
    }
    if (buffer != NULL) {
        CopyMemory(buffer, text, length);
    }
    return buffer;
}

LIBC_FN int sysv_chdir(const char *path)
{
    wchar_t wide[SHIM_PATH_MAX];
    return (with_wide(path, wide) == 0 && SetCurrentDirectoryW(wide)) ? 0 : (GetLastError() ? shim_fail_from_win() : -1);
}

LIBC_FN char *sysv_realpath(const char *path, char *resolved)
{
    wchar_t wide[SHIM_PATH_MAX];
    wchar_t full[SHIM_PATH_MAX];
    char text[SHIM_PATH_MAX];
    if (with_wide(path, wide) != 0 || GetFullPathNameW(wide, SHIM_PATH_MAX, full, NULL) == 0 ||
        GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
        shim_set_errno(LINUX_ENOENT);
        return NULL;
    }
    if (shim_wide_to_linux_path(full, text, sizeof(text)) != 0) {
        return NULL;
    }
    size_t length = strlen(text) + 1;
    char *out = resolved != NULL ? resolved : (char *)HeapAlloc(GetProcessHeap(), 0, length);
    if (out != NULL) {
        CopyMemory(out, text, length);
    }
    return out;
}

/* No symbolic links exist in a translated AppDir (they were turned into copies), so nothing can be read. */
LIBC_FN int64_t sysv_readlink(const char *path, char *buffer, size_t size)
{
    (void)path;
    (void)buffer;
    (void)size;
    return shim_fail(LINUX_EINVAL);
}

LIBC_FN int sysv_symlink(const char *target, const char *link)
{
    (void)target;
    (void)link;
    return shim_fail(LINUX_EPERM);
}

LIBC_FN int sysv_utimes(const char *path, const int64_t times[4])
{
    wchar_t wide[SHIM_PATH_MAX];
    if (with_wide(path, wide) != 0) {
        return -1;
    }
    HANDLE handle = CreateFileW(wide, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return shim_fail_from_win();
    }
    FILETIME access;
    FILETIME modified;
    FILETIME *access_time = NULL;
    FILETIME *modified_time = NULL;
    if (times != NULL) {
        uint64_t atime = MS_UNIX_EPOCH + ((uint64_t)times[0] * 10000000ull) + ((uint64_t)times[1] * 10ull);
        uint64_t mtime = MS_UNIX_EPOCH + ((uint64_t)times[2] * 10000000ull) + ((uint64_t)times[3] * 10ull);
        access = (FILETIME){(DWORD)atime, (DWORD)(atime >> 32)};
        modified = (FILETIME){(DWORD)mtime, (DWORD)(mtime >> 32)};
        access_time = &access;
        modified_time = &modified;
    }
    int ok = SetFileTime(handle, NULL, access_time, modified_time);
    CloseHandle(handle);
    return ok ? 0 : shim_fail_from_win();
}

/* Replaces the trailing XXXXXX of `pattern` with random characters and opens the new file exclusively. */
LIBC_FN int sysv_mkstemp64(char *pattern)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t length = strlen(pattern);
    if (length < TEMP_SUFFIX_LENGTH || strcmp(pattern + length - TEMP_SUFFIX_LENGTH, "XXXXXX") != 0) {
        return shim_fail(LINUX_EINVAL);
    }
    for (int attempt = 0; attempt < TEMP_RETRIES; attempt++) {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        uint64_t bits = (uint64_t)counter.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ (uint64_t)attempt * 2654435761u;
        for (int i = 0; i < TEMP_SUFFIX_LENGTH; i++) {
            pattern[length - TEMP_SUFFIX_LENGTH + i] = alphabet[bits % (sizeof(alphabet) - 1)];
            bits /= (sizeof(alphabet) - 1);
            bits ^= bits << 7;
        }
        int fd = shim_fd_open(pattern, LINUX_O_RDWR | LINUX_O_CREAT | LINUX_O_EXCL, 0600);
        if (fd >= 0 || *shim_errno_ptr() != LINUX_EEXIST) {
            return fd;
        }
    }
    return shim_fail(LINUX_EEXIST);
}

LIBC_FN char *sysv_dirname(char *path)
{
    if (path == NULL || *path == '\0') {
        return (char *)".";
    }
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') {
        end--;
    }
    while (end > 0 && path[end - 1] != '/') {
        end--;
    }
    while (end > 1 && path[end - 1] == '/') {
        end--;
    }
    if (end == 0) {
        return (char *)".";
    }
    path[end] = '\0';
    return path;
}

LIBC_FN c_long sysv_pathconf(const char *path, int name)
{
    enum { PC_NAME_MAX = 3, PC_PATH_MAX = 4 };
    (void)path;
    return name == PC_NAME_MAX ? 255 : (name == PC_PATH_MAX ? 4096 : shim_fail(LINUX_EINVAL));
}

LIBC_FN void *sysv_opendir(const char *path)
{
    wchar_t wide[SHIM_PATH_MAX];
    if (with_wide(path, wide) != 0 || lstrlenW(wide) + 3 >= SHIM_PATH_MAX) {
        return NULL;
    }
    lstrcatW(wide, L"\\*");
    Directory *directory = (Directory *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Directory));
    if (directory == NULL) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    directory->find = FindFirstFileW(wide, &directory->data);
    if (directory->find == INVALID_HANDLE_VALUE) {
        shim_fail_from_win();
        HeapFree(GetProcessHeap(), 0, directory);
        return NULL;
    }
    directory->has_pending = 1;
    return directory;
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

LIBC_FN int sysv_closedir(void *handle)
{
    Directory *directory = (Directory *)handle;
    FindClose(directory->find);
    HeapFree(GetProcessHeap(), 0, directory);
    return 0;
}
