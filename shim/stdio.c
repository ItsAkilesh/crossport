/*
 * File: shim/stdio.c
 * Purpose: Stream management: the standard streams, opening, closing, buffering, flushing and seeking
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "stdio_internal.h"

LIBC_DATA(ShimFile *, stdin);
LIBC_DATA(ShimFile *, stdout);
LIBC_DATA(ShimFile *, stderr);

static ShimFile g_stdin_file;
static ShimFile g_stdout_file;
static ShimFile g_stderr_file;
static ShimFile *g_open_list;
static SRWLOCK g_list_lock = SRWLOCK_INIT;

void file_lock(ShimFile *file)
{
    DWORD self = GetCurrentThreadId();
    if (file->owner == self) {
        file->depth++;
        return;
    }
    AcquireSRWLockExclusive(&file->mutex);
    file->owner = self;
    file->depth = 1;
}

void file_unlock(ShimFile *file)
{
    if (--file->depth == 0) {
        file->owner = 0;
        ReleaseSRWLockExclusive(&file->mutex);
    }
}

static void link_file(ShimFile *file)
{
    AcquireSRWLockExclusive(&g_list_lock);
    file->next_open = g_open_list;
    g_open_list = file;
    ReleaseSRWLockExclusive(&g_list_lock);
}

static void unlink_file(ShimFile *file)
{
    AcquireSRWLockExclusive(&g_list_lock);
    for (ShimFile **at = &g_open_list; *at != NULL; at = &(*at)->next_open) {
        if (*at == file) {
            *at = file->next_open;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_list_lock);
}

static void reset_pointers(ShimFile *file)
{
    file->read_ptr = file->read_end = file->read_base = NULL;
    file->write_base = file->write_ptr = file->write_end = NULL;
}

static int ensure_buffer(ShimFile *file)
{
    if (file->buf_base != NULL) {
        return 0;
    }
    char *block = (char *)HeapAlloc(GetProcessHeap(), 0, SHIM_BUFFER_BYTES);
    if (block == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    file->buf_base = block;
    file->buf_end = block + SHIM_BUFFER_BYTES;
    file->has_own_buffer = 1;
    return 0;
}

int file_flush(ShimFile *file)
{
    if (file->state != STATE_WRITING || file->write_ptr == file->write_base) {
        return 0;
    }
    int64_t written = shim_fd_write(file->fileno, file->write_base, (size_t)(file->write_ptr - file->write_base));
    int failed = written < 0;
    file->write_ptr = file->write_base;
    if (failed) {
        file->flags |= IO_ERR_SEEN;
    }
    return failed ? -1 : 0;
}

/* The unread part of the read buffer is given back to the descriptor, so its offset matches the stream's. */
static void discard_read_buffer(ShimFile *file)
{
    if (file->read_ptr != NULL && file->read_end > file->read_ptr) {
        shim_fd_seek(file->fileno, -(int64_t)(file->read_end - file->read_ptr), LINUX_SEEK_CUR);
    }
    reset_pointers(file);
}

int file_prepare(ShimFile *file, int state)
{
    if (file->state == state) {
        return 0;
    }
    if (file->state == STATE_WRITING && file_flush(file) != 0) {
        return -1;
    }
    if (file->state == STATE_READING) {
        discard_read_buffer(file);
    }
    reset_pointers(file);
    file->state = STATE_IDLE;
    if (ensure_buffer(file) != 0) {
        return -1;
    }
    if (state == STATE_WRITING) {
        file->write_base = file->write_ptr = file->buf_base;
        file->write_end = file->buffer_mode == BUFFER_FULL ? file->buf_end : file->buf_base;
    }
    file->state = state;
    return 0;
}

ShimFile *file_new(int fd, int is_readable, int is_writable)
{
    ShimFile *file = (ShimFile *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ShimFile));
    if (file == NULL) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    file->fileno = fd;
    file->is_readable = is_readable;
    file->is_writable = is_writable;
    file->buffer_mode = BUFFER_FULL;
    link_file(file);
    return file;
}

static void init_standard(ShimFile *file, int fd, int is_writable, int mode)
{
    file->fileno = fd;
    file->is_readable = !is_writable;
    file->is_writable = is_writable;
    file->buffer_mode = mode;
    file->is_static = 1;
    link_file(file);
}

