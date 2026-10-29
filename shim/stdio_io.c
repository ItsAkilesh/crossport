/*
 * File: shim/stdio_io.c
 * Purpose: Buffered reading and writing: getc, fgets, fread, ungetc, putc, fputs, fwrite, puts and glibc's hooks
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "stdio_internal.h"

int file_put(ShimFile *file, const void *data, size_t count)
{
    if (count == 0) {
        return 0;
    }
    if (!file->is_writable) {
        file->flags |= IO_ERR_SEEN;
        return shim_fail(LINUX_EBADF);
    }
    if (file_prepare(file, STATE_WRITING) != 0) {
        file->flags |= IO_ERR_SEEN;
        return -1;
    }
    size_t capacity = (size_t)(file->buf_end - file->buf_base);
    if (file->buffer_mode == BUFFER_NONE || count >= capacity) {
        if (file_flush(file) != 0 || shim_fd_write(file->fileno, data, count) < 0) {
            file->flags |= IO_ERR_SEEN;
            return -1;
        }
        return 0;
    }
    if (count > (size_t)(file->buf_end - file->write_ptr) && file_flush(file) != 0) {
        return -1;
    }
    CopyMemory(file->write_ptr, data, count);
    file->write_ptr += count;
    int has_newline = file->buffer_mode == BUFFER_LINE && memchr(data, '\n', count) != NULL;
    return has_newline ? file_flush(file) : 0;
}

int file_fill(ShimFile *file)
{
    if (!file->is_readable) {
        file->flags |= IO_ERR_SEEN;
        return shim_fail(LINUX_EBADF), SHIM_EOF;
    }
    if (file_prepare(file, STATE_READING) != 0) {
        file->flags |= IO_ERR_SEEN;
        return SHIM_EOF;
    }
    /* One byte of room is kept in front so ungetc always has somewhere to put a character back. */
    int64_t got = shim_fd_read(file->fileno, file->buf_base + 1, (size_t)(file->buf_end - file->buf_base) - 1);
    if (got <= 0) {
        file->flags |= got == 0 ? IO_EOF_SEEN : IO_ERR_SEEN;
        return SHIM_EOF;
    }
    file->read_base = file->buf_base;
    file->read_ptr = file->buf_base + 1;
    file->read_end = file->read_ptr + got;
    return (uint8_t)*file->read_ptr++;
}

static int get_locked(ShimFile *file)
{
    if (file->state == STATE_READING && file->read_ptr != NULL && file->read_ptr < file->read_end) {
        return (uint8_t)*file->read_ptr++;
    }
    return file_fill(file);
}

LIBC_FN int sysv___uflow(ShimFile *file)
{
    file_lock(file);
    int c = get_locked(file);
    file_unlock(file);
    return c;
}

LIBC_FN int sysv___overflow(ShimFile *file, int c)
{
    file_lock(file);
    int result;
    if (c == SHIM_EOF) {
        result = file_flush(file) == 0 ? 0 : SHIM_EOF;
    } else {
        uint8_t byte = (uint8_t)c;
        result = file_put(file, &byte, 1) == 0 ? c & 0xff : SHIM_EOF;
    }
    file_unlock(file);
    return result;
}

LIBC_FN int sysv_fgetc(ShimFile *file)
{
    return sysv___uflow(file);
}

LIBC_FN int sysv_getc(ShimFile *file)
{
    return sysv___uflow(file);
}

LIBC_FN int sysv_ungetc(int c, ShimFile *file)
{
    if (c == SHIM_EOF) {
        return SHIM_EOF;
    }
    file_lock(file);
    int result = SHIM_EOF;
    if (file_prepare(file, STATE_READING) == 0) {
        if (file->read_ptr == NULL) {
            file->read_base = file->buf_base;
            file->read_ptr = file->read_end = file->buf_base + 1;
        }
        if (file->read_ptr > file->read_base) {
            *--file->read_ptr = (char)c;
            file->flags &= ~IO_EOF_SEEN;
            result = (uint8_t)c;
        }
    }
    file_unlock(file);
    return result;
}

LIBC_FN char *sysv_fgets(char *buffer, int size, ShimFile *file)
{
    if (size <= 0) {
        return NULL;
    }
    file_lock(file);
    int count = 0;
    while (count < size - 1) {
        int c = get_locked(file);
        if (c == SHIM_EOF) {
            break;
        }
        buffer[count++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    file_unlock(file);
    if (count == 0) {
        return NULL;
    }
    buffer[count] = '\0';
    return buffer;
}

LIBC_FN size_t sysv_fread(void *destination, size_t size, size_t count, ShimFile *file)
{
    if (size == 0 || count == 0 || count > SIZE_MAX / size) {
        return 0;
    }
    size_t wanted = size * count;
    uint8_t *out = (uint8_t *)destination;
    size_t done = 0;
    file_lock(file);
    while (done < wanted) {
        if (file->state == STATE_READING && file->read_ptr != NULL && file->read_ptr < file->read_end) {
            size_t available = (size_t)(file->read_end - file->read_ptr);
            size_t take = available < wanted - done ? available : wanted - done;
            CopyMemory(out + done, file->read_ptr, take);
            file->read_ptr += take;
            done += take;
            continue;
        }
        int c = file_fill(file);
        if (c == SHIM_EOF) {
            break;
        }
        out[done++] = (uint8_t)c;
    }
    file_unlock(file);
    return done / size;
}

LIBC_FN size_t sysv_fwrite(const void *source, size_t size, size_t count, ShimFile *file)
{
    if (size == 0 || count == 0 || count > SIZE_MAX / size) {
        return 0;
    }
    file_lock(file);
    int failed = file_put(file, source, size * count) != 0;
    file_unlock(file);
    return failed ? 0 : count;
}

LIBC_FN int sysv_fputs(const char *text, ShimFile *file)
{
    file_lock(file);
    int failed = file_put(file, text, strlen(text)) != 0;
    file_unlock(file);
    return failed ? SHIM_EOF : 1;
}

LIBC_FN int sysv_fputc(int c, ShimFile *file)
{
    return sysv___overflow(file, c);
}

LIBC_FN int sysv_putc(int c, ShimFile *file)
{
    return sysv___overflow(file, c);
}

LIBC_FN int sysv_putchar(int c)
{
    return sysv___overflow(crossport_data_stdout, c);
}

LIBC_FN int sysv_puts(const char *text)
{
    ShimFile *out = crossport_data_stdout;
    file_lock(out);
    int failed = file_put(out, text, strlen(text)) != 0 || file_put(out, "\n", 1) != 0;
    file_unlock(out);
    return failed ? SHIM_EOF : 1;
}

LIBC_FN void sysv_perror(const char *prefix)
{
    char scratch[48];
    ShimFile *err = crossport_data_stderr;
    file_lock(err);
    if (prefix != NULL && *prefix != '\0') {
        file_put(err, prefix, strlen(prefix));
        file_put(err, ": ", 2);
    }
    const char *message = shim_strerror(*shim_errno_ptr(), scratch, sizeof(scratch));
    file_put(err, message, strlen(message));
    file_put(err, "\n", 1);
    file_unlock(err);
}
