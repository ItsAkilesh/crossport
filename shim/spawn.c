/*
 * File: shim/spawn.c
 * Purpose: posix_spawn with file actions, and the exec family as run-then-exit
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "process.h"

enum ActionKind { ACTION_DUP2, ACTION_CLOSE, ACTION_OPEN };

typedef struct {
    int kind;
    int fd;
    int other;      /* the descriptor dup2 copies, or the open flags */
    int mode;       /* open mode */
    char *path;     /* open path, owned by the action */
} Action;

/* Overlays posix_spawn_file_actions_t (80 bytes in glibc). */
typedef struct {
    Action *items;
    int count;
    int capacity;
} FileActions;

_Static_assert(sizeof(FileActions) <= 80, "FileActions must fit in posix_spawn_file_actions_t");

typedef struct {
    int fd;
    int pid;
    void *stream;
} Popen;

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN NORETURN void sysv__exit(int status);

LIBC_FN int sysv_posix_spawn_file_actions_init(FileActions *actions)
{
    *actions = (FileActions){0};
    return 0;
}

LIBC_FN int sysv_posix_spawn_file_actions_destroy(FileActions *actions)
{
    for (int i = 0; i < actions->count; i++) {
        sysv_free(actions->items[i].path);
    }
    sysv_free(actions->items);
    *actions = (FileActions){0};
    return 0;
}

static Action *next_action(FileActions *actions)
{
    if (actions->count == actions->capacity) {
        int capacity = actions->capacity == 0 ? 4 : actions->capacity * 2;
        Action *grown = (Action *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)capacity * sizeof(Action));
        if (grown == NULL) {
            return NULL;
        }
        if (actions->items != NULL) {
            memcpy(grown, actions->items, (size_t)actions->count * sizeof(Action));
            HeapFree(GetProcessHeap(), 0, actions->items);
        }
        actions->items = grown;
        actions->capacity = capacity;
    }
    return &actions->items[actions->count++];
}

LIBC_FN int sysv_posix_spawn_file_actions_adddup2(FileActions *actions, int fd, int target)
{
    Action *action = next_action(actions);
    if (action == NULL) {
        return LINUX_ENOMEM;
    }
    *action = (Action){.kind = ACTION_DUP2, .fd = target, .other = fd};
    return 0;
}

LIBC_FN int sysv_posix_spawn_file_actions_addclose(FileActions *actions, int fd)
{
    Action *action = next_action(actions);
    if (action == NULL) {
        return LINUX_ENOMEM;
    }
    *action = (Action){.kind = ACTION_CLOSE, .fd = fd};
    return 0;
}

LIBC_FN int sysv_posix_spawn_file_actions_addopen(FileActions *actions, int fd, const char *path, int flags, unsigned mode)
{
    Action *action = next_action(actions);
    size_t length = strlen(path) + 1;
    if (action == NULL) {
        return LINUX_ENOMEM;
    }
    *action = (Action){.kind = ACTION_OPEN, .fd = fd, .other = flags, .mode = (int)mode,
                       .path = (char *)sysv_malloc(length)};
    if (action->path == NULL) {
        actions->count--;
        return LINUX_ENOMEM;
    }
    memcpy(action->path, path, length);
    return 0;
}

LIBC_FN int sysv_posix_spawnattr_init(void *attributes)
{
    memset(attributes, 0, 336);
    return 0;
}

LIBC_FN int sysv_posix_spawnattr_destroy(void *attributes)
{
    (void)attributes;
    return 0;
}

/*
 * Applies the file actions that touch descriptors 0 to 2 to a copy of the child's stream list. Opening a
 * file for the child needs a descriptor in this process for a moment; it is closed again afterwards.
 */
static int apply_actions(const FileActions *actions, ChildStreams *streams, int opened[3])
{
    int *slots[3] = {&streams->input, &streams->output, &streams->error};
    for (int i = 0; actions != NULL && i < actions->count; i++) {
        const Action *action = &actions->items[i];
        if (action->fd < 0 || action->fd > 2) {
            continue;
        }
        if (action->kind == ACTION_CLOSE) {
            *slots[action->fd] = -1;
        } else if (action->kind == ACTION_DUP2) {
            *slots[action->fd] = action->other;
        } else {
            int fd = shim_fd_open(action->path, action->other, action->mode);
            if (fd < 0) {
                return -1;
            }
            opened[action->fd] = fd;
            *slots[action->fd] = fd;
        }
    }
    return 0;
}

