/*
 * File: shim/stdio_more.c
 * Purpose: getline, getdelim, asprintf, dprintf, scanf from streams, temporary files, getchar and the unlocked stdio names
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "stdio_internal.h"

#define LINE_START_CAPACITY 120
#define TEMPLATE_SUFFIX "XXXXXX"
#define TEMPLATE_SUFFIX_LENGTH 6
#define MAX_SCAN_LINE 4096
#define TEMP_NAME_COUNT 238328

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void *sysv_realloc(void *block, size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN int sysv_fgetc(ShimFile *file);
LIBC_FN int sysv_fputc(int c, ShimFile *file);
LIBC_FN int sysv_ungetc(int c, ShimFile *file);
LIBC_FN char *sysv_fgets(char *buffer, int size, ShimFile *file);
LIBC_FN size_t sysv_fwrite(const void *buffer, size_t size, size_t count, ShimFile *file);
LIBC_FN int sysv_fputs(const char *text, ShimFile *file);
LIBC_FN int sysv_getc(ShimFile *file);
LIBC_FN int sysv_putc(int c, ShimFile *file);
LIBC_FN int sysv_fflush(ShimFile *file);
LIBC_FN int sysv_feof(ShimFile *file);
LIBC_FN int sysv_ferror(ShimFile *file);
LIBC_FN void sysv_clearerr(ShimFile *file);
LIBC_FN int sysv_fseeko64(ShimFile *file, int64_t offset, int whence);
LIBC_FN int64_t sysv_ftello64(ShimFile *file);
LIBC_FN int sysv_setvbuf(ShimFile *file, char *buffer, int mode, size_t size);
LIBC_FN int sysv_vsnprintf(char *buffer, size_t size, const char *format, __builtin_sysv_va_list ap);
LIBC_FN int sysv_vsscanf(const char *text, const char *format, __builtin_sysv_va_list ap);
LIBC_FN int sysv_mkstemp64(char *pattern);
LIBC_FN int64_t sysv_write(int fd, const void *buffer, size_t count);
LIBC_FN int sysv_access(const char *path, int mode);
LIBC_FN size_t sysv_fread(void *destination, size_t size, size_t count, ShimFile *file);

/* Reads up to and including `delimiter` into a growing heap buffer. Returns the length read, or -1 at end of input. */
LIBC_FN int64_t sysv_getdelim(char **line, size_t *capacity, int delimiter, ShimFile *file)
{
    if (line == NULL || capacity == NULL) {
        return shim_fail(LINUX_EINVAL);
    }
    if (*line == NULL || *capacity == 0) {
        *capacity = LINE_START_CAPACITY;
        *line = (char *)sysv_malloc(*capacity);
        if (*line == NULL) {
            return shim_fail(LINUX_ENOMEM);
        }
    }
    size_t length = 0;
    int c;
    while ((c = sysv_fgetc(file)) != SHIM_EOF) {
        if (length + 2 > *capacity) {
            char *grown = (char *)sysv_realloc(*line, *capacity * 2);
            if (grown == NULL) {
                return shim_fail(LINUX_ENOMEM);
            }
            *line = grown;
            *capacity *= 2;
        }
        (*line)[length++] = (char)c;
        if (c == delimiter) {
            break;
        }
    }
    if (length == 0) {
        return -1;
    }
    (*line)[length] = '\0';
    return (int64_t)length;
}

LIBC_FN int64_t sysv___getdelim(char **line, size_t *capacity, int delimiter, ShimFile *file)
{
    return sysv_getdelim(line, capacity, delimiter, file);
}

LIBC_FN int64_t sysv_getline(char **line, size_t *capacity, ShimFile *file)
{
    return sysv_getdelim(line, capacity, '\n', file);
}

LIBC_FN int sysv_getchar(void)
{
    return sysv_fgetc(crossport_data_stdin);
}

