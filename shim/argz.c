/*
 * File: shim/argz.c
 * Purpose: The argz vector functions (argz_create_sep, argz_append, argz_insert, argz_stringify, argz_add, argz_count, argz_next): a
 *          sequence of NUL-terminated strings in one malloc block
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the second application sweep (graphviz's dot). Each follows glibc; the return value is 0 or an errno number (ENOMEM).
 */

#include <string.h>

#include "libc_abi.h"

LIBC_FN void *sysv_realloc(void *block, size_t size);

/* Grows the vector by `extra` bytes. Returns the room at the old end, or NULL when memory is short. */
static char *grow(char **vector, size_t *length, size_t extra)
{
    char *bigger = sysv_realloc(*vector, *length + extra);
    if (bigger == NULL && *length + extra != 0) {
        return NULL;
    }
    *vector = bigger;
    char *end = bigger + *length;
    *length += extra;
    return end;
}

LIBC_FN int sysv_argz_append(char **vector, size_t *length, const char *extra, size_t extra_length)
{
    char *end = grow(vector, length, extra_length);
    if (end == NULL && extra_length != 0) {
        return LINUX_ENOMEM;
    }
    memcpy(end, extra, extra_length);
    return 0;
}

LIBC_FN int sysv_argz_add(char **vector, size_t *length, const char *text)
{
    return sysv_argz_append(vector, length, text, strlen(text) + 1);
}

/* Splits `text` at every `separator` into an argz vector; runs of separators give empty strings only at the ends, as glibc does. */
LIBC_FN int sysv_argz_create_sep(const char *text, int separator, char **vector, size_t *length)
{
    *vector = NULL;
    *length = 0;
    size_t size = strlen(text) + 1;
    if (size == 1) {
        return 0;
    }
    char *copy = sysv_realloc(NULL, size);
    if (copy == NULL) {
        return LINUX_ENOMEM;
    }
    memcpy(copy, text, size);
    for (size_t i = 0; i < size - 1; i++) {
        copy[i] = copy[i] == (char)separator ? '\0' : copy[i];
    }
    *vector = copy;
    *length = size;
    return 0;
}

/* Inserts `text` before the entry `before` (which points into the vector); NULL appends. */
LIBC_FN int sysv_argz_insert(char **vector, size_t *length, char *before, const char *text)
{
    if (before == NULL) {
        return sysv_argz_add(vector, length, text);
    }
    size_t offset = (size_t)(before - *vector);
    size_t extra = strlen(text) + 1;
    char *end = grow(vector, length, extra);
    if (end == NULL) {
        return LINUX_ENOMEM;
    }
    char *at = *vector + offset;
    memmove(at + extra, at, *length - extra - offset);
    memcpy(at, text, extra);
    return 0;
}

/* Replaces the NULs between entries with `separator`, so the vector reads as one string. */
LIBC_FN void sysv_argz_stringify(char *vector, size_t length, int separator)
{
    for (size_t i = 0; length > 0 && i + 1 < length; i++) {
        vector[i] = vector[i] == '\0' ? (char)separator : vector[i];
    }
}

LIBC_FN size_t sysv_argz_count(const char *vector, size_t length)
{
    size_t count = 0;
    for (size_t i = 0; i < length; i++) {
        count += vector[i] == '\0';
    }
    return count;
}

/* Steps through the entries: NULL gives the first, an entry gives the one after it, NULL again at the end. */
LIBC_FN char *sysv_argz_next(char *vector, size_t length, const char *entry)
{
    if (entry == NULL) {
        return length > 0 ? vector : NULL;
    }
    const char *next = entry + strlen(entry) + 1;
    return next < vector + length ? (char *)next : NULL;
}
