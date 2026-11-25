/*
 * File: shim/libc_compat.c
 * Purpose: Older and alternate names for C library calls, fortified forms, and small functions found missing by the application sweep:
 *          creat, euidaccess, fpathconf, mincore, sigaltstack, sigwait, setpgid, the __xstat family, _IO_getc and _IO_putc, bcmp,
 *          gnu_get_libc_version, thread stack queries, lutimes, glob_pattern_p, getpass, innetgr, fallocate and the _chk forms of pread,
 *          wcrtomb, wcstombs and wctomb
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Each one is what glibc does, or the honest answer when Windows has no equivalent (fallocate, getpass, innetgr).
 */

#include <string.h>

#include "fd.h"
#include "libc_abi.h"
#include "stdio_internal.h"

#define PAGE_SIZE_BYTES 4096
#define O_WRONLY_FLAG 01
#define O_CREAT_FLAG 0100
#define O_TRUNC_FLAG 01000
#define GUARD_SIZE_BYTES 4096
#define SIGNAL_STACK_DISABLED 2
#define LIBC_VERSION "2.39"
#define LIBC_RELEASE "stable"

/* Declared here because the header hides it below Windows 8; kernel32 has exported it since then. */
VOID WINAPI GetCurrentThreadStackLimits(PULONG_PTR low_limit, PULONG_PTR high_limit);

LIBC_FN size_t sysv_mbrtowc(int32_t *out, const char *text, size_t count, void *state);
LIBC_FN uintptr_t sysv_signal(int signal_number, uintptr_t handler);
LIBC_FN int sysv_eaccess(const char *path, int mode);
LIBC_FN int sysv_utimes(const char *path, const int64_t times[4]);
LIBC_FN int sysv_stat(const char *path, void *buffer);
LIBC_FN int sysv_lstat(const char *path, void *buffer);
LIBC_FN int sysv_fstat(int fd, void *buffer);
LIBC_FN int64_t sysv_pathconf(const char *path, int name);
LIBC_FN int64_t sysv_pread(int fd, void *buffer, size_t count, int64_t offset);
LIBC_FN size_t sysv_wcrtomb(char *out, int32_t wide, void *state);
LIBC_FN size_t sysv_wcstombs(char *out, const int32_t *wide, size_t count);
LIBC_FN int sysv_wctomb(char *out, int32_t wide);
LIBC_FN int sysv_memcmp(const void *left, const void *right, size_t count);
LIBC_FN int sysv_getc(ShimFile *file);
LIBC_FN int sysv_putc(int byte, ShimFile *file);
LIBC_FN NORETURN void sysv___chk_fail(void);

LIBC_FN size_t sysv___mbrlen(const char *text, size_t count, void *state)
{
    return sysv_mbrtowc(NULL, text, count, state);
}

LIBC_FN int sysv_tcflow(int fd, int action)
{
    (void)fd;
    (void)action;
    return 0;
}

LIBC_FN uintptr_t sysv___sysv_signal(int signal_number, uintptr_t handler)
{
    return sysv_signal(signal_number, handler);
}

LIBC_FN char **sysv_backtrace_symbols(void *const *frames, int count)
{
    (void)frames;
    (void)count;
    shim_set_errno(LINUX_ENOMEM);
    return NULL;
}

LIBC_FN int sysv_creat(const char *path, unsigned mode)
{
    return shim_fd_open(path, O_WRONLY_FLAG | O_CREAT_FLAG | O_TRUNC_FLAG, mode);
}

LIBC_FN int sysv_euidaccess(const char *path, int mode)
{
    return sysv_eaccess(path, mode);
}

LIBC_FN int64_t sysv_fpathconf(int fd, int name)
{
    (void)fd;
    return sysv_pathconf("/", name);
}

/* mincore(2): fails with ENOMEM when any page of the range is not mapped (programs probe the end of a mapping this way) and otherwise reports
 * every page resident. */
