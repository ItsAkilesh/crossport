/*
 * File: shim/exit.c
 * Purpose: Process termination: exit, atexit handlers, abort and assertion failure, in glibc's order
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "libc_abi.h"
#include "stdio_internal.h"

#define MAX_EXIT_HANDLERS 256
#define ABORT_EXIT_CODE 134 /* 128 + SIGABRT, as a shell reports it */

typedef void (SYSV *ExitHandler)(void *);

typedef struct {
    ExitHandler handler;
    void *argument;
} ExitEntry;

static ExitEntry g_handlers[MAX_EXIT_HANDLERS];
static size_t g_handler_count;
static SRWLOCK g_handlers_lock = SRWLOCK_INIT;

/* Implemented in crossport_libc.c. Flushing stdio between the two is the caller's job. */
void shim_run_destructors(void);
void shim_run_thread_destructors(void);
NORETURN void shim_finish(int status);

LIBC_FN int sysv___cxa_atexit(ExitHandler handler, void *argument, void *dso)
{
    (void)dso;
    int result = -1;
    AcquireSRWLockExclusive(&g_handlers_lock);
    if (g_handler_count < MAX_EXIT_HANDLERS) {
        g_handlers[g_handler_count].handler = handler;
        g_handlers[g_handler_count].argument = argument;
        g_handler_count++;
        result = 0;
    }
    ReleaseSRWLockExclusive(&g_handlers_lock);
    return result;
}

LIBC_FN int sysv_atexit(void (SYSV *handler)(void))
{
    return sysv___cxa_atexit((ExitHandler)handler, NULL, NULL);
}

/* Handlers run last-registered first; one that registers another sees it run too. */
NORETURN void shim_exit(int status)
{
    shim_run_thread_destructors();
    for (;;) {
        ExitEntry entry = {NULL, NULL};
        AcquireSRWLockExclusive(&g_handlers_lock);
        if (g_handler_count > 0) {
            entry = g_handlers[--g_handler_count];
        }
        ReleaseSRWLockExclusive(&g_handlers_lock);
        if (entry.handler == NULL) {
            break;
        }
        entry.handler(entry.argument);
    }
    shim_run_destructors();
    stdio_flush_all();
    shim_finish(status);
}

LIBC_FN NORETURN void sysv_exit(int status)
{
    shim_exit(status);
}

/* _exit and _Exit skip handlers and flushing. */
LIBC_FN NORETURN void sysv__exit(int status)
{
    ExitProcess((UINT)status);
    __builtin_unreachable();
}

LIBC_FN NORETURN void sysv__Exit(int status)
{
    sysv__exit(status);
}

LIBC_FN NORETURN void sysv_abort(void)
{
    ExitProcess(ABORT_EXIT_CODE);
    __builtin_unreachable();
}

/* The short program name: the executable's file name without a trailing ".exe". */
static const char *program_name(char *buffer, size_t size)
{
    DWORD length = GetModuleFileNameA(NULL, buffer, (DWORD)size);
    char *name = buffer;
    for (DWORD i = 0; i < length; i++) {
        if (buffer[i] == '\\' || buffer[i] == '/') {
            name = buffer + i + 1;
        }
    }
    DWORD name_length = (DWORD)lstrlenA(name);
    if (name_length > 4 && lstrcmpiA(name + name_length - 4, ".exe") == 0) {
        name[name_length - 4] = '\0';
    }
    return name;
}

/* glibc's message: "prog: file:line: function: Assertion `expression' failed." */
LIBC_FN NORETURN void sysv___assert_fail(const char *expression, const char *file, unsigned line, const char *function)
{
    char path[MAX_PATH];
    char message[1024];
    const char *name = program_name(path, sizeof(path));
    int length = wsprintfA(message, "%s: %s:%u: %s%sAssertion `%s' failed.\n", name, file, line,
                           function != NULL ? function : "", function != NULL ? ": " : "", expression);
    stdio_flush_all();
    shim_write_all(GetStdHandle(STD_ERROR_HANDLE), message, (size_t)(length > 0 ? length : 0));
    sysv_abort();
}
