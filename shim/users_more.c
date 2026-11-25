/*
 * File: shim/users_more.c
 * Purpose: The non-reentrant group and password lookups, database iteration, login name, login records, process groups and the
 *          identity calls the shim's one fixed user can answer
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the compatibility survey (getgrgid and getgrnam block 92 and 85 programs of Ubuntu's /usr/bin). The shim has one user and one
 * group, named "user" with id 1000 (users.c), so every database holds exactly one entry; the shadow and login-record databases are empty.
 */

#include <string.h>

#include "libc_abi.h"

#define FIXED_UID 1000u
#define FIXED_GID 1000u
#define LOOKUP_BUFFER_BYTES 1024
#define LOGIN_NAME "user"
#define CBAUD_MASK 0010017u
#define DEFAULT_PRIORITY 0
#define DESCRIPTOR_LIMIT 1024

typedef struct Passwd Passwd;
typedef struct Group Group;

/* x86-64 struct termios: four flag words, the line discipline, 32 control characters and two speeds. */
typedef struct {
    uint32_t input_flags;
    uint32_t output_flags;
    uint32_t control_flags;
    uint32_t local_flags;
    uint8_t line;
    uint8_t control_chars[32];
    uint32_t input_speed;
    uint32_t output_speed;
} Termios;

LIBC_FN int sysv_getpwuid_r(unsigned uid, Passwd *entry, char *buffer, size_t size, Passwd **result);
LIBC_FN int sysv_getgrgid_r(unsigned gid, Group *entry, char *buffer, size_t size, Group **result);
LIBC_FN int sysv_getgrnam_r(const char *name, Group *entry, char *buffer, size_t size, Group **result);
LIBC_FN int sysv_getpid(void);
LIBC_FN int sysv_isatty(int fd);

/* A Passwd and a Group are 48 and 32 bytes; the static storage below is for the non-reentrant calls. */
static uint8_t g_passwd_storage[64];
static uint8_t g_group_storage[64];
static char g_passwd_buffer[LOOKUP_BUFFER_BYTES];
static char g_group_buffer[LOOKUP_BUFFER_BYTES];
static int g_passwd_cursor;
static int g_group_cursor;

LIBC_FN Group *sysv_getgrgid(unsigned gid)
{
    Group *result;
    sysv_getgrgid_r(gid, (Group *)g_group_storage, g_group_buffer, sizeof(g_group_buffer), &result);
    return result;
}

LIBC_FN Group *sysv_getgrnam(const char *name)
{
    Group *result;
    sysv_getgrnam_r(name, (Group *)g_group_storage, g_group_buffer, sizeof(g_group_buffer), &result);
    return result;
}

LIBC_FN Group *sysv_getgrent(void)
{
    Group *entry = g_group_cursor == 0 ? sysv_getgrgid(FIXED_GID) : NULL;
    g_group_cursor++;
    return entry;
}

LIBC_FN void sysv_setgrent(void)
{
    g_group_cursor = 0;
}

LIBC_FN void sysv_endgrent(void)
{
    g_group_cursor = 0;
}

LIBC_FN Group *sysv_fgetgrent(void *stream)
{
    (void)stream;
    return NULL;
}

LIBC_FN Passwd *sysv_getpwent(void)
{
    Passwd *result = NULL;
    if (g_passwd_cursor == 0) {
        sysv_getpwuid_r(FIXED_UID, (Passwd *)g_passwd_storage, g_passwd_buffer, sizeof(g_passwd_buffer), &result);
    }
    g_passwd_cursor++;
    return result;
}

LIBC_FN Passwd *sysv_fgetpwent(void *stream)
{
    (void)stream;
    return NULL;
}

/* The shadow and gshadow databases are not readable by an unprivileged program, and the shim has none. */
LIBC_FN void *sysv_fgetspent(void *stream)
{
    (void)stream;
    return NULL;
}

LIBC_FN void *sysv_fgetsgent(void *stream)
{
    (void)stream;
    return NULL;
}

LIBC_FN int sysv_getspnam_r(const char *name, void *entry, char *buffer, size_t size, void **result)
{
    (void)name;
    (void)entry;
    (void)buffer;
    (void)size;
    *result = NULL;
    return LINUX_ENOENT;
}

LIBC_FN int sysv_getsgnam_r(const char *name, void *entry, char *buffer, size_t size, void **result)
{
    return sysv_getspnam_r(name, entry, buffer, size, result);
}

LIBC_FN int sysv_initgroups(const char *user, unsigned group)
{
    (void)user;
    (void)group;
    return 0;
}