LIBC_FN int sysv_vasprintf(char **out, const char *format, __builtin_sysv_va_list ap)
{
    __builtin_sysv_va_list copy;
    __builtin_sysv_va_copy(copy, ap);
    int length = sysv_vsnprintf(NULL, 0, format, copy);
    __builtin_sysv_va_end(copy);
    if (length < 0) {
        return -1;
    }
    *out = (char *)sysv_malloc((size_t)length + 1);
    if (*out == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    return sysv_vsnprintf(*out, (size_t)length + 1, format, ap);
}

LIBC_FN int sysv_asprintf(char **out, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vasprintf(out, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___asprintf_chk(char **out, int flag, const char *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vasprintf(out, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___vasprintf_chk(char **out, int flag, const char *format, __builtin_sysv_va_list ap)
{
    (void)flag;
    return sysv_vasprintf(out, format, ap);
}

LIBC_FN int sysv_vdprintf(int fd, const char *format, __builtin_sysv_va_list ap)
{
    char *text = NULL;
    int length = sysv_vasprintf(&text, format, ap);
    if (length >= 0 && sysv_write(fd, text, (size_t)length) != length) {
        length = -1;
    }
    sysv_free(text);
    return length;
}

LIBC_FN int sysv_dprintf(int fd, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vdprintf(fd, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___dprintf_chk(int fd, int flag, const char *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vdprintf(fd, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

/*
 * Scanning from a stream works on one line at a time: a line is read, scanned, and what the format did
 * not consume of it is dropped. That is the common use (one conversion per line); a program that scans
 * part of a line and then reads the rest with another call sees the rest missing.
 */
LIBC_FN int sysv_vfscanf(ShimFile *file, const char *format, __builtin_sysv_va_list ap)
{
    char line[MAX_SCAN_LINE];
    if (sysv_fgets(line, sizeof(line), file) == NULL) {
        return -1;
    }
    return sysv_vsscanf(line, format, ap);
}

LIBC_FN int sysv_fscanf(ShimFile *file, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(file, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv_vscanf(const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(crossport_data_stdin, format, ap);
}

LIBC_FN int sysv_scanf(const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(crossport_data_stdin, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc99_fscanf(ShimFile *file, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(file, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc23_fscanf(ShimFile *file, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(file, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc99_scanf(const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(crossport_data_stdin, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc23_scanf(const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(crossport_data_stdin, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc99_vfscanf(ShimFile *file, const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(file, format, ap);
}

LIBC_FN int sysv___isoc23_vfscanf(ShimFile *file, const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(file, format, ap);
}

LIBC_FN int sysv___isoc99_vscanf(const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(crossport_data_stdin, format, ap);
}

LIBC_FN int sysv___isoc23_vscanf(const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(crossport_data_stdin, format, ap);
}

LIBC_FN int sysv_mkstemp(char *pattern)
{
    return sysv_mkstemp64(pattern);
}

LIBC_FN int sysv_mkostemp(char *pattern, int flags)
{
    (void)flags;
    return sysv_mkstemp64(pattern);
}

LIBC_FN int sysv_mkostemp64(char *pattern, int flags)
{
    (void)flags;
    return sysv_mkstemp64(pattern);
}

/* Fills in a unique name that does not exist yet (the file is not created: the classic mktemp race). */
LIBC_FN char *sysv_mktemp(char *pattern)
{
    size_t length = strlen(pattern);
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    if (length < TEMPLATE_SUFFIX_LENGTH || strcmp(pattern + length - TEMPLATE_SUFFIX_LENGTH, TEMPLATE_SUFFIX) != 0) {
        shim_set_errno(LINUX_EINVAL);
        pattern[0] = '\0';
        return pattern;
    }
    for (int attempt = 0; attempt < TEMP_NAME_COUNT; attempt++) {
        uint32_t bits = (uint32_t)GetTickCount() ^ ((uint32_t)attempt * 2654435761u) ^ GetCurrentProcessId();
        for (int i = 0; i < TEMPLATE_SUFFIX_LENGTH; i++) {
            pattern[length - TEMPLATE_SUFFIX_LENGTH + i] = alphabet[bits % (sizeof(alphabet) - 1)];
            bits = bits / (sizeof(alphabet) - 1) + (uint32_t)attempt;
        }
        if (sysv_access(pattern, 0) != 0) {
            return pattern;
        }
    }
    pattern[0] = '\0';
    return pattern;
}

LIBC_FN char *sysv_tmpnam(char *buffer)
{
    static char fallback[32];
    char *name = buffer != NULL ? buffer : fallback;
    strcpy(name, "/tmp/tmpXXXXXX");
    return sysv_mktemp(name);
}

LIBC_FN void sysv_setbuf(ShimFile *file, char *buffer)
{
    sysv_setvbuf(file, buffer, buffer != NULL ? BUFFER_FULL : BUFFER_NONE, SHIM_BUFFER_BYTES);
}

LIBC_FN void sysv_setlinebuf(ShimFile *file)
{
    sysv_setvbuf(file, NULL, BUFFER_LINE, 0);
}

LIBC_FN int sysv_fseeko(ShimFile *file, int64_t offset, int whence)
{
    return sysv_fseeko64(file, offset, whence);
}

LIBC_FN int64_t sysv_ftello(ShimFile *file)
{
    return sysv_ftello64(file);
}

/* The unlocked variants are the same functions: the shim's stream lock is cheap and recursive. */
LIBC_FN int sysv_getc_unlocked(ShimFile *file)
{
    return sysv_getc(file);
}

LIBC_FN int sysv_fgetc_unlocked(ShimFile *file)
{
    return sysv_fgetc(file);
}

LIBC_FN int sysv_getchar_unlocked(void)
{
    return sysv_fgetc(crossport_data_stdin);
}

LIBC_FN int sysv_putc_unlocked(int c, ShimFile *file)
{
    return sysv_putc(c, file);
}

LIBC_FN int sysv_fputc_unlocked(int c, ShimFile *file)
{
    return sysv_fputc(c, file);
}

LIBC_FN int sysv_putchar_unlocked(int c)
{
    return sysv_fputc(c, crossport_data_stdout);
}

LIBC_FN int sysv_fputs_unlocked(const char *text, ShimFile *file)
{
    return sysv_fputs(text, file);
}

LIBC_FN size_t sysv_fwrite_unlocked(const void *buffer, size_t size, size_t count, ShimFile *file)
{
    return sysv_fwrite(buffer, size, count, file);
}

LIBC_FN size_t sysv_fread_unlocked(void *destination, size_t size, size_t count, ShimFile *file)
{
    return sysv_fread(destination, size, count, file);
}

LIBC_FN int sysv_fflush_unlocked(ShimFile *file)
{
    return sysv_fflush(file);
}

LIBC_FN char *sysv_fgets_unlocked(char *buffer, int size, ShimFile *file)
{
    return sysv_fgets(buffer, size, file);
}

LIBC_FN char *sysv___fgets_chk(char *buffer, size_t capacity, int size, ShimFile *file);

LIBC_FN char *sysv___fgets_unlocked_chk(char *buffer, size_t capacity, int size, ShimFile *file)
{
    return sysv___fgets_chk(buffer, capacity, size, file);
}

LIBC_FN int sysv_feof_unlocked(ShimFile *file)
{
    return sysv_feof(file);
}

LIBC_FN int sysv_ferror_unlocked(ShimFile *file)
{
    return sysv_ferror(file);
}

LIBC_FN void sysv_clearerr_unlocked(ShimFile *file)
{
    sysv_clearerr(file);
}

/* Bytes written to the stream but not yet flushed. */
LIBC_FN size_t sysv___fpending(ShimFile *file)
{
    return file->write_ptr > file->write_base ? (size_t)(file->write_ptr - file->write_base) : 0;
}

LIBC_FN int sysv___fsetlocking(ShimFile *file, int type)
{
    (void)file;
    (void)type;
    return 1; /* FSETLOCKING_INTERNAL: the shim always locks */
}
