/*
 * File: shim/process_posix.c
 * Purpose: posix_spawn, system, popen, exec*, waitpid and kill on top of the Windows process layer; fork is not supported
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "process.h"

#define SPAWN_WNOHANG 1
#define MAX_POPEN 64
#define SIGNAL_RAISE_LIMIT 64

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

static Popen g_popen[MAX_POPEN];
static SRWLOCK g_popen_lock = SRWLOCK_INIT;

LIBC_FN int sysv_raise(int signal_number);
LIBC_FN int sysv_getpid(void);
LIBC_FN int sysv_pipe2(int fds[2], int flags);
LIBC_FN void *sysv_fdopen(int fd, const char *mode);
LIBC_FN int sysv_fclose(void *stream);
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

LIBC_FN int sysv_waitpid(int pid, int *status, int options)
{
    return shim_process_wait(pid > 0 ? pid : -1, status, (options & SPAWN_WNOHANG) != 0);
}

LIBC_FN int sysv_wait(int *status)
{
    return shim_process_wait(-1, status, 0);
}

LIBC_FN int sysv_wait4(int pid, int *status, int options, void *usage)
{
    (void)usage;
    return sysv_waitpid(pid, status, options);
}

LIBC_FN int sysv_kill(int pid, int signal_number)
{
    if (pid == sysv_getpid()) {
        return signal_number == 0 ? 0 : sysv_raise(signal_number);
    }
    if (signal_number == 0) {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (process == NULL) {
            return shim_fail(LINUX_ESRCH);
        }
        CloseHandle(process);
        return 0;
    }
    return signal_number < 0 || signal_number > SIGNAL_RAISE_LIMIT ? shim_fail(LINUX_EINVAL)
                                                                    : shim_process_terminate(pid, signal_number);
}

/* Runs `cmd.exe /c` for a shell command; the shell dialect differs from sh, but simple commands agree. */
static int start_shell(const char *command, const ChildStreams *streams)
{
    wchar_t *wide = NULL;
    int count = MultiByteToWideChar(CP_UTF8, 0, command, -1, NULL, 0);
    static const wchar_t PREFIX[] = L"cmd.exe /d /s /c \"";
    size_t prefix = sizeof(PREFIX) / sizeof(wchar_t) - 1;
    if (count > 0) {
        wide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (prefix + (size_t)count + 2) * sizeof(wchar_t));
    }
    if (wide == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    wcscpy(wide, PREFIX);
    MultiByteToWideChar(CP_UTF8, 0, command, -1, wide + prefix, count);
    wcscat(wide, L"\"");
    int pid = shim_process_start(NULL, wide, NULL, streams);
    HeapFree(GetProcessHeap(), 0, wide);
    return pid;
}

LIBC_FN int sysv_system(const char *command)
{
    ChildStreams streams = CHILD_STREAMS_INHERIT;
    int status = 0;
    if (command == NULL) {
        return 1;
    }
    int pid = start_shell(command, &streams);
    return pid < 0 || shim_process_wait(pid, &status, 0) < 0 ? -1 : status;
}

static int remember_popen(int fd, int pid, void *stream)
{
    int is_stored = 0;
    AcquireSRWLockExclusive(&g_popen_lock);
    for (size_t i = 0; i < MAX_POPEN && !is_stored; i++) {
        if (g_popen[i].stream == NULL) {
            g_popen[i] = (Popen){.fd = fd, .pid = pid, .stream = stream};
            is_stored = 1;
        }
    }
    ReleaseSRWLockExclusive(&g_popen_lock);
    return is_stored;
}

LIBC_FN void *sysv_popen(const char *command, const char *mode)
{
    int ends[2];
    int is_read = mode != NULL && mode[0] == 'r';
    if (mode == NULL || (mode[0] != 'r' && mode[0] != 'w') || sysv_pipe2(ends, 0) != 0) {
        shim_set_errno(LINUX_EINVAL);
        return NULL;
    }
    int parent_end = is_read ? ends[0] : ends[1];
    int child_end = is_read ? ends[1] : ends[0];
    ChildStreams streams = CHILD_STREAMS_INHERIT;
    *(is_read ? &streams.output : &streams.input) = child_end;
    int pid = start_shell(command, &streams);
    shim_fd_close(child_end);
    void *stream = pid >= 0 ? sysv_fdopen(parent_end, is_read ? "r" : "w") : NULL;
    if (stream == NULL || !remember_popen(parent_end, pid, stream)) {
        shim_fd_close(parent_end);
        return NULL;
    }
    return stream;
}

LIBC_FN int sysv_pclose(void *stream)
{
    int pid = -1;
    AcquireSRWLockExclusive(&g_popen_lock);
    for (size_t i = 0; i < MAX_POPEN && pid < 0; i++) {
        if (g_popen[i].stream == stream) {
            pid = g_popen[i].pid;
            g_popen[i] = (Popen){0};
        }
    }
    ReleaseSRWLockExclusive(&g_popen_lock);
    if (pid < 0) {
        return shim_fail(LINUX_ECHILD);
    }
    int status = 0;
    sysv_fclose(stream);
    return shim_process_wait(pid, &status, 0) < 0 ? -1 : status;
}

/* exec replaces the process image; here the new program runs as a child and this process then ends with its status. */
static int run_instead(const char *path, char *const argv[], char *const envp[], int is_search)
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

/* fork would have to copy the whole address space; programs that need it must use the spawn family. */
LIBC_FN int sysv_fork(void)
{
    return shim_fail(LINUX_ENOSYS);
}

LIBC_FN int sysv_vfork(void)
{
    return shim_fail(LINUX_ENOSYS);
}