void stdio_init(void)
{
    int interactive = sysv_isatty(1);
    init_standard(&g_stdin_file, 0, 0, BUFFER_FULL);
    init_standard(&g_stdout_file, 1, 1, interactive ? BUFFER_LINE : BUFFER_FULL);
    init_standard(&g_stderr_file, 2, 1, BUFFER_NONE);
    crossport_data_stdin = &g_stdin_file;
    crossport_data_stdout = &g_stdout_file;
    crossport_data_stderr = &g_stderr_file;
}

void stdio_flush_all(void)
{
    AcquireSRWLockShared(&g_list_lock);
    for (ShimFile *file = g_open_list; file != NULL; file = file->next_open) {
        file_lock(file);
        file_flush(file);
        file_unlock(file);
    }
    ReleaseSRWLockShared(&g_list_lock);
}

/* Parses "r", "w", "a" with optional "+", "b", "x", "e" into open(2) flags. Returns -1 for a bad mode. */
static int parse_mode(const char *mode, int *flags, int *is_readable, int *is_writable)
{
    int plus = strchr(mode, '+') != NULL;
    int exclusive = strchr(mode, 'x') != NULL;
    switch (mode[0]) {
    case 'r':
        *flags = plus ? LINUX_O_RDWR : LINUX_O_RDONLY;
        break;
    case 'w':
        *flags = (plus ? LINUX_O_RDWR : LINUX_O_WRONLY) | LINUX_O_CREAT | LINUX_O_TRUNC | (exclusive ? LINUX_O_EXCL : 0);
        break;
    case 'a':
        *flags = (plus ? LINUX_O_RDWR : LINUX_O_WRONLY) | LINUX_O_CREAT | LINUX_O_APPEND;
        break;
    default:
        return shim_fail(LINUX_EINVAL);
    }
    *is_readable = mode[0] == 'r' || plus;
    *is_writable = mode[0] != 'r' || plus;
    return 0;
}

LIBC_FN ShimFile *sysv_fopen(const char *path, const char *mode)
{
    int flags;
    int is_readable;
    int is_writable;
    if (parse_mode(mode, &flags, &is_readable, &is_writable) != 0) {
        return NULL;
    }
    int fd = shim_fd_open(path, flags, 0666);
    if (fd < 0) {
        return NULL;
    }
    ShimFile *file = file_new(fd, is_readable, is_writable);
    if (file == NULL) {
        shim_fd_close(fd);
    }
    return file;
}

LIBC_FN ShimFile *sysv_fopen64(const char *path, const char *mode)
{
    return sysv_fopen(path, mode);
}

LIBC_FN ShimFile *sysv_fdopen(int fd, const char *mode)
{
    int flags;
    int is_readable;
    int is_writable;
    if (shim_fd_handle(fd) == INVALID_HANDLE_VALUE || parse_mode(mode, &flags, &is_readable, &is_writable) != 0) {
        return NULL;
    }
    return file_new(fd, is_readable, is_writable);
}

LIBC_FN int sysv_fclose(ShimFile *file)
{
    file_lock(file);
    int result = file_flush(file);
    if (shim_fd_close(file->fileno) != 0) {
        result = -1;
    }
    unlink_file(file);
    if (file->has_own_buffer) {
        HeapFree(GetProcessHeap(), 0, file->buf_base);
    }
    int is_static = file->is_static;
    file_unlock(file);
    if (!is_static) {
        HeapFree(GetProcessHeap(), 0, file);
    }
    return result;
}

LIBC_FN ShimFile *sysv_freopen64(const char *path, const char *mode, ShimFile *file)
{
    int flags;
    int is_readable;
    int is_writable;
    if (path == NULL || parse_mode(mode, &flags, &is_readable, &is_writable) != 0) {
        shim_set_errno(LINUX_EINVAL);
        return NULL;
    }
    file_lock(file);
    file_flush(file);
    int fd = shim_fd_open(path, flags, 0666);
    if (fd < 0) {
        file_unlock(file);
        return NULL;
    }
    shim_fd_close(file->fileno);
    reset_pointers(file);
    file->fileno = fd;
    file->state = STATE_IDLE;
    file->flags = 0;
    file->is_readable = is_readable;
    file->is_writable = is_writable;
    file_unlock(file);
    return file;
}

