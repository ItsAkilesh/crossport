/*
 * File: shim/libc_tail.c
 * Purpose: The rest of the small C library calls the second survey found missing: gettext's dngettext and __dcgettext, ctermid, the password
 *          lock, killpg, nice, thread names, signal descriptions, lockf, getsid, sync_file_range and the database end-of-iteration calls
 * Author: Akilesh
 * Date: 2026-11-24
 */

#include <string.h>

#include "libc_abi.h"

#define THREAD_NAME_BYTES 16
#define CONTROLLING_TERMINAL "/dev/tty"

LIBC_FN char *sysv_dcgettext(const char *domain, const char *message, int category);
LIBC_FN char *sysv_strsignal(int number);
LIBC_FN int sysv_kill(int pid, int signal_number);
LIBC_FN int sysv_getpid(void);

/* The C library's own gettext domain. */
LIBC_DATA(char, _libc_intl_domainname[5]) = "libc";

/* There are no message catalogs, so the original text is the translation; the plural form is chosen as in English. */
LIBC_FN char *sysv_dngettext(const char *domain, const char *singular, const char *plural, unsigned long count)
{
    (void)domain;
    return (char *)(count == 1 ? singular : plural);
}

LIBC_FN char *sysv___dcgettext(const char *domain, const char *message, int category)
{
    return sysv_dcgettext(domain, message, category);
}

LIBC_FN char *sysv_ctermid(char *buffer)
{
    static char fallback[sizeof(CONTROLLING_TERMINAL)];
    char *out = buffer != NULL ? buffer : fallback;
    memcpy(out, CONTROLLING_TERMINAL, sizeof(CONTROLLING_TERMINAL));
    return out;
}

LIBC_FN int sysv_lckpwdf(void)
{
    return 0;
}

LIBC_FN int sysv_ulckpwdf(void)
{
    return 0;
}

/* A process group is the process itself here, so a signal to the group is a signal to the process. */
LIBC_FN int sysv_killpg(int group, int signal_number)
{
    return group == 0 || group == sysv_getpid() ? sysv_kill(sysv_getpid(), signal_number) : shim_fail(LINUX_ESRCH);
}

LIBC_FN int sysv_nice(int increment)
{
    (void)increment;
    return 0;
}

/* Threads have no names here; glibc's call writes at most 16 bytes and fails with ERANGE when the buffer is shorter. */
LIBC_FN int sysv_pthread_getname_np(uint64_t thread, char *name, size_t size)
{
    (void)thread;
    if (size < THREAD_NAME_BYTES) {
        return LINUX_ERANGE;
    }
    name[0] = '\0';
    return 0;
}

LIBC_FN const char *sysv_sigdescr_np(int number)
{
    return number > 0 && number < 65 ? sysv_strsignal(number) : NULL;
}

/* lockf(3) places an advisory lock; with one process per file at a time on Windows, every request is granted. */
LIBC_FN int sysv_lockf(int fd, int command, int64_t length)
{
    (void)fd;
    (void)command;
    (void)length;
    return 0;
}

LIBC_FN int sysv_getsid(int pid)
{
    return pid == 0 || pid == sysv_getpid() ? sysv_getpid() : shim_fail(LINUX_ESRCH);
}

LIBC_FN int sysv_sync_file_range(int fd, int64_t offset, int64_t length, unsigned flags)
{
    (void)fd;
    (void)offset;
    (void)length;
    (void)flags;
    return 0;
}

LIBC_FN void sysv_setservent(int keep_open)
{
    (void)keep_open;
}

LIBC_FN void sysv_endhostent(void)
{
}

LIBC_FN void sysv_endnetent(void)
{
}

LIBC_FN void sysv_endprotoent(void)
{
}

LIBC_FN void sysv_endspent(void)
{
}

LIBC_FN void *sysv_getspnam(const char *name)
{
    (void)name;
    shim_set_errno(LINUX_ENOENT);
    return NULL;
}