LIBC_FN char *sysv_getlogin(void)
{
    return (char *)LOGIN_NAME;
}

LIBC_FN int sysv_getlogin_r(char *buffer, size_t size)
{
    if (size < sizeof(LOGIN_NAME)) {
        return LINUX_ERANGE;
    }
    memcpy(buffer, LOGIN_NAME, sizeof(LOGIN_NAME));
    return 0;
}

/* Login records (utmp): no one is logged in as far as the shim knows. */
LIBC_FN void *sysv_getutxent(void)
{
    return NULL;
}

LIBC_FN void *sysv_getutxid(const void *record)
{
    (void)record;
    return NULL;
}

LIBC_FN void sysv_setutxent(void)
{
}

LIBC_FN void sysv_endutxent(void)
{
}

LIBC_FN void *sysv_getutent(void)
{
    return NULL;
}

LIBC_FN void sysv_setutent(void)
{
}

LIBC_FN void sysv_endutent(void)
{
}

LIBC_FN int sysv_getpriority(int which, unsigned who)
{
    (void)which;
    (void)who;
    return DEFAULT_PRIORITY;
}

LIBC_FN int sysv_getdtablesize(void)
{
    return DESCRIPTOR_LIMIT;
}

/* The process cannot change identity; setting what it already has succeeds. */
static int keep_identity(unsigned wanted, unsigned have)
{
    return wanted == have || wanted == (unsigned)-1 ? 0 : shim_fail(LINUX_EPERM);
}

LIBC_FN int sysv_seteuid(unsigned uid)
{
    return keep_identity(uid, FIXED_UID);
}

LIBC_FN int sysv_setegid(unsigned gid)
{
    return keep_identity(gid, FIXED_GID);
}

LIBC_FN int sysv_setreuid(unsigned real, unsigned effective)
{
    return keep_identity(real, FIXED_UID) == 0 ? keep_identity(effective, FIXED_UID) : -1;
}

LIBC_FN int sysv_setfsuid(unsigned uid)
{
    (void)uid;
    return (int)FIXED_UID;
}

LIBC_FN int sysv_setfsgid(unsigned gid)
{
    (void)gid;
    return (int)FIXED_GID;
}

/* A process group per process: the shim has no job control. */
LIBC_FN int sysv_getpgrp(void)
{
    return sysv_getpid();
}

LIBC_FN int sysv_getpgid(int pid)
{
    return pid == 0 || pid == sysv_getpid() ? sysv_getpid() : shim_fail(LINUX_ESRCH);
}

LIBC_FN int sysv_tcgetpgrp(int fd)
{
    return sysv_isatty(fd) ? sysv_getpid() : shim_fail(LINUX_ENOTTY);
}

LIBC_FN char *sysv_ttyname(int fd)
{
    (void)fd;
    shim_set_errno(LINUX_ENOTTY);
    return NULL;
}

LIBC_FN int sysv_ttyname_r(int fd, char *buffer, size_t size)
{
    (void)fd;
    (void)buffer;
    (void)size;
    return LINUX_ENOTTY;
}

LIBC_FN int sysv_tcflush(int fd, int queue)
{
    (void)fd;
    (void)queue;
    return 0;
}

LIBC_FN unsigned sysv_cfgetospeed(const Termios *attributes)
{
    return attributes->control_flags & CBAUD_MASK;
}

LIBC_FN unsigned sysv_cfgetispeed(const Termios *attributes)
{
    return attributes->control_flags & CBAUD_MASK;
}

/* cfmakeraw(3): no input processing, no output processing, 8-bit characters, no echo and no signals. */
LIBC_FN void sysv_cfmakeraw(Termios *attributes)
{
    enum { IGNBRK = 1, BRKINT = 2, PARMRK = 8, ISTRIP = 040, INLCR = 0100, IGNCR = 0200, ICRNL = 0400, IXON = 02000 };
    enum { OPOST = 1 };
    enum { CSIZE = 060, PARENB = 0400, CS8 = 060 };
    enum { ISIG = 1, ICANON = 2, ECHO = 010, ECHONL = 0100, IEXTEN = 0100000 };
    enum { VMIN_INDEX = 6, VTIME_INDEX = 5 };
    attributes->input_flags &= ~(unsigned)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    attributes->output_flags &= ~(unsigned)OPOST;
    attributes->local_flags &= ~(unsigned)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    attributes->control_flags &= ~(unsigned)(CSIZE | PARENB);
    attributes->control_flags |= CS8;
    attributes->control_chars[VMIN_INDEX] = 1;
    attributes->control_chars[VTIME_INDEX] = 0;
}