LIBC_FN int sysv_fflush(ShimFile *file)
{
    if (file == NULL) {
        stdio_flush_all();
        return 0;
    }
    file_lock(file);
    int result = file_flush(file);
    file_unlock(file);
    return result;
}

LIBC_FN int sysv_setvbuf(ShimFile *file, char *buffer, int mode, size_t size)
{
    if (mode < BUFFER_FULL || mode > BUFFER_NONE) {
        return shim_fail(LINUX_EINVAL);
    }
    file_lock(file);
    file_flush(file);
    file->buffer_mode = mode;
    if (buffer != NULL && size > 0) {
        if (file->has_own_buffer) {
            HeapFree(GetProcessHeap(), 0, file->buf_base);
        }
        file->buf_base = buffer;
        file->buf_end = buffer + size;
        file->has_own_buffer = 0;
    }
    reset_pointers(file);
    file->state = STATE_IDLE;
    file_unlock(file);
    return 0;
}

static int64_t current_offset(ShimFile *file)
{
    int64_t base = shim_fd_seek(file->fileno, 0, LINUX_SEEK_CUR);
    if (base < 0) {
        return -1;
    }
    if (file->state == STATE_READING && file->read_ptr != NULL) {
        base -= (int64_t)(file->read_end - file->read_ptr);
    } else if (file->state == STATE_WRITING) {
        base += (int64_t)(file->write_ptr - file->write_base);
    }
    return base;
}

LIBC_FN int sysv_fseeko64(ShimFile *file, int64_t offset, int whence)
{
    file_lock(file);
    /* Flushing or discarding the buffer first makes the descriptor's offset the stream's offset. */
    if (file_prepare(file, STATE_IDLE) != 0) {
        file_unlock(file);
        return -1;
    }
    int result = shim_fd_seek(file->fileno, offset, whence) < 0 ? -1 : 0;
    reset_pointers(file);
    if (result == 0) {
        file->flags &= ~IO_EOF_SEEN;
    }
    file_unlock(file);
    return result;
}

LIBC_FN int sysv_fseek(ShimFile *file, c_long offset, int whence)
{
    return sysv_fseeko64(file, offset, whence);
}

LIBC_FN int64_t sysv_ftello64(ShimFile *file)
{
    file_lock(file);
    int64_t offset = current_offset(file);
    file_unlock(file);
    return offset;
}

LIBC_FN c_long sysv_ftell(ShimFile *file)
{
    return (c_long)sysv_ftello64(file);
}

LIBC_FN void sysv_rewind(ShimFile *file)
{
    sysv_fseeko64(file, 0, LINUX_SEEK_SET);
    file->flags &= ~IO_ERR_SEEN;
}

LIBC_FN int sysv_feof(ShimFile *file)
{
    return (file->flags & IO_EOF_SEEN) != 0;
}

LIBC_FN int sysv_ferror(ShimFile *file)
{
    return (file->flags & IO_ERR_SEEN) != 0;
}

LIBC_FN void sysv_clearerr(ShimFile *file)
{
    file->flags &= ~(IO_EOF_SEEN | IO_ERR_SEEN);
}

LIBC_FN int sysv_fileno(ShimFile *file)
{
    return file->fileno;
}

LIBC_FN void sysv_flockfile(ShimFile *file)
{
    file_lock(file);
}

LIBC_FN void sysv_funlockfile(ShimFile *file)
{
    file_unlock(file);
}

LIBC_FN ShimFile *sysv_tmpfile64(void)
{
    char pattern[64] = "/tmp/crossport-tmpXXXXXX";
    int fd = sysv_mkstemp64(pattern);
    if (fd < 0) {
        return NULL;
    }
    ShimFile *file = file_new(fd, 1, 1);
    if (file == NULL) {
        shim_fd_close(fd);
    }
    sysv_unlink(pattern);
    return file;
}

LIBC_FN int sysv_remove(const char *path)
{
    int result = sysv_unlink(path);
    return (result != 0 && *shim_errno_ptr() == LINUX_EISDIR) ? sysv_rmdir(path) : result;
}
