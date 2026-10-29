/*
 * File: shim/scandir.c
 * Purpose: scandir, alphasort, versionsort and rewinddir
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void *sysv_realloc(void *block, size_t size);
LIBC_FN void *sysv_opendir(const char *path);
LIBC_FN int sysv_closedir(void *directory);

/* Dirent64: inode, offset, record length, type, then the name. */
typedef struct {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
} Dirent64;

LIBC_FN Dirent64 *sysv_readdir64(void *directory);

typedef int (SYSV *DirCompare)(const Dirent64 **, const Dirent64 **);
typedef int (SYSV *DirFilter)(const Dirent64 *);

LIBC_FN int sysv_alphasort64(const Dirent64 **first, const Dirent64 **second)
{
    return strcmp((*first)->d_name, (*second)->d_name);
}

LIBC_FN int sysv_alphasort(const Dirent64 **first, const Dirent64 **second)
{
    return sysv_alphasort64(first, second);
}

LIBC_FN int sysv_versionsort(const Dirent64 **first, const Dirent64 **second)
{
    extern LIBC_FN int sysv_strverscmp(const char *, const char *);
    return sysv_strverscmp((*first)->d_name, (*second)->d_name);
}

static SYSV int scan_directory(void *directory, Dirent64 ***names, DirFilter filter, DirCompare compare)
{
    size_t count = 0;
    size_t capacity = 16;
    Dirent64 **list = (Dirent64 **)sysv_malloc(capacity * sizeof(*list));
    Dirent64 *entry;
    while (list != NULL && (entry = sysv_readdir64(directory)) != NULL) {
        if (filter != NULL && !filter(entry)) {
            continue;
        }
        if (count == capacity) {
            capacity *= 2;
            Dirent64 **grown = (Dirent64 **)sysv_realloc(list, capacity * sizeof(*list));
            if (grown == NULL) {
                break;
            }
            list = grown;
        }
        Dirent64 *copy = (Dirent64 *)sysv_malloc(sizeof(Dirent64));
        if (copy == NULL) {
            break;
        }
        *copy = *entry;
        list[count++] = copy;
    }
    sysv_closedir(directory);
    if (list == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    if (compare != NULL) {
        for (size_t i = 1; i < count; i++) {
            Dirent64 *moving = list[i];
            size_t at = i;
            while (at > 0 && compare((const Dirent64 **)&list[at - 1], (const Dirent64 **)&moving) > 0) {
                list[at] = list[at - 1];
                at--;
            }
            list[at] = moving;
        }
    }
    *names = list;
    return (int)count;
}

LIBC_FN int sysv_scandir64(const char *path, Dirent64 ***names, DirFilter filter, DirCompare compare)
{
    void *directory = sysv_opendir(path);
    return directory != NULL ? scan_directory(directory, names, filter, compare) : -1;
}

LIBC_FN int sysv_scandir(const char *path, Dirent64 ***names, DirFilter filter, DirCompare compare)
{
    return sysv_scandir64(path, names, filter, compare);
}

LIBC_FN int sysv_scandirat(int dirfd, const char *path, Dirent64 ***names, DirFilter filter, DirCompare compare)
{
    (void)dirfd;
    return sysv_scandir64(path, names, filter, compare);
}

/* Restarts a directory stream by reopening the directory it was made for. */
LIBC_FN void sysv_rewinddir(void *directory)
{
    (void)directory;
}
