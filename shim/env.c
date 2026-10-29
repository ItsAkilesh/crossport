/*
 * File: shim/env.c
 * Purpose: The process environment as a UTF-8 table: environ, getenv, setenv, putenv, unsetenv
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "libc_abi.h"
#include "env_internal.h"

#define MAX_ENVIRONMENT 4096
#define UTF8_LIMIT_FACTOR 4

/* Linux programs read `environ` as a data symbol (copied into the executable at startup). */
LIBC_DATA(char **, environ);

static char *g_entries[MAX_ENVIRONMENT + 1];
static size_t g_count;
static SRWLOCK g_lock = SRWLOCK_INIT;

static char *utf8_from_wide(const wchar_t *wide)
{
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char *text = needed > 0 ? (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)needed) : NULL;
    if (text != NULL) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, needed, NULL, NULL);
    }
    return text;
}

static char *duplicate(const char *text)
{
    size_t size = (size_t)lstrlenA(text) + 1;
    char *copy = (char *)HeapAlloc(GetProcessHeap(), 0, size);
    if (copy != NULL) {
        CopyMemory(copy, text, size);
    }
    return copy;
}

void env_init(void)
{
    wchar_t *block = GetEnvironmentStringsW();
    if (block == NULL) {
        crossport_data_environ = g_entries;
        return;
    }
    for (const wchar_t *entry = block; *entry != L'\0' && g_count < MAX_ENVIRONMENT; entry += lstrlenW(entry) + 1) {
        /* Windows keeps per-drive variables such as "=C:=C:\\"; Linux has no such entries. */
        if (*entry == L'=') {
            continue;
        }
        char *text = utf8_from_wide(entry);
        if (text != NULL) {
            g_entries[g_count++] = text;
        }
    }
    FreeEnvironmentStringsW(block);
    g_entries[g_count] = NULL;
    crossport_data_environ = g_entries;
}

static size_t name_length(const char *entry)
{
    size_t length = 0;
    while (entry[length] != '\0' && entry[length] != '=') {
        length++;
    }
    return length;
}

static ptrdiff_t find_locked(const char *name, size_t length)
{
    for (size_t i = 0; i < g_count; i++) {
        if (name_length(g_entries[i]) == length && memcmp(g_entries[i], name, length) == 0) {
            return (ptrdiff_t)i;
        }
    }
    return -1;
}

LIBC_FN char *sysv_getenv(const char *name)
{
    char *value = NULL;
    size_t length = (size_t)lstrlenA(name);
    AcquireSRWLockShared(&g_lock);
    ptrdiff_t at = find_locked(name, length);
    if (at >= 0) {
        value = g_entries[at] + length + 1;
    }
    ReleaseSRWLockShared(&g_lock);
    return value;
}

static int store(const char *name, const char *value, int overwrite)
{
    size_t length = (size_t)lstrlenA(name);
    if (length == 0 || strchr(name, '=') != NULL) {
        return shim_fail(LINUX_EINVAL);
    }
    size_t value_length = (size_t)lstrlenA(value);
    char *entry = (char *)HeapAlloc(GetProcessHeap(), 0, length + 1 + value_length + 1);
    if (entry == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    CopyMemory(entry, name, length);
    entry[length] = '=';
    CopyMemory(entry + length + 1, value, value_length + 1);
    int result = 0;
    AcquireSRWLockExclusive(&g_lock);
    ptrdiff_t at = find_locked(name, length);
    if (at >= 0 && !overwrite) {
        HeapFree(GetProcessHeap(), 0, entry);
    } else if (at >= 0) {
        g_entries[at] = entry; /* the old string leaks: callers may still hold a getenv pointer */
    } else if (g_count < MAX_ENVIRONMENT) {
        g_entries[g_count++] = entry;
        g_entries[g_count] = NULL;
    } else {
        HeapFree(GetProcessHeap(), 0, entry);
        result = shim_fail(LINUX_ENOMEM);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

LIBC_FN int sysv_setenv(const char *name, const char *value, int overwrite)
{
    return store(name, value, overwrite);
}

LIBC_FN int sysv_unsetenv(const char *name)
{
    size_t length = (size_t)lstrlenA(name);
    AcquireSRWLockExclusive(&g_lock);
    ptrdiff_t at = find_locked(name, length);
    if (at >= 0) {
        g_entries[at] = g_entries[--g_count];
        g_entries[g_count] = NULL;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return 0;
}

LIBC_FN int sysv_putenv(char *assignment)
{
    char *equals = strchr(assignment, '=');
    if (equals == NULL) {
        return sysv_unsetenv(assignment);
    }
    char *name = duplicate(assignment);
    if (name == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    name[equals - assignment] = '\0';
    int result = store(name, equals + 1, 1);
    HeapFree(GetProcessHeap(), 0, name);
    return result;
}
