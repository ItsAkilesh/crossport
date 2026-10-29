/*
 * File: shim/process_posix.c
 * Purpose: waitpid, kill, system, popen and pclose; fork is not supported
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

/* fork would have to copy the whole address space; programs that need it must use the spawn family. */
LIBC_FN int sysv_fork(void)
{
    return shim_fail(LINUX_ENOSYS);
}

LIBC_FN int sysv_vfork(void)
{
    return shim_fail(LINUX_ENOSYS);
}
