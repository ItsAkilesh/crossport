/*
 * File: shim/fd.c
 * Purpose: Linux file descriptors over Win32 handles: open, read, write, seek and close
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "fd.h"

typedef struct {
    HANDLE handle;
    int is_in_use;
    int is_append;
    int is_standard; /* the process's own stdin/stdout/stderr: never really closed */
    int is_nonblocking;
    int access_mode;
    FdKind kind;
    void *state; /* kind-specific data owned by the descriptor (for example an EventState) */
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
        g_table[fd].access_mode = fd == 0 ? LINUX_O_RDONLY : LINUX_O_WRONLY;
        g_table[fd].kind = (g_table[fd].is_in_use && GetFileType(g_table[fd].handle) == FILE_TYPE_PIPE) ? FD_KIND_PIPE
                                                                                                          : FD_KIND_FILE;
    }
}

/* Takes the lowest free descriptor, as POSIX requires. */
int shim_fd_adopt_kind(FdKind kind, HANDLE handle, void *state, int flags)
{
    int result = shim_fail(LINUX_EMFILE);
    AcquireSRWLockExclusive(&g_lock);
    for (int fd = 0; fd < SHIM_MAX_FDS; fd++) {
        if (!g_table[fd].is_in_use) {
            g_table[fd] = (FdEntry){.handle = handle, .is_in_use = 1, .is_append = (flags & LINUX_O_APPEND) != 0,
                                    .is_nonblocking = (flags & LINUX_O_NONBLOCK) != 0,
                                    .access_mode = flags & LINUX_O_ACCMODE, .kind = kind, .state = state};
            result = fd;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

/* Puts a descriptor at a chosen number (dup2), closing whatever was there. */
int shim_fd_adopt_at(int target, FdKind kind, HANDLE handle, void *state, int flags)
{
    if (target < 0 || target >= SHIM_MAX_FDS) {
        return shim_fail(LINUX_EBADF);
    }
    FdEntry previous;
    AcquireSRWLockExclusive(&g_lock);
    previous = g_table[target];
    g_table[target] = (FdEntry){.handle = handle, .is_in_use = 1, .is_append = (flags & LINUX_O_APPEND) != 0,
                                .is_nonblocking = (flags & LINUX_O_NONBLOCK) != 0,
                                .access_mode = flags & LINUX_O_ACCMODE, .kind = kind, .state = state};
    ReleaseSRWLockExclusive(&g_lock);
    if (previous.is_in_use && !previous.is_standard) {
        CloseHandle(previous.handle);
        if (previous.kind == FD_KIND_EVENT) {
            HeapFree(GetProcessHeap(), 0, previous.state);
        }
    }
    return target;
}

int shim_fd_adopt(HANDLE handle, int flags)
{
    return shim_fd_adopt_kind(FD_KIND_FILE, handle, NULL, flags);
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

FdKind shim_fd_kind(int fd)
{
    FdEntry entry;
    return lookup(fd, &entry) == 0 ? entry.kind : FD_KIND_FILE;
}

void *shim_fd_state(int fd)
{
    FdEntry entry;
    return lookup(fd, &entry) == 0 ? entry.state : NULL;
}

int shim_fd_flags(int fd)
{
    FdEntry entry;
    if (lookup(fd, &entry) != 0) {
        return -1;
    }
    return entry.access_mode | (entry.is_append ? LINUX_O_APPEND : 0) | (entry.is_nonblocking ? LINUX_O_NONBLOCK : 0);
}

int shim_fd_set_nonblocking(int fd, int is_nonblocking)
{
    FdEntry entry;
    if (lookup(fd, &entry) != 0) {
        return -1;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_table[fd].is_nonblocking = is_nonblocking;
    ReleaseSRWLockExclusive(&g_lock);
    return 0;
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
    if (entry.kind == FD_KIND_EVENT) {
        CloseHandle(entry.handle);
        HeapFree(GetProcessHeap(), 0, entry.state);
        return 0;
    }
    if (entry.kind == FD_KIND_SOCKET) {
        return shim_socket_close((SocketState *)entry.state);
    }
    return (entry.is_standard || CloseHandle(entry.handle)) ? 0 : shim_fail_from_win();
}

int64_t shim_fd_read(int fd, void *buffer, size_t count)
{
    FdEntry entry;
    if (lookup(fd, &entry) != 0) {
        return -1;
    }
    if (entry.kind == FD_KIND_EVENT) {
        return shim_event_read(fd, buffer, count);
    }
    if (entry.kind == FD_KIND_SOCKET) {
        return shim_socket_read(fd, buffer, count);
    }
    if (entry.kind == FD_KIND_PIPE && entry.is_nonblocking && (shim_fd_ready(fd, 1) & 1) == 0) {
        return shim_fail(LINUX_EAGAIN);
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
    if (entry.kind == FD_KIND_EVENT) {
        return shim_event_write(fd, buffer, count);
    }
    if (entry.kind == FD_KIND_SOCKET) {
        return shim_socket_write(fd, buffer, count);
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
