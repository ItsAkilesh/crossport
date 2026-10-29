/*
 * File: shim/fd.c
 * Purpose: Linux file descriptors over Win32 handles: open, read, write, seek and close
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "fd.h"

typedef struct {
    HANDLE handle;
    int is_in_use;
    int is_append;
    int is_standard; /* the process's own stdin/stdout/stderr: never really closed */
} FdEntry;

static FdEntry g_table[SHIM_MAX_FDS];
static SRWLOCK g_lock = SRWLOCK_INIT;

void fd_init(void)
{
    DWORD ids[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (int fd = 0; fd < 3; fd++) {
        g_table[fd].handle = GetStdHandle(ids[fd]);
        g_table[fd].is_in_use = g_table[fd].handle != NULL && g_table[fd].handle != INVALID_HANDLE_VALUE;
        g_table[fd].is_standard = 1;
    }
}

/* Takes the lowest free descriptor, as POSIX requires. */
int shim_fd_adopt(HANDLE handle, int flags)
{
    int result = shim_fail(LINUX_EMFILE);
    AcquireSRWLockExclusive(&g_lock);
    for (int fd = 0; fd < SHIM_MAX_FDS; fd++) {
        if (!g_table[fd].is_in_use) {
            g_table[fd].handle = handle;
            g_table[fd].is_in_use = 1;
            g_table[fd].is_append = (flags & LINUX_O_APPEND) != 0;
            g_table[fd].is_standard = 0;
            result = fd;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

static int lookup(int fd, FdEntry *copy)
{
    if (fd < 0 || fd >= SHIM_MAX_FDS) {
        return shim_fail(LINUX_EBADF);
    }
    AcquireSRWLockShared(&g_lock);
    *copy = g_table[fd];
    ReleaseSRWLockShared(&g_lock);
    return copy->is_in_use ? 0 : shim_fail(LINUX_EBADF);
}

HANDLE shim_fd_handle(int fd)
{
    FdEntry entry;
    return lookup(fd, &entry) == 0 ? entry.handle : INVALID_HANDLE_VALUE;
}

static DWORD access_for(int flags)
{
    int mode = flags & LINUX_O_ACCMODE;
    DWORD write = (flags & LINUX_O_APPEND) ? FILE_APPEND_DATA : GENERIC_WRITE;
    return mode == LINUX_O_RDONLY ? GENERIC_READ : (mode == LINUX_O_WRONLY ? write : (GENERIC_READ | write));
}

static DWORD disposition_for(int flags)
{
    int create = (flags & LINUX_O_CREAT) != 0;
    int exclusive = (flags & LINUX_O_EXCL) != 0;
    int truncate = (flags & LINUX_O_TRUNC) != 0;
    if (create && exclusive) {
        return CREATE_NEW;
    }
    if (create) {
        return truncate ? CREATE_ALWAYS : OPEN_ALWAYS;
    }
    return truncate ? TRUNCATE_EXISTING : OPEN_EXISTING;
}

int shim_fd_open(const char *path, int flags, int mode)
{
    (void)mode;
    if (strcmp(path, "/dev/stdin") == 0 || strcmp(path, "/dev/stdout") == 0 || strcmp(path, "/dev/stderr") == 0) {
        int standard = path[5] == 's' && path[6] == 't' && path[7] == 'd' ? (path[8] == 'i' ? 0 : (path[8] == 'o' ? 1 : 2)) : 2;
        HANDLE copy;
        return DuplicateHandle(GetCurrentProcess(), g_table[standard].handle, GetCurrentProcess(), &copy, 0, FALSE,
                               DUPLICATE_SAME_ACCESS)
                   ? shim_fd_adopt(copy, flags)
                   : shim_fail_from_win();
    }
    wchar_t wide[SHIM_PATH_MAX];
    if (shim_path_to_wide(path, wide, SHIM_PATH_MAX) != 0) {
        return -1;
    }
    DWORD attributes = FILE_ATTRIBUTE_NORMAL | ((flags & LINUX_O_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0);
    HANDLE handle = CreateFileW(wide, access_for(flags), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                disposition_for(flags), attributes, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        DWORD found = GetFileAttributesW(wide);
        int is_directory = found != INVALID_FILE_ATTRIBUTES && (found & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return shim_fail(is_directory && error == ERROR_ACCESS_DENIED ? LINUX_EISDIR : shim_errno_from_win(error));
    }
    int fd = shim_fd_adopt(handle, flags);
    if (fd < 0) {
        CloseHandle(handle);
    }
    return fd;
}

int shim_fd_close(int fd)
{
    FdEntry entry;
    if (lookup(fd, &entry) != 0) {
        return -1;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_table[fd].is_in_use = 0;
    ReleaseSRWLockExclusive(&g_lock);
    return (entry.is_standard || CloseHandle(entry.handle)) ? 0 : shim_fail_from_win();
}

int64_t shim_fd_read(int fd, void *buffer, size_t count)
{
    FdEntry entry;
    if (lookup(fd, &entry) != 0) {
        return -1;
    }
    DWORD chunk = count > 0x40000000u ? 0x40000000u : (DWORD)count;
    DWORD got = 0;
    if (!ReadFile(entry.handle, buffer, chunk, &got, NULL)) {
        DWORD error = GetLastError();
        return (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) ? 0 : shim_fail(shim_errno_from_win(error));
    }
    return got;
}

int64_t shim_fd_write(int fd, const void *buffer, size_t count)
{
    FdEntry entry;
    if (lookup(fd, &entry) != 0) {
        return -1;
    }
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0;
    while (done < count) {
        DWORD chunk = (count - done) > 0x40000000u ? 0x40000000u : (DWORD)(count - done);
        DWORD wrote = 0;
        if (!WriteFile(entry.handle, bytes + done, chunk, &wrote, NULL)) {
            return done > 0 ? (int64_t)done : shim_fail_from_win();
        }
        done += wrote;
    }
    return (int64_t)done;
}

int64_t shim_fd_seek(int fd, int64_t offset, int whence)
{
    FdEntry entry;
    if (lookup(fd, &entry) != 0) {
        return -1;
    }
    DWORD type = GetFileType(entry.handle);
    if (type != FILE_TYPE_DISK) {
        return shim_fail(LINUX_ESPIPE);
    }
    DWORD method = whence == LINUX_SEEK_SET ? FILE_BEGIN : (whence == LINUX_SEEK_CUR ? FILE_CURRENT : FILE_END);
    if (whence < LINUX_SEEK_SET || whence > LINUX_SEEK_END) {
        return shim_fail(LINUX_EINVAL);
    }
    LARGE_INTEGER target;
    LARGE_INTEGER position;
    target.QuadPart = offset;
    return SetFilePointerEx(entry.handle, target, &position, method) ? position.QuadPart : shim_fail_from_win();
}
