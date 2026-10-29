/*
 * File: shim/glob.c
 * Purpose: glob and globfree: expand a wildcard path pattern into the sorted list of existing paths that match
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The pattern is expanded one path component at a time. A component without wildcards is taken as it is;
 * one with wildcards is matched against the entries of every directory reached so far. Braces and tilde
 * expansion (GLOB_BRACE, GLOB_TILDE) are not supported.
 */

#include <stdlib.h>
#include <string.h>

#include "libc_abi.h"

#define GLOB_ERR 1
#define GLOB_MARK 2
#define GLOB_NOSORT 4
#define GLOB_DOOFFS 8
#define GLOB_NOCHECK 16
#define GLOB_APPEND 32
#define GLOB_NOESCAPE 64
#define GLOB_PERIOD 128
#define GLOB_NOSPACE 1
#define GLOB_ABORTED 2
#define GLOB_NOMATCH 3
#define FNM_NOESCAPE_FLAG 2
#define FNM_PERIOD_FLAG 4
#define MAX_COMPONENT 256
#define DIRECTORY_TYPE 4

/* struct glob_t on x86-64 (72 bytes); only the first four fields are used by programs. */
typedef struct {
    size_t count;
    char **paths;
    size_t offset;
    int flags;
    void *reserved[5];
} GlobT;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} PathList;

typedef struct {
    uint64_t inode;
    int64_t offset;
    uint16_t length;
    uint8_t type;
    char name[256];
} DirEntry;

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void *sysv_realloc(void *block, size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN char *sysv_strdup(const char *text);
LIBC_FN int sysv_fnmatch(const char *pattern, const char *text, int flags);
LIBC_FN int sysv_access(const char *path, int mode);
LIBC_FN void *sysv_opendir(const char *path);
LIBC_FN DirEntry *sysv_readdir64(void *directory);
LIBC_FN int sysv_closedir(void *directory);

static SYSV int has_wildcard(const char *component)
{
    return strpbrk(component, "*?[") != NULL;
}

static SYSV int push(PathList *list, char *path)
{
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        char **grown = (char **)sysv_realloc(list->items, capacity * sizeof(char *));
        if (grown == NULL) {
            sysv_free(path);
            return 0;
        }
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count++] = path;
    return 1;
}

/* `base` joined to `name` with a separator unless base is empty or already ends in one. */
static SYSV char *join(const char *base, const char *name)
{
    size_t base_length = strlen(base);
    int needs_slash = base_length > 0 && base[base_length - 1] != '/';
    char *path = (char *)sysv_malloc(base_length + (size_t)needs_slash + strlen(name) + 1);
    if (path != NULL) {
        memcpy(path, base, base_length);
        path[base_length] = '/';
        strcpy(path + base_length + (size_t)needs_slash, name);
    }
    return path;
}

static SYSV int is_directory_entry(const char *path)
{
    void *directory = sysv_opendir(path);
    if (directory == NULL) {
        return 0;
    }
    sysv_closedir(directory);
    return 1;
}

/* Every entry of the directories in `current` whose name matches `component`, joined onto the directory. */
static SYSV int match_component(const PathList *current, const char *component, int flags, PathList *next)
{
    int fnmatch_flags = ((flags & GLOB_NOESCAPE) ? FNM_NOESCAPE_FLAG : 0) | ((flags & GLOB_PERIOD) ? 0 : FNM_PERIOD_FLAG);
    for (size_t i = 0; i < current->count; i++) {
        const char *base = current->items[i];
        void *directory = sysv_opendir(base[0] == '\0' ? "." : base);
        DirEntry *entry;
        while (directory != NULL && (entry = sysv_readdir64(directory)) != NULL) {
            int is_dot = strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0;
            if (is_dot || sysv_fnmatch(component, entry->name, fnmatch_flags) != 0) {
                continue;
            }
            char *path = join(base, entry->name);
            if (path == NULL || !push(next, path)) {
                sysv_closedir(directory);
                return 0;
            }
        }
        if (directory != NULL) {
            sysv_closedir(directory);
        }
    }
    return 1;
}