LIBC_FN int sysv_mincore(void *address, size_t length, uint8_t *vector)
{
    if (((uintptr_t)address & (PAGE_SIZE_BYTES - 1)) != 0) {
        return shim_fail(LINUX_EINVAL);
    }
    uintptr_t at = (uintptr_t)address;
    uintptr_t end = at + length;
    while (at < end) {
        MEMORY_BASIC_INFORMATION region;
        if (VirtualQuery((void *)at, &region, sizeof(region)) == 0 || region.State != MEM_COMMIT) {
            return shim_fail(LINUX_ENOMEM);
        }
        at = (uintptr_t)region.BaseAddress + region.RegionSize;
    }
    memset(vector, 1, (length + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES);
    return 0;
}

/* struct stack_t: pointer, flags, size. No alternate signal stack exists; asking to set one is accepted. */
typedef struct {
    void *stack_pointer;
    int flags;
    size_t size;
} SignalStack;

LIBC_FN int sysv_sigaltstack(const SignalStack *wanted, SignalStack *previous)
{
    (void)wanted;
    if (previous != NULL) {
        memset(previous, 0, sizeof(*previous));
        previous->flags = SIGNAL_STACK_DISABLED;
    }
    return 0;
}

/* sigwait(3) would block until a signal the shim never delivers asynchronously. */
LIBC_FN int sysv_sigwait(const void *set, int *signal_number)
{
    (void)set;
    (void)signal_number;
    return LINUX_EINVAL;
}

LIBC_FN int sysv_setpgid(int pid, int group)
{
    (void)pid;
    (void)group;
    return 0;
}

LIBC_FN int sysv_posix_spawnattr_setpgroup(void *attributes, int group)
{
    (void)attributes;
    (void)group;
    return 0;
}

LIBC_FN int sysv___xstat(int version, const char *path, void *buffer)
{
    (void)version;
    return sysv_stat(path, buffer);
}

LIBC_FN int sysv___lxstat(int version, const char *path, void *buffer)
{
    (void)version;
    return sysv_lstat(path, buffer);
}

LIBC_FN int sysv___fxstat(int version, int fd, void *buffer)
{
    (void)version;
    return sysv_fstat(fd, buffer);
}

LIBC_FN int sysv__IO_getc(ShimFile *file)
{
    return sysv_getc(file);
}

LIBC_FN int sysv__IO_putc(int byte, ShimFile *file)
{
    return sysv_putc(byte, file);
}

LIBC_FN int sysv_bcmp(const void *left, const void *right, size_t count)
{
    return sysv_memcmp(left, right, count);
}

LIBC_FN const char *sysv_gnu_get_libc_version(void)
{
    return LIBC_VERSION;
}

LIBC_FN const char *sysv_gnu_get_libc_release(void)
{
    return LIBC_RELEASE;
}

/* The calling thread's stack, from the limits Windows records for it. pthread_attr_t is opaque to programs; the first 24 bytes are used here. */
typedef struct {
    void *stack_low;
    size_t stack_size;
    size_t guard_size;
} StackAttributes;

LIBC_FN int sysv_pthread_getattr_np(uint64_t thread, StackAttributes *attributes)
{
    ULONG_PTR low;
    ULONG_PTR high;
    (void)thread;
    GetCurrentThreadStackLimits(&low, &high);
    attributes->stack_low = (void *)low;
    attributes->stack_size = (size_t)(high - low);
    attributes->guard_size = GUARD_SIZE_BYTES;
    return 0;
}

LIBC_FN int sysv_pthread_attr_getstack(const StackAttributes *attributes, void **address, size_t *size)
{
    *address = attributes->stack_low;
    *size = attributes->stack_size;
    return 0;
}

LIBC_FN int sysv_pthread_attr_getguardsize(const StackAttributes *attributes, size_t *size)
{
    (void)attributes;
    *size = GUARD_SIZE_BYTES;
    return 0;
}

LIBC_FN int sysv_lutimes(const char *path, const int64_t times[4])
{
    return sysv_utimes(path, times);
}

/* glob_pattern_p(3): does the pattern contain a wildcard? A backslash quotes the next character when `quote` is non-zero. */
LIBC_FN int sysv_glob_pattern_p(const char *pattern, int quote)
{
    for (const char *at = pattern; *at != '\0'; at++) {
        if (*at == '*' || *at == '?' || *at == '[') {
            return 1;
        }
        at += (quote && *at == '\\' && at[1] != '\0') ? 1 : 0;
    }
    return 0;
}

LIBC_FN char *sysv_getpass(const char *prompt)
{
    (void)prompt;
    shim_set_errno(LINUX_ENOTTY);
    return NULL;
}

LIBC_FN int sysv_innetgr(const char *group, const char *host, const char *user, const char *domain)
{
    (void)group;
    (void)host;
    (void)user;
    (void)domain;
    return 0;
}

LIBC_FN int sysv_fallocate(int fd, int mode, int64_t offset, int64_t length)
{
    (void)fd;
    (void)mode;
    (void)offset;
    (void)length;
    return shim_fail(LINUX_ENOSYS);
}

LIBC_FN int64_t sysv___pread_chk(int fd, void *buffer, size_t count, int64_t offset, size_t buffer_size)
{
    if (count > buffer_size) {
        sysv___chk_fail();
    }
    return sysv_pread(fd, buffer, count, offset);
}

LIBC_FN size_t sysv___wcrtomb_chk(char *out, int32_t wide, void *state, size_t buffer_size)
{
    (void)buffer_size;
    return sysv_wcrtomb(out, wide, state);
}

LIBC_FN size_t sysv___wcstombs_chk(char *out, const int32_t *wide, size_t count, size_t buffer_size)
{
    if (out != NULL && count > buffer_size) {
        sysv___chk_fail();
    }
    return sysv_wcstombs(out, wide, count);
}

LIBC_FN int sysv___wctomb_chk(char *out, int32_t wide, size_t buffer_size)
{
    (void)buffer_size;
    return sysv_wctomb(out, wide);
}
