/*
 * File: shim/users.c
 * Purpose: The reentrant user and group lookups, groups, uname, umask and the other identity calls (one fixed user)
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define FIXED_UID 1000u
#define FIXED_GID 1000u
#define UTS_FIELD 65
#define DEFAULT_UMASK 022

/* struct passwd and struct group on x86-64. */
typedef struct {
    char *name;
    char *password;
    uint32_t uid;
    uint32_t gid;
    char *gecos;
    char *home;
    char *shell;
} Passwd;

typedef struct {
    char *name;
    char *password;
    uint32_t gid;
    char **members;
} Group;

/* struct utsname with glibc's 65-byte fields. */
typedef struct {
    char sysname[UTS_FIELD];
    char nodename[UTS_FIELD];
    char release[UTS_FIELD];
    char version[UTS_FIELD];
    char machine[UTS_FIELD];
    char domainname[UTS_FIELD];
} UtsName;

LIBC_FN char *sysv_getenv(const char *name);

static const char USER_NAME[] = "user";
static const char HOME_FALLBACK[] = "/";
static const char SHELL_PATH[] = "/bin/sh";
static unsigned g_umask = DEFAULT_UMASK;

/* The user's home directory in Linux path form: HOME if set, else "/". */
static const char *home_directory(void)
{
    const char *home = sysv_getenv("HOME");
    return home != NULL && home[0] != '\0' ? home : HOME_FALLBACK;
}

/* Lays the strings of a passwd entry out in the caller's buffer; ERANGE when it is too small. */
static int fill_passwd(Passwd *entry, char *buffer, size_t size, Passwd **result)
{
    const char *home = home_directory();
    size_t name_bytes = sizeof(USER_NAME);
    size_t home_bytes = strlen(home) + 1;
    size_t shell_bytes = sizeof(SHELL_PATH);
    *result = NULL;
    if (name_bytes * 2 + 1 + home_bytes + shell_bytes > size) {
        return LINUX_ERANGE;
    }
    char *name = buffer;
    char *empty = name + name_bytes;
    char *gecos = empty + 1;
    char *home_copy = gecos + name_bytes;
    char *shell = home_copy + home_bytes;
    memcpy(name, USER_NAME, name_bytes);
    empty[0] = '\0';
    memcpy(gecos, USER_NAME, name_bytes);
    memcpy(home_copy, home, home_bytes);
    memcpy(shell, SHELL_PATH, shell_bytes);
    *entry = (Passwd){.name = name, .password = empty, .uid = FIXED_UID, .gid = FIXED_GID, .gecos = gecos,
                      .home = home_copy, .shell = shell};
    *result = entry;
    return 0;
}

LIBC_FN int sysv_getpwuid_r(unsigned uid, Passwd *entry, char *buffer, size_t size, Passwd **result)
{
    if (uid != FIXED_UID) {
        *result = NULL;
        return 0;
    }
    return fill_passwd(entry, buffer, size, result);
}

LIBC_FN int sysv_getpwnam_r(const char *name, Passwd *entry, char *buffer, size_t size, Passwd **result)
{
    if (strcmp(name, USER_NAME) != 0) {
        *result = NULL;
        return 0;
    }
    return fill_passwd(entry, buffer, size, result);
}

LIBC_FN int sysv_getpwent_r(Passwd *entry, char *buffer, size_t size, Passwd **result)
{
    (void)entry;
    (void)buffer;
    (void)size;
    *result = NULL;
    return LINUX_ENOENT;
}

LIBC_FN void sysv_setpwent(void)
{
}

