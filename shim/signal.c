/*
 * File: shim/signal.c
 * Purpose: Signal handler registration and raise; no asynchronous delivery except what raise and abort do
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Handlers are recorded so programs that install them (Lua and sqlite do for SIGINT) behave, and raise()
 * calls them. Asynchronous signals such as Ctrl-C are not delivered yet.
 */

#include "libc_abi.h"

#define SIGNAL_COUNT 65
#define SIGNAL_ABORT 6
#define SIG_DEFAULT ((uintptr_t)0)
#define SIG_IGNORED ((uintptr_t)1)
#define SIG_ERROR ((uintptr_t)-1)
#define SIGSET_BYTES 128
#define ABORT_EXIT_CODE 134

typedef void (SYSV *Handler)(int);

/* Linux struct sigaction on x86-64: handler, 128-byte mask, flags, restorer. */
typedef struct {
    uintptr_t handler;
    uint8_t mask[SIGSET_BYTES];
    int flags;
    void *restorer;
} Sigaction;

static uintptr_t g_handlers[SIGNAL_COUNT];

static int is_valid(int signal_number)
{
    return signal_number > 0 && signal_number < SIGNAL_COUNT && signal_number != 9 && signal_number != 19;
}

LIBC_FN uintptr_t sysv_signal(int signal_number, uintptr_t handler)
{
    if (!is_valid(signal_number)) {
        shim_set_errno(LINUX_EINVAL);
        return SIG_ERROR;
    }
    uintptr_t previous = g_handlers[signal_number];
    g_handlers[signal_number] = handler;
    return previous;
}

LIBC_FN int sysv_sigaction(int signal_number, const Sigaction *action, Sigaction *previous)
{
    if (!is_valid(signal_number)) {
        return shim_fail(LINUX_EINVAL);
    }
    if (previous != NULL) {
        ZeroMemory(previous, sizeof(*previous));
        previous->handler = g_handlers[signal_number];
    }
    if (action != NULL) {
        g_handlers[signal_number] = action->handler;
    }
    return 0;
}

LIBC_FN int sysv_sigemptyset(void *set)
{
    ZeroMemory(set, SIGSET_BYTES);
    return 0;
}

LIBC_FN int sysv_sigfillset(void *set)
{
    FillMemory(set, SIGSET_BYTES, 0xFF);
    return 0;
}

LIBC_FN int sysv_sigaddset(void *set, int signal_number)
{
    if (!is_valid(signal_number)) {
        return shim_fail(LINUX_EINVAL);
    }
    ((uint8_t *)set)[(signal_number - 1) / 8] |= (uint8_t)(1u << ((signal_number - 1) % 8));
    return 0;
}

LIBC_FN int sysv_sigprocmask(int how, const void *set, void *old)
{
    (void)how;
    (void)set;
    if (old != NULL) {
        ZeroMemory(old, SIGSET_BYTES);
    }
    return 0;
}

/* Runs the registered handler, ignores the signal, or applies the default (abort kills, the rest are ignored). */
LIBC_FN int sysv_raise(int signal_number)
{
    if (!is_valid(signal_number)) {
        return shim_fail(LINUX_EINVAL);
    }
    uintptr_t handler = g_handlers[signal_number];
    if (handler == SIG_IGNORED) {
        return 0;
    }
    if (handler != SIG_DEFAULT) {
        ((Handler)handler)(signal_number);
        return 0;
    }
    if (signal_number == SIGNAL_ABORT) {
        ExitProcess(ABORT_EXIT_CODE);
    }
    return 0;
}
