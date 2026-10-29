/*
 * File: shim/stdio_more.c
 * Purpose: Temporary file names, setbuf, fseeko and the stdio extension functions
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "stdio_internal.h"

#define TEMPLATE_SUFFIX "XXXXXX"
#define TEMPLATE_SUFFIX_LENGTH 6
#define TEMP_NAME_COUNT 238328

LIBC_FN int sysv_fseeko64(ShimFile *file, int64_t offset, int whence);
LIBC_FN int64_t sysv_ftello64(ShimFile *file);
LIBC_FN int sysv_setvbuf(ShimFile *file, char *buffer, int mode, size_t size);
LIBC_FN int sysv_mkstemp64(char *pattern);
LIBC_FN int sysv_access(const char *path, int mode);

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