static SYSV void free_list(PathList *list)
{
    for (size_t i = 0; i < list->count; i++) {
        sysv_free(list->items[i]);
    }
    sysv_free(list->items);
    *list = (PathList){0};
}

/* Appends `component` to every path in `current` (a literal component: no directory is read). */
static SYSV int extend_literal(const PathList *current, const char *component, PathList *next)
{
    for (size_t i = 0; i < current->count; i++) {
        char *path = current->items[i][0] == '\0' && component[0] == '\0' ? sysv_strdup("") : join(current->items[i], component);
        if (path == NULL || !push(next, path)) {
            return 0;
        }
    }
    return 1;
}

static SYSV int expand(const char *pattern, int flags, PathList *result)
{
    PathList current = {0};
    char *text = sysv_strdup(pattern);
    if (text == NULL || !push(&current, sysv_strdup(pattern[0] == '/' ? "/" : ""))) {
        sysv_free(text);
        free_list(&current);
        return 0;
    }
    for (char *part = text; part != NULL;) {
        char *slash = strchr(part, '/');
        if (slash != NULL) {
            *slash = '\0';
        }
        char *following = slash != NULL ? slash + 1 : NULL;
        if (part[0] != '\0') {
            PathList next = {0};
            int ok = has_wildcard(part) ? match_component(&current, part, flags, &next) : extend_literal(&current, part, &next);
            free_list(&current);
            current = next;
            if (!ok) {
                free_list(&current);
                sysv_free(text);
                return 0;
            }
        }
        part = following;
    }
    sysv_free(text);
    *result = current;
    return 1;
}

static int compare_paths(const void *first, const void *second)
{
    return strcmp(*(char *const *)first, *(char *const *)second);
}

LIBC_FN int sysv_glob(const char *pattern, int flags, void *error_function, GlobT *result)
{
    (void)error_function;
    PathList found = {0};
    if (!expand(pattern, flags, &found)) {
        return GLOB_NOSPACE;
    }
    size_t kept = 0;
    for (size_t i = 0; i < found.count; i++) {
        int exists = sysv_access(found.items[i], 0) == 0;
        if (exists && (flags & GLOB_MARK) && is_directory_entry(found.items[i])) {
            char *marked = join(found.items[i], "");
            if (marked != NULL) {
                sysv_free(found.items[i]);
                found.items[i] = marked;
            }
        }
        if (exists) {
            found.items[kept++] = found.items[i];
        } else {
            sysv_free(found.items[i]);
        }
    }
    found.count = kept;
    if (kept == 0 && (flags & GLOB_NOCHECK) && push(&found, sysv_strdup(pattern))) {
        kept = 1;
    }
    if (kept == 0) {
        free_list(&found);
        return GLOB_NOMATCH;
    }
    if (!(flags & GLOB_NOSORT)) {
        qsort(found.items, kept, sizeof(char *), compare_paths);
    }
    size_t offset = (flags & GLOB_DOOFFS) ? result->offset : 0;
    size_t before = (flags & GLOB_APPEND) ? result->count : 0;
    char **paths = (char **)sysv_malloc((offset + before + kept + 1) * sizeof(char *));
    if (paths == NULL) {
        free_list(&found);
        return GLOB_NOSPACE;
    }
    memset(paths, 0, offset * sizeof(char *));
    if (before > 0) {
        memcpy(paths + offset, result->paths + offset, before * sizeof(char *));
    }
    memcpy(paths + offset + before, found.items, kept * sizeof(char *));
    paths[offset + before + kept] = NULL;
    sysv_free(found.items);
    if (before > 0) {
        sysv_free(result->paths);
    }
    result->paths = paths;
    result->count = before + kept;
    result->offset = offset;
    return 0;
}

LIBC_FN void sysv_globfree(GlobT *result)
{
    for (size_t i = 0; i < result->count; i++) {
        sysv_free(result->paths[result->offset + i]);
    }
    sysv_free(result->paths);
    result->paths = NULL;
    result->count = 0;
}
