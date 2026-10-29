/*
 * File: shim/sys.c
 * Purpose: Process identity, system configuration and resource usage
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * There is no Unix user database, so the process runs as one fixed unprivileged user.
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
