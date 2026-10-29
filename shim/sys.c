/*
 * File: shim/sys.c
 * Purpose: Process identity, system configuration, resource usage, anonymous memory mappings and unsupported process spawning
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * There is no Unix user database, so the process runs as one fixed unprivileged user. Mappings of files
 * are not supported yet (callers fall back to reading); anonymous mappings use VirtualAlloc. popen and
 * system fail with ENOSYS rather than guessing how a Linux shell command line maps to Windows.
 */

#include <string.h>

#include "libc_abi.h"

#define FIXED_UID 1000u
#define FIXED_GID 1000u
#define PAGE_BYTES 4096
#define OPEN_MAX_FALLBACK 1024
#define CLOCK_TICKS_PER_SECOND 100
#define MICROSECONDS_PER_SECOND 1000000ull
#define FILETIME_UNITS_PER_MICROSECOND 10ull
#define RUSAGE_BYTES 144

enum { SC_CLK_TCK = 2, SC_OPEN_MAX = 4, SC_PAGESIZE = 30, SC_NPROCESSORS_CONF = 83, SC_NPROCESSORS_ONLN = 84 };
enum { MAP_ANONYMOUS = 0x20, PROT_WRITE = 2, PROT_EXEC = 4 };
#define MAP_FAILED_VALUE ((void *)(intptr_t)-1)

/* Linux struct passwd on x86-64. */
typedef struct {
    char *name;
    char *password;
    uint32_t uid;
    uint32_t gid;
    char *gecos;
    char *home;
    char *shell;
} Passwd;

static char g_user_name[] = "user";
static char g_empty[] = "";
static char g_home[] = "/";
static char g_shell[] = "/bin/sh";
static Passwd g_passwd = {g_user_name, g_empty, FIXED_UID, FIXED_GID, g_user_name, g_home, g_shell};

LIBC_FN int sysv_getpid(void)
{
    return (int)GetCurrentProcessId();
}

LIBC_FN int sysv_getppid(void)
{
    return 1;
}

LIBC_FN unsigned sysv_getuid(void)
{
    return FIXED_UID;
}

LIBC_FN unsigned sysv_geteuid(void)
{
    return FIXED_UID;
}

LIBC_FN unsigned sysv_getgid(void)
{
    return FIXED_GID;
}

LIBC_FN unsigned sysv_getegid(void)
{
    return FIXED_GID;
}

LIBC_FN Passwd *sysv_getpwuid(unsigned uid)
{
    return uid == FIXED_UID ? &g_passwd : NULL;
}

LIBC_FN Passwd *sysv_getpwnam(const char *name)
{
    return strcmp(name, g_user_name) == 0 ? &g_passwd : NULL;
}

LIBC_FN c_long sysv_sysconf(int name)
{
    SYSTEM_INFO info;
    switch (name) {
    case SC_PAGESIZE:
        return PAGE_BYTES;
    case SC_CLK_TCK:
        return CLOCK_TICKS_PER_SECOND;
    case SC_OPEN_MAX:
        return OPEN_MAX_FALLBACK;
    case SC_NPROCESSORS_CONF:
    case SC_NPROCESSORS_ONLN:
        GetSystemInfo(&info);
        return (c_long)info.dwNumberOfProcessors;
    default:
        return shim_fail(LINUX_EINVAL);
    }
}

static void write_timeval(uint64_t *out, FILETIME time)
{
    uint64_t units = ((uint64_t)time.dwHighDateTime << 32) | time.dwLowDateTime;
    uint64_t microseconds = units / FILETIME_UNITS_PER_MICROSECOND;
    out[0] = microseconds / MICROSECONDS_PER_SECOND;
    out[1] = microseconds % MICROSECONDS_PER_SECOND;
}

/* Fills user and system CPU time; the rest of struct rusage reads as zero. */
LIBC_FN int sysv_getrusage(int who, void *usage)
{
    (void)who;
    FILETIME created, exited, kernel, user;
    ZeroMemory(usage, RUSAGE_BYTES);
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return shim_fail_from_win();
    }
    uint64_t *fields = usage;
    write_timeval(fields, user);
    write_timeval(fields + 2, kernel);
    return 0;
}

static DWORD windows_protection(int protection)
{
    if (protection & PROT_EXEC) {
        return (protection & PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    }
    if (protection & PROT_WRITE) {
        return PAGE_READWRITE;
    }
    return protection == 0 ? PAGE_NOACCESS : PAGE_READONLY;
}

LIBC_FN void *sysv_mmap64(void *address, size_t length, int protection, int flags, int fd, int64_t offset)
{
    (void)address;
    (void)offset;
    if (length == 0) {
        shim_set_errno(LINUX_EINVAL);
        return MAP_FAILED_VALUE;
    }
    if (!(flags & MAP_ANONYMOUS) || fd != -1) {
        shim_set_errno(LINUX_ENODEV);
        return MAP_FAILED_VALUE;
    }
    void *region = VirtualAlloc(NULL, length, MEM_RESERVE | MEM_COMMIT, windows_protection(protection));
    if (region == NULL) {
        shim_set_errno(LINUX_ENOMEM);
        return MAP_FAILED_VALUE;
    }
    return region;
}

LIBC_FN int sysv_munmap(void *address, size_t length)
{
    (void)length;
    return VirtualFree(address, 0, MEM_RELEASE) ? 0 : shim_fail(LINUX_EINVAL);
}

LIBC_FN void *sysv_mremap(void *address, size_t old_length, size_t new_length, int flags)
{
    (void)address;
    (void)old_length;
    (void)new_length;
    (void)flags;
    shim_set_errno(LINUX_ENOMEM);
    return MAP_FAILED_VALUE;
}

LIBC_FN int sysv_system(const char *command)
{
    if (command == NULL) {
        return 0;
    }
    return shim_fail(LINUX_ENOSYS);
}

LIBC_FN void *sysv_popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    shim_set_errno(LINUX_ENOSYS);
    return NULL;
}

LIBC_FN int sysv_pclose(void *stream)
{
    (void)stream;
    return shim_fail(LINUX_ECHILD);
}