static wchar_t *resolve_program(const char *path, int is_search)
{
    wchar_t wide[SHIM_PATH_MAX];
    wchar_t found[SHIM_PATH_MAX];
    if (shim_path_to_wide(path, wide, SHIM_PATH_MAX) != 0) {
        return NULL;
    }
    int has_directory = strchr(path, '/') != NULL;
    DWORD length = (has_directory && !is_search) || has_directory
                       ? GetFullPathNameW(wide, SHIM_PATH_MAX, found, NULL)
                       : SearchPathW(NULL, wide, L".exe", SHIM_PATH_MAX, found, NULL);
    if (length == 0 || length >= SHIM_PATH_MAX) {
        shim_set_errno(LINUX_ENOENT);
        return NULL;
    }
    if (GetFileAttributesW(found) == INVALID_FILE_ATTRIBUTES) {
        size_t end = wcslen(found);
        if (end + 4 >= SHIM_PATH_MAX || wcsrchr(found, L'.') > wcsrchr(found, L'\\')) {
            shim_set_errno(LINUX_ENOENT);
            return NULL;
        }
        wcscpy(found + end, L".exe");
        if (GetFileAttributesW(found) == INVALID_FILE_ATTRIBUTES) {
            shim_set_errno(LINUX_ENOENT);
            return NULL;
        }
    }
    wchar_t *copy = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (wcslen(found) + 1) * sizeof(wchar_t));
    if (copy != NULL) {
        wcscpy(copy, found);
    }
    return copy;
}

static int spawn(int *pid, const char *path, const FileActions *actions, char *const argv[], char *const envp[], int is_search)
{
    ChildStreams streams = CHILD_STREAMS_INHERIT;
    int opened[3] = {-1, -1, -1};
    wchar_t *program = resolve_program(path, is_search);
    wchar_t *line = program != NULL ? shim_command_line(argv) : NULL;
    wchar_t *environment = envp != NULL ? shim_environment_block(envp) : NULL;
    int started = -1;
    if (line != NULL && apply_actions(actions, &streams, opened) == 0) {
        started = shim_process_start(program, line, environment, &streams);
    }
    for (int i = 0; i < 3; i++) {
        if (opened[i] >= 0) {
            shim_fd_close(opened[i]);
        }
    }
    HeapFree(GetProcessHeap(), 0, program);
    HeapFree(GetProcessHeap(), 0, line);
    HeapFree(GetProcessHeap(), 0, environment);
    if (started < 0) {
        return *shim_errno_ptr();
    }
    if (pid != NULL) {
        *pid = started;
    }
    return 0;
}

LIBC_FN int sysv_posix_spawn(int *pid, const char *path, const FileActions *actions, const void *attributes,
                             char *const argv[], char *const envp[])
{
    (void)attributes;
    return spawn(pid, path, actions, argv, envp, 0);
}

LIBC_FN int sysv_posix_spawnp(int *pid, const char *file, const FileActions *actions, const void *attributes,
                              char *const argv[], char *const envp[])
{
    (void)attributes;
    return spawn(pid, file, actions, argv, envp, 1);
}

/* exec replaces the process image; here the new program runs as a child and this process then ends with its status. */
static SYSV int run_instead(const char *path, char *const argv[], char *const envp[], int is_search)
{
    int pid = 0;
    int status = 0;
    int error = spawn(&pid, path, NULL, argv, envp, is_search);
    if (error != 0) {
        return shim_fail(error);
    }
    shim_process_wait(pid, &status, 0);
    sysv__exit((status >> 8) & 0xff);
}

LIBC_FN int sysv_execv(const char *path, char *const argv[])
{
    return run_instead(path, argv, NULL, 0);
}

LIBC_FN int sysv_execve(const char *path, char *const argv[], char *const envp[])
{
    return run_instead(path, argv, envp, 0);
}

LIBC_FN int sysv_execvp(const char *file, char *const argv[])
{
    return run_instead(file, argv, NULL, 1);
}

LIBC_FN int sysv_execvpe(const char *file, char *const argv[], char *const envp[])
{
    return run_instead(file, argv, envp, 1);
}