static int fill_group(Group *entry, char *buffer, size_t size, Group **result)
{
    size_t name_bytes = sizeof(USER_NAME);
    *result = NULL;
    if (name_bytes + 1 + 2 * sizeof(char *) > size) {
        return LINUX_ERANGE;
    }
    char *name = buffer;
    char *empty = name + name_bytes;
    char **members = (char **)(((uintptr_t)(empty + 1) + sizeof(char *) - 1) & ~(uintptr_t)(sizeof(char *) - 1));
    if ((char *)(members + 1) > buffer + size) {
        return LINUX_ERANGE;
    }
    memcpy(name, USER_NAME, name_bytes);
    empty[0] = '\0';
    members[0] = NULL;
    *entry = (Group){.name = name, .password = empty, .gid = FIXED_GID, .members = members};
    *result = entry;
    return 0;
}

LIBC_FN int sysv_getgrgid_r(unsigned gid, Group *entry, char *buffer, size_t size, Group **result)
{
    if (gid != FIXED_GID) {
        *result = NULL;
        return 0;
    }
    return fill_group(entry, buffer, size, result);
}

LIBC_FN int sysv_getgrnam_r(const char *name, Group *entry, char *buffer, size_t size, Group **result)
{
    if (strcmp(name, USER_NAME) != 0) {
        *result = NULL;
        return 0;
    }
    return fill_group(entry, buffer, size, result);
}

LIBC_FN int sysv_getgroups(int size, unsigned *list)
{
    if (size > 0) {
        list[0] = FIXED_GID;
    }
    return 1;
}

LIBC_FN int sysv___getgroups_chk(int size, unsigned *list, size_t capacity)
{
    if ((size_t)size * sizeof(unsigned) > capacity) {
        return -1;
    }
    return sysv_getgroups(size, list);
}

LIBC_FN int sysv_getgrouplist(const char *user, unsigned group, unsigned *groups, int *count)
{
    (void)user;
    int wanted = *count;
    *count = 1;
    if (wanted < 1) {
        return -1;
    }
    groups[0] = group;
    return 1;
}

LIBC_FN int sysv_getresuid(unsigned *real, unsigned *effective, unsigned *saved)
{
    *real = *effective = *saved = FIXED_UID;
    return 0;
}

LIBC_FN int sysv_getresgid(unsigned *real, unsigned *effective, unsigned *saved)
{
    *real = *effective = *saved = FIXED_GID;
    return 0;
}

/* The process cannot change identity: asking for the one it has succeeds, anything else is refused. */
LIBC_FN int sysv_setuid(unsigned uid)
{
    return uid == FIXED_UID ? 0 : shim_fail(LINUX_EPERM);
}

LIBC_FN int sysv_setresuid(unsigned real, unsigned effective, unsigned saved)
{
    int is_allowed = (real == FIXED_UID || real == (unsigned)-1) && (effective == FIXED_UID || effective == (unsigned)-1) &&
                     (saved == FIXED_UID || saved == (unsigned)-1);
    return is_allowed ? 0 : shim_fail(LINUX_EPERM);
}

LIBC_FN int sysv_setresgid(unsigned real, unsigned effective, unsigned saved)
{
    int is_allowed = (real == FIXED_GID || real == (unsigned)-1) && (effective == FIXED_GID || effective == (unsigned)-1) &&
                     (saved == FIXED_GID || saved == (unsigned)-1);
    return is_allowed ? 0 : shim_fail(LINUX_EPERM);
}

LIBC_FN int sysv_setsid(void)
{
    return (int)GetCurrentProcessId();
}

LIBC_FN unsigned sysv_umask(unsigned mask)
{
    unsigned previous = g_umask;
    g_umask = mask & 0777;
    return previous;
}

LIBC_FN int sysv_uname(UtsName *out)
{
    memset(out, 0, sizeof(*out));
    strcpy(out->sysname, "Linux");
    strcpy(out->release, "6.8.0-crossport");
    strcpy(out->version, "#1 SMP Crossport");
    strcpy(out->machine, "x86_64");
    strcpy(out->domainname, "(none)");
    DWORD size = UTS_FIELD;
    if (!GetComputerNameExA(ComputerNameDnsHostname, out->nodename, &size)) {
        strcpy(out->nodename, "localhost");
    }
    return 0;
}
