/*
 * File: shim/stdlib_more.c
 * Purpose: div, strtoimax, reallocarray, heap introspection, getpagesize, getauxval and clearenv
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

LIBC_FN void *sysv_realloc(void *block, size_t size);
LIBC_FN char *sysv_getcwd(char *buffer, size_t size);
LIBC_FN char *sysv_strdup(const char *text);
LIBC_FN int sysv_unsetenv(const char *name);
LIBC_FN int64_t sysv_strtoll(const char *text, char **end, int base);
LIBC_FN uint64_t sysv_strtoull(const char *text, char **end, int base);

typedef struct {
    int quotient;
    int remainder;
} DivT;

typedef struct {
    int64_t quotient;
    int64_t remainder;
} LDivT;

LIBC_FN DivT sysv_div(int numerator, int denominator)
{
    return (DivT){numerator / denominator, numerator % denominator};
}

LIBC_FN LDivT sysv_ldiv(int64_t numerator, int64_t denominator)
{
    return (LDivT){numerator / denominator, numerator % denominator};
}

LIBC_FN LDivT sysv_lldiv(int64_t numerator, int64_t denominator)
{
    return sysv_ldiv(numerator, denominator);
}

LIBC_FN int64_t sysv_strtoimax(const char *text, char **end, int base)
{
    return sysv_strtoll(text, end, base);
}

LIBC_FN uint64_t sysv_strtoumax(const char *text, char **end, int base)
{
    return sysv_strtoull(text, end, base);
}

LIBC_FN int64_t sysv___isoc23_strtoimax(const char *text, char **end, int base)
{
    return sysv_strtoll(text, end, base);
}

LIBC_FN uint64_t sysv___isoc23_strtoumax(const char *text, char **end, int base)
{
    return sysv_strtoull(text, end, base);
}

LIBC_FN void *sysv_reallocarray(void *block, size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    return sysv_realloc(block, count * size);
}

LIBC_FN size_t sysv_malloc_usable_size(void *block)
{
    return block == NULL ? 0 : HeapSize(GetProcessHeap(), 0, block);
}

LIBC_FN int sysv_malloc_trim(size_t pad)
{
    (void)pad;
    return 0;
}

/* struct mallinfo2 is 80 bytes returned through a hidden pointer; the shim keeps no statistics. */
LIBC_FN void sysv_mallinfo2(uint64_t *result)
{
    memset(result, 0, 10 * sizeof(uint64_t));
}

LIBC_FN int sysv_getpagesize(void)
{
    return 4096;
}

LIBC_FN int sysv_gettid(void)
{
    return (int)GetCurrentThreadId();
}

/* No auxiliary vector is exposed; every lookup reports "not found". */
LIBC_FN uint64_t sysv_getauxval(uint64_t type)
{
    (void)type;
    shim_set_errno(LINUX_ENOENT);
    return 0;
}

LIBC_FN char *sysv_get_current_dir_name(void)
{
    char buffer[4096];
    return sysv_getcwd(buffer, sizeof(buffer)) != NULL ? sysv_strdup(buffer) : NULL;
}

LIBC_FN int sysv_clearenv(void)
{
    extern char **crossport_data_environ;
    while (crossport_data_environ != NULL && crossport_data_environ[0] != NULL) {
        char name[256];
        const char *entry = crossport_data_environ[0];
        size_t length = 0;
        while (entry[length] != '=' && entry[length] != '\0' && length < sizeof(name) - 1) {
            name[length] = entry[length];
            length++;
        }
        name[length] = '\0';
        if (sysv_unsetenv(name) != 0) {
            break;
        }
    }
    return 0;
}
